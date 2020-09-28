// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_FTL_FTL_H_
#define ZIRCON_SYSTEM_ULIB_FTL_FTL_H_

#include <stdint.h>
#include <zircon/compiler.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

//
// Configuration.
//
#define INC_FTL_NDM_MLC FALSE  // TargetFTL-NDM on NDM MLC.
#define INC_FTL_NDM_SLC TRUE   // TargetFTL-NDM on NDM SLC.

#if INC_FTL_NDM_MLC && INC_FTL_NDM_SLC
#error Set INC_FTL_NDM_MLC or INC_FTL_NDM_SLC to TRUE, not both
#elif !INC_FTL_NDM_MLC && !INC_FTL_NDM_SLC
#error Need INC_FTL_NDM_MLC or INC_FTL_NDM_SLC set to TRUE
#endif

#define CACHE_LINE_SIZE 32       // CPU data cache line size.
#define NV_NDM_CTRL_STORE FALSE  // Enables NvNdmCtrlPgRd() speedup.

#define FS_DVR_TEST FALSE  // TRUE to run FS driver test.
#undef FTL_RESUME_STRESS
#define FTL_NAME_MAX 32

// The lag that separates blocks with low wear from high wear. Blocks that are
// within this value of the lowest wear count are considered low wear, whilst
// blocks that exceed this are considered having high wear.
//
// This number has been initially chosen because it matches WC_LIM0_LAG_190,
// which used to be the point where the recycle strategy changed. It's
// slightly different, because it was based on average wear lag, whereas this
// value is based on maximum lag. It's possible that we could make this
// smaller; 190 seems like plenty of variation and making it smaller might not
// adversely affect performance, whilst keeping the range of wear closer.
#define FTL_LOW_WEAR_BOOST_LAG 190

// If there are more than this number of blocks free, allocate volume pages
// from free blocks that have the lowest wear rather than the highest wear.
// Recycling will only occur when there are not many free blocks, at which
// point we will allocate volume pages from highest wear. This is what we want
// because we're trying to move cold data from blocks with low wear to blocks
// with high wear.
#define FTL_FREE_THRESHOLD_FOR_LOW_WEAR_ALLOCATION 40

// Default block read limits to avoid read-disturb errors.
#define MLC_NAND_RC_LIMIT 100000
#define SLC_NAND_RC_LIMIT 1000000

//
// Symbol Definitions.
//
// Flag values for the file systems' driver flags field.
#define FTLN_FATAL_ERR (1u << 0)  // Fatal I/O error has occurred.
#define FTLN_MOUNTED (1u << 1)    // FTL is mounted flag.
#define FSF_EXTRA_FREE (1u << 2)
#define FSF_TRANSFER_PAGE (1u << 3)
#define FSF_MULTI_ACCESS (1u << 4)
#define FSF_FREE_SPARE_ECC (1u << 5)   // Spare decode has no overhead.
#define FSF_NDM_INIT_WRITE (1u << 6)   // Re-write NDM metadata on init.
#define FSF_READ_WEAR_LIMIT (1u << 7)  // Driver specs read-wear limit.
#define FSF_READ_ONLY_INIT (1u << 8)   // Dev is read-only during init.
#define FTLN_VERBOSE (1u << 9)         // Turn debug messages on.

#define NDM_PART_NAME_LEN 15  // Partition name size in bytes.
#define NDM_PART_USER 0       // Number of uint32_t in partition for user.

// Various NAND device types.
#define NDM_SLC (1 << 0)
#define NDM_MLC (1 << 1)

// Various function return types.
#define NDM_CTRL_BLOCK 2
#define NDM_REG_BLOCK 3

// Various states for a page - used by data_and_spare_check().
#define NDM_PAGE_ERASED 0
#define NDM_PAGE_VALID 1
#define NDM_PAGE_INVALID 2

// write_data_and_spare action parameter values.
#define NDM_ECC 1
#define NDM_ECC_VAL 2

// FsErrCode Error Code Assignments.
enum FsErrorCode {
  NDM_OK = 0,  // No errors.

