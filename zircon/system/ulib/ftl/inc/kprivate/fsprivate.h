// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <posix.h>
#include <fsdriver.h>

// Symbol Definitions.

// Default block read limits to avoid read-disturb errors.
#define MLC_NAND_RC_LIMIT 100000
#define SLC_NAND_RC_LIMIT 1000000

// FsErrCode Error Code Assignments.
enum FsError
{
    // TargetNDM Symbols.
    NDM_EIO = 1,       // Fatal I/O error.
    NDM_CFG_ERR,       // NDM config error
    NDM_ASSERT,        // Inconsistent NDM internal values.
    NDM_ENOMEM,        // NDM memory allocation failure.
    NDM_SEM_CRE_ERR,   // NDM semCreate() failed.
    NDM_NO_META_BLK,   // No metadata block found.
    NDM_NO_META_DATA,  // metadata page missing.
    NDM_BAD_META_DATA, // Invalid metadata contents.
    NDM_TOO_MANY_IBAD, // Too many initial bad blocks.
    NDM_TOO_MANY_RBAD, // Too many running bad blocks.
    NDM_NO_FREE_BLK,   // No free block in NDM pool.
    NDM_IMAGE_RBB_CNT, // Bad block count in NDM image.
    NDM_RD_ECC_FAIL,   // Read_page ECC decode failed.
    NDM_NOT_FOUND,     // ndmDelDev() unknown handle.
    NDM_BAD_BLK_RECOV, // Running bad block recovery needed during RO-init.
    NDM_META_WR_REQ,   // Metadata write request during RO-init.

    // TargetFTL-NDM Symbols.
    FTL_CFG_ERR,       // FTL config error.
    FTL_ASSERT,        // Inconsistent FTL internal values.
    FTL_ENOMEM,        // FTL memory allocation failure.
    FTL_MOUNTED,       // mount()/unformat() on mounted FTL.
    FTL_UNMOUNTED,     // unmount() on unmounted FTL.
    FTL_NOT_FOUND,     // FtlNdmDelVol() unknown name.
    FTL_NO_FREE_BLK,   // No free FTL block.
    FTL_NO_MAP_BLKS,   // No map block found during RO-init.
    FTL_NO_RECYCLE_BLK,// Recycle block selection failed.
    FTL_RECYCLE_CNT,   // Repeated recycles did not free blocks.
    // Following would result in block erase except for RO-init flag.
    FTL_VOL_BLK_XFR,   // Found interrupted volume block resume.
    FTL_MAP_BLK_XFR,   // Found interrupted map block resume.
    FTL_UNUSED_MBLK,   // Found unused map block during RO-init.
    FTL_VBLK_RESUME,   // Low free blk cnt: would resume volume block.
    FTL_MBLK_RESUME,   // Low free blk cnt: would resume map block.
};

// Macro Definitions.
// Bitmap accessors.
#define BITMAP_ON(start, i) (*((ui8*)(start) + (i) / 8) |= (ui8)(1 << ((i) % 8)))
#define BITMAP_OFF(start, i) (*((ui8*)(start) + (i) / 8) &= (ui8) ~(1 << ((i) % 8)))
#define IS_BITMAP_ON(start, i) (*((ui8*)(start) + (i) / 8) & (1 << ((i) % 8)))

// Variable Declarations.
extern int FsErrCode;   // file system error code (enum).

// Function Prototypes.

int fsPerror(int fs_err_code);
int FsError(int err_code);
int FsError2(int fs_err_code, int errno_code);
int FNameEqu(const char* s1, const char* s2);
void* FtlNdmAddXfsFTL(FtlNdmVol* ftl_dvr, XfsVol* xfs_dvr);
void FtlnFreeFTL(void* ftl);
void FsMemPrn(void);
ui32 FsMemPeakRst(void);

void* FsCalloc(size_t nmemb, size_t size);
void* FsMalloc(size_t size);
void* FsAalloc(size_t size);
void FsFreeClear(void* ptr_ptr);
void FsAfreeClear(void* ptr_ptr);
void FsFree(void* ptr);

#ifdef __cplusplus
}
#endif
