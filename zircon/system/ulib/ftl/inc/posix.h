// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>    // for size_t definition
#include <stdint.h>    // for fixed width types

/***********************************************************************/
/* Symbol Definitions                                                  */
/***********************************************************************/

// vstat() file system types - also used for NDM partitions, so existing
// values cannot be modified, only new values added or old ones removed
#define XFS_VOL 6

// vstat() FTL types
#define FTL_NDM 2

// Types of Flash
#define FFS_NAND_SLC (1 << 0)
#define FFS_NAND_MLC (1 << 1)

/***********************************************************************/
/* Type definitions                                                    */
/***********************************************************************/

// Driver count statistics for TargetFTL-NDM volumes
typedef struct {
    uint32_t write_page;
    uint32_t read_page;
    uint32_t read_spare;
    uint32_t page_check;
    uint32_t page_erased;
    uint32_t transfer_page;
    uint32_t erase_block;
    uint32_t ram_used;
    uint32_t wear_count;
} ftl_ndm_stats;

// Driver count statistics for TargetFTL volumes
typedef union {
    ftl_ndm_stats ndm;
} ftl_drvr_stats;

// Driver count statistics for TargetXFS/TargetFTL volumes
typedef struct {
    uint32_t write_pages;
    uint32_t read_pages;
    ftl_drvr_stats ftl;
} xfs_drvr_stats;

typedef struct // Must be the same as vstat_fat up to driver stats
{
    uint32_t vol_type;      // set to XFS_VOL
    uint32_t garbage_level; // garbage level as percentage 0 to 100
    uint32_t ftl_type;
    xfs_drvr_stats drvr_stats; // driver count statistics
} vstat_xfs;

union vstat {
    uint32_t vol_type; // FS volume type
    vstat_xfs xfs;
};

int FtlNdmDelVol(const char* name);

#ifdef __cplusplus
}
#endif
