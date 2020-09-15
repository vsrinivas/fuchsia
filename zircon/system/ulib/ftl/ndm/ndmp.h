// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_FTL_NDM_NDMP_H_
#define ZIRCON_SYSTEM_ULIB_FTL_NDM_NDMP_H_

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/compiler.h>

#include "ftl_private.h"

//
// Configuration.
//
#undef NDM_DEBUG
#define NDM_DEBUG FALSE     // TRUE for TargetNDM debug output
#define RDBACK_CHECK FALSE  // TRUE for metadata read-back check

//
// Symbol Definitions.
//
#define CTRL_SIG_SZ 7  // ctrl sig bytes
#define CTRL_SIG "NDMTA01"

//
// Location in control header of all header fields. A header consists of:
//   - 2 bytes of current page number in this control sequence.
//   - 2 bytes of last page number in this control sequence.
//   - 4 bytes of sequence number.
//   - 4 bytes of CRC.
//
// A "header format 2" adds version information to the beginning of the header,
// which means that all other fields are shifted down by 4 bytes:
//   - 2 bytes for major version number.
//   - 2 bytes for minor version number.
//
// Note that an old header (where the version fields are not present), has the
// current and last sequence numbers where the version information of a version 2
// header would be (in the first four bytes of the page). If the geometry of the
// device means that a control block will never span multiple nand pages, those
// two numbers will always be 1, which means that "version 2 code" will see the
// version information as 1.1, hence being able to detect the old format.
//
// That knowledge about current control blocks, in practice, never requiring
// more than a page, is what allows this code to deal with the two versions of
// the format. Looking forward, the FTL will not write the old format on any
// device so even if in the future control blocks require multiple pages, this
// code is safe.
//
// Every use of these values must be in the context of code that decides whether
// or not to apply a HDR_V2_SHIFT.
//
#define HDR_CURR_LOC 0
#define HDR_LAST_LOC 2
#define HDR_SEQ_LOC 4
#define HDR_CRC_LOC 8
#define CTRL_DATA_START 12

// This is the shift to apply to other header fields when dealing with version 2
// of the header format.
#define HDR_V2_SHIFT 4

// Control scan flag value
#define PARTIAL_SCAN 2

// Actions that can cause a block to go bad
#define ERASE_BLOCK 1
#define WRITE_PAGE 2

//
// Layout for the spare area:
//  - byte 0 - bad block mark byte
//  - bytes 1 - 14 - reserved for the above layers - will be ECC-ed
//  - byte 15 - NDM regular page mark byte
//  - rest are left to the driver to place ECC codes in them
//
#define EB_BBLOCK_MARK 0
#define EB_FRST_RESERVED 1
#define EB_LAST_RESERVED 14
#define EB_REG_MARK 15  // NDM control page iff zero

//
// Type Declarations.
//

// <key, value> pair
typedef struct {
  ui32 key;  // vblk
  ui32 val;  // pblk
} Pair;

// NDM Control Block
struct ndm {
  CircLink link;       // linked list of NDM devices
  ui32 num_vblks;      // number of virtual blocks
  SEM sem;             // access semaphore
  ui8* main_buf;       // main page data buffer
  ui8* spare_buf;      // spare area buffer
  ui8* tmp_spare;      // temp buffer for driver transfer routine
  ui32* init_bad_blk;  // initial bad blocks list
  Pair* run_bad_blk;   // running bad blocks list
  ui32 num_rbb;        // number of blocks in running bad block list
  ui32 num_bad_blks;   // current total number of bad blocks
  ui32 frst_reserved;  // first block reserved for NDM
  ui32 free_virt_blk;  // next free block NDM uses for bad virtual
  ui32 free_ctrl_blk;  // next free block NDM uses for bad control
  ui32 ctrl_blk0;      // two blocks used for control info
  ui32 ctrl_blk1;
  ui32 frst_ctrl_page;   // first page of control information
  ui32 last_ctrl_page;   // last page of control information
  ui32 next_ctrl_start;  // starting page of next control write
  ui32 ctrl_pages;       // number of control pages
  ui32 ctrl_seq;         // control information sequence number
  ui32 xfr_tblk;         // interrupted 'transfer to' block
  ui32 xfr_fblk;         // interrupted 'transfer from' block
  ui32 xfr_bad_po;       // bad page offset in 'transfer from' block
  ui32 last_wr_vbn;      // last virtual block number written to
  ui32 last_wr_pbn;      // last physical block number written to
  ui32 last_rd_vbn;      // last virtual block number read from
  ui32 last_rd_pbn;      // last physical block number read from
  ui32 flags;            // option flags