  // TargetNDM Symbols.
  NDM_EIO = 1,             // Fatal I/O error.
  NDM_CFG_ERR = 2,         // NDM config error.
  NDM_ASSERT = 3,          // Inconsistent NDM internal values.
  NDM_ENOMEM = 4,          // NDM memory allocation failure.
  NDM_SEM_CRE_ERR = 5,     // NDM semCreate() failed.
  NDM_NO_META_BLK = 6,     // No metadata block found.
  NDM_NO_META_DATA = 7,    // Metadata page missing.
  NDM_BAD_META_DATA = 8,   // Invalid metadata contents.
  NDM_TOO_MANY_IBAD = 9,   // Too many initial bad blocks.
  NDM_TOO_MANY_RBAD = 10,  // Too many running bad blocks.
  NDM_NO_FREE_BLK = 11,    // No free block in NDM pool.
  NDM_IMAGE_RBB_CNT = 12,  // Bad block count in NDM image.
  NDM_RD_ECC_FAIL = 13,    // Read_page ECC decode failed.
  NDM_NOT_FOUND = 14,      // ndmDelDev() unknown handle.
  NDM_BAD_BLK_RECOV = 15,  // Running bad block recovery needed during RO-init.
  NDM_META_WR_REQ = 16,    // Metadata write request during RO-init.
  NDM_RBAD_LOCATION = 17,  // Running bad block replacement in virtual location.

  // TargetFTL-NDM Symbols.
  FTL_CFG_ERR = 20,         // FTL config error.
  FTL_ASSERT = 21,          // Inconsistent FTL internal values.
  FTL_ENOMEM = 22,          // FTL memory allocation failure.
  FTL_MOUNTED = 23,         // mount()/unformat() on mounted FTL.
  FTL_UNMOUNTED = 24,       // unmount() on unmounted FTL.
  FTL_NOT_FOUND = 25,       // FtlNdmDelVol() unknown name.
  FTL_NO_FREE_BLK = 26,     // No free FTL block.
  FTL_NO_MAP_BLKS = 27,     // No map block found during RO-init.
  FTL_NO_RECYCLE_BLK = 28,  // Recycle block selection failed.
  FTL_RECYCLE_CNT = 29,     // Repeated recycles did not free blocks.

  // Following would result in block erase except for RO-init flag.
  FTL_VOL_BLK_XFR = 40,  // Found interrupted volume block resume.
  FTL_MAP_BLK_XFR = 41,  // Found interrupted map block resume.
  FTL_UNUSED_MBLK = 42,  // Found unused map block during RO-init.
  FTL_VBLK_RESUME = 43,  // Low free block count: would resume volume block.
  FTL_MBLK_RESUME = 44,  // Low free block count: would resume map block.
};

// FS Report Events.
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
  FS_FORMAT_RESET_WC,
} FS_EVENTS;

//
// Type Declarations.
//

// NDM Partition Information.
typedef struct {
  uint32_t first_block;  // First virtual block for partition.
  uint32_t num_blocks;   // Number of virtual blocks in partition.
#if NDM_PART_USER
  uint32_t user[NDM_PART_USER];  // Reserved for the user.
#endif
  char name[NDM_PART_NAME_LEN];  // Partition name.
  uint8_t type;                  // Partition type - same as vstat().
} NDMPartition;

// Optional user data attached to a partition.
typedef struct {
  uint32_t data_size;  // Number of bytes on |data|.
  uint8_t data[];
} NDMPartitionUserData;

// Partition information version 2.
// TODO(fxbug.dev/40208): Merge with NDMPartition once the transition is made and the code
// stops writing version 1 data.
typedef struct {
  NDMPartition basic_data;
  NDMPartitionUserData user_data;
} NDMPartitionInfo;

// NDM Control Block.
typedef struct ndm* NDM;
typedef const struct ndm* CNDM;

typedef void (*LogFunction)(const char*, ...) __PRINTFLIKE(1, 2);

