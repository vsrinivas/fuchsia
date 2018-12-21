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

// To allow including FTLs with FAT/XFS or stand-alone
#if INC_FAT && !defined(INC_SECT_FTL)
#define INC_SECT_FTL TRUE
#endif
#if INC_XFS && !defined(INC_PAGE_FTL)
#define INC_PAGE_FTL TRUE
#endif


// Default is to include page cache with 512B sector-mode FTL
#if INC_SECT_FTL && !defined(INC_FTL_PAGE_CACHE)
#define INC_FTL_PAGE_CACHE TRUE
#endif

/***********************************************************************/
/* Symbol Definitions                                                  */
/***********************************************************************/

// Flag values for the file systems' driver flags field

#define FSF_QUOTA_ENABLED   (1 << 0)
#define FSF_READ_ONLY       (1 << 1)
#define FSF_AUTO_MOUNT      (1 << 2)
#define FSF_EXTRA_FREE      (1 << 3)
#define FSF_DATA_CACHE      (1 << 4)
#define FSF_FAT_MIN_CLUST   (1 << 5)    // use driver min cluster size
#define FSF_SYNCS_ON        (1 << 6)
#define FSF_BLUNK_FTL       (1 << 7)
#define FSF_TRANSFER_PAGE   (1 << 8)
#define FSF_MULTI_ACCESS    (1 << 9)
#define FSF_SLOW_MOUNT      (1 << 10)
#define FSF_DRVR_SEM        (1 << 11)
#define FSF_XFS_MIN_SECT    (1 << 12)
#define FSF_FAT_SYNC_FATS   (1 << 13)   // sync FATs though both valid
#define FSF_CRYPT           (1 << 14)   // use encryption layer
#define FSF_ERASE_WAIT      (1 << 15)
#define FSF_NOATIME         (1 << 16)   // dont update access time
#define FSF_NO_OVERWRITE    (1 << 17)
#define FSF_FTL_PAGE_CACHE  (1 << 18)
#define FSF_XFS_DCACHE      (1 << 19)
#define FSF_FAT_SECT_SIZE   (1 << 20)   // use driver sector size
#define FSF_NOMODTIME       (1 << 21)   // dont update modification time
#define FSF_FREE_SPARE_ECC  (1 << 22)   // spare decode has no overhead
#define FSF_NDM_INIT_WRITE  (1 << 23)   // re-write NDM metadata on init
#define FSF_READ_WEAR_LIMIT (1 << 24)   // driver specs read-wear limit

#define FSF_ALL                                                                                \
    (FSF_QUOTA_ENABLED | FSF_READ_ONLY | FSF_AUTO_MOUNT | FSF_EXTRA_FREE | FSF_DATA_CACHE |    \
     FSF_FAT_MIN_CLUST | FSF_SYNCS_ON | FSF_BLUNK_FTL | FSF_TRANSFER_PAGE | FSF_MULTI_ACCESS | \
     FSF_SLOW_MOUNT | FSF_DRVR_SEM | FSF_XFS_MIN_SECT | FSF_FAT_SYNC_FATS | FSF_CRYPT |        \
     FSF_ERASE_WAIT | FSF_NOATIME | FSF_NO_OVERWRITE | FSF_FTL_PAGE_CACHE | FSF_XFS_DCACHE |   \
     FSF_FAT_SECT_SIZE | FSF_NOMODTIME | FSF_FREE_SPARE_ECC | FSF_NDM_INIT_WRITE |             \
     FSF_READ_WEAR_LIMIT)


// This flag is obsolete. It is now the default for TargetNDM driver
// routines to use page numbers instead of byte addresses.
#define FSF_DRVR_PAGES 0

// Head/Sector/Cylinder Address Conversion Constants. The specific
// values are not critical, but are used for consistency when our code
// needs to assign a value or to convert an LBA to a CHS address.
#define FAT_NUM_HEADS 4
#define FAT_SECTS_PER_TRACK 64

// Valid TargetFAT partition types
#define FAT_12BIT 0x01
#define FAT_16BIT 0x04
#define FAT_BIGDOS 0x06
#define FAT_32BIT 0x0B

// Size in bytes of a FAT sector
#define FAT_SECT_SZ 512

/***********************************************************************/
/* Macro Definitions                                                   */
/***********************************************************************/
#if FS_ASSERT
void AssertError(int line, char* file);
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
#if FS_CRYPT
// FS Crypt Driver Definitions
typedef enum {
    FS_AES_CTR,
    FS_AES_XTS,
} FS_CRYPTS;

