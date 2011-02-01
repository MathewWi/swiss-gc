/* deviceHandler-DVD.c
	- device implementation for DVD (Discs)
	by emu_kidid
 */


#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <ogc/dvd.h>
#include <ogc/machine/processor.h>
#include "deviceHandler.h"
#include "gui/FrameBufferMagic.h"
#include "gui/IPLFontWrite.h"
#include "main.h"
#include "dvd.h"

#define OFFSET_NOTSET 0
#define OFFSET_SET    1

file_handle initial_DVD =
	{ "\\",     // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  DRV_ERROR
};

static char error_str[256];
static int dvd_init = 0;
char *dvdDiscTypeStr = NotInitStr;
int dvdDiscTypeInt = 0;
int drive_status = 0;

char *dvd_error_str()
{
  unsigned int err = dvd_get_error();
  if(!err) return "OK";
  
  memset(&error_str[0],0,256);
  switch(err>>24) {
    case 0:
      break;
    case 1:
      strcpy(&error_str[0],"Lid open");
      break;
    case 2:
      strcpy(&error_str[0],"No disk/Disk changed");
      break;
    case 3:
      strcpy(&error_str[0],"No disk");
      break;  
    case 4:
      strcpy(&error_str[0],"Motor off");
      break;  
    case 5:
      strcpy(&error_str[0],"Disk not initialized");
      break;
  }
  switch(err&0xFFFFFF) {
    case 0:
      break;
    case 0x020400:
      strcat(&error_str[0]," Motor Stopped");
      break;
    case 0x020401:
      strcat(&error_str[0]," Disk ID not read");
      break;
    case 0x023A00:
      strcat(&error_str[0]," Medium not present / Cover opened");
      break;  
    case 0x030200:
      strcat(&error_str[0]," No Seek complete");
      break;  
    case 0x031100:
      strcat(&error_str[0]," UnRecoverd read error");
      break;
    case 0x040800:
      strcat(&error_str[0]," Transfer protocol error");
      break;
    case 0x052000:
      strcat(&error_str[0]," Invalid command operation code");
      break;
    case 0x052001:
      strcat(&error_str[0]," Audio Buffer not set");
      break;  
    case 0x052100:
      strcat(&error_str[0]," Logical block address out of range");
      break;  
    case 0x052400:
      strcat(&error_str[0]," Invalid Field in command packet");
      break;
    case 0x052401:
      strcat(&error_str[0]," Invalid audio command");
      break;
    case 0x052402:
      strcat(&error_str[0]," Configuration out of permitted period");
      break;
    case 0x053000:
      strcat(&error_str[0]," DVD-R"); //?
      break;
    case 0x053100:
      strcat(&error_str[0]," Wrong Read Type"); //?
      break;
    case 0x056300:
      strcat(&error_str[0]," End of user area encountered on this track");
      break;  
    case 0x062800:
      strcat(&error_str[0]," Medium may have changed");
      break;  
    case 0x0B5A01:
      strcat(&error_str[0]," Operator medium removal request");
      break;
  }
  if(!error_str[0])
    sprintf(&error_str[0],"Unknown %08X",err);
  return &error_str[0];
}

int initialize_disc(u32 streaming) {
  int patched = NORMAL_MODE;
  DrawFrameStart();
  DrawProgressBar(33, "DVD Is Initializing");
  DrawFrameFinish();
	if(mfpvr() == GC_CPU_VERSION)
	{
	  if((dvd_get_error()>>24) > 1) { //we have an error (besides open lid) Try to enable patches
    	DrawFrameStart();
      DrawProgressBar(66, "Enabling Patches");
      DrawFrameFinish();
  	  DVD_Mount();
    	dvd_read_id();
    	if(!dvd_get_error()) {
    	  patched=DEBUG_MODE;
  	  }
  	}
  	else if((dvd_get_error()>>24) == 1) {  // Lid is open, tell the user!
    	DrawFrameStart();
      sprintf(txtbuffer, "Error %s. Press A.",dvd_error_str());
      DrawMessageBox(D_FAIL, txtbuffer);
      DrawFrameFinish();
      wait_press_A();
    	return DRV_ERROR;
  	}
  	if((streaming == ENABLE_AUDIO) || (streaming == DISABLE_AUDIO)) {
    	dvd_set_streaming(streaming);
  	}
  	else {
    	dvd_set_streaming(*(char*)0x80000008);
  	}
  	xeno_disable();
  }
	else {  //Wii, in GC Mode
	  DVD_Reset(DVD_RESETHARD);
	  dvd_read_id();
  }
  if(dvd_get_error()) //no disc, or no game id. 
	{
    DrawFrameStart();
    sprintf(txtbuffer, "Error: %s",dvd_error_str());
    DrawMessageBox(D_FAIL, txtbuffer);
    DrawFrameFinish();
  	wait_press_A();
    return DRV_ERROR;
	}
	DrawFrameStart();
  DrawProgressBar(100, "Initialization Complete");
  DrawFrameFinish();
	return patched;
}

