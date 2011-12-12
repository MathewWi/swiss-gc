/*
*
*   Swiss - The Gamecube IPL replacement
*
*/

#include <stdio.h>
#include <gccore.h>		/*** Wrapper to include common libogc headers ***/
#include <ogcsys.h>		/*** Needed for console support ***/
#include <ogc/color.h>
#include <ogc/exi.h>
#include <ogc/usbgecko.h>
#include <ogc/video_types.h>
#include <sdcard/card_cmn.h>
#include <ogc/lwp_threads.h>
#include <ogc/machine/processor.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <asndlib.h>
#include <mp3player.h>

#include "swiss.h"
#include "main.h"
#include "exi.h"
#include "patcher.h"
#include "banner.h"
#include "qchparse.h"
#include "dvd.h"
#include "gcm.h"
#include "aram/sidestep.h"
#include "gui/FrameBufferMagic.h"
#include "gui/IPLFontWrite.h"
#include "devices/deviceHandler.h"

static DiskHeader GCMDisk;      //Gamecube Disc Header struct
char IPLInfo[256] __attribute__((aligned(32)));
GXRModeObj *newmode = NULL;
char txtbuffer[2048];           //temporary text buffer
file_handle curFile;     //filedescriptor for current file
u32 GC_SD_SPEED   = EXI_SPEED16MHZ;
int SDHCCard = 0; //0 == SDHC, 1 == normal SD
int curDevice = 0;  //SD_CARD or DVD_DISC or IDEEXI or WODE
int curCopyDevice = 0;  //SD_CARD or DVD_DISC or IDEEXI or WODE
int noVideoPatch = 0;
char *videoStr = NULL;

/* The default boot up MEM1 lowmem values (from ipl when booting a game) */
static const u32 GC_DefaultConfig[56] =
{
	0x0D15EA5E, 0x00000001, 0x01800000, 0x00000003, //  0.. 3 80000020
	0x00000000, 0x816FFFF0, 0x817FE8C0, 0x00000024, //  4.. 7 80000030
	0x00000000, 0x00000000, 0x00000000, 0x00000000, //  8..11 80000040
	0x00000000, 0x00000000, 0x00000000, 0x00000000, // 12..15 80000050
	0x00000000, 0x00000000, 0x00000000, 0x00000000, // 16..19 80000060
	0x00000000, 0x00000000, 0x00000000, 0x00000000, // 20..23 80000070
	0x00000000, 0x00000000, 0x00000000, 0x00000000, // 24..27 80000080
	0x00000000, 0x00000000, 0x00000000, 0x00000000, // 28..31 80000090
	0x00000000, 0x00000000, 0x00000000, 0x00000000, // 32..35 800000A0
	0x00000000, 0x00000000, 0x00000000, 0x00000000, // 36..39 800000B0
	0x015D47F8, 0xF8248360, 0x00000000, 0x00000001, // 40..43 800000C0
	0x00000000, 0x00000000, 0x00000000, 0x00000000, // 44..47 800000D0
	0x814B7F50, 0x815D47F8, 0x00000000, 0x81800000, // 48..51 800000E0
	0x01800000, 0x817FC8C0, 0x09A7EC80, 0x1CF7C580  // 52..55 800000F0
};

char *getVideoString()
{
	switch(swissSettings.curVideoSelection)
	{
		case 0:
			return "NTSC";
		case 1:
			return "PAL50";
		case 2:
			return "PAL60";
		case 3:
			return "Auto";
		case 4:
			return "480p";
	}
	return "Autodetect";
}
int ask_stop_drive(); 
int ask_set_cheats();

void print_gecko(char *string)
{
	if(swissSettings.debugUSB) {
		char tbuf[30];
		char buffer[2048];
		struct timeval tv;
		time_t curtime;
		gettimeofday(&tv, NULL); 
		curtime=tv.tv_sec;
		strftime(tbuf,30,"%m-%d-%Y %T.",localtime(&curtime));
		sprintf(buffer,"%s%ld: %s",tbuf,tv.tv_usec, string);
		usb_sendbuffer_safe(1,buffer,strlen(buffer));
	}
}

/* re-init video for a given game */
void ogc_video__reset()
{
    DrawFrameStart();
    if(swissSettings.curVideoSelection==AUTO) {
		noVideoPatch = 1;
		switch(GCMDisk.CountryCode) {
			case 'P': // PAL
			case 'D': // German
			case 'F': // French
			case 'S': // Spanish
			case 'I': // Italian
			case 'L': // Japanese Import to PAL
			case 'M': // American Import to PAL
			case 'X': // PAL other languages?
			case 'Y': // PAL other languages?
				swissSettings.curVideoSelection = PAL50;
				break;
			case 'E':
			case 'J':
				swissSettings.curVideoSelection = NTSC;
				break;
			case 'U':
				swissSettings.curVideoSelection = PAL60;
				break;
			default:
				break;
		}
    }

    /* set TV mode for current game*/
	if(swissSettings.curVideoSelection!=AUTO)	{		//if not autodetect
		switch(swissSettings.curVideoSelection) {
			case PAL60:
				*(volatile unsigned long*)0x80002F40 = VI_TVMODE_EURGB60_INT;
				newmode = &TVEurgb60Hz480IntDf;
				DrawMessageBox(D_INFO,"Video Mode: PAL 60Hz");
				break;
			case PAL50:
				*(volatile unsigned long*)0x80002F40 = VI_TVMODE_PAL_INT;
				newmode = &TVPal528IntDf;
				DrawMessageBox(D_INFO,"Video Mode: PAL 50Hz");
				break;
			case NTSC:
				*(volatile unsigned long*)0x80002F40 = VI_TVMODE_NTSC_INT;
				newmode = &TVNtsc480IntDf;
				DrawMessageBox(D_INFO,"Video Mode: NTSC 60Hz");
				break;
			case P480:
				if(VIDEO_HaveComponentCable()) {
					*(volatile unsigned long*)0x80002F40 = VI_TVMODE_NTSC_PROG;
					newmode = &TVNtsc480Prog;
					DrawMessageBox(D_INFO,"Video Mode: NTSC 480p");
				}
				else {
					*(volatile unsigned long*)0x80002F40 = VI_TVMODE_NTSC_INT;
					newmode = &TVNtsc480IntDf;
					DrawMessageBox(D_INFO,"Video Mode: NTSC 60Hz");
				}
				break;
			default:
				*(volatile unsigned long*)0x80002F40 = VI_TVMODE_NTSC_INT;
				newmode = &TVNtsc480IntDf;
				DrawMessageBox(D_INFO,"Video Mode: NTSC 60Hz");
		}
		DrawFrameFinish();
	}
}

void do_videomode_swap() {
	if(vmode!=newmode) {
		vmode = newmode;
		VIDEO_Configure (vmode);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		if (vmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
		else while (VIDEO_GetNextField())  VIDEO_WaitVSync();
	}
}

void doBackdrop()
{
	DrawFrameStart();
	DrawMenuButtons(-1);
}

char *getRelativeName(char *str) {
	int i;
	for(i=strlen(str);i>0;i--){
		if(str[i]=='/') {
			return str+i+1;
		}
	}
	return str;
}

char *stripInvalidChars(char *str) {
	int i = 0;
	for(i = 0; i < strlen(str); i++) {
		if(str[i] == '\\' || str[i] == '/' || str[i] == ':'|| str[i] == '*'
		|| str[i] == '?'|| str[i] == '"'|| str[i] == '<'|| str[i] == '>'|| str[i] == '|') {
			str[i] = '_';
		}
	}
	return str;
}

// textFileBrowser lives on :)
void textFileBrowser(file_handle** directory, int num_files)
{
	int i = 0,j=0,max;
	memset(txtbuffer,0,sizeof(txtbuffer));
	if(num_files<=0) return;
  
	while(1){
		doBackdrop();
		i = MIN(MAX(0,curSelection-FILES_PER_PAGE/2),MAX(0,num_files-FILES_PER_PAGE));
		max = MIN(num_files, MAX(curSelection+FILES_PER_PAGE/2,FILES_PER_PAGE));
		for(j = 0; i<max; ++i,++j) {
			DrawFileBrowserButton(50,90+(j*50), 550, 90+(j*50)+50, getRelativeName((*directory)[i].name),&((*directory)[i]), (i == curSelection) ? B_SELECTED:B_NOSELECT,-1);
		}
		DrawFrameFinish();
		while ((PAD_StickY(0) > -16 && PAD_StickY(0) < 16) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_UP) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_DOWN));
		if((PAD_ButtonsHeld(0) & PAD_BUTTON_UP) || PAD_StickY(0) > 16){	curSelection = (--curSelection < 0) ? num_files-1 : curSelection;}
		if((PAD_ButtonsHeld(0) & PAD_BUTTON_DOWN) || PAD_StickY(0) < -16) {curSelection = (curSelection + 1) % num_files;	}
		if((PAD_ButtonsHeld(0) & PAD_BUTTON_A))	{
			//go into a folder or select a file
			if((*directory)[curSelection].fileAttrib==IS_DIR) {
				memcpy(&curFile, &(*directory)[curSelection], sizeof(file_handle));
				needsRefresh=1;
			}
			else if((*directory)[curSelection].fileAttrib==IS_SPECIAL){
				//go up a folder
				int len = strlen(&curFile.name[0]);
				while(len && curFile.name[len-1]!='/') {
      				len--;
				}
				if(len != strlen(&curFile.name[0])) {
					curFile.name[len-1] = '\0';
					needsRefresh=1;
				}
			}
			else if((*directory)[curSelection].fileAttrib==IS_FILE){
				memcpy(&curFile, &(*directory)[curSelection], sizeof(file_handle));
				manage_file();
			}
			return;
		}
		
		if(PAD_ButtonsHeld(0) & PAD_BUTTON_B)	{
			memcpy(&curFile, &(*directory)[curSelection], sizeof(file_handle));
			curMenuLocation=ON_OPTIONS;
			return;
		}
		if(PAD_StickY(0) < -16 || PAD_StickY(0) > 16) {
			usleep(50000 - abs(PAD_StickY(0)*256));
		}
		else {
			while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_UP) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_DOWN)));
		}
	}
}