typedef struct {
    // Initialized by user
    FS_CRYPTS type; // algorithm type
    ui8* key;       // algorithm secret key
    int keylen;     // key length

    // Private data for encryption/file system layer
    ui32 page_sz; // file system min read/write size
    ui32 buf_pgs; // encryption buffer size in pages
    void* fs_vol; // parameter passed to FS driver functions
    int (*fs_wr)(const void* buf, ui32 frst, int n, void* fs_vol);
    int (*fs_rd)(void* buf, ui32 frst, int n, void* fs_vol);
} FSCryptDrvr;
#endif /* FS_CRYPT */

// FFS NAND specific driver interface
typedef struct {
    ui32 start_page;      // volume first page on flash
    ui32 read_wear_limit; // device read-wear limit
    int (*write_page)(const void* buffer, ui32 pn, ui32 type, void* vol);
    int (*write_pages)(ui32 start_pn, ui32 count, const void* data, void* spare, void* ndm);
    int (*read_page)(ui32 pn, void* buffer, void* vol);
    int (*read_pages)(ui32 start_pn, ui32 count, void* data, void* spare, void* ndm);
    int (*transfer_page)(ui32 old_pn, ui32 new_pn, ui8* data, ui8* spare, void* ndm);
    int (*read_type)(ui32 pn, ui32* typep, void* vol);
    int (*page_erased)(ui32 pn, void* vol);
    int (*erase_block)(ui32 pn, void* vol);
#if INC_FFS_NDM_MLC || INC_FTL_NDM_MLC
    ui32 (*pair_offset)(ui32 page_offset, void* vol);
#endif
#if FS_DVR_TEST
    int spare_size;
    void (*chip_show)(void* vol);
    int (*rd_raw_page)(ui32 pn, void* buf, void* vol);
    int (*is_block_bad)(ui32 addr, void* dev);
#endif
} FsNandDriver;

// FFS NOR specific driver interface
typedef struct {
    int (*read_byte)(ui32 addr, void* vol);
    int (*write_byte)(ui32 addr, ui8 data, void* vol);
    int (*write_page)(const void* buffer, ui32 addr, void* vol);
    int (*page_erased)(ui32 addr, void* vol);
    int (*read_page)(void* buffer, ui32 addr, void* vol);
    int (*transfer_page)(ui32 old_addr, ui32 new_addr, ui8* buf, void* vol);
    int (*erase_block)(ui32 addr, void* vol);
    void (*erase_wait)(void* vol);
#if FS_DVR_TEST
    void (*chip_show)(void* vol);
    int (*chip_erase)(void* vol);
#endif
} FsNorDriver;

typedef union {
    FsNandDriver nand;
    FsNorDriver nor;
} FfsDriver;

// FFS structure holding all driver information
typedef struct {
    char* name;          // name of this volume
    ui32 type;           // vol flash type
    ui32 block_size;     // minimum erasable block size in bytes
    ui32 page_size;      // page size in bytes
    ui32 num_blocks;     // number of blocks in volume
    ui32 mem_base;       // volume base address
    ui32 extra_free;     // extra free space for faster execution
    ui32 file_cache_kbs; // user data cache size in KBs
    void* vol;           // driver's volume pointer
    void* vol_handle;    // next_sect_chain() handle
    ui32 flags;
    FfsDriver driver;
} FfsVol;

// XFS structure holding all driver information
typedef struct XfsVol {
    // Driver functions
    int (*write_pages)(const void* buf, ui32 frst_pg, int cnt, void* vol);
    int (*read_pages)(void* buf, ui32 frst_pg, int cnt, void* vol);
    int (*report)(void* vol, ui32 msg, ...);

#if FS_CRYPT
    FSCryptDrvr fs_crypt; // handle for encryption layer
#endif

    const char* name;       // volume name
    ui32 flags;             // option flags
    ui32 start_page;        // first page in volume
    ui32 num_pages;         // number of pages in volume
    ui32 page_size;         // page size in bytes
    ui32 file_cache_kbs;    // user data cache size in kbs
    ui32 min_sect_size;     // minimum desired sector size
    ui32 dcache_min_d_ents; // minimum/maximum number of directory
    ui32 dcache_max_d_ents; // entries for a directory to be cached
    ui32 dcache_size;       // number of directories to be cached
    void* vol;              // driver's volume pointer
    void* ftl_volume;       // ftl layer (block device) volume
} XfsVol;