int gettype_disc() {  
  const char *dvdid = (const char*)0x80000000;
  
  //Attempt to determine the disc type from the 32 byte header at 0x80000000
  if (strncmp(dvdid, "COBRAM", 6) == 0) {
    dvdDiscTypeStr = CobraStr;
    return COBRA_MULTIGAME_DISC;  //"COBRAM" is Cobra MultiBoot
  }
  
  if ((strncmp(dvdid, "GGCOSD", 6) == 0) || (strncmp(dvdid, "RGCOSD", 6) == 0)) {
    dvdDiscTypeStr = GCOSD5Str;
    return GCOSD5_MULTIGAME_DISC;  //"RGCOSD" or "GGCOSD" is GCOS Single Layer MultiGame Disc
  }
  
  if (strncmp(dvdid, "GCOPDVD9", 8) == 0) {
    dvdDiscTypeStr = GCOSD9Str;
    return GCOSD9_MULTIGAME_DISC;  //"GCOPDVD9" is GCOS Dual Layer MultiGame Disc
  }
  
  //Attempt to identify a ISO9660 disc
  char *checkBuf = (char*)memalign(32,32);
  DVD_Read((void*)checkBuf,32769,32);
  if (strncmp(checkBuf, "CD001", 5) == 0) {
    dvdDiscTypeStr = ISO9660Str;
    return ISO9660_DISC;  //"CD001" at 32769 is iso9660
  }
  free(checkBuf);

  if ((*(volatile unsigned int*)(0x8000001C)) == DVD_MAGIC) {
    if(dvdid[6]) {
      dvdDiscTypeStr = MDStr;
      return MULTIDISC_DISC;  //Gamecube 2 Disc (or more?) Game
    }
    else {
      dvdDiscTypeStr = GCDStr;
      return GAMECUBE_DISC;   //Gamecube Game
    }
  }
  
  dvdDiscTypeStr = UnkStr;
  return UNKNOWN_DISC;
}
	