void select_dest_dir(file_handle* directory, file_handle* selection)
{
	file_handle *directories = NULL;
	int i = 0, j = 0, max = 0, refresh = 1, num_files =0, idx = 0;
	
	while(1){
		// Read the directory
		if(refresh) {
			num_files = deviceHandler_dest_readDir(directory, &directories, IS_DIR);
			refresh = idx = 0;
		}
		doBackdrop();
		DrawEmptyBox(20,40, vmode->fbWidth-20, 450, COLOR_BLACK);
		WriteFont(50, 55, "Enter directory and press X");
		i = MIN(MAX(0,idx-FILES_PER_PAGE/2),MAX(0,num_files-FILES_PER_PAGE));
		max = MIN(num_files, MAX(idx+FILES_PER_PAGE/2,FILES_PER_PAGE));
		for(j = 0; i<max; ++i,++j) {
			DrawSelectableButton(50,90+(j*50), 550, 90+(j*50)+50, getRelativeName((directories)[i].name), (i == idx) ? B_SELECTED:B_NOSELECT,-1);
		}
		DrawFrameFinish();
		while ((PAD_StickY(0) > -16 && PAD_StickY(0) < 16) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_X) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_UP) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_DOWN));
		if((PAD_ButtonsHeld(0) & PAD_BUTTON_UP) || PAD_StickY(0) > 16){	idx = (--idx < 0) ? num_files-1 : idx;}
		if((PAD_ButtonsHeld(0) & PAD_BUTTON_DOWN) || PAD_StickY(0) < -16) {idx = (idx + 1) % num_files;	}
		if((PAD_ButtonsHeld(0) & PAD_BUTTON_A))	{
			//go into a folder or select a file
			if((directories)[idx].fileAttrib==IS_DIR) {
				memcpy(directory, &(directories)[idx], sizeof(file_handle));
				refresh=1;
			}
			else if(directories[idx].fileAttrib==IS_SPECIAL){
				//go up a folder
				int len = strlen(directory->name);
				while(len && directory->name[len-1]!='/') {
      				len--;
				}
				if(len != strlen(directory->name)) {
					directory->name[len-1] = '\0';
					refresh=1;
				}
			}
		}
		if(PAD_StickY(0) < -16 || PAD_StickY(0) > 16) {
			usleep(50000 - abs(PAD_StickY(0)*256));
		}
		if((PAD_ButtonsHeld(0) & PAD_BUTTON_X))	{
			memcpy(selection, directory, sizeof(file_handle));
			break;
		}
		while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_X) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_UP) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_DOWN)));
	}
	free(directories);
}

void override_memory_limits() {
	// reserve memory for patch code high up
	u32 base = get_base_addr();
    *(volatile u32*)0x80000028 = (u32)base & 0x01FFFFFF;	//Physical Memory Size
    *(volatile u32*)0x8000002C = (u32)0x00000003;			//Retail GC
    *(volatile u32*)0x80000034 = 0;							//Arena Lo
    *(volatile u32*)0x80000038 = 0;							//Arena Hi
    *(volatile u32*)0x800000EC = (u32)base;					//Debug Monitor Location
    *(volatile u32*)0x800000F0 = (u32)base & 0x01FFFFFF;	//Console Simulated Mem size
    *(volatile u32*)0x800000F4 = 0;							//DVD BI2 Location
}

