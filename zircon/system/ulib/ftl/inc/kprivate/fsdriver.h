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

#define FSF_EXTRA_FREE      (1 << 0)
#define FSF_TRANSFER_PAGE   (1 << 1)
#define FSF_MULTI_ACCESS    (1 << 2)
#define FSF_FREE_SPARE_ECC  (1 << 3)    // spare decode has no overhead
#define FSF_NDM_INIT_WRITE  (1 << 4)    // re-write NDM metadata on init
#define FSF_READ_WEAR_LIMIT (1 << 5)    // driver specs read-wear limit
#define FSF_READ_ONLY_INIT  (1 << 6)    // dev is read-only during init

#define FSF_ALL                                                                             \
    (FSF_EXTRA_FREE     | FSF_TRANSFER_PAGE   | FSF_MULTI_ACCESS   | FSF_FREE_SPARE_ECC |   \
     FSF_NDM_INIT_WRITE | FSF_READ_WEAR_LIMIT | FSF_READ_ONLY_INIT)

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

// XFS structure holding all driver information
typedef struct XfsVol {
    // Driver functions
    int (*write_pages)(const void* buf, ui32 frst_pg, int cnt, void* vol);
    int (*read_pages)(void* buf, ui32 frst_pg, int cnt, void* vol);
    int (*report)(void* vol, ui32 msg, ...);

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

// FTL NDM structure holding all driver information
typedef struct {
    ui32 block_size;       // size of a block in bytes
    ui32 num_blocks;       // total number of blocks
    ui32 page_size;        // flash page data size in bytes
    ui32 eb_size;          // flash page spare size in bytes
    ui32 start_page;       // volume first page on flash
    ui32 cached_map_pages; // number of map pages to be cached
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
#if INC_FTL_NDM_MLC
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
int NdmInit(void);
int FtlInit(void);
int XfsAddVol(XfsVol* vol);
int FtlNorAddXfsVol(FtlNorVol* ftl_dvr, XfsVol* xfs_dvr);
int FtlWr1AddXfsVol(FtlWr1Vol* ftl_dvr, XfsVol* xfs_dvr);

// FAT volume label setting/retrieval APIs
int FatReadLabel(const char* vol_name, char* label, size_t label_sz);
int FatWriteLabel(const char* vol_name, const char* label);

// Partition access/modification functions
int FatGetPartitions(const ui8* boot_rec, FATPartition* partitions, int max_partitions);

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
