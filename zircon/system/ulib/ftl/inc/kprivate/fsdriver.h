// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <kernel.h>
#include <bsp.h>  // for CACHE_LINE_SIZE definition

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************/
/* Configuration                                                       */
/***********************************************************************/
#define NV_NDM_CTRL_STORE FALSE

/***********************************************************************/
/* Symbol Definitions                                                  */
/***********************************************************************/

// Flag values for the file systems' driver flags field
#define FTLN_FATAL_ERR      (1 << 0)    // fatal I/O error has occurred
#define FTLN_MOUNTED        (1 << 1)    // FTL is mounted flag
#define FSF_EXTRA_FREE      (1 << 2)
#define FSF_TRANSFER_PAGE   (1 << 3)
#define FSF_MULTI_ACCESS    (1 << 4)
#define FSF_FREE_SPARE_ECC  (1 << 5)    // spare decode has no overhead
#define FSF_NDM_INIT_WRITE  (1 << 6)    // re-write NDM metadata on init
#define FSF_READ_WEAR_LIMIT (1 << 7)    // driver specs read-wear limit
#define FSF_READ_ONLY_INIT  (1 << 8)    // dev is read-only during init

// Size in bytes of a FAT sector
#define FAT_SECT_SZ 512

/***********************************************************************/
/* Macro Definitions                                                   */
/***********************************************************************/
#if FS_ASSERT
void AssertError(int line, const char* file);
#define PF_DEBUG
#define PfAssert(c)                          \
    do {                                     \
        if (!(c))                            \
            AssertError(__LINE__, __FILE__); \
    } while (0)
#else
#define PfAssert(c) \
    do {            \
    } while (0)
#endif

// Count number of bits set to 1 in a byte/32 bit value
#define ONES_UI8(b) (NumberOnes[(b) >> 4] + NumberOnes[(b)&0xF])
#define ONES_UI32(w)                                                               \
    (ONES_UI8(((ui8*)&w)[0]) + ONES_UI8(((ui8*)&w)[1]) + ONES_UI8(((ui8*)&w)[2]) + \
     ONES_UI8(((ui8*)&w)[3]))

/***********************************************************************/
/* Type Definitions                                                    */
/***********************************************************************/

// FTL NDM structure holding all driver information
typedef struct {
    ui32 block_size;        // size of a block in bytes
    ui32 num_blocks;        // total number of blocks
    ui32 page_size;         // flash page data size in bytes
    ui32 eb_size;           // flash page spare size in bytes
    ui32 start_page;        // volume first page on flash
    ui32 cached_map_pages;  // number of map pages to be cached
    ui32 extra_free;        // volume percentage left unused
    ui32 read_wear_limit;   // device read-wear limit
    void* ndm;              // driver's NDM pointer
    ui32 flags;             // option flags
} FtlNdmVol;

// FS Report Events
typedef enum {
    FS_MOUNT,
    FS_UNMOUNT,
    FS_FORMAT,
    FS_VCLEAN,
    FS_MARK_UNUSED,
    FS_SYNC,
    FS_FLUSH_PAGE,
    FS_VSTAT,
    FS_UNFORMAT,
    FS_PAGE_SZ,
    FS_FAT_SECTS,
    FS_FORMAT_RESET_WC,
} FS_EVENTS;

/***********************************************************************/
/* Variable Declarations                                               */
/***********************************************************************/
extern SEM FileSysSem;
extern SEM FsNvramSem;
extern const ui8 NumberOnes[];

/***********************************************************************/
/* Function Prototypes                                                 */
/***********************************************************************/
int NdmInit(void);
int FtlInit(void);
int XfsAddVol(XfsVol* vol);

// File System API to interact with NVRAM
void FsSaveMeta(ui32 vol_id, ui32 meta, const char* vol_name);
int FsReadMeta(ui32 vol_id, ui32* meta, const char* vol_name);

// TargetNDM NVRAM Control Page Storage
void NvNdmCtrlPgWr(ui32 frst);
ui32 NvNdmCtrlPgRd(void);

#ifdef __cplusplus
}
#endif