static void nothing(const char* txt, ...){}
unsigned int load_app(int mode)
{
	// Apploader internal function defines
	void  (*app_init) (void (*report)(const char* fmt, ...));
	int   (*app_main) (void** dst, int* size, int* offset);
	void* (*app_final)();
	void  (*app_entry)(void(**init)(void (*report)(const char* fmt, ...)),
	int (**main)(), void *(**final)());
	char* gameID = (char*)0x80000000;
	int zeldaVAT = 0;
	// Apploader related variables
	void* app_dst = 0;
	int app_len = 0,app_offset = 0, apploader_info[3], res;
	
	VIDEO_SetPostRetraceCallback (NULL);

	if(mode == NO_CHEATS) {
		memcpy((void*)0x80000020,GC_DefaultConfig,0xE0);
	}
  
	// Read the game header to 0x80000000 & apploader header
	deviceHandler_seekFile(&curFile,0,DEVICE_HANDLER_SEEK_SET);
	if(deviceHandler_readFile(&curFile,(unsigned char*)0x80000000,32) != 32) {
		DrawFrameStart();
		DrawMessageBox(D_FAIL, "Apploader Header Failed to read");
		DrawFrameFinish();
		while(1);
	}
	deviceHandler_seekFile(&curFile,0x2450,DEVICE_HANDLER_SEEK_SET);
	if(deviceHandler_readFile(&curFile,(unsigned char*)apploader_info,sizeof(int)*3) != sizeof(int)*3) {
		DrawFrameStart();
		DrawMessageBox(D_FAIL, "Apploader Header Failed to read");
		DrawFrameFinish();
		while(1);
	}
	// Fix Zelda WW on Wii (__GXSetVAT? patch)
	if (!is_gamecube() && (!strncmp(gameID, "GZLP01", 6) || !strncmp(gameID, "GZLE01", 6) || !strncmp(gameID, "GZLJ01", 6))) {
		if(!strncmp(gameID, "GZLP01", 6))
			zeldaVAT = 1;	//PAL
		else 
			zeldaVAT = 2;	//NTSC-U,NTSC-J
	}
	
	// Will we use the low mem area for our patch code?
	if (!strncmp(gameID, "GPXE01", 6) || !strncmp(gameID, "GPXP01", 6) || !strncmp(gameID, "GPXJ01", 6)) {
		swissSettings.useHiMemArea = 1;
	}

	// If not, setup the game for High mem patch code
	set_base_addr(swissSettings.useHiMemArea);
	if(swissSettings.useHiMemArea) {
		override_memory_limits();
	}

	sprintf(txtbuffer,"Reading Apploader (%d bytes)",apploader_info[1]+apploader_info[2]);
	DrawFrameStart();
	DrawProgressBar(33, txtbuffer);
	DrawFrameFinish();

	// Read the apploader itself
	deviceHandler_seekFile(&curFile,0x2460,DEVICE_HANDLER_SEEK_SET);
	if(deviceHandler_readFile(&curFile,(void*)0x81200000,apploader_info[1]+apploader_info[2]) != apploader_info[1]+apploader_info[2]) {
		DrawFrameStart();
		DrawMessageBox(D_FAIL, "Apploader Data Failed to read");
		DrawFrameFinish();
		while(1);
	}
	DCFlushRange((void*)0x81200000,apploader_info[1]+apploader_info[2]);
	ICInvalidateRange((void*)0x81200000,apploader_info[1]+apploader_info[2]);
  
	DrawFrameStart();
	DrawProgressBar(66, "Executing Apploader...");
	DrawFrameFinish();
	
	app_entry = (void (*)(void(**)(void (*)(const char*, ...)), int (**)(), void *(**)()))(apploader_info[0]);
	app_entry(&app_init, &app_main, &app_final);
	app_init((void (*)(const char*, ...))nothing);
	
	while(1) {
		res = app_main(&app_dst, &app_len, &app_offset);
		if(!res) {
			break;
		}
		if(swissSettings.debugUSB) {
			sprintf(txtbuffer,"Apploader Reading: %dbytes from %08X to %08x\n",app_len,app_offset,(u32)app_dst);
			print_gecko(txtbuffer);
		}
		// Read app_len amount to app_dst specified by the apploader from offset app_offset
		deviceHandler_seekFile(&curFile,app_offset,DEVICE_HANDLER_SEEK_SET);
		if(deviceHandler_readFile(&curFile,app_dst,app_len) != app_len) {
			DrawFrameStart();
			DrawMessageBox(D_FAIL, "DOL Failed to read");
			DrawFrameFinish();
			while(1);
		}
		// Patch to read from SD/HDD
		if((curDevice == SD_CARD)||((curDevice == IDEEXI))) {
			if(swissSettings.useHiLevelPatch) {
				applyPatches(app_dst, app_len, swissSettings.disableInterrupts);
			} else {
				dvd_patchDVDRead(app_dst, app_len);
			}
			dvd_patchDVDReadID(app_dst, app_len);
			dvd_patchAISCount(app_dst, app_len);
		}
		// Fix Zelda WW on Wii
		if(zeldaVAT) {
			patchZeldaWW(app_dst, app_len, zeldaVAT);
		}
		// 2 Disc support with no modchip
		if((curDevice == DVD_DISC) && (is_gamecube()) && (drive_status == DEBUG_MODE)) {
			dvd_patchreset(app_dst,app_len);  
		}
		// Patch OSReport to print out over USBGecko
		if(swissSettings.debugUSB) {
			dvd_patchfwrite(app_dst, app_len);
		}
		// 480p Forcing
		if(swissSettings.curVideoSelection == P480) {
			patch_video_480p(app_dst, app_len);
		}
		DCFlushRange(app_dst, app_len);
		ICInvalidateRange(app_dst, app_len);
	}
	deviceHandler_deinit(&curFile);

	DrawFrameStart();
	DrawProgressBar(100, "Executing Game!");
	DrawFrameFinish();

	do_videomode_swap();
	switch(*(char*)0x80000003) {
		case 'P': // PAL
		case 'D': // German
		case 'F': // French
		case 'S': // Spanish
		case 'I': // Italian
		case 'L': // Japanese Import to PAL
		case 'M': // American Import to PAL
		case 'X': // PAL other languages?
		case 'Y': // PAL other languages?
		case 'U':
			*(volatile unsigned long*)0x800000CC = 1;
			break;
		case 'E':
		case 'J':
			*(volatile unsigned long*)0x800000CC = 0;
			break;
		default:
			*(volatile unsigned long*)0x800000CC = 0;
	}

	// install our assembly code into memory
	install_code();
  
	DCFlushRange((void*)0x80000000, 0x3100);
	ICInvalidateRange((void*)0x80000000, 0x3100);
	
	if(swissSettings.debugUSB) {
		sprintf(txtbuffer,"Sector: %08X%08X Speed: %08x Type: %08X\n",*(volatile unsigned int*)0x80002F00,
		*(volatile unsigned int*)0x80002F04,*(volatile unsigned int*)0x80002F30,*(volatile unsigned int*)0x80002F34);
		print_gecko(txtbuffer);
	}
	
	// Check DVD Status, make sure it's error code 0
	sprintf(txtbuffer, "DVD: %08X\r\n",dvd_get_error());
	print_gecko(txtbuffer);

	// Disable interrupts and exceptions
	SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
	
	if(mode == CHEATS) {
		return (u32)app_final();
	}
 
	__lwp_thread_stopmultitasking((void(*)())app_final());
	
	return 0;
}

void boot_dol()
{ 
	unsigned char *dol_buffer;
	unsigned char *ptr;
  
	sprintf(txtbuffer, "Loading DOL [%d/%d Kb] ..",curFile.size/1024,(SYS_GetArena1Hi()-SYS_GetArena1Lo())/1024);
	DrawFrameStart();
	DrawMessageBox(D_INFO,txtbuffer);
	DrawFrameFinish();
  
	dol_buffer = (unsigned char*)memalign(32,curFile.size);
	if(!dol_buffer) {
		DrawFrameStart();
		DrawMessageBox(D_FAIL,"DOL is too big. Press A.");
		DrawFrameFinish();
		wait_press_A();
		return;
	}
		
	int i=0;
	ptr = dol_buffer;
	for(i = 0; i < curFile.size; i+= 131072) {
		sprintf(txtbuffer, "Loading DOL [%d/%d Kb] ..",curFile.size/1024,(SYS_GetArena1Size()+curFile.size)/1024);
		DrawFrameStart();
		DrawProgressBar((int)((float)((float)i/(float)curFile.size)*100), txtbuffer);
		DrawFrameFinish();
    
		deviceHandler_seekFile(&curFile,i,DEVICE_HANDLER_SEEK_SET);
		int size = i+131072 > curFile.size ? curFile.size-i : 131072; 
		if(deviceHandler_readFile(&curFile,ptr,size)!=size) {
			DrawFrameStart();
			DrawMessageBox(D_FAIL,"Failed to read DOL. Press A.");
			DrawFrameFinish();
			wait_press_A();
			return;
		}
  		ptr+=size;
	}
	
	DOLtoARAM(dol_buffer);
}

int mp3Reader(void *cbdata, void *dst, int size) {
	u32 *offset = cbdata;
	deviceHandler_seekFile(&curFile,*offset,DEVICE_HANDLER_SEEK_SET);
	int ret = deviceHandler_readFile(&curFile,dst,size);
	*offset+=size;
	return ret;
}

/* Plays a MP3 file */
void play_mp3() {
	// Initialise the audio subsystem
	ASND_Init(NULL);
	MP3Player_Init();
	u32 offset = 0;
	deviceHandler_seekFile(&curFile,0,DEVICE_HANDLER_SEEK_SET);
	DrawFrameStart();
	sprintf(txtbuffer,"Playing %s",getRelativeName(curFile.name));
	DrawMessageBox(D_INFO,txtbuffer);
	DrawFrameFinish();
	MP3Player_PlayFile(&offset, &mp3Reader, NULL);
	while(MP3Player_IsPlaying() && (!(PAD_ButtonsHeld(0) & PAD_BUTTON_B))){
		usleep(5000);
	}
	MP3Player_Stop();
}