typedef struct {
  // Logger interface for different log levels.
  LogFunction trace;
  LogFunction debug;
  LogFunction info;
  LogFunction warning;
  LogFunction error;
  LogFunction fatal;
} Logger;

// FTL NDM structure holding all driver information.
typedef struct {
  uint32_t block_size;        // Size of a block in bytes.
  uint32_t num_blocks;        // Total number of blocks.
  uint32_t page_size;         // Flash page data size in bytes.
  uint32_t eb_size;           // Flash page spare size in bytes.
  uint32_t start_page;        // Volume first page on flash.
  uint32_t cached_map_pages;  // Number of map pages to be cached.
  uint32_t extra_free;        // Volume percentage left unused.
  uint32_t read_wear_limit;   // Device read-wear limit.
  void* ndm;                  // Driver's NDM pointer.
  uint32_t flags;             // Option flags.
  Logger logger;
} FtlNdmVol;

// TargetNDM Configuration Structure.
typedef struct NDMDrvr {
  uint32_t num_blocks;        // Total number of blocks on device.
  uint32_t max_bad_blocks;    // Maximum number of bad blocks.
  uint32_t block_size;        // Block size in bytes.
  uint32_t page_size;         // Page data area in bytes.
  uint32_t eb_size;           // Used spare area in bytes.
  uint32_t flags;             // Option flags.
  uint32_t type;              // Type of device.
  uint32_t format_version_2;  // "Boolean" variable: FALSE for control header version 1.
  void* dev;                  // Optional value set by driver.

  // Driver Functions.
  int (*write_data_and_spare)(uint32_t pn, const uint8_t* data, uint8_t* spare, int action,
                              void* dev);
  int (*write_pages)(uint32_t pn, uint32_t count, const uint8_t* data, uint8_t* spare, int action,
                     void* dev);
  int (*read_decode_data)(uint32_t pn, uint8_t* data, uint8_t* spare, void* dev);
  int (*read_pages)(uint32_t pn, uint32_t count, uint8_t* data, uint8_t* spare, void* dev);
  int (*transfer_page)(uint32_t old_pn, uint32_t new_pn, uint8_t* data, uint8_t* old_spare,
                       uint8_t* new_spare, int encode_spare, void* dev);
#if INC_FTL_NDM_MLC
  uint32_t (*pair_offset)(uint32_t page_offset, void* dev);
#endif
  int (*read_decode_spare)(uint32_t pn, uint8_t* spare, void* dev);
  int (*read_spare)(uint32_t pn, uint8_t* spare, void* dev);
  int (*data_and_spare_erased)(uint32_t pn, uint8_t* data, uint8_t* spare, void* dev);
  int (*data_and_spare_check)(uint32_t pn, uint8_t* data, uint8_t* spare, int* status, void* dev);
  int (*erase_block)(uint32_t pn, void* dev);
  int (*is_block_bad)(uint32_t pn, void* dev);
#if FS_DVR_TEST
  uint32_t dev_eb_size; /* Device spare area size. */
  void (*chip_show)(void* vol);
  int (*rd_raw_spare)(uint32_t p, uint8_t* spare, void* dev);
  int (*rd_raw_page)(uint32_t p, void* data, void* dev);
#endif
  Logger logger;
} NDMDrvr;

// Driver count statistics for TargetFTL-NDM volumes.
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
  uint32_t garbage_level;  // Garbage level as percentage 0 to 100.
} ftl_ndm_stats;

typedef struct {
  uint32_t num_blocks;

  // Percentage of space that is dirty from the total available. [0, 100).
  // Calculated as 100 x (1 - free_pages / volume_size - used_pages).
  uint32_t garbage_level;

  // Histogram of the wear level distribution. Each bucket represents about 5%
  // of the valid range, with the first bucket storing the number of blocks
  // with the lowest wear count, and the last bucket the most reused blocks.
  // If all blocks have the same wear count, the first 19 buckets will have no
  // samples.
  uint32_t wear_histogram[20];
  ftl_ndm_stats ndm;
} vstat;

