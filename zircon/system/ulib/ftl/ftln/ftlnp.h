// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inc/config.h>

#include <errno.h>
#include <string.h>
#include <ftl_private.h>
#include <fsprivate.h>
#include <kprivate/ndm.h>
#include <ftl_mc.h>

//
// Configuration.
//
#define FTLN_LEGACY TRUE // TRUE to be backward compatible
#define INC_ELIST TRUE   // if true, write erased blocks list
#define DEBUG_ELIST FALSE
#ifndef FTLN_DEBUG
#define FTLN_DEBUG TRUE
#endif
#ifndef FTLN_DEBUG_PTR
#define FTLN_DEBUG_PTR FALSE
#endif
#if !FTLN_LEGACY
#define FTLN_3B_PN TRUE // if true, use 3B page numbers
#endif

//
// Symbol Definitions.
//
#define FTLN_MIN_FREE_BLKS 4

//
// FTL meta-page information
//
#define FTLN_META_VER1 20180423  // current metapage version
#define FTLN_META_VER_LOC 0      // version location in page
#define FTLN_META_TYP_LOC 4      // page type location
#define FTLN_META_DATA_BEG 8     // starting data offset

//
// Meta-Page Types
//
#define CONT_FORMAT 0
#define ERASED_LIST 1

//
// Macro Definitions.
//
//
// Block Array Definitions
//
// A bdata entry is a 32 bit value that holds block metadata in RAM
// Bit 31 indicates if a block is free (1) or used (0)
// For free blocks:
//   Bit 30 indicates if a free block is erased (1) or not (0)
// For used blocks:
//   Bit 30 - indicates if a block is map (1) or volume (0) block
//   Bits 29 through 20 - number of used pages in block
//   Bits 19 through  0 - block read count
//
// 0xC0000000 - free/erased block
// 0x80000000 - free block
// 0x7XXXXXXX-0x4XXXXXXX - map block
// 0x3XXXXXXX-0x0XXXXXXX - used block
//
#define BLK_STATE_MASK 0xC0000000
#define FREE_BLK_FLAG 0x80000000
#define ERASED_BLK_FLAG 0x40000000  // applies only to free blocks
#define MAP_BLK_STATE 0x40000000
#define USED_MASK 0x3FF00000  // applies to map/vol blocks
#define RC_MASK 0x000FFFFF    // applies to map/vol blocks

#define PGS_PER_BLK_MAX (USED_MASK >> 20)

#define IS_FREE(b) ((b)&FREE_BLK_FLAG)
#define IS_ERASED(b) (((b)&BLK_STATE_MASK) == (FREE_BLK_FLAG | ERASED_BLK_FLAG))
#define IS_MAP_BLK(b) (((b)&BLK_STATE_MASK) == MAP_BLK_STATE)
#define SET_MAP_BLK(bd) (bd = MAP_BLK_STATE)

#define NUM_USED(bd) (((bd)&USED_MASK) >> 20)
#define DEC_USED(bd) ((bd) -= (1 << 20))
#define INC_USED(bd) ((bd) += (1 << 20))
#define GET_RC(bd) ((bd)&RC_MASK)
#define SET_RC(bd, n) ((bd) = ((bd) & ~RC_MASK) | n)
#define INC_RC(ftl, bdp, c)               \
    do {                                  \
        ui32 rc = GET_RC(*bdp) + c;       \
                                          \
        if (rc > RC_MASK)                 \
            rc = RC_MASK;                 \
        *bdp = rc | (*bdp & ~RC_MASK);    \
        if (rc >= (ftl)->max_rc)          \
            (ftl)->max_rc_blk = (ui32)-2; \
    } /*lint !e717*/ \
    while (0)
#define SET_MAX_RC(ftl, bdp)                      \
    do {                                          \
        (ftl)->max_rc_blk = (ui32)-2;             \
        *bdp = (ftl)->max_rc | (*bdp & ~RC_MASK); \
    } /*lint !e717*/ \
    while (0)


// Map Page Array Definitions
//
// Get/set the physical page corresponding to a VPN
//
#if FTLN_3B_PN
#define FTLN_PN_SZ 3
#define UNMAPPED_PN 0x00FFFFFF
#define GET_MAP_PPN(maddr) RD24_LE(maddr)
#define SET_MAP_PPN(maddr, pn) WR24_LE(pn, maddr)
#else
#define FTLN_PN_SZ 4
#define UNMAPPED_PN 0xFFFFFFFF
#define GET_MAP_PPN(maddr) RD32_LE(maddr)
#define SET_MAP_PPN(maddr, pn) WR32_LE(pn, maddr)
#endif

//
// Spare Area Access Definitions.
//
// Layout of Spare Area (Extra Bytes)
//  - byte   0: bad block mark byte - unused by the FTL
//  - bytes  1 -  4: virtual page number
//  - bytes  5 -  8: block count (BC)
//  - bytes  9 - 11 and MSH of 12: block wear count (WC)
//  - bytes LSH of 12 and 13 - 14: page validity check
//  - byte  15: NDM control page mark byte
//
//
// Get/set virtual page number from spare area (bytes 1 - 4)
//
#define GET_SA_VPN(spare) RD32_LE(&spare[1])
#define SET_SA_VPN(vpn, spare) WR32_LE(vpn, &spare[1])