/* Manage file  - The user will be asked what they want to do with the currently selected file - copy/move/delete*/
void manage_file() {
	// If it's a file
	if(curFile.fileAttrib == IS_FILE) {
		// Ask the user what they want to do with it
		DrawFrameStart();
		DrawEmptyBox(10,150, vmode->fbWidth-10, 350, COLOR_BLACK);
		WriteFontStyled(640/2, 160, "Manage File:", 1.0f, true, defaultColor);
		float scale = GetTextScaleToFitInWidth(getRelativeName(curFile.name), vmode->fbWidth-10-10);
		WriteFontStyled(640/2, 200, getRelativeName(curFile.name), scale, true, defaultColor);
		if(deviceHandler_deleteFile) {
			WriteFontStyled(640/2, 230, "(A) Load (X) Copy (Y) Move (Z) Delete", 1.0f, true, defaultColor);
		}
		else {
			WriteFontStyled(640/2, 230, "(A) Load (X) Copy", 1.0f, true, defaultColor);
		}
		WriteFontStyled(640/2, 300, "Press an option to Continue, or B to return", 1.0f, true, defaultColor);
		DrawFrameFinish();
		while(PAD_ButtonsHeld(0) & PAD_BUTTON_A);
		int option = 0;
		while(1) {
			u32 buttons = PAD_ButtonsHeld(0);
			if(buttons & PAD_BUTTON_X) {
				option = COPY_OPTION;
				while(PAD_ButtonsHeld(0) & PAD_BUTTON_X);
				break;
			}
			if(deviceHandler_deleteFile && (buttons & PAD_BUTTON_Y)) {
				option = MOVE_OPTION;
				while(PAD_ButtonsHeld(0) & PAD_BUTTON_Y);
				break;
			}
			if(deviceHandler_deleteFile && (buttons & PAD_TRIGGER_Z)) {
				option = DELETE_OPTION;
				while(PAD_ButtonsHeld(0) & PAD_TRIGGER_Z);
				break;
			}
			if(buttons & PAD_BUTTON_A) {
				load_file();
				return;
			}
			if(buttons & PAD_BUTTON_B) {
				return;
			}
		}
	
		// If delete, delete it + refresh the device
		if(option == DELETE_OPTION) {
			if(!deviceHandler_deleteFile(&curFile)) {
				//go up a folder
				int len = strlen(&curFile.name[0]);
				while(len && curFile.name[len-1]!='/') {
      				len--;
				}
				curFile.name[len-1] = '\0';
				DrawFrameStart();
				DrawMessageBox(D_INFO,"File deleted! Press A to continue");
				DrawFrameFinish();
				wait_press_A();
				needsRefresh=1;
			}
			else {
				DrawFrameStart();
				DrawMessageBox(D_INFO,"Delete Failed! Press A to continue");
				DrawFrameFinish();
			}
		}
		// If copy, ask which device is the destination device and copy
		else if((option == COPY_OPTION) || (option == MOVE_OPTION)) {
			int ret = 0;
			// Show a list of destination devices (the same device is also a possibility)
			select_copy_device();
			// If the devices are not the same, init the second, fail on non-existing device/etc
			if(deviceHandler_initial != deviceHandler_dest_initial) {
				deviceHandler_dest_deinit( deviceHandler_dest_initial );
				ret = deviceHandler_dest_init( deviceHandler_dest_initial );
				if(!ret) {
					DrawFrameStart();
					sprintf(txtbuffer, "Failed to init destination device! (%i)",ret);
					DrawMessageBox(D_FAIL,txtbuffer);
					DrawFrameFinish();
					wait_press_A();
					return;
				}
			}
			// Traverse this destination device and let the user select a directory to dump the file in
			file_handle *destFile = memalign(32,sizeof(file_handle));
			
			// Show a directory only browser and get the destination file location
			select_dest_dir(deviceHandler_dest_initial, destFile);
			destFile->fp = 0;
			destFile->fileBase = 0;
			destFile->size = 0;
			destFile->fileAttrib = IS_FILE;
			destFile->status = 0;
			destFile->offset = 0;
			
			// Same (fat based) device and user wants to move the file, just rename ;)
			if(deviceHandler_initial == deviceHandler_dest_initial 
				&& option == MOVE_OPTION 
				&& (deviceHandler_initial->name[0] =='s' || deviceHandler_initial->name[0] =='i')) {
				sprintf(destFile->name, "%s/%s",destFile->name,getRelativeName(&curFile.name[0]));
				ret = rename(&curFile.name[0], &destFile->name[0]);
				//go up a folder
				int len = strlen(&curFile.name[0]);
				while(len && curFile.name[len-1]!='/') {
					len--;
				}
				curFile.name[len-1] = '\0';
				needsRefresh=1;
				DrawFrameStart();
				DrawMessageBox(D_INFO,ret ? "Move Failed! Press A to continue":"File moved! Press A to continue");
				DrawFrameFinish();
				wait_press_A();
			}
			else {
				sprintf(destFile->name, "%s/%s",destFile->name,stripInvalidChars(getRelativeName(&curFile.name[0])));
				
				u32 isDestCard = deviceHandler_dest_writeFile == deviceHandler_CARD_writeFile;
				if(isDestCard && strstr(destFile->name,".gci")==NULL && strstr(destFile->name,".GCI")==NULL) {
					// Only .GCI files can go to the memcard
					DrawFrameStart();
					DrawMessageBox(D_INFO,"Only GCI files allowed on memcard. Press A to continue");
					DrawFrameFinish();
					wait_press_A();
					return;
				}
			
				// Read from one file and write to the new directory
				u32 isCard = deviceHandler_readFile == deviceHandler_CARD_readFile;
				u32 curOffset = 0, cancelled = 0, chunkSize = (isCard||isDestCard) ? curFile.size : 0x8000;
				char *readBuffer = (char*)memalign(32,chunkSize);
				
				while(curOffset < curFile.size) {
					u32 buttons = PAD_ButtonsHeld(0);
					if(buttons & PAD_BUTTON_B) {
						cancelled = 1;
						break;
					}
					sprintf(txtbuffer, "Copying to: %s",getRelativeName(destFile->name));
					DrawFrameStart();
					DrawProgressBar((int)((float)((float)curOffset/(float)curFile.size)*100), txtbuffer);
					DrawFrameFinish();
					u32 amountToCopy = curOffset + chunkSize > curFile.size ? curFile.size - curOffset : chunkSize;
					ret = deviceHandler_readFile(&curFile, readBuffer, amountToCopy);
					if(ret != amountToCopy) {
						DrawFrameStart();
						sprintf(txtbuffer, "Failed to Read! (%i %i)",amountToCopy,ret);
						DrawMessageBox(D_FAIL,txtbuffer);
						DrawFrameFinish();
						wait_press_A();
						return;
					}
					ret = deviceHandler_dest_writeFile(destFile, readBuffer, amountToCopy);
					if(ret != amountToCopy) {
						DrawFrameStart();
						sprintf(txtbuffer, "Failed to Write! (%i %i)",amountToCopy,ret);
						DrawMessageBox(D_FAIL,txtbuffer);
						DrawFrameFinish();
						wait_press_A();
						return;
					}
					curOffset+=amountToCopy;
				}
				deviceHandler_dest_deinit( destFile );
				free(destFile);
				DrawFrameStart();
				if(!cancelled) {
					// If cut, delete from source device
					if(option == MOVE_OPTION) {
						deviceHandler_deleteFile(&curFile);
						needsRefresh=1;
						DrawMessageBox(D_INFO,"Move Complete!");
					}
					else {
						DrawMessageBox(D_INFO,"Copy Complete! Press A to continue");
					}
				} 
				else {
					DrawMessageBox(D_INFO,"Operation Cancelled! Press A to continue");
				}
				DrawFrameFinish();
				wait_press_A();
			}
		}
	}
	// Else if directory, mention support not yet implemented.
	else {
		DrawFrameStart();
		DrawMessageBox(D_INFO,"Directory support not implemented");
		DrawFrameFinish();
	}
}

/* Execute/Load/Parse the currently selected file */
void load_file()
{
	char *fileName = &curFile.name[0];
	int isPrePatched = 0;
	
	if((curDevice==WODE)) {
		DrawFrameStart();
		DrawMessageBox(D_INFO, "Setup WODE ISO Please Wait ..");
		DrawFrameFinish();
		deviceHandler_setupFile(&curFile, 0);
	}
	
	// Cheats file?
	if(strlen(fileName)>4) {
		if((strstr(fileName,".QCH")!=NULL) || (strstr(fileName,".qch")!=NULL)) {
			if(ask_set_cheats()) {
				DrawFrameStart();
				DrawMessageBox(D_INFO, "Loading Cheats File ..");
				DrawFrameFinish();
				QCH_SetCheatsFile(&curFile);
				return;
			}
		}
	}
	
	if((curDevice != DVD_DISC) || (dvdDiscTypeInt==ISO9660_DISC)) {
		//if it's a DOL, boot it
		if(strlen(fileName)>4) {
			if((strstr(fileName,".DOL")!=NULL) || (strstr(fileName,".dol")!=NULL)) {
				boot_dol();
				// if it was invalid (overlaps sections, too large, etc..) it'll return
				DrawFrameStart();
				DrawMessageBox(D_WARN, "Invalid DOL");
				DrawFrameFinish();
				sleep(2);
				return;
			}
			if((strstr(fileName,".MP3")!=NULL) || (strstr(fileName,".mp3")!=NULL)) {
				play_mp3();
				return;
			}
			if(!((strstr(fileName,".iso")!=NULL) || (strstr(fileName,".gcm")!=NULL) 
				|| (strstr(fileName,".ISO")!=NULL) || (strstr(fileName,".GCM")!=NULL))) {
				DrawFrameStart();
				DrawMessageBox(D_WARN, "Unknown File Type!");
				DrawFrameFinish();
				sleep(1);
				return;
			}
		}
	}
	
	// boot the GCM/ISO file, gamecube disc or multigame selected entry
	deviceHandler_seekFile(&curFile,0,DEVICE_HANDLER_SEEK_SET);
	if(deviceHandler_readFile(&curFile,&GCMDisk,sizeof(DiskHeader)) != sizeof(DiskHeader)) {
		DrawFrameStart();
		DrawMessageBox(D_WARN, "Invalid or Corrupt File!");
		DrawFrameFinish();
		sleep(2);
		return;
	}

	// Show game info and allow the user to select cheats or return to the menu
	if(!info_game()) {
		return;
	}
	
	
	// Report to the user the patch status of this GCM/ISO file
	if((curDevice == SD_CARD) || ((curDevice == IDEEXI))) {
		isPrePatched = check_game();
		if(isPrePatched < 0) {
			return;
		}
	}
	
	// Start up the DVD Drive
	if((curDevice != DVD_DISC) && (curDevice != WODE)) {
		if(initialize_disc(GCMDisk.AudioStreaming) == DRV_ERROR) {
			return; //fail
		}
		if(!isPrePatched && ask_stop_drive()) {
			dvd_motor_off();
		}
	}
	
  	if((curDevice!=WODE)) {
		file_handle *secondDisc = NULL;
		
		// If we're booting from SD card or IDE hdd
		if((curDevice == SD_CARD) || ((curDevice == IDEEXI))) {
			// look to see if it's a two disc game
			// set things up properly to allow disc swapping
			// the files must be setup as so: game-disc1.xxx game-disc2.xxx
			secondDisc = memalign(32,sizeof(file_handle));
			memcpy(secondDisc,&curFile,sizeof(file_handle));
			secondDisc->fp = 0;
			
			// you're trying to load a disc1 of something
			if(curFile.name[strlen(secondDisc->name)-5] == '1') {
				secondDisc->name[strlen(secondDisc->name)-5] = '2';
			} else if(curFile.name[strlen(secondDisc->name)-5] == '2') {
				secondDisc->name[strlen(secondDisc->name)-5] = '1';
			}
			else {
				free(secondDisc);
				secondDisc = NULL;
			}
		}
	  
		// Call the special setup for each device (e.g. SD will set the sector(s))
		deviceHandler_setupFile(&curFile, secondDisc);

		
		if(secondDisc) {
			free(secondDisc);
		}
	}

	// setup the video mode before we kill libOGC kernel
	ogc_video__reset();
	
	if(getCodeBasePtr()[0]) {
		GCARSStartGame(getCodeBasePtr());
	}
	else {
		load_app(NO_CHEATS);
	}
}

