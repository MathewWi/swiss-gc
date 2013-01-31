/* deviceHandler-FAT.c
	- device implementation for FAT (via SDGecko/IDE-EXI)
	by emu_kidid
 */

#include <fat.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <ogc/dvd.h>
#include <sys/dir.h>
#include <ogc/machine/processor.h>
#include <sdcard/gcsd.h>
#include "deviceHandler.h"
#include "gui/FrameBufferMagic.h"
#include "gui/IPLFontWrite.h"
#include "swiss.h"
#include "main.h"
#include "ata.h"
#include "frag.h"
#include "patcher.h"

const DISC_INTERFACE* carda = &__io_gcsda;
const DISC_INTERFACE* cardb = &__io_gcsdb;
const DISC_INTERFACE* ideexia = &__io_ataa;
const DISC_INTERFACE* ideexib = &__io_atab;
extern void sdgecko_initIODefault();

file_handle initial_SD0 =
	{ "sda:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};
	
file_handle initial_SD1 =
	{ "sdb:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};
	
file_handle initial_IDE0 =
	{ "idea:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};
	
file_handle initial_IDE1 =
	{ "ideb:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};

int deviceHandler_FAT_readDir(file_handle* ffile, file_handle** dir, unsigned int type){	
  
	DIR* dp = opendir( ffile->name );
	if(!dp) return -1;
	struct dirent *entry;
	struct stat fstat;
	
	// Set everything up to read
	int num_entries = 1, i = 1;
	char file_name[1024];
	*dir = malloc( num_entries * sizeof(file_handle) );
	memset(*dir,0,sizeof(file_handle) * num_entries);
	(*dir)[0].fileAttrib = IS_SPECIAL;
	strcpy((*dir)[0].name, "..");
	
	// Read each entry of the directory
	while( (entry = readdir(dp)) != NULL ){
		if(strlen(entry->d_name) <= 2  && (entry->d_name[0] == '.' || entry->d_name[1] == '.')) {
			continue;
		}
		memset(&file_name[0],0,1024);
		sprintf(&file_name[0], "%s/%s", ffile->name, entry->d_name);
		stat(&file_name[0],&fstat);
		// Do we want this one?
		if(type == -1 || ((fstat.st_mode & S_IFDIR) ? (type==IS_DIR) : (type==IS_FILE))) {
			// Make sure we have room for this one
			if(i == num_entries){
				++num_entries;
				*dir = realloc( *dir, num_entries * sizeof(file_handle) ); 
			}
			sprintf((*dir)[i].name, "%s/%s", ffile->name, entry->d_name);
			(*dir)[i].offset = 0;
			(*dir)[i].size     = fstat.st_size;
			(*dir)[i].fileAttrib   = (fstat.st_mode & S_IFDIR) ? IS_DIR : IS_FILE;
			(*dir)[i].fp = 0;
			(*dir)[i].fileBase = 0;
			++i;
		}
	}
	
	closedir(dp);

	int isSDCard = ffile->name[0] == 's';
	if(isSDCard) {
		int slot = (ffile->name[2] == 'b');
	    // Set the card type (either block addressed (0) or byte addressed (1))
	    SDHCCard = sdgecko_getAddressingType(slot);
	    // set the page size to 512 bytes
	    if(sdgecko_setPageSize(slot, 512)!=0) {
	      DrawFrameStart();
	      DrawMessageBox(D_WARN,"Failed to set the page size");
	      DrawFrameFinish();
	      sleep(2);
	    }
	}
	
  return num_entries;
}

int deviceHandler_FAT_seekFile(file_handle* file, unsigned int where, unsigned int type){
	if(type == DEVICE_HANDLER_SEEK_SET) file->offset = where;
	else if(type == DEVICE_HANDLER_SEEK_CUR) file->offset += where;
	return file->offset;
}

int deviceHandler_FAT_readFile(file_handle* file, void* buffer, unsigned int length){
  	if(!file->fp) {
		file->fp = fopen( file->name, "r+" );
	}
	if(!file->fp) return -1;
	
	fseek(file->fp, file->offset, SEEK_SET);
	int bytes_read = fread(buffer, 1, length, file->fp);
	if(bytes_read > 0) file->offset += bytes_read;
	
	return bytes_read;
}

int deviceHandler_FAT_writeFile(file_handle* file, void* buffer, unsigned int length){
	if(!file->fp) {
		// Append
		file->fp = fopen( file->name, "r+" );
		if(file->fp <= 0) {
			// Create
			file->fp = fopen( file->name, "wb" );
		}
	}
	if(!file->fp) return -1;
	fseek(file->fp, file->offset, SEEK_SET);
		
	int bytes_written = fwrite(buffer, 1, length, file->fp);
	if(bytes_written > 0) file->offset += bytes_written;
	
	return bytes_written;
}

int unlockedDVD = 0;
void unlockCB() {
	unlockedDVD = 1;
}

void deviceHandler_FAT_setupFile(file_handle* file, file_handle* file2) {
	// If there are 2 discs, we only allow 5 fragments per disc.
	int maxFrags = file2 ? ((VAR_FRAG_SIZE/12)/2) : (VAR_FRAG_SIZE/12), i = 0;
	u32 *fragList = (u32*)VAR_FRAG_LIST;
	
	memset((void*)VAR_FRAG_LIST, 0, VAR_FRAG_SIZE);
	
  	// If disc 1 is fragmented, make a note of the fragments and their sizes
	get_frag_list(file->name);
	if(frag_list->num < maxFrags) {
		for(i = 0; i < frag_list->num; i++) {
			fragList[i*3] = frag_list->frag[i].offset*512;
			fragList[(i*3)+1] = frag_list->frag[i].count*512;
			fragList[(i*3)+2] = frag_list->frag[i].sector;
		}
	}
	else {
		// file is too fragmented - go defrag it!
	}

	// If there is a disc 2 and it's fragmented, make a note of the fragments and their sizes
	if(file2) {
		get_frag_list(file2->name);
		if(frag_list->num < maxFrags) {
			for(i = 0; i < frag_list->num; i++) {
				fragList[(i*3) + (maxFrags*3)] = frag_list->frag[i].offset*512;
				fragList[((i*3) + 1) + (maxFrags*3)]  = frag_list->frag[i].count*512;
				fragList[((i*3) + 2) + (maxFrags*3)] = frag_list->frag[i].sector;
			}
		}
		else {
			// file is too fragmented - go defrag it!
		}
	}

	/*
	for(i = 0; i < maxFrags*3; i+=3) {
		if(*(u32*)(VAR_FRAG_LIST + ((i+1)*4)) == 0) break;
		print_gecko("File fragment [%i]: offset %08X size %08X (bytes) sector %08X\r\n",
			i/3, fragList[i], fragList[i+1], fragList[i+2]);
	}
	if(file2) {
		for(i = 0; i < maxFrags*3; i+=3) {
			if(*(u32*)(VAR_FRAG_LIST + (maxFrags*4) + ((i+1)*4)) == 0) break;
			print_gecko("File 2 fragment [%i]: offset %08X size %08X (bytes) sector %08X\r\n",
				i/3, fragList[i+(maxFrags*3)], fragList[(i+1) + (maxFrags*3)], fragList[(i+2) + (maxFrags*3)]);
		}
	}*/

	// Disk 1 sector
	*(volatile unsigned int*)VAR_DISC_1_LBA = fragList[3];
	// Disk 2 sector
	*(volatile unsigned int*)VAR_DISC_2_LBA = file2 ? fragList[3 + (maxFrags*3)]:fragList[3];
	// Currently selected disk sector
	*(volatile unsigned int*)VAR_CUR_DISC_LBA = fragList[3];
	// Copy the current speed
	*(volatile unsigned int*)VAR_EXI_BUS_SPD = !swissSettings.exiSpeed ? 192:208;
	// Card Type
	*(volatile unsigned int*)VAR_SD_TYPE = SDHCCard;
	// Copy the actual freq
	*(volatile unsigned int*)VAR_EXI_FREQ = !swissSettings.exiSpeed ? EXI_SPEED16MHZ:EXI_SPEED32MHZ;
	// Device slot (0 or 1)
	*(volatile unsigned int*)VAR_EXI_SLOT = (file->name[0] == 's') ? (file->name[2] == 'b') : (file->name[3] == 'b');
	// Is this an IDE-EXI v1 or 2?
	*(volatile unsigned int*)VAR_TMP4 = _ideexi_version;
}

int EXI_ResetSD(int drv) {
	/* Wait for any pending transfers to complete */
	while ( *(vu32 *)0xCC00680C & 1 );
	while ( *(vu32 *)0xCC006820 & 1 );
	while ( *(vu32 *)0xCC006834 & 1 );
	*(vu32 *)0xCC00680C = 0xC0A;
	*(vu32 *)0xCC006820 = 0xC0A;
	*(vu32 *)0xCC006834 = 0x80A;
	/*** Needed to re-kick after insertion etc ***/
	EXI_ProbeEx(drv);
	return 1;
}

int deviceHandler_FAT_init(file_handle* file){
	int isSDCard = file->name[0] == 's';
	int slot = isSDCard ? (file->name[2] == 'b') : (file->name[3] == 'b');
	
	DrawFrameStart();
	sprintf(txtbuffer, "Reading %s in slot %s", isSDCard ? "SD":"IDE-EXI", !slot ? "A":"B");
	DrawMessageBox(D_INFO,txtbuffer);
	DrawFrameFinish();
	
	// Slot A - SD Card
	if(isSDCard && !slot && EXI_ResetSD(0)) {
		carda->shutdown();
		carda->startup();
		return fatMountSimple ("sda", carda) ? 1 : 0;
	}
	// Slot B - SD Card
	if(isSDCard && slot && EXI_ResetSD(1)) {
		cardb->shutdown();
		cardb->startup();
		return fatMountSimple ("sdb", cardb) ? 1 : 0;
	}
	// Slot A - IDE-EXI
	if(!isSDCard && !slot) {
		ideexia->startup();
		return fatMountSimple ("idea", ideexia) ? 1 : 0;
	}
	// Slot B - IDE-EXI
	if(!isSDCard && slot) {
		ideexib->startup();
		return fatMountSimple ("ideb", ideexib) ? 1 : 0;
	}
	return 0;
}

int deviceHandler_FAT_deinit(file_handle* file) {
	int isSDCard = file->name[0] == 's';
	int slot = isSDCard ? (file->name[2] == 'b') : (file->name[3] == 'b');
	if(file && file->fp) {
		fclose(file->fp);
		file->fp = 0;
	}
	if(isSDCard) {
		EXI_Detach(slot);
		sdgecko_initIODefault();
	}
	return 0;
}

int deviceHandler_FAT_deleteFile(file_handle* file) {
	if(remove(file->name) == -1) {
		DrawFrameStart();
		DrawMessageBox(D_FAIL,"Error Deleting File");
		DrawFrameFinish();
		wait_press_A();
		return -1;
	}
	return 0;
}