// FAT structure holding all driver information
typedef struct {
    // Driver functions
    int (*write_sectors)(const void* buf, ui32 f_sect, int cnt, void* vol);
    int (*read_sectors)(void* buf, ui32 first_sect, int count, void* vol);
    int (*report)(void* vol, ui32 msg, ...);
    void (*slow_mount)(const char* name);

#if FS_CRYPT
    FSCryptDrvr fs_crypt; // handle for encryption layer
#endif

    const char* name;            // volume name
    void* vol_sem;               // driver-provided volume semaphore
    ui32 serial_num;             // volume serial number - for FSUID
    ui32 cached_fat_sects;       // FAT cache size (# of sectors)
    ui32 num_heads;              // number of heads
    ui32 sects_per_trk;          // sectors per track
    ui32 start_sect;             // starting sector for partition
    ui32 num_sects;              // total number of volume sectors
    ui32 sect_size;              // sector size in bytes
    ui32 file_cache_kbs;         // user data cache size in KBs
    ui32 dir_cache_kbs;          // directory cache size in KBs
    ui32 min_clust_size;         // minimum cluster size in bytes
    ui32 flags;                  // option flags
    void* vol;                   // driver's volume pointer
    void* vol_handle;            // next_sect_chain() handle
    ui8 desired_sects_per_clust; // desired cluster size
    ui8 desired_type;            // desired FAT type
    ui8 fixed;                   // fixed/removable media flag
} FatVol;

// FTL NDM structure holding all driver information
typedef struct {
    ui32 block_size;       // size of a block in bytes
    ui32 num_blocks;       // total number of blocks
    ui32 page_size;        // flash page data size in bytes
    ui32 eb_size;          // flash page spare size in bytes
    ui32 start_page;       // volume first page on flash
    ui32 cached_map_pages; // number of map pages to be cached
#if INC_FTL_PAGE_CACHE
    ui32 cached_vol_pages; // number of volume pages to be cached
#endif
    ui32 extra_free;      // volume percentage left unused
    ui32 read_wear_limit; // device read-wear limit
    void* ndm;            // driver's NDM pointer
    ui32 flags;           // option flags
    ui32 type;            // device type

    // Driver functions:
    int (*write_data_and_spare)(ui32 pn, const void* data, void* spare, void* ndm);
    int (*write_pages)(ui32 start_pn, ui32 count, const void* data, void* spare, void* ndm);
    int (*read_spare)(ui32 pn, void* spare, void* ndm);
    int (*read_pages)(ui32 start_pn, ui32 count, void* data, void* spare, void* ndm);
    int (*page_check)(ui32 pn, ui8* data, ui8* spare, void* ndm);
    int (*transfer_page)(ui32 old_pn, ui32 new_pn, ui8* data, ui8* spare, void* ndm);
    int (*erase_block)(ui32 pn, void* ndm);
#if INC_FFS_NDM_MLC || INC_FTL_NDM_MLC
    ui32 (*pair_offset)(ui32 page_offset, void* ndm);
#endif
} FtlNdmVol;

// FTL NOR SLC/MLC/SIB/XDS structure holding all driver information
typedef struct {
    ui32 block_size;       // size of a block in bytes
    ui32 num_blocks;       // total number of blocks
    ui32 mem_base;         // base address
    ui32 type;             // vol flash type
    ui32 cached_map_pages; // number of cached map pages
    ui32 extra_free;       // volume percentage left unused
    ui32 read_wear_limit;  // device read-wear limit
    void* vol;             // driver's volume pointer
    ui32 flags;            // option flags

    // Driver functions
    int (*write_page)(ui32 addr, const void* data, void* vol);
    int (*transfer_page)(ui32 old_addr, ui32 new_addr, ui8* buf, void* vol);
    int (*read_page)(ui32 addr, void* data, void* vol);
    int (*read_pages)(ui32 start_addr, ui32 count, void* data, void* vol);
    int (*and_byte)(ui32 addr, ui8 data, void* vol);
    int (*read_byte)(ui32 addr, ui8* data, void* vol);
    int (*write_long)(ui32 addr, ui32 data, void* vol);
    int (*read_long)(ui32 addr, ui32* data, void* vol);
    int (*erase_block)(ui32 addr, void* vol);
#if FS_DVR_TEST
    void (*chip_show)(void* vol);
    int (*chip_erase)(void* vol);
#endif
} FtlNorVol;