int check_game()
{ 	
	DrawFrameStart();
	DrawMessageBox(D_INFO,"Checking Game ..");
	DrawFrameFinish();

	char buffer[256];
	deviceHandler_seekFile(&curFile,0x100,DEVICE_HANDLER_SEEK_SET);
	deviceHandler_readFile(&curFile,&buffer,256);
	if(!strcmp(&buffer[0],"Pre-Patched by Swiss v0.1")) {
		deviceHandler_seekFile(&curFile,0x120,DEVICE_HANDLER_SEEK_SET);
		deviceHandler_readFile(&curFile,&swissSettings.useHiLevelPatch,4);
		deviceHandler_readFile(&curFile,&swissSettings.useHiMemArea,4);
		deviceHandler_readFile(&curFile,&swissSettings.disableInterrupts,4);
		return 0;
	}
	
	ExecutableFile *filesToPatch = memalign(32, sizeof(ExecutableFile)*64);
	int numToPatch = parse_gcm(&curFile, filesToPatch);
	if(numToPatch>0) {
		// Game requires pre-patching, lets ask to do it.
		DrawFrameStart();
		DrawMessageBox(D_INFO,"This Game Requires irreversible Pre-Patching\nPress A to Continue");
		DrawFrameFinish();
		wait_press_A();
		
		int res = patch_gcm(&curFile, filesToPatch, numToPatch);
		DrawFrameStart();
		DrawMessageBox(D_INFO,res ? "Game Patched Successfully":"Game could not be patched or not required");
		DrawFrameFinish();
	}
	free(filesToPatch);
	return 0;
}

int cheats_game()
{ 
	int ret;
  
	DrawFrameStart();
	DrawMessageBox(D_INFO,"Loading Cheat DB");
	DrawFrameFinish();
	ret = QCH_Init();
	if(!ret) {
		DrawFrameStart();
		DrawMessageBox(D_FAIL,"Failed to open cheats.qch. Press A.");
		DrawFrameFinish();
		wait_press_A();
		return 0;
	}
  
	ret = QCH_Parse(NULL);
	if(ret <= 0) {
		DrawFrameStart();
		DrawMessageBox(D_FAIL,"Failed to parse cheat DB. Press A.");
		DrawFrameFinish();
		wait_press_A();
		return 0;
	}
	sprintf(txtbuffer,"Found %d Games",ret);
	DrawFrameStart();
	DrawMessageBox(D_INFO,txtbuffer);
	DrawFrameFinish();
	
	curGameCheats *selected_cheats = memalign(32,sizeof(curGameCheats));
	QCH_Find(&GCMDisk.GameName[0],selected_cheats);
	free(selected_cheats);
  
	QCH_DeInit();  
	return 1;
}

