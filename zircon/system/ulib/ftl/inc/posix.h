// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>    // for size_t definition
#include <stdint.h>    // for fixed width types
#include <time.h>      // for time_t definition
#include <stdio.h>     // for FILENAME_MAX

/***********************************************************************/
/* Symbol Definitions                                                  */
/***********************************************************************/
#define dev_t uint32_t
#define gid_t uint16_t
#define ino_t uint32_t
#define nlink_t uint32_t
#define off_t int32_t
#define off64_t int64_t
#define uid_t uint16_t
#define pid_t uint32_t
#define mode_t uint16_t

// vstat() file system types - also used for NDM partitions, so existing
// values cannot be modified, only new values added or old ones removed

#define FAT_VOL 2
#define XFS_VOL 6
#define USR_VOL 7

// vstat() FTL types

#define FTL_NONE 1
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

// Driver count statistics for TargetFAT/TargetFTL volumes
typedef struct {
    uint32_t write_sectors;
    uint32_t read_sectors;
    ftl_drvr_stats ftl;
} fat_drvr_stats;

// Driver count statistics for TargetXFS/TargetFTL volumes
typedef struct {
    uint32_t write_pages;
    uint32_t read_pages;
    ftl_drvr_stats ftl;
} xfs_drvr_stats;

typedef struct {
    uint32_t vol_type;      // set to FAT_VOL
    uint32_t sect_size;     // sector size in bytes
    uint32_t garbage_level; // garbage level as percentage 0 to 100
    uint32_t ftl_type;
    fat_drvr_stats drvr_stats; // driver count statistics
    uint32_t clust_size;       // cluster size in bytes
    uint32_t num_clusts;       // number of clusters in volume
    uint32_t num_sects;        // number of FAT sectors in volume
    uint32_t used_clusts;      // number of used clusters
    uint32_t free_clusts;      // number of free clusters
    uint32_t fat_type;         // FAT12, FAT16, or FAT32
    uint32_t root_dir_sects;   // number of root directory sectors
    uint32_t sects_per_fat;    // FAT (1 table) size in sectors
    uint32_t num_fats;         // number of FAT tables
    uint32_t ram_used;         // amount of RAM in bytes used by volume
    uint32_t cached_clusts;    // size of data cache in clusters
    int cache_hits;            // percentage of data cache hits
} vstat_fat;

typedef struct // Must be the same as vstat_fat up to driver stats
{
    uint32_t vol_type;      // set to XFS_VOL
    uint32_t sect_size;     // sector size in bytes
    uint32_t garbage_level; // garbage level as percentage 0 to 100
    uint32_t ftl_type;
    xfs_drvr_stats drvr_stats; // driver count statistics
    uint32_t num_sects;        // number of sectors on volume
    uint32_t free_sects;       // number of free sectors on volume
    uint32_t free_2_sync;      // # of free sectors before forced sync
    uint32_t pages_per_stbl;   // number of pages per sector table
    uint32_t ram_used;         // amount of RAM in bytes used by volume
    uint32_t cached_pages;     // size of data cache in pages
    int cache_hits;            // percentage of data cache hits
} vstat_xfs;

union vstat {
    uint32_t vol_type; // FS volume type
    vstat_fat fat;
    vstat_xfs xfs;
};

typedef struct file DIR_TFS;

#define D_NAME_PTR FALSE // set to TRUE to use char * for d_name
struct dirent_TFS {
    long d_ino;
#if D_NAME_PTR
    char* d_name;
#else
    char d_name[FILENAME_MAX + 1];
#endif
};

struct utimbuf {
    time_t actime;  // access time
    time_t modtime; // modification time
};

struct stat_TFS {
    dev_t st_dev;     // ID of device containing this file
    ino_t st_ino;     // file serial number
    nlink_t st_nlink; // number of links
    dev_t st_rdev;    // device ID (if inode device)
    off_t st_size;    // the file size in bytes
    time_t st_atime;  // time of last access
    time_t st_mtime;  // time of last data modification
    time_t st_ctime;  // time of last status change
    mode_t st_mode;   // file mode
    uid_t st_uid;     // user ID of file's owner
    gid_t st_gid;     // group ID of file's owner
};

// Structure containing file/dir info for sortdir() comparisons
typedef struct {
    const char* st_name; // file name
    ino_t st_ino;        // file serial number
    nlink_t st_nlink;    // number of links
    off_t st_size;       // file size in bytes
    time_t st_atime;     // time of last access
    time_t st_mtime;     // time of last data modification
    time_t st_ctime;     // time of last status change
    mode_t st_mode;      // file mode
    uid_t st_uid;        // user ID of file's owner
    gid_t st_gid;        // group ID of file's owner
} DirEntry;

int XfsDelVol(const char* name);
int FtlNdmDelVol(const char* name);

#ifdef __cplusplus
}
#endif