  // Partition Information:
  // The first variable reflects the current status of the NDM, which means either
  // the format used to write the control header or the format of the newest control
  // header read from NAND.
  // On the other hand, the second variable reflects the format to be used when
  // creating new volumes. We retain the ability to format devices using the old
  // format only to simplify testing (upgrade ability).
  // TODO(40208): Remove upgrading code after all devices in the field are using
  // the new format.
  ui32 version_2;       // "Boolean" variable: FALSE for control header version 1.
  ui32 format_with_v2;  // "Boolean" variable: FALSE to use control header version 1.
  ui32 num_partitions;
  NDMPartition* partitions;  // Points to an NDMPartitionInfo when version_2 is TRUE.

  // Driver Functions
  int (*write_page)(ui32 pn, const ui8* data, ui8* spare, int action, void* dev);
  int (*write_pages)(ui32 pn, ui32 count, const ui8* data, ui8* spare, int action, void* dev);
  int (*read_page)(ui32 pn, ui8* data, ui8* spare, void* dev);
  int (*read_pages)(ui32 pn, ui32 count, ui8* data, ui8* spare, void* dev);
  int (*xfr_page)(ui32 old_pn, ui32 new_pn, ui8* data, ui8* old_spare, ui8* new_spare,
                  int encode_spare, void* dev);
#if INC_FTL_NDM_MLC
  ui32 (*pair_offset)(ui32 page_offset, void* dev);
#endif
  int (*read_decode_spare)(ui32 pn, ui8* spare, void* dev);
  int (*read_spare)(ui32 pn, ui8* spare, void* dev);
  int (*page_blank)(ui32 pn, ui8* data, ui8* spare, void* dev);
  int (*check_page)(ui32 pn, ui8* data, ui8* spr, int* stat, void* dev);
  int (*erase_block)(ui32 pn, void* dev);
  int (*is_block_bad)(ui32 pn, void* dev);

  Logger logger;

  // Device Dependent Variables
  void* dev;          // optional value set by driver
  void* dev_ndm;      // driver/ndm handle used with transfer page
  ui32 num_dev_blks;  // number of device blocks
  ui32 block_size;    // block size in bytes
  ui32 max_bad_blks;  // maximum number of bad blocks
  ui32 pgs_per_blk;   // number of pages in a block
  ui32 page_size;     // page size in bytes
  ui8 eb_size;        // spare area size in bytes
};

__BEGIN_CDECLS

//
// Variable Declarations.
//
extern CircLink NdmDevs;
extern SEM NdmSem;

//
// Function Prototypes.
//
int ndmInitBadBlock(CNDM ndm, ui32 b);
int ndmWrCtrl(NDM ndm);
void ndmCkMeta(NDM ndm);
int ndmMarkBadBlock(NDM ndm, ui32 arg, ui32 action);
ui32 ndmReadControlCrc(CNDM ndm);
ui32 ndmGetHeaderCurrentLocation(CNDM ndm);
ui32 ndmGetHeaderLastLocation(CNDM ndm);
ui32 ndmGetHeaderSequenceLocation(CNDM ndm);
ui32 ndmGetHeaderControlDataStart(CNDM ndm);

#if NDM_DEBUG
int printf(const char*, ...);
#endif

__END_CDECLS

#endif  // ZIRCON_SYSTEM_ULIB_FTL_NDM_NDMP_H_