void install_game()
{
	char dumpName[32];
  
	if((curDevice!=SD_CARD) && (curDevice!=IDEEXI)) {
		DrawFrameStart();
		DrawMessageBox(D_INFO, "Only available in SD/IDE Card mode");
		DrawFrameFinish();
		sleep(2);
		return;
	}
	while(PAD_ButtonsHeld(0) & PAD_BUTTON_A);
  
	DrawFrameStart();
	DrawEmptyBox(10,120, vmode->fbWidth-10, 470, COLOR_BLACK);
	WriteFontStyled(640/2, 130, "*** WARNING ***", 1.0f, true, defaultColor);
	WriteFontStyled(640/2, 200, "This program is not responsible for any", 1.0f, true, defaultColor);
	WriteFontStyled(640/2, 230, "loss of data or file system corruption", 1.0f, true, defaultColor);
	WriteFontStyled(640/2, 300, "Press A to Continue, or B to return", 1.0f, true, defaultColor);
	DrawFrameFinish();
	while(1) {
		u16 btns = PAD_ButtonsHeld(0);
		if(btns & PAD_BUTTON_A) {
			while((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
			break;
		}
		if(btns & PAD_BUTTON_B) {
			while((PAD_ButtonsHeld(0) & PAD_BUTTON_B));
			return;
		}
	}
   
	DrawFrameStart();
	DrawMessageBox(D_INFO,"Insert a DVD and press the A button");
	DrawFrameFinish();
	wait_press_A();
    
	DrawFrameStart();
	DrawMessageBox(D_INFO,"Mounting Disc");
	DrawFrameFinish();

	initialize_disc(DISABLE_AUDIO);
	char *gameID = memalign(32,2048);
		
	memset(gameID,0,2048);
	DVD_Read(gameID, 0, 32);
	if(!gameID[0]) {
		free(gameID);
		DrawFrameStart();
		DrawMessageBox(D_FAIL,"Invalid Disc. Press A to exit");
		DrawFrameFinish();
		wait_press_A();
		return;
	}

	sprintf(dumpName, "%s/disc-%s%02X.iso", deviceHandler_initial->name, gameID, gameID[6]);
	free(gameID);
  
	sprintf(txtbuffer,"Creating: %s",dumpName);
	DrawFrameStart();
	DrawMessageBox(D_INFO,txtbuffer);
	DrawFrameFinish();
  
	file_handle* dumpFile = malloc(sizeof(file_handle));
	
	sprintf(dumpFile->name, "%s", (const char*)&dumpName[0]);
	dumpFile->offset = 0;
	dumpFile->size = 0;
	dumpFile->fileAttrib = IS_FILE;
	dumpFile->fileBase = 0;
	dumpFile->fp = 0;
    
	unsigned char *dvdDumpBuffer = (unsigned char*)memalign(32,CHUNK_SIZE);
    
	long long copyTime = gettime();
	long long startTime = gettime();
	unsigned int writtenOffset = 0, bytesWritten = 0;
	
	for(writtenOffset = 0; (writtenOffset+CHUNK_SIZE) < DISC_SIZE; writtenOffset+=CHUNK_SIZE)
	{
		DVD_LowRead64(dvdDumpBuffer, CHUNK_SIZE, writtenOffset);
		bytesWritten = deviceHandler_writeFile(dumpFile,&dvdDumpBuffer[0],CHUNK_SIZE);
		long long timeNow = gettime();
		int etaTime = (((DISC_SIZE-writtenOffset)/1024)/(CHUNK_SIZE/diff_msec(copyTime,timeNow))); //in secs
		sprintf(txtbuffer,(bytesWritten!=CHUNK_SIZE) ? 
                      "Write Error!!!"  :
                      "%dMb dumped @ %.2fmb/s - ETA %02d:%02d",
                      (int)(writtenOffset/(1024*1024)),(float)((float)((float)CHUNK_SIZE/(float)diff_msec(copyTime,timeNow))/1024),
                      (etaTime/60),(etaTime%60));
		DrawFrameStart();
		DrawProgressBar((int)((float)((float)writtenOffset/(float)DISC_SIZE)*100), txtbuffer);
		DrawFrameFinish();
		copyTime = gettime();       
	}
  
	if(writtenOffset<DISC_SIZE) {
		DVD_LowRead64(dvdDumpBuffer, DISC_SIZE-writtenOffset, writtenOffset);
		bytesWritten = deviceHandler_writeFile(dumpFile,&dvdDumpBuffer[0],(DISC_SIZE-writtenOffset));
		if((bytesWritten!=(DISC_SIZE-writtenOffset))) {
			DrawFrameStart();
			DrawProgressBar(100.0f, "Write Error!!!");
			DrawFrameFinish();
		}
		copyTime = gettime();
	}
	deviceHandler_deinit(dumpFile);
	free(dumpFile);
	free(dvdDumpBuffer);
  
	sprintf(txtbuffer,"Copy completed in %d mins. Press A",diff_sec(startTime, gettime())/60);
	DrawFrameStart();
	DrawMessageBox(D_INFO, txtbuffer);
	DrawFrameFinish();
	wait_press_A();
	needsRefresh=1;
}

/* Show info about the game */
int info_game()
{
	DrawFrameStart();
	DrawEmptyBox(75,120, vmode->fbWidth-78, 400, COLOR_BLACK);
	if(GCMDisk.DVDMagicWord == DVD_MAGIC) {
		u32 bannerOffset = showBanner(215, 240, 2);  //Convert & display game banner
		if(bannerOffset) {
			char description[128];
			deviceHandler_seekFile(&curFile,bannerOffset+0x18e0,DEVICE_HANDLER_SEEK_SET);
			if(deviceHandler_readFile(&curFile,&description[0],0x80)==0x80) {
				char * tok = strtok (&description[0],"\n");
				int line = 0;
				float scale = GetTextScaleToFitInWidth(tok,(vmode->fbWidth-78)-75);
				while (tok != NULL)	{
					WriteFontStyled(640/2, 315+(line*scale*24), tok, scale, true, defaultColor);
					tok = strtok (NULL, "\n");
					line++;
				}
			}
		}
	}
	sprintf(txtbuffer,"%s",(GCMDisk.DVDMagicWord != DVD_MAGIC)?getRelativeName(&curFile.name[0]):GCMDisk.GameName);
	float scale = GetTextScaleToFitInWidth(txtbuffer,(vmode->fbWidth-78)-75);
	WriteFontStyled(640/2, 130, txtbuffer, scale, true, defaultColor);

	if((curDevice==SD_CARD)||(curDevice == IDEEXI) ||((curDevice == DVD_DISC) && (dvdDiscTypeInt==ISO9660_DISC))) {
		sprintf(txtbuffer,"Size: %.2fMB", (float)curFile.size/1024/1024);
		WriteFontStyled(640/2, 160, txtbuffer, 0.8f, true, defaultColor);
		if((u32)(curFile.fileBase&0xFFFFFFFF) == -1) {
			sprintf(txtbuffer,"File is Fragmented!");
		}
		else {
			sprintf(txtbuffer,"Position on Disk: %08X",(u32)(curFile.fileBase&0xFFFFFFFF));
		}
		WriteFontStyled(640/2, 180, txtbuffer, 0.8f, true, defaultColor);
	}
	else if(curDevice == DVD_DISC)  {
		sprintf(txtbuffer,"%s DVD disc", dvdDiscTypeStr);
		WriteFontStyled(640/2, 160, txtbuffer, 0.8f, true, defaultColor);
	}
	else if(curDevice == QOOB_FLASH) {
		sprintf(txtbuffer,"Size: %.2fKb (%i blocks)", (float)curFile.size/1024, curFile.size/0x10000);
		WriteFontStyled(640/2, 160, txtbuffer, 0.8f, true, defaultColor);
		sprintf(txtbuffer,"Position on Flash: %08X",(u32)(curFile.fileBase&0xFFFFFFFF));
		WriteFontStyled(640/2, 180, txtbuffer, 0.8f, true, defaultColor);
	}
	else if(curDevice == WODE) {
		sprintf(txtbuffer,"Partition: %i, ISO: %i", (int)(curFile.fileBase>>24)&0xFF,(int)(curFile.fileBase&0xFFFFFF));
		WriteFontStyled(640/2, 160, txtbuffer, 0.8f, true, defaultColor);
	}
	else if(curDevice == MEMCARD) {
		sprintf(txtbuffer,"Size: %.2fKb (%i blocks)", (float)curFile.size/1024, curFile.size/8192);
		WriteFontStyled(640/2, 160, txtbuffer, 0.8f, true, defaultColor);
		sprintf(txtbuffer,"Position on Card: %08X",curFile.offset);
		WriteFontStyled(640/2, 180, txtbuffer, 0.8f, true, defaultColor);
	}
	if(GCMDisk.DVDMagicWord == DVD_MAGIC) {
		sprintf(txtbuffer,"Region [%s] Audio Streaming [%s]",(GCMDisk.CountryCode=='P') ? "PAL":"NTSC",(GCMDisk.AudioStreaming=='\1') ? "YES":"NO");
		WriteFontStyled(640/2, 200, txtbuffer, 0.8f, true, defaultColor);
	}

	WriteFontStyled(640/2, 370, "Cheats(Y) - Exit(B) - Continue (A)", 1.0f, true, defaultColor);
	DrawFrameFinish();
	while((PAD_ButtonsHeld(0) & PAD_BUTTON_B) || (PAD_ButtonsHeld(0) & PAD_BUTTON_Y) || (PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	while(!(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_Y) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	while(1){
		if(PAD_ButtonsHeld(0) & PAD_BUTTON_Y) {
			while(PAD_ButtonsHeld(0) & PAD_BUTTON_Y);
			return cheats_game();
		}
		if((PAD_ButtonsHeld(0) & PAD_BUTTON_B) || (PAD_ButtonsHeld(0) & PAD_BUTTON_A)){
			return (PAD_ButtonsHeld(0) & PAD_BUTTON_A);
		}
	}
}

void settings()
{ 
	int currentSettingPos = 0, maxSettingPos = ((curDevice == SD_CARD) || (curDevice == IDEEXI)) ? 2 : 0;
	while(PAD_ButtonsHeld(0) & PAD_BUTTON_A);
	
	while(1)
	{
		DrawFrameStart();
		DrawEmptyBox (75,120, vmode->fbWidth-78, 400, COLOR_BLACK);
		WriteFontStyled(640/2, 130, "Game Setup", 1.0f, true, defaultColor);
		
		//write out all the settings (dodgy)
		WriteFont(80, 160+(32*1), "Game Video Mode");
		DrawSelectableButton(vmode->fbWidth-230, 160+(32*1), -1, 160+(32*1)+30, getVideoString(), (!currentSettingPos) ? B_SELECTED:B_NOSELECT,-1);
		if((curDevice == SD_CARD) || (curDevice == IDEEXI)) {
			WriteFont(80, 160+(32*2), "Patch Mode");
			DrawSelectableButton(vmode->fbWidth-230, 160+(32*2), -1, 160+(32*2)+30, swissSettings.useHiLevelPatch ? "High Level":"Low Level", (currentSettingPos==1) ? B_SELECTED:B_NOSELECT,-1);
			if(swissSettings.useHiLevelPatch) {
				WriteFont(80, 160+(32*3), "Disable Interrupts");
				DrawSelectableButton(vmode->fbWidth-230, 160+(32*3), -1, 160+(32*3)+30, swissSettings.disableInterrupts ? "Yes":"No", (currentSettingPos==2) ? B_SELECTED:B_NOSELECT,-1);
			} else {
				WriteFont(80, 160+(32*3), "Patch Location");
				DrawSelectableButton(vmode->fbWidth-230, 160+(32*3), -1, 160+(32*3)+30, swissSettings.useHiMemArea ? "High Mem":"Low Mem", (currentSettingPos==2) ? B_SELECTED:B_NOSELECT,-1);
			}
		}
		
		WriteFontStyled(640/2, 370, "Press B to return", 1.0f, true, defaultColor);
		DrawFrameFinish();
		while (!(PAD_ButtonsHeld(0) & PAD_BUTTON_UP) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_DOWN) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B));
		u16 btns = PAD_ButtonsHeld(0);
		if(btns & PAD_BUTTON_RIGHT)
		{
			if(!currentSettingPos)
				swissSettings.curVideoSelection = (swissSettings.curVideoSelection<MAX_VIDEO_MODES) ? (swissSettings.curVideoSelection+1):0;
			else if(currentSettingPos==1)
				swissSettings.useHiLevelPatch^=1;
			else if((currentSettingPos==2) && (!swissSettings.useHiLevelPatch))
				swissSettings.useHiMemArea^=1;
			else if((currentSettingPos==2) && (swissSettings.useHiLevelPatch))
				swissSettings.disableInterrupts^=1;
		}
		if(btns & PAD_BUTTON_LEFT)
		{
			if(!currentSettingPos)
				swissSettings.curVideoSelection = (swissSettings.curVideoSelection>0) ? (swissSettings.curVideoSelection-1):MAX_VIDEO_MODES;
			else if(currentSettingPos==1)
				swissSettings.useHiLevelPatch^=1;
			else if((currentSettingPos==2) && (!swissSettings.useHiLevelPatch))
				swissSettings.useHiMemArea^=1;
			else if((currentSettingPos==2) && (swissSettings.useHiLevelPatch))
				swissSettings.disableInterrupts^=1;
		}
		if(btns & PAD_BUTTON_UP)	currentSettingPos = (currentSettingPos>0) ? (currentSettingPos-1):maxSettingPos;
		if(btns & PAD_BUTTON_DOWN)	currentSettingPos = (currentSettingPos<maxSettingPos) ? (currentSettingPos+1):0;
		if(btns & PAD_BUTTON_B)
			break;
		while ((PAD_ButtonsHeld(0) & PAD_BUTTON_DOWN) || (PAD_ButtonsHeld(0) & PAD_BUTTON_UP) || (PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) || (PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) || (PAD_ButtonsHeld(0) & PAD_BUTTON_B));
	}
	while(PAD_ButtonsHeld(0) & PAD_BUTTON_B);
}

int ask_stop_drive()
{  
	int sel = 0;
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	while(1) {
		doBackdrop();
		DrawEmptyBox(75,190, vmode->fbWidth-78, 330, COLOR_BLACK);
		WriteFontStyled(640/2, 215, "Stop DVD Motor?", 1.0f, true, defaultColor);
		DrawSelectableButton(100, 280, -1, 310, "Yes", (sel==1) ? B_SELECTED:B_NOSELECT,-1);
		DrawSelectableButton(380, 280, -1, 310, "No", (!sel) ? B_SELECTED:B_NOSELECT,-1);
		DrawFrameFinish();
		while (!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B)&& !(PAD_ButtonsHeld(0) & PAD_BUTTON_A));
		u16 btns = PAD_ButtonsHeld(0);
		if((btns & PAD_BUTTON_RIGHT) || (btns & PAD_BUTTON_LEFT)) {
			sel^=1;
		}
		if((btns & PAD_BUTTON_A) || (btns & PAD_BUTTON_B))
			break;
		while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A)));
	}
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	return sel;
}