// FTL NOR WR1 structure holding all driver information
typedef struct {
    ui32 block_size;       // size of a block in bytes
    ui32 num_blocks;       // total number of blocks
    ui32 mem_base;         // base address
    ui32 cached_map_pages; // number of cached map pages
    ui32 extra_free;       // volume percentage left unused
    ui32 read_wear_limit;  // device read-wear limit
    void* vol;             // driver's volume pointer
    ui32 flags;            // option flags

    // Driver functions
    int (*write_512B)(ui32 addr, const void* data, void* vol);
    int (*transfer_512B)(ui32 old_addr, ui32 new_addr, ui8* buf, void* vol);
    int (*read_512B)(ui32 start_addr, ui32 count, void* data, void* vol);
    int (*erased_512B)(ui32 addr, void* vol);
    int (*write_32B)(ui32 addr, const void* data, void* vol);
    int (*read_32B)(ui32 addr, void* data, void* vol);
    int (*erased_32B)(ui32 addr, void* vol);
    int (*erase_block)(ui32 addr, void* vol);
#if FS_DVR_TEST
    void (*chip_show)(void* vol);
    int (*chip_erase)(void* vol);
#endif
} FtlWr1Vol;

// A partition entry in the partition table
// Following values for system_id are supported:
// 0x01 = FAT12 partition with fewer than 32680 sectors
// 0x04 = FAT16 partition with between 32680 and 65535 sectors
// 0x05 = extended DOS partition
// 0x06 = BIGDOS FAT primary or logical drive
// 0x0B = FAT32 partition up to 2047 GB
typedef struct {
    ui32 first_sect; // first actual sector of partition (from 0)
    ui32 num_sects;  // total number of sectors in partition
    ui16 start_cyl;  // starting cylinder
    ui16 end_cyl;    // ending cylinder
    ui8 boot_id;     // 0x80 if bootable partition, 0x00 otherwise
    ui8 start_head;  // starting head
    ui8 start_sect;  // starting sector in track
    ui8 type;        // partition type
    ui8 end_head;    // ending head
    ui8 end_sect;    // ending sector in track
} FATPartition;

// Opaque Definition of TargetFAT's Internal Control Block
typedef struct fat FAT;

// FS Report Events
typedef enum {
    FS_MOUNT,
    FS_UNMOUNT,
    FS_FORMAT,
    FS_VCLEAN,
    FS_MARK_UNUSED,
    FS_SYNC,
    FS_FLUSH_SECT,
    FS_VSTAT,
    FS_UNFORMAT,
    FS_PAGE_SZ,
    FS_FAT_SECTS,
    FS_FORMAT_RESET_WC,
} FS_EVENTS;

// Flash Controller Configuration Codes
typedef enum {
    AMD_LVM_CFG,
    HY27US08B_CFG,
    MT29F_CLR_CFG,
    MT29F_SLC_CFG,
    MT29F_SPI_CFG,
    MT29F_ECC_CFG,
    SAMS_K9GAG_CFG,
    SAMS_K9WAG_CFG,
    SAMS_ETC_CFG,
    SAMS_KFK_CFG,
    SPSN_FLP_CFG,
    SPSN_GLN_CFG,
    SPSN_GLS_CFG,
    SPSN_WSP_CFG,
    SPSN_XDS_CFG,
    S25FL032_CFG,
    S34ML01G1_CFG,
    S34ML02G1_CFG,
    ST_NANDA_CFG,
    ST_NANDB_CFG,
    ST_25P32_CFG,
    NMX_MLC_CFG,
    NMX_M29EW_CFG,
    NMX_SIB_CFG,
    M25PE80_CFG,
    TC58BVG0S_CFG,
    RAM_DVR_CFG,
    SAMS_K9F1G_CFG
} FL_BUS_CFG;

/***********************************************************************/
/* Variable Declarations                                               */
/***********************************************************************/
extern SEM FileSysSem;
extern SEM FsNvramSem;
extern const ui8 NumberOnes[];

/***********************************************************************/
/* Function Prototypes                                                 */
/***********************************************************************/
int FfsAddVol(FfsVol* vol);
int FfsAddNdmVol(FfsVol* vol, ui8* spare_buf);
int FatAddVol(FatVol* vol);
int XfsAddVol(XfsVol* vol);
int FtlNorAddFatVol(FtlNorVol* ftl_dvr, FatVol* fat_dvr);
int FtlWr1AddFatVol(FtlWr1Vol* ftl_dvr, FatVol* fat_dvr);
int FtlNorAddXfsVol(FtlNorVol* ftl_dvr, XfsVol* xfs_dvr);
int FtlWr1AddXfsVol(FtlWr1Vol* ftl_dvr, XfsVol* xfs_dvr);

// FAT interface to its underlying driver
FAT* FatVolOpen(const char* name);
int FatVolClose(FAT** fat);
int FatVolSync(FAT* fat);
int FatVolWriteSectors(FAT* fat, void* buf, ui32 first_sect, int count);
int FatVolReadSectors(FAT* fat, void* buf, ui32 first_sect, int count);
int FatVolSize(const FAT* fat, ui32* num_sects, ui32* sect_size);