//
// Get/set block count from spare area (bytes 5 - 8)
//
#define GET_SA_BC(spare) RD32_LE(&spare[5])
#define SET_SA_BC(bc, spare) WR32_LE(bc, &spare[5])

//
// Get/set block wear count from spare area (bytes 9 - 11 and MSH of 12)
//
#define GET_SA_WC(spare) (RD24_LE(&spare[9]) | ((spare[12] & 0xF0) << 20))
#define SET_SA_WC(wc, spare)                                 \
    {                                                        \
        WR24_LE(wc, &spare[9]);                              \
        spare[12] = (spare[12] & 0xF) | ((wc >> 20) & 0xF0); \
    }

//
// Type Declarations.
//
//
// The TargetFTL-NDM volume type
//
typedef struct ftln* FTLN;
typedef const struct ftln* CFTLN;
struct ftln {
    CircLink link;          // volume list link

    // Driver Dependent Variables
    ui32 num_pages;         // total number of pages
    ui32 pgs_per_blk;       // number of pages in a block
    ui32 block_size;        // block size in bytes
    ui32 num_blks;          // number of blocks
    ui32 page_size;         // page size in bytes
    ui32 start_pn;          // first page on device for volume
    void* ndm;              // pointer to NDM this FTL belongs to

    ui32 flags;             // holds various FTL flags
    ui32* bdata;            // block metadata: flags and counts
    ui8* blk_wc_lag;        // amount block erase counts lag 'high_wc'
    ui32* mpns;             // array holding phy page # of map pages

    FTLMC* map_cache;       // handle to map page cache
    ui32 free_vpn;          // next free page for volume page write
    ui32 free_mpn;          // next free page for map page write
    ui32 mappings_per_mpg;  // number of phys page numbers per map page
    ui32 num_vpages;        // number of volume pages
    ui32 num_free_blks;     // number of free blocks
    ui32 num_map_pgs;       // number of pages holding map data
    ui32 high_wc;           // highest block wear count
    ui32 high_bc;           // highest map block write count
    ui32 max_rc;            // per block read wear limit
    ui32 max_rc_blk;        // if not -1, # of block w/high read cnt
    ui32 high_bc_mblk;      // last map block
    ui32 high_bc_mblk_po;   // used page offset on last map block
    ui32 resume_vblk;       // vblk in interrupted recycle recovery
    ui32 resume_tblk;       // tmp blk for interrupted recycle recovery
    ui32 resume_po;         // resume vblk's highest used page offset
#if INC_ELIST
    ui32 elist_blk;         // if valid, # of block holding erased list
#endif
    ftl_ndm_stats stats;    // driver call counts

    ui8* main_buf;          // NAND main page buffer
    ui8* spare_buf;         // spare buffer for single/multi-pg access

    ui8 eb_size;            // spare area size in bytes
    ui8 copy_end_found;     // vblk resume copy-end mark found
    ui8 deferment;          // # of recycles before applying wear limit
#if FTLN_DEBUG
    ui8 max_wc_lag;         // maximum observed lag below hi wear count
    ui8 max_wc_over;        // # of times max WC (0xFF) was exceeded
#endif
#if FS_ASSERT
    ui8 assert_no_recycle;  // test no recycle changes physical page #
#endif
    char vol_name[FILENAME_MAX]; // volume name
};

//
// Variable Declarations.
//
extern CircLink FtlnVols;
#if FTLN_DEBUG_PTR
extern FTLN Ftln;
#endif

//
// Function Prototypes.
//
int FtlnDelVol(FTLN ftl);
int FtlnWrPages(const void* buf, ui32 first, int count, void* vol);
int FtlnRdPages(void* buf, ui32 first, int count, void* vol);
int FtlnReport(void* vol, ui32 msg, ...);
ui32 FtlnGarbLvl(CFTLN ftl);
int FtlnVclean(FTLN ftl);
int FtlnMapGetPpn(CFTLN ftl, ui32 vpn, ui32* pnp);
int FtlnMapSetPpn(CFTLN ftl, ui32 vpn, ui32 ppn);
int FtlnRecCheck(FTLN ftl, int wr_cnt);
int FtlnRecNeeded(CFTLN ftl, int wr_cnt);
int FtlnRdPage(FTLN ftl, ui32 pn, void* buf);

int FtlnMapWr(void* vol, ui32 mpn, void* buf);
int FtlnMetaWr(FTLN ftl, ui32 type);

void FtlnDecUsed(FTLN ftl, ui32 pn, ui32 vpn);
int FtlnFormat(FTLN ftl, ui32 meta_block);
void FtlnStateRst(FTLN ftl);
int FtlnEraseBlk(FTLN ftl, ui32 b);
ui32 FtlnLoWcFreeBlk(CFTLN ftl);
ui32 FtlnHiWcFreeBlk(CFTLN ftl);
int FtlnRecycleMapBlk(FTLN ftl, ui32 recycle_b);

int FtlnSetClustSect1(FTLN ftl, const ui8* bpb, int format_req);
int FtlnFatErr(FTLN ftl);

#if INC_FTL_NDM_MLC
void FtlnMlcSafeFreeVpn(FTLN ftl);
#endif

void FtlnBlkStats(CFTLN ftl);
void FtlnStats(FTLN ftl);
void FtlnShowBlks(void);
void FtlnCheckBlank(FTLN ftl, ui32 b);