int ask_set_cheats()
{  
	int sel = 0;
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	while(1) {
		doBackdrop();
		DrawEmptyBox(75,190, vmode->fbWidth-78, 330, COLOR_BLACK);
		WriteFontStyled(640/2, 215, "Load this cheats file?", 1.0f, true, defaultColor);
		DrawSelectableButton(100, 280, -1, 310, "Yes", (sel==1) ? B_SELECTED:B_NOSELECT,-1);
		DrawSelectableButton(380, 280, -1, 310, "No", (!sel) ? B_SELECTED:B_NOSELECT,-1);
		DrawFrameFinish();
		while (!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B)&& !(PAD_ButtonsHeld(0) & PAD_BUTTON_A));
		u16 btns = PAD_ButtonsHeld(0);
		if((btns & PAD_BUTTON_RIGHT) || (btns & PAD_BUTTON_LEFT)) {
			sel^=1;
		}
		if((btns & PAD_BUTTON_A) || (btns & PAD_BUTTON_B))
			break;
		while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A)));
	}
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	return sel;
}

void select_speed()
{ 	
	GC_SD_SPEED=EXI_SPEED32MHZ;
	while(1)
	{
		doBackdrop();
		DrawEmptyBox (75,190, vmode->fbWidth-78, 330, COLOR_BLACK);
		WriteFontStyled(640/2, 215, "Select Speed and press A", 1.0f, true, defaultColor);
		DrawSelectableButton(100, 280, -1, 310, "Compatible", (GC_SD_SPEED==EXI_SPEED16MHZ) ? B_SELECTED:B_NOSELECT,-1);
		DrawSelectableButton(380, 280, -1, 310, "Fast", (GC_SD_SPEED==EXI_SPEED32MHZ) ? B_SELECTED:B_NOSELECT,-1);
		DrawFrameFinish();
		while (!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B)&& !(PAD_ButtonsHeld(0) & PAD_BUTTON_A));
		u16 btns = PAD_ButtonsHeld(0);
		if((btns & PAD_BUTTON_RIGHT) || (btns & PAD_BUTTON_LEFT)){
			if(GC_SD_SPEED==EXI_SPEED16MHZ) GC_SD_SPEED=EXI_SPEED32MHZ;
			else GC_SD_SPEED=EXI_SPEED16MHZ;
		 }
		if((btns & PAD_BUTTON_A) || (btns & PAD_BUTTON_B))
			break;
		while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A)));
	}
	if(curDevice == SD_CARD)
		sdgecko_setSpeed(GC_SD_SPEED);
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
}

int select_slot()
{  
	int slot = 0;
	while(1) {
		doBackdrop();
		DrawEmptyBox(75,190, vmode->fbWidth-78, 330, COLOR_BLACK);
		WriteFontStyled(640/2, 215, "Select Slot and press A", 1.0f, true, defaultColor);
		DrawSelectableButton(100, 280, -1, 310, "Slot A", (slot==0) ? B_SELECTED:B_NOSELECT,-1);
		DrawSelectableButton(380, 280, -1, 310, "Slot B", (slot==1) ? B_SELECTED:B_NOSELECT,-1);
		DrawFrameFinish();
		while (!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B)&& !(PAD_ButtonsHeld(0) & PAD_BUTTON_A));
		u16 btns = PAD_ButtonsHeld(0);
		if((btns & PAD_BUTTON_RIGHT) || (btns & PAD_BUTTON_LEFT)) {
			slot^=1;
		}
		if((btns & PAD_BUTTON_A) || (btns & PAD_BUTTON_B))
			break;
		while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A)));
	}
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	return slot;
}

