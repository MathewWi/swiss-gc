#ifndef PATCHER_H
#define PATCHER_H

#include "../../reservedarea.h"

typedef struct FuncPattern
{
	u32 Length;
	u32 Loads;
	u32 Stores;
	u32 FCalls;
	u32 Branch;
	u32 Moves;
	u8 *Patch;
	u32 PatchLength;
	char *Name;
	u32 offsetFoundAt;
} FuncPattern;

/* the SDGecko/IDE-EXI patches */
extern u8 hdd_bin[];
extern u32 hdd_bin_size;
extern u8 sd_bin[];
extern u32 sd_bin_size;
extern u8 usbgecko_bin[];
extern u32 usbgecko_bin_size;

/* SDK patches */
extern u8 DVDCancelAsync[];
extern u32 DVDCancelAsync_length;
extern u8 DVDCancel[];
extern u32 DVDCancel_length;
extern u8 DVDGetDriveStatus[];
extern u32 DVDGetDriveStatus_length;
extern u8 DVDGetCommandBlockStatus[];
extern u32 DVDGetCommandBlockStatus_length;
extern u8 DVDCompareDiskId[];
extern u32 DVDCompareDiskId_length;
extern u8 ForceProgressive[];
extern u32 ForceProgressive_length;
extern u8 ForceProgressive576p[];
extern u32 ForceProgressive576p_length;
extern u8 GXGetYScaleFactorPre[];
extern u32 GXGetYScaleFactorPre_length;
extern u8 GXInitTexObjLODPre[];
extern u32 GXInitTexObjLODPre_length;
extern u8 GXSetProjectionPre[];
extern u32 GXSetProjectionPre_length;
extern u8 GXSetScissorPre[];
extern u32 GXSetScissorPre_length;
extern u8 MTXFrustumPre[];
extern u32 MTXFrustumPre_length;
extern u8 MTXLightFrustumPre[];
extern u32 MTXLightFrustumPre_length;
extern u8 MTXOrthoPre[];
extern u32 MTXOrthoPre_length;

/* SDK CARD library patches */
extern u8 __CARDSync[];
extern u32 __CARDSync_length;
extern u8 CARDCheck[];
extern u32 CARDCheck_length;
extern u8 CARDCheckAsync[];
extern u32 CARDCheckAsync_length;
extern u8 CARDCheckEx[];
extern u32 CARDCheckEx_length;
extern u8 CARDCheckExAsync[];
extern u32 CARDCheckExAsync_length;
extern u8 CARDClose[];
extern u32 CARDClose_length;
extern u8 CARDCreate[];
extern u32 CARDCreate_length;
extern u8 CARDCreateAsync[];
extern u32 CARDCreateAsync_length;
extern u8 CARDDelete[];
extern u32 CARDDelete_length;
extern u8 CARDDeleteAsync[];
extern u32 CARDDeleteAsync_length;
extern u8 CARDFastOpen[];
extern u32 CARDFastOpen_length;
extern u8 CARDFreeBlocks[];
extern u32 CARDFreeBlocks_length;
extern u8 CARDGetEncoding[];
extern u32 CARDGetEncoding_length;
extern u8 CARDGetMemSize[];
extern u32 CARDGetMemSize_length;
extern u8 CARDGetResultCode[];
extern u32 CARDGetResultCode_length;
extern u8 CARDGetSerialNo[];
extern u32 CARDGetSerialNo_length;
extern u8 CARDGetStatus[];
extern u32 CARDGetStatus_length;
extern u8 CARDMount[];
extern u32 CARDMount_length;
extern u8 CARDMountAsync[];
extern u32 CARDMountAsync_length;
extern u8 CARDOpen[];
extern u32 CARDOpen_length;
extern u8 CARDProbe[];
extern u32 CARDProbe_length;
extern u8 CARDProbeEX[];
extern u32 CARDProbeEX_length;
extern u8 CARDRead[];
extern u32 CARDRead_length;
extern u8 CARDReadAsync[];
extern u32 CARDReadAsync_length;
extern u8 CARDSetStatus[];
extern u32 CARDSetStatus_length;
extern u8 CARDSetStatusAsync[];
extern u32 CARDSetStatusAsync_length;
extern u8 CARDWrite[];
extern u32 CARDWrite_length;
extern u8 CARDWriteAsync[];
extern u32 CARDWriteAsync_length;

#define SWISS_MAGIC 0x53574953 /* "SWIS" */

#define LO_RESERVE 0x80001800

/* Function jump locations in our patch code */
#define QUEUE_READ_OFFSET	 	(LO_RESERVE)
#define PATCHED_MEMCPY		 	(LO_RESERVE | 0x04)
#define FAKE_IRQ			 	(LO_RESERVE | 0x08)
#define CARD_OPEN_OFFSET 		(LO_RESERVE | 0x0C)
#define CARD_FASTOPEN_OFFSET 	(LO_RESERVE | 0x10)
#define CARD_CLOSE_OFFSET 		(LO_RESERVE | 0x14)	
#define CARD_CREATE_OFFSET 		(LO_RESERVE | 0x18)
#define CARD_DELETE_OFFSET 		(LO_RESERVE | 0x1C)
#define CARD_READ_OFFSET 		(LO_RESERVE | 0x20)
#define CARD_WRITE_OFFSET 		(LO_RESERVE | 0x24)
#define CARD_GETSTATUS_OFFSET 	(LO_RESERVE | 0x28)
#define CARD_SETSTATUS_OFFSET 	(LO_RESERVE | 0x2C)
#define CARD_SETUP_OFFSET 		(LO_RESERVE | 0x30)

#define READ_PATCHED_ALL 		(0x111111)

/* Types of files we may patch */
#define PATCH_DOL		0
#define PATCH_ELF		1
#define PATCH_LOADER	2

u32 Patch_DVDLowLevelRead(void *addr, u32 length, int dataType);
int Patch_ProgVideo(u8 *data, u32 length, int dataType);
void Patch_WideAspect(u8 *data, u32 length, int dataType);
int Patch_TexFilt(u8 *data, u32 length, int dataType);
int Patch_DVDAudioStreaming(u8 *data, u32 length);
int Patch_DVDStatusFunctions(u8 *data, u32 length);
void Patch_Fwrite(void *addr, u32 length);
void Patch_DVDReset(void *addr,u32 length);
int Patch_DVDCompareDiskId(u8 *data, u32 length);
void Patch_GXSetVATZelda(void *addr, u32 length,int mode);
int Patch_CARDFunctions(u8 *data, u32 length);
u32 Calc_ProperAddress(u8 *data, u32 type, u32 offsetFoundAt);
int Patch_CheatsHook(u8 *data, u32 length, u32 type);
void install_code();


#endif

