#include <stdio.h>
#include <gccore.h>		/*** Wrapper to include common libogc headers ***/
#include <ogcsys.h>		/*** Needed for console support ***/
#include <ogc/color.h>
#include <ogc/exi.h>
#include <ogc/usbgecko.h>
#include <ogc/video_types.h>
#include <sdcard/card_cmn.h>
#include <ogc/machine/processor.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <sys/types.h>
#include <unistd.h>
#include "main.h"
#include "swiss.h"
#include "font.h"
#include "exi.h"
#include "dvd.h"
#include "gui/FrameBufferMagic.h"
#include "gui/IPLFontWrite.h"
#include "devices/deviceHandler.h"

/* SD-Boot vars */
GXRModeObj *vmode = NULL;				//Graphics Mode Object
u32 *xfb[2] = { NULL, NULL };   //Framebuffers
int whichfb = 0;       		 	    //Frame buffer toggle
u8 driveVersion[8];
static file_handle* allFiles;   //all the files
int curMenuLocation = ON_FILLIST; //where are we on the screen?
int files = 0;                  //number of files in a directory
int curMenuSelection = 0;	      //menu selection
int curSelection = 0;		        //game selection
int needsDeviceChange = 0;
int needsRefresh = 0;
SwissSettings swissSettings;

static void ProperScanPADS()	{ 
	PAD_ScanPads(); 
}

/* Initialise Video, PAD, DVD, Font */
void* Initialise (void)
{
	VIDEO_Init ();
	PAD_Init ();  
	DVD_Init(); 
	*(volatile unsigned long*)0xcc00643c = 0x00000000; //allow 32mhz exi bus
	
	// Disable IPL modchips to allow access to IPL ROM fonts
	ipl_set_config(6); 
	usleep(1000); //wait for modchip to disable (overkill)
	
	
	__SYS_ReadROM(IPLInfo,256,0);	// Read IPL tag
	
	if(VIDEO_HaveComponentCable()) {
		vmode = &TVNtsc480Prog; //Progressive 480p
		videoStr = ProgStr;
	}
	else {
		//try to use the IPL region
		if(strstr(IPLInfo,"PAL")!=NULL) {
			vmode = &TVPal528IntDf;         //PAL
			videoStr = PALStr;
		}
		else if(strstr(IPLInfo,"NTSC")!=NULL) {
			vmode = &TVNtsc480IntDf;        //NTSC
			videoStr = NTSCStr;
		}
		else {
			vmode = VIDEO_GetPreferredMode(NULL); //Last mode used
			videoStr = UnkStr;
		}
	}

	VIDEO_Configure (vmode);
	xfb[0] = (u32 *) MEM_K0_TO_K1 (SYS_AllocateFramebuffer (vmode));
	xfb[1] = (u32 *) MEM_K0_TO_K1 (SYS_AllocateFramebuffer (vmode));
	VIDEO_ClearFrameBuffer (vmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer (vmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer (xfb[0]);
	VIDEO_SetPostRetraceCallback (ProperScanPADS);
	VIDEO_SetBlack (0);
	VIDEO_Flush ();
	VIDEO_WaitVSync ();
	if (vmode->viTVMode & VI_NON_INTERLACE) {
		VIDEO_WaitVSync ();
	}
	init_font();
	whichfb = 0;
	
	 drive_version(&driveVersion[0]);
	
	if(!driveVersion[0]) {
		// Reset DVD if there was a modchip
		whichfb ^= 1;
		WriteFont(55,250, "Initialise DVD from cold boot.. (Qoob user?)");
		VIDEO_SetNextFramebuffer (xfb[whichfb]);
		VIDEO_Flush ();
		VIDEO_WaitVSync ();
		if(vmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
		DVD_Reset(DVD_RESETHARD);
		dvd_read_id();
		drive_version(&driveVersion[0]);
		dvd_motor_off();
	}
	
	return xfb[0];
}

void main_loop()
{ 
	int i = 0,max,j;	
	
	while(PAD_ButtonsHeld(0) & PAD_BUTTON_A);
	select_device();
	// Make sure the device is ready before we browse the filesystem
	deviceHandler_deinit( deviceHandler_initial );
	deviceHandler_init( deviceHandler_initial );
  
	while(1) {
		if(needsRefresh) {
			curSelection=0; files=0; curMenuSelection=0; needsRefresh = 0;
			
			// Read the directory/device TOC
			if(allFiles){ free(allFiles); allFiles = NULL; }
			files = deviceHandler_readDir(&curFile, &allFiles, -1);
			if(files<1) { break;}
			curMenuLocation=ON_FILLIST;
		}
		while(PAD_ButtonsHeld(0) & PAD_BUTTON_A);
		doBackdrop();
		// print files
		i = MIN(MAX(0,curSelection-FILES_PER_PAGE/2),MAX(0,files-FILES_PER_PAGE));
		max = MIN(files, MAX(curSelection+FILES_PER_PAGE/2,FILES_PER_PAGE));
		for(j = 0; i<max; ++i,++j){
			DrawFileBrowserButton(50,90+(j*50), 550, 90+(j*50)+50, getRelativeName(&allFiles[i].name[0]), &allFiles[i], (i == curSelection)?((curMenuLocation==ON_FILLIST)?B_SELECTED:B_NOSELECT):B_NOSELECT, -1);
		}
		// print menu
		DrawMenuButtons((curMenuLocation==ON_OPTIONS)?curMenuSelection:-1);
		DrawFrameFinish();

		u16 btns = PAD_ButtonsHeld(0);
		if(curMenuLocation==ON_OPTIONS) {
			if(btns & PAD_BUTTON_LEFT){	curMenuSelection = (--curMenuSelection < 0) ? (MENU_MAX-1) : curMenuSelection;}
			else if(btns & PAD_BUTTON_RIGHT){curMenuSelection = (curMenuSelection + 1) % MENU_MAX;	}
		}
		if((btns & PAD_BUTTON_B)||(curMenuLocation==ON_FILLIST))	{
			while(PAD_ButtonsHeld(0) & PAD_BUTTON_B);
			curMenuLocation=ON_FILLIST;
			textFileBrowser(&allFiles, files);
		}
		else if(btns & PAD_BUTTON_A) {
			//handle menu event
			switch(curMenuSelection) {
				case 0:		// Device change
					needsDeviceChange = 1;  //Change from SD->DVD or vice versa
					break;
				case 1:		// Settings
					settings();
					break;
				case 2:		// Credits
					show_info();
					break;
				case 3:
					needsRefresh=1;
					break;
				case 4:
					
					break;
			}
			
		}
		while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT)));
		if(needsDeviceChange) {
			break;
		}
	}
}

/****************************************************************************
* Main
****************************************************************************/
int main () 
{
	void *fb;   
	fb = Initialise();
	if(!fb) {
		return -1;
	}
	
	// Setup defaults
	swissSettings.useHiMemArea = 0;
	swissSettings.disableInterrupts = 1;
	swissSettings.useHiLevelPatch = 0;
	swissSettings.debugUSB = 0;
	swissSettings.curVideoSelection = AUTO;
	
	//debugging stuff
	if(swissSettings.debugUSB) {
		if(usb_isgeckoalive(1)) {
			usb_flush(1);
		}
		sprintf(txtbuffer, "Arena Size: %iKb\r\n",(SYS_GetArena1Hi()-SYS_GetArena1Lo())/1024);
		print_gecko(txtbuffer);
	}

	while(1) {
		needsRefresh = 1;
		needsDeviceChange = 0;
		main_loop();
	}
	return 0;
}