void select_copy_device()
{  
	while(1) {
		doBackdrop();
		DrawEmptyBox(20,190, vmode->fbWidth-20, 355, COLOR_BLACK);
		WriteFontStyled(640/2, 195, "Select destination device and press A", 1.0f, true, defaultColor);
		if(curCopyDevice==DEST_SD_CARD) {
			DrawSelectableButton(170, 230, 450, 340, "SDGecko", B_NOSELECT,COLOR_BLACK);
			DrawImage(TEX_SDSMALL, 360, 245, 60, 80, 0, 0.0f, 1.0f, 0.0f, 1.0f);
		}
		else if(curCopyDevice==DEST_IDEEXI) {
			DrawSelectableButton(170, 230, 450, 340, "Ide-Exi", B_NOSELECT,COLOR_BLACK);
			DrawImage(TEX_HDD, 340, 245, 80, 80, 0, 0.0f, 1.0f, 0.0f, 1.0f);
		}
		else if(curCopyDevice==DEST_MEMCARD) {
			DrawSelectableButton(170, 230, 450, 340, "Memory Card",B_NOSELECT,COLOR_BLACK);
		}
		if(curCopyDevice != 2) {
			WriteFont(520, 300, "->");
		}
		if(curCopyDevice != 0) {
			WriteFont(100, 300, "<-");
		}
		DrawFrameFinish();
		while (!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B)&& !(PAD_ButtonsHeld(0) & PAD_BUTTON_A));
		u16 btns = PAD_ButtonsHeld(0);
		if((btns & PAD_BUTTON_RIGHT) && curCopyDevice < 2)
			curCopyDevice++;
		if((btns & PAD_BUTTON_LEFT) && curCopyDevice > 0)
			curCopyDevice--;
		if((btns & PAD_BUTTON_A) || (btns & PAD_BUTTON_B))
			break;
		while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A)));
	}
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	// Deinit any existing deviceHandler state
	if(deviceHandler_dest_deinit) deviceHandler_dest_deinit( deviceHandler_dest_initial );
	// Change all the deviceHandler pointers based on the current device
	int slot = 0;
	switch(curCopyDevice) {
		case DEST_SD_CARD:
		case DEST_IDEEXI:
			select_speed();
			slot = select_slot();
			if(curCopyDevice==DEST_IDEEXI)
				deviceHandler_dest_initial = !slot ? &initial_IDE0 : &initial_IDE1;
			else
				deviceHandler_dest_initial = !slot ? &initial_SD0 : &initial_SD1;
			deviceHandler_dest_readDir  =  deviceHandler_FAT_readDir;
			deviceHandler_dest_readFile =  deviceHandler_FAT_readFile;
			deviceHandler_dest_writeFile=  deviceHandler_FAT_writeFile;
			deviceHandler_dest_deleteFile=  deviceHandler_FAT_deleteFile;
			deviceHandler_dest_seekFile =  deviceHandler_FAT_seekFile;
			deviceHandler_dest_setupFile=  deviceHandler_FAT_setupFile;
			deviceHandler_dest_init     =  deviceHandler_FAT_init;
			deviceHandler_dest_deinit   =  deviceHandler_FAT_deinit;
		break;
		case DEST_MEMCARD:
			slot = select_slot();
			deviceHandler_dest_initial = !slot ? &initial_CARDA : &initial_CARDB;
			deviceHandler_dest_readDir  =  deviceHandler_CARD_readDir;
			deviceHandler_dest_readFile =  deviceHandler_CARD_readFile;
			deviceHandler_dest_writeFile=  deviceHandler_CARD_writeFile;
			deviceHandler_dest_deleteFile=  deviceHandler_CARD_deleteFile;
			deviceHandler_dest_seekFile =  deviceHandler_CARD_seekFile;
			deviceHandler_dest_setupFile=  deviceHandler_CARD_setupFile;
			deviceHandler_dest_init     =  deviceHandler_CARD_init;
			deviceHandler_dest_deinit   =  deviceHandler_CARD_deinit;
		break;
	}
}

void select_device()
{  
	dvdDiscTypeStr = NotInitStr;
	while(1) {
		doBackdrop();
		DrawEmptyBox(20,190, vmode->fbWidth-20, 355, COLOR_BLACK);
		WriteFontStyled(640/2, 195, "Select device and press A", 1.0f, true, defaultColor);
		if(curDevice==DVD_DISC) {
			DrawSelectableButton(170, 230, 450, 340, "DVD Disc", B_NOSELECT,COLOR_BLACK);
			DrawImage(TEX_GCDVDSMALL, 340, 250, 80, 79, 0, 0.0f, 1.0f, 0.0f, 1.0f);
		}
		else if(curDevice==SD_CARD) {
			DrawSelectableButton(170, 230, 450, 340, "SDGecko", B_NOSELECT,COLOR_BLACK);
			DrawImage(TEX_SDSMALL, 360, 245, 60, 80, 0, 0.0f, 1.0f, 0.0f, 1.0f);
		}
		else if(curDevice==IDEEXI) {
			DrawSelectableButton(170, 230, 450, 340, "Ide-Exi", B_NOSELECT,COLOR_BLACK);
			DrawImage(TEX_HDD, 340, 245, 80, 80, 0, 0.0f, 1.0f, 0.0f, 1.0f);
		}
		else if(curDevice==QOOB_FLASH) {
			DrawSelectableButton(170, 230, 450, 340, "Qoob PRO",B_NOSELECT,COLOR_BLACK);
			DrawImage(TEX_QOOB, 350, 245, 70, 80, 0, 0.0f, 1.0f, 0.0f, 1.0f);
		}
		else if(curDevice==WODE) {
			DrawSelectableButton(170, 230, 450, 340, "WODE",B_NOSELECT,COLOR_BLACK);
			DrawImage(TEX_WODEIMG, 290, 245, 146, 72, 0, 0.0f, 1.0f, 0.0f, 1.0f);
		}
		else if(curDevice==MEMCARD) {
			DrawSelectableButton(170, 230, 450, 340, "Memory Card",B_NOSELECT,COLOR_BLACK);
		}
		if(curDevice != 5) {
			WriteFont(520, 300, "->");
		}
		if(curDevice != 0) {
			WriteFont(100, 300, "<-");
		}
		DrawFrameFinish();
		while (!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B)&& !(PAD_ButtonsHeld(0) & PAD_BUTTON_A));
		u16 btns = PAD_ButtonsHeld(0);
		if((btns & PAD_BUTTON_RIGHT) && curDevice < 5)
			curDevice++;
		if((btns & PAD_BUTTON_LEFT) && curDevice > 0)
			curDevice--;
		if((btns & PAD_BUTTON_A) || (btns & PAD_BUTTON_B))
			break;
		while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A)));
	}
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	// Deinit any existing deviceHandler state
	if(deviceHandler_deinit) deviceHandler_deinit( deviceHandler_initial );
	// Change all the deviceHandler pointers based on the current device
	int slot = 0;
	switch(curDevice) {
		case SD_CARD:
		case IDEEXI:
			select_speed();
			slot = select_slot();
			if(curDevice==IDEEXI)
				deviceHandler_initial = !slot ? &initial_IDE0 : &initial_IDE1;
			else
				deviceHandler_initial = !slot ? &initial_SD0 : &initial_SD1;
			deviceHandler_readDir  =  deviceHandler_FAT_readDir;
			deviceHandler_readFile =  deviceHandler_FAT_readFile;
			deviceHandler_writeFile=  deviceHandler_FAT_writeFile;
			deviceHandler_deleteFile=  deviceHandler_FAT_deleteFile;
			deviceHandler_seekFile =  deviceHandler_FAT_seekFile;
			deviceHandler_setupFile=  deviceHandler_FAT_setupFile;
			deviceHandler_init     =  deviceHandler_FAT_init;
			deviceHandler_deinit   =  deviceHandler_FAT_deinit;
		break;
		case DVD_DISC:
			deviceHandler_initial = &initial_DVD;
			deviceHandler_readDir  =  deviceHandler_DVD_readDir;
			deviceHandler_readFile =  deviceHandler_DVD_readFile;
			deviceHandler_seekFile =  deviceHandler_DVD_seekFile;
			deviceHandler_setupFile=  deviceHandler_DVD_setupFile;
			deviceHandler_init     =  deviceHandler_DVD_init;
			deviceHandler_deinit   =  deviceHandler_DVD_deinit;
			deviceHandler_deleteFile = NULL;
 		break;
 		case QOOB_FLASH:
			deviceHandler_initial = &initial_Qoob;
			deviceHandler_readDir  =  deviceHandler_Qoob_readDir;
			deviceHandler_readFile =  deviceHandler_Qoob_readFile;
			deviceHandler_seekFile =  deviceHandler_Qoob_seekFile;
			deviceHandler_setupFile=  deviceHandler_Qoob_setupFile;
			deviceHandler_init     =  deviceHandler_Qoob_init;
			deviceHandler_deinit   =  deviceHandler_Qoob_deinit;
			deviceHandler_deleteFile = NULL;
 		break;
 		case WODE:
			deviceHandler_initial = &initial_WODE;
			deviceHandler_readDir  =  deviceHandler_WODE_readDir;
			deviceHandler_readFile =  deviceHandler_WODE_readFile;
			deviceHandler_seekFile =  deviceHandler_WODE_seekFile;
			deviceHandler_setupFile=  deviceHandler_WODE_setupFile;
			deviceHandler_init     =  deviceHandler_WODE_init;
			deviceHandler_deinit   =  deviceHandler_WODE_deinit;
			deviceHandler_deleteFile = NULL;
 		break;
		case MEMCARD:
			slot = select_slot();
			deviceHandler_initial = !slot ? &initial_CARDA : &initial_CARDB;
			deviceHandler_readDir  =  deviceHandler_CARD_readDir;
			deviceHandler_readFile =  deviceHandler_CARD_readFile;
			deviceHandler_writeFile=  deviceHandler_CARD_writeFile;
			deviceHandler_deleteFile=  deviceHandler_CARD_deleteFile;
			deviceHandler_seekFile =  deviceHandler_CARD_seekFile;
			deviceHandler_setupFile=  deviceHandler_CARD_setupFile;
			deviceHandler_init     =  deviceHandler_CARD_init;
			deviceHandler_deinit   =  deviceHandler_CARD_deinit;
		break;
	}
	memcpy(&curFile, deviceHandler_initial, sizeof(file_handle));
}