int deviceHandler_DVD_readDir(file_handle* ffile, file_handle** dir){	
  
  unsigned int i = 0, isGC = (mfpvr()==GC_CPU_VERSION);
  unsigned int  *tmpTable = NULL;
  char *tmpName  = NULL;
  u64 tmpOffset = 0LL;
  
  int num_entries = 0, ret = 0, num = 0;
  
  if(dvd_get_error() || !dvd_init) { //if some error
    ret = initialize_disc(ENABLE_BYDISK);
    if(ret == DRV_ERROR) {    //try init
      return -1; //fail
    }
    dvd_init = 1;
  }
  
  dvdDiscTypeInt = gettype_disc();
  if(dvdDiscTypeInt == UNKNOWN_DISC) {
    return -1;
  }
  
  //GCOS or Cobra MultiGame DVD Disc
  if((dvdDiscTypeInt == COBRA_MULTIGAME_DISC)||(dvdDiscTypeInt == GCOSD5_MULTIGAME_DISC)||(dvdDiscTypeInt == GCOSD9_MULTIGAME_DISC)) {
    
    //Read in the whole table of offsets
    tmpTable = (unsigned int*)memalign(32,MAX_MULTIGAME*4);
    tmpName = (char*)memalign(32,512);
    memset(tmpTable,0,sizeof(tmpTable));
    memset(tmpName,0,sizeof(tmpName));
    DVD_Read(&tmpTable[0],MULTIGAME_TABLE_OFFSET,MAX_MULTIGAME*4);
    
    // count entries
    for(i = 0; i < MAX_MULTIGAME; i++) {
	    tmpOffset = (dvdDiscTypeInt == GCOSD9_MULTIGAME_DISC) ? (tmpTable[i]<<2):(tmpTable[i]);
	    if((tmpOffset) && (tmpOffset%(isGC?0x8000:0x20000)==0) && (tmpOffset<(isGC?DISC_SIZE:WII_D9_SIZE))) {
				num_entries++;
			}
		}
		
		if(num_entries <= 0) { return num_entries; }
		
		// malloc the directory structure
		*dir = malloc( num_entries * sizeof(file_handle) );
		
		// parse entries
		for(i = 0; i < MAX_MULTIGAME; i++) {
	    tmpOffset = (dvdDiscTypeInt == GCOSD9_MULTIGAME_DISC) ? (tmpTable[i]<<2):(tmpTable[i]);
	    if((tmpOffset) && (tmpOffset%(isGC?0x8000:0x20000)==0) && (tmpOffset<(isGC?DISC_SIZE:WII_D9_SIZE))) {
				DVD_Read(&tmpName[0],tmpOffset+32, 512);
				strcpy( (*dir)[num].name, &tmpName[0] );
				(*dir)[num].fileBase = tmpOffset;
		    (*dir)[num].offset = 0;
		    (*dir)[num].size   = DISC_SIZE;
		    (*dir)[num].fileAttrib	 = IS_FILE;
		    num++;
		}
		free(tmpTable);
		free(tmpName);
    }
  }
  else if((dvdDiscTypeInt == GAMECUBE_DISC) || (dvdDiscTypeInt == MULTIDISC_DISC)) {
    num_entries = 1;
    *dir = malloc( num_entries * sizeof(file_handle) );
    DVD_Read((*dir)[i].name, 32, 128);
    (*dir)[i].fileBase = 0;
    (*dir)[i].offset = 0;
    (*dir)[i].size = DISC_SIZE;
    (*dir)[i].fileAttrib = IS_FILE;
  }
  else if(dvdDiscTypeInt == ISO9660_DISC) {
    // Call the corresponding DVD function
	  num_entries = dvd_read_directoryentries(ffile->fileBase,ffile->size);
	  // If it was not successful, just return the error
	  if(num_entries <= 0) return -1;
	  // Convert the DVD "file" data to fileBrowser_files
  	*dir = malloc( num_entries * sizeof(file_handle) );
  	int i;
  	for(i=0; i<num_entries; ++i){
  		strcpy( (*dir)[i].name, DVDToc->file[i].name );
  		(*dir)[i].fileBase = (uint64_t)(((uint64_t)DVDToc->file[i].sector)*2048);
  		(*dir)[i].offset = 0;
  		(*dir)[i].size   = DVDToc->file[i].size;
  		(*dir)[i].fileAttrib	 = IS_FILE;
  		if(DVDToc->file[i].flags == 2)//on DVD, 2 is a dir
  			(*dir)[i].fileAttrib   = IS_DIR; 
  		if((*dir)[i].name[strlen((*dir)[i].name)-1] == '/' )
  			(*dir)[i].name[strlen((*dir)[i].name)-1] = 0;	//get rid of trailing '/'
  	}
  	//kill the large TOC so we can have a lot more memory ingame (256k more)
  	free(DVDToc);
    DVDToc = NULL;
  		
  	if(strlen((*dir)[0].name) == 0)
  		strcpy( (*dir)[0].name, ".." );
  }
  return num_entries;
}

int deviceHandler_DVD_seekFile(file_handle* file, unsigned int where, unsigned int type){
	if(type == DEVICE_HANDLER_SEEK_SET) file->offset = where;
	else if(type == DEVICE_HANDLER_SEEK_CUR) file->offset += where;
	return file->offset;
}

int deviceHandler_DVD_readFile(file_handle* file, void* buffer, unsigned int length){
	u64 actualOffset = file->fileBase+file->offset;
  
  if(file->status == OFFSET_SET) {
  	actualOffset = file->offset;
	}
  int bytesread = DVD_Read(buffer,actualOffset,length);
	if(bytesread > 0) {
		file->offset += bytesread;
	}
	return bytesread;
}

void deviceHandler_DVD_setupFile(file_handle* file, file_handle* file2) {
  if((dvdDiscTypeInt == COBRA_MULTIGAME_DISC)||(dvdDiscTypeInt == GCOSD5_MULTIGAME_DISC)||(dvdDiscTypeInt == GCOSD9_MULTIGAME_DISC)) {
  	dvd_set_offset(file->fileBase);
  	file->status = OFFSET_SET;
	}
}

int deviceHandler_DVD_init(file_handle* file){
  file->status = initialize_disc(ENABLE_BYDISK);
  if(file->status == DRV_ERROR){
      DrawFrameStart();
      DrawMessageBox(D_FAIL,"Failed to mount DVD. Press A");
      DrawFrameFinish();
      wait_press_A();
      return file->status;
  }
  dvd_init=1;
  return file->status;
}

int deviceHandler_DVD_deinit(file_handle* file) {
	return 0;
}