// FAT recovery API
int FatVolCheck(const char* name);
int FatVolFix(const char* name);

// FAT volume label setting/retrieval APIs
int FatReadLabel(const char* vol_name, char* label, size_t label_sz);
int FatWriteLabel(const char* vol_name, const char* label);

// Partition access/modification functions
int FatRdPartitions(const FatVol* fat_vol, FATPartition* partitions, int max_partitions);
int FatWrPartitions(const FatVol* fat_vol, FATPartition* partitions, int num_partitions);
int FatWrPartition(const FatVol* fat_vol);
int FatGetPartitions(const ui8* boot_rec, FATPartition* partitions, int max_partitions);
int FatTotNumPartitions(const FatVol* fat_vol);

// FAT-Lite functions
int FatlAddVol(FatVol* driver);
int FatlDelVol(void);
int FatlRdPartitions(const FatVol* fat_vol, FATPartition* partitions, int max_partitions);
int FatlNumPartitions(const FatVol* fat_vol);
int FatlError(int err_code);

// 1 bit correction ECC encoding/decoding functions
void eccEnc512B1E(const ui8* data, ui8* ecc); // 3 bytes of ECC
int eccDec512B1E(ui8* data, const ui8* ecc);  // 3 bytes of ECC
int eccDec512B1E2(ui8* data, const ui8* read_ecc, const ui8* calc_ecc);
void eccEnc14B1E(const ui8* data, ui8* ecc); // 2 bytes of ECC
int eccDec14B1E(ui8* data, const ui8* ecc);  // 2 bytes of ECC

// 4 bit correction ECC encoding/decoding functions
void eccEnc512B4E(const ui8* data, ui8* ecc); // 10 bytes of ECC
int eccDec512B4E(ui8* data, const ui8* ecc);  // 10 bytes of ECC
void eccEnc14B4E(const ui8* data, ui8* ecc);  // 5 bytes of ECC
int eccDec14B4E(ui8* data, const ui8* ecc);   // 5 bytes of ECC

// File System API to interact with NVRAM
void FsSaveMeta(ui32 vol_id, ui32 meta, const char* vol_name);
int FsReadMeta(ui32 vol_id, ui32* meta, const char* vol_name);

// Driver Test Routines
int FfsNorDvrTestAdd(const FfsVol* vol);
int FtlrDvrTestAdd(const FtlNorVol* ftl_vol);
int FtlWr1DvrTestAdd(const FtlWr1Vol* ftl_vol);

// NAND Flash Controller
int nandInit(FL_BUS_CFG config);
void nandCfgShow(char* beg, char* end);
void nandWrEn(void);
void nandWrDis(void);
void nandLowerCE(int instance);
void nandRaiseCE(int instance);
void nandAddr1B(uint b1);
void nandAddr2B(uint b1, uint b2);
void nandAddr3B(uint b1, uint b2, uint b3);
void nandAddr4B(uint b1, uint b2, uint b3, uint b4);
void nandAddr5B(uint b1, uint b2, uint b3, uint b4, uint b5);
void nandCmd(uint cnd);
void nandBusyWait(int instance);
void nandWrData8(const ui8* src, uint number);
void nandRdData8(ui8* dst, uint number);
int nandErased8(uint number);
void nandEccStart(void);
void nandEccStop(void);
void nandGet512B1E(ui8* ecc_ptr);
void nandGet512B1ETest(ui8* data, ui8* ecc);
int nandDec512B1E(ui8* data, const ui8* read_ecc, const ui8* calc_ecc);
void nandKeyStart(void);
ui32 nandValidKey(const void* data, uint page_len, ui32 init_val);
void nandIntrWait(void);

// NOR Flash Controller
ui32 norInit(FL_BUS_CFG config);
void norCfgShow(char* beg, char* end);
void norLowerCE(int instance);
ui32 norEnable(ui32 addr, int instance);
void norDisable(void);
void norWaitINT(void);

// SPI Flash Controller
void* spiConfig(FL_BUS_CFG config);

extern ui32 (*FlCfgFirst)(void);
extern ui32 (*FlCfgFaster)(int rc);
extern ui32 (*FlCfgSlower)(void);
extern void (*FlCfgShow)(char* beg, char* end);
extern void (*FlCfgSet)(ui32 cfg);

// TargetNDM NVRAM Control Page Storage
void NvNdmCtrlPgWr(ui32 frst);
ui32 NvNdmCtrlPgRd(void);

int next_sect_chain(void* vol_handle, ui32 curr_sect, ui32* next_sect);

#ifdef __cplusplus
}
#endif