// FTL Interface Structure.
typedef struct XfsVol {
  // Driver functions.
  int (*write_pages)(const void* buf, uint32_t page0, int cnt, void* vol);
  int (*read_pages)(void* buf, uint32_t page0, int cnt, void* vol);
  int (*report)(void* vol, uint32_t msg, ...);

  const char* name;    // Volume name.
  uint32_t flags;      // Option flags.
  uint32_t num_pages;  // Number of pages in volume.
  uint32_t page_size;  // Page size in bytes.
  void* vol;           // Driver's volume pointer.
  void* ftl_volume;    // FTL layer (block device) volume.
} XfsVol;

// FTL Wear Data Structure.
typedef struct {
  double lag_sd;              // Standard deviation of block wear lag.
  double used_sd;             // Standard deviation of used pages per block.
  uint32_t recycle_cnt;       // Total number of recycles performed.
  uint32_t max_consec_rec;    // Maximum number of consecutive recycles.
  uint32_t avg_consec_rec;    // Average number of consecutive recycles.
  uint32_t wc_sum_recycles;   // # recycles when average lag exceeds limit.
  uint32_t wc_lim1_recycles;  // # recycles when a lag exceeds WC_LAG_LIM1.
  uint32_t wc_lim2_recycles;  // # recycles when a lag exceeds WC_LAG_LIM2.
  uint32_t write_amp_max;     // Max fl pgs per vol pgs in FtlnWrPages().
  uint32_t write_amp_avg;     // 10 x flash wr pgs per FtlnWrPages() pgs.
  uint32_t avg_wc_lag;        // Average wear count lag.
  uint32_t lag_ge_lim0;       // # of blks w/wear count lag >= lag limit 0.
  uint32_t lag_ge_lim1;       // # of blks w/wear count lag >= lag limit 1.
  uint32_t max_ge_lim2;       // Max blks w/wear lag concurrently >= lim2.
  uint32_t max_wc_over;       // # of times max delta (0xFF) was exceeded.
  uint8_t lft_max_lag;        // Lifetime max wear lag below hi wear count.
  uint8_t cur_max_lag;        // Current max wear lag.
} FtlWearData;

__BEGIN_CDECLS

//
// Function Prototypes.
//
// FTL API.
int NdmInit(void);
int FtlInit(void);

int XfsAddVol(XfsVol* vol);
int GetFsErrCode(void);
void SetFsErrCode(int error);

// General API.
NDM ndmAddDev(const NDMDrvr* drvr);
int ndmDelDev(NDM ndm);
uint32_t ndmGetNumVBlocks(CNDM ndm);
int ndmUnformat(NDM ndm);

// Partitions API.
uint32_t ndmGetNumPartitions(CNDM ndm);
int ndmSetNumPartitions(NDM ndm, uint32_t num_partitions);
const NDMPartitionInfo* ndmGetPartitionInfo(CNDM ndm);
int ndmWritePartitionInfo(NDM ndm, const NDMPartitionInfo* partition);
const NDMPartition* ndmGetPartition(CNDM ndm, uint32_t part_num);
int ndmWritePartition(NDM ndm, const NDMPartition* part, uint32_t part_num, const char* name);
void ndmDeletePartitionTable(NDM ndm);
int ndmSavePartitionTable(NDM ndm);
int ndmDelVols(CNDM ndm);
int ndmDelVol(CNDM ndm, uint32_t part_num);

// FTL Volume API.
void* ndmAddVolFTL(NDM ndm, uint32_t part_no, FtlNdmVol* ftl, XfsVol* fs);

// Driver Test/Special Routines.
int ndmExtractBBL(NDM ndm);
int ndmInsertBBL(NDM ndm);
int NdmDvrTestAdd(const NDMDrvr* dev);
FtlWearData FtlnGetWearData(void* ftl);

// TargetNDM NVRAM Control Page Storage.
void NvNdmCtrlPgWr(uint32_t frst);
uint32_t NvNdmCtrlPgRd(void);

__END_CDECLS

#endif  // ZIRCON_SYSTEM_ULIB_FTL_FTL_H_
