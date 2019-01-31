// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio_tfs.h>
#include <posix.h>
#include <fsdriver.h>

/***********************************************************************/
/* Configuration                                                       */
/***********************************************************************/
#ifndef FS_MEM_PROFILE
#define FS_MEM_PROFILE FALSE  // TRUE to enable FS heap profiling
#endif
#define INC_FAT_MBR INC_FAT

/***********************************************************************/
/* Symbol Definitions                                                  */
/***********************************************************************/
// FS API input parameter checking
#define FS_PARM_CHECK (OS_PARM_CHECK || CIFS_INCLUDED)

// FSEARCH codes
#define FIRST_DIR   0
#define CURR_DIR    1
#define PARENT_DIR  2
#define ACTUAL_DIR  3
#define DIR_FILE    4

// The different types of FileTable entries
#define FEMPTY 0
#define FDIREN 1
#define FCOMMN 2
#define FFILEN 3

// The different types of sectors
#define FOTHR 0
#define FHEAD 1
#define FTAIL 2

// Number of entries per file table
#define FNUM_ENT 20

// Flag values for writing/skipping control information
#define FFLUSH_DO 1
#define FFLUSH_DONT 0

// Flag values for adjusting file pointers in file control blocks
#define ADJUST_FCBs TRUE
#define DONT_ADJUST_FCBs FALSE

// Flag values for acquiring exclusive access to volumes
#define FS_GRAB_SEM TRUE
#define FS_NO_SEM FALSE

// Flag values for FS sync
#define FORCED_SYNC TRUE
#define UNFORCED_SYNC FALSE

// Flag values for the file/directory structure
#define F_WRITE            (1u << 0)
#define F_READ             (1u << 1)
#define F_EXECUTE          (1u << 2)
#define F_APPEND           (1u << 3)
#define F_NONBLOCK         (1u << 4)
#define F_ASYNC            (1u << 5)

#define FCB_DIR            (1u << 6)
#define FCB_FILE           (1u << 7)
#define FCB_TTY            (1u << 8)
#define FCB_MOD            (1u << 9)
#define FCB_NO_UPDATE      (1u << 10)
#define FCB_CLOSE          (1u << 11)
#define FCB_ESC_HIT        (1u << 12)
#define FCB_NO_OVERWRITE   (1u << 13)
#define FSEARCH_NO_SEM     (1u << 14)

// Flag values for FSearchFSUID()
#define FILE_NAME 1
#define PATH_NAME 2

// Return values for TFFS recycle function
#define RECYCLE_FAILED      -1
#define RECYCLE_NOT_NEEDED  1
#define RECYCLE_OK          0

#define REMOVED_LINK 0xFFFFFFFF // Value assigned to next and
                                // prev for removed link
#define OFF_REMOVED_LINK 0xFFFE

#define ROOT_DIR_INDEX FOPEN_MAX // index in Files[] for root

// Maximum allowed number of FFS_VOLS + FAT_VOLS with NAND_FTLS or fixed
// This value is used to issue volume ID numbers for the FSUID when a
// serial number is not provided
#define MAX_FIXED_VOLS 32

/*
** Number of bits the file ID, volume ID take in the 32 bit FSUID
*/
#define FID_LEN 20
#define VID_LEN 8

#define FID_MASK 0xFFFFFu // MUST be 2^FID_LEN - 1
#define VID_MASK 0xFFu    // MUST be 2^VID_LEN - 1
#define FSUID_MASK ((VID_MASK << FID_LEN) | FID_MASK)

// Default block read limits to avoid read-disturb errors
#define MLC_NAND_RC_LIMIT 100000
#define SLC_NAND_RC_LIMIT 1000000
#define NOR_RC_LIMIT 1000000

// Symbols for excluding/including SLC/MLC-specific code
#define INC_NDM_SLC (INC_FTL_NDM_SLC || INC_FFS_NDM_SLC)
#define INC_NDM_MLC (INC_FFS_NDM_MLC || INC_FTL_NDM_MLC)

/***********************************************************************/
/* Macro Definitions                                                   */
/***********************************************************************/
// Bitmap accessors
#define BITMAP_ON(start, i) (*((ui8*)(start) + (i) / 8) |= (ui8)(1 << ((i) % 8)))
#define BITMAP_OFF(start, i) (*((ui8*)(start) + (i) / 8) &= (ui8) ~(1 << ((i) % 8)))
#define IS_BITMAP_ON(start, i) (*((ui8*)(start) + (i) / 8) & (1 << ((i) % 8)))

// Create an FSUID val(ui32) based on volume number and file number
#define FSUID(vid, fid) (ui32)((((vid)&VID_MASK) << FID_LEN) | ((fid)&FID_MASK))

// Accessor macros for volume ID, file ID from an FSUID
#define GET_VID(fsuid) (((fsuid) & (VID_MASK << FID_LEN)) >> FID_LEN)
#define GET_FID(fsuid) ((fsuid)&FID_MASK)

// Determine if an FCB pointer/fid is valid
#define IS_VALID_FCB(fcbp) ((fcbp) && (fcbp) >= &Files[0] && (fcbp) < &Files[FOPEN_MAX])
#define IS_VALID_FID(fid) ((fid) >= 0 && (fid) < FOPEN_MAX)

// Determine if a file system volume is on the mounted list
#define IS_MOUNTED(vol) ((vol)->sys.prev || (MountedList.head == &((vol)->sys)))

/***********************************************************************/
/* Type Definitions                                                    */
/***********************************************************************/
typedef struct file_sys FileSys;
struct file_sys {
    FileSys* next;
    FileSys* prev;
    ui32 flags; // holds various volume bit flags
#define FSYS_FATAL_ERR (1 << 0)
#define FSYS_READ_ONLY (1 << 1)
#define FSYS_VOL_BUSY (1 << 2)
    void* (*ioctl)(FILE_TFS* stream, int code, ...);
    void* volume;
#if FILE_AIO_INC
    int aio_buf_size; // max number of buffered AIO transfers
    int aio_num_bufs; // number of sectors in each AIO transfer
#endif
    char name[(FILENAME_MAX + 3) & ~3]; // avoid extra padding
};

typedef struct head_entry {
    FileSys* head;
    FileSys* tail;
} HeadEntry;

#if FILE_OFFSETS
// An indivual value in the offsets values array
typedef struct {
    ui32 sect_num; // volume sector for offset
    ui32 sect_off; // sector offset within file for offset
} FOff;

// FCB offsets type
typedef struct {
    FOff values[FILE_OFFSETS]; // recorded file offsets
    ui32 f_sects;              // recorded file size in sectors
} FOffs;
#endif // FILE_OFFSETS

// FILE implementation for stdio.h, DIR implementation for posix.h
struct file {
    SEM sem; // internal locking semaphore
    void (*acquire)(FILE_TFS* fcb, uint access_mode);
    void (*release)(FILE_TFS* fcb, uint access_mode);
    void* handle; // self handle to be passed to ioctl
    void* volume; // file system file/dir belongs to
    void* pos;    // directory position; used for readdir
    void* (*ioctl)(FILE_TFS* fcb, int code, ...);
    int (*read_TFS)(FILE_TFS* fcb, ui8* buf, ui32 len);
    int (*write_TFS)(FILE_TFS* fcb, const ui8* buf, ui32 len);
#if FILE_AIO_INC
    void* aio_ext; // asynchronous I/O control block
#endif
#if FILE_OFFSETS
    FOffs* offsets; // file offsets
#endif
    int hold_char;
    int errcode;
    struct dirent_TFS dent; // used by readdir()
    fpos_t_TFS curr_ptr;    // current position in file
    fpos_t_TFS old_ptr;     // previous position in file
    ui32 flags;             // file/dir specific flags
    ui32 parent;            // parent directory
};

// Ioctl() Commands
typedef enum {
    // Begin list of APIs that cause state change. Must be grouped together
    // numerically because XFS uses range comparisons to test for these.
    CHMOD = 1,
    CHOWN,
    CLOSE_UNLINK,
    FCLOSE,
    FFLUSH,
    FSEEK,
    FSEEK64,
    FSTAT_SET,
    FTRUNCATE,
    FTRUNCATE64,
    LINK,
    MKDIR,
    OPEN,
    REMOVE,
    RENAME,
    RMDIR,
    SHRED,
    SORTDIR,
    STAT_SET,
    TRUNCATE,
    TRUNCATE64,
    UNLINK_ALL,
    UTIME,
    VFS_CREATE,
    VFS_LINK,
    VFS_MKDIR,
    VFS_RENAME,
    VFS_TRUNCATE,
    VFS_FTRUNCATE64,
    VFS_UNLINK,
    // End list of APIs that cause change of media state
    ACCESS,
    ATTRIB,
    CHDIR,
    CLOSEDIR,
    CTIME,
    DISABLE_SYNC,
    DUP,
    ENABLE_SYNC,
    FEOF,
    FREOPEN,
    FSEARCH,
    FSETPOS,
    FRSTAT,
    FSTAT,
    FTELL,
    GETCWD,
    GET_FL,
    GET_FSUID,
    GET_FSUID_FID,
    GET_NAME,
    GET_QUOTA,
    GET_QUOTAM,
    ISOPEN,
    MMAP,
    NXT_SECT_CHAIN,
    OPENDIR,
    PARAM_DELETE,
    PARAM_READ,
    PARAM_SIZE,
    PARAM_WRITE,
    READDIR,
    READDIR_OPEN,
    READDIR_STAT,
    REWINDDIR,
    RSTAT,
    SECT_FIRST,
    SECT_NEXT,
    SETVBUF,
    SET_FL,
    STAT,
    TMPFILE,
    TMPNAM,
    UNMOUNT,
    VCLEAN,
    VMEMGB,
    VSTAT,
    VSYNC,
    VFS_OPEN
} IOCTLS;

typedef struct f_f_e FFSEnt;
typedef struct r_f_e RFSEnt;
typedef struct flash_entries FFSEnts;
typedef struct ram_entries RFSEnts;

typedef struct {
    ui16 sector;
    ui16 offset;
} OffLoc;

// Represents structure of common info between dir and file in RAM
typedef struct {
    uid_t user_id;  // used ID
    gid_t group_id; // group ID
    mode_t mode;    // file/dir create mode
    ui16 padding;
    time_t mod_time;      // last modified time for entry
    time_t ac_time;       // last access time for entry
    ui32 size;            // size of file (0 for dirs)
    ui32 fileno;          // file/dir number
    ui32 attrib;          // attribute() field
    FFSEnt* addr;         // entry location in RAM
    OffLoc one_past_last; // pointer to one past the EOF
    ui16 frst_sect;       // first sector for files (0 for dirs)
    ui16 last_sect;       // last sector for files (0 for dirs)
    ui8 link_cnt;         // number of links from file/dir
    ui8 open_cnt;         // number of file/dir open FCBs
    ui8 open_mode;        // file/dir open mode
    ui8 opl_offset_hi;    // one past last offset overflow byte
} FCOM_T;

// RFS variation of common entry
typedef struct {
    uid_t user_id;  // user ID
    gid_t group_id; // group ID
    mode_t mode;    // file/dir create mode
    ui16 padding;
    time_t mod_time;    // last modified time for entry
    time_t ac_time;     // last access time for entry
    ui32 size;          // size of file (0 for dirs)
    ui32 fileno;        // file/dir number
    RFSEnt* addr;       // entry location in RAM
    ui32 one_past_last; // offset within last sector
    void* first;        // first sector(file)/entry(directory)
    void* last;         // last sector(file)
    ui8 link_cnt;       // number of links from file/dir
    ui8 open_cnt;       // number of file/dir open FCBs
    ui8 open_mode;      // file/dir open mode
    ui8 temp;           // indicates temporary file
} RCOM_T;

// Represents structure of a link to a file as used in RAM
typedef struct {
    char* name;         // link name
    FFSEnt* next;       // next entry in parent directory
    FFSEnt* prev;       // prev entry in parent directory
    FCOM_T* comm;       // pointer to actual file info
    FFSEnt* parent_dir; // pointer to parent directory
    uint open_cnt;      // number of file entry opens
} FFIL_T;

// Represents structure of a link to a dir as used in RAM
typedef struct {
    char* name;         // directory name
    FFSEnt* next;       // next entry in parent directory
    FFSEnt* prev;       // prev entry in parent directory
    FCOM_T* comm;       // pointer to actual dir info
    FFSEnt* parent_dir; // pointer to parent directory
    uint open_cnt;      // number of directory entry opens
    FFSEnt* first;      // head of contents list for dir
#if QUOTA_ENABLED
    ui32 max_q;      // max quota
    ui32 min_q;      // min quota
    ui32 used;       // used space
    ui32 free;       // free space
    ui32 free_below; // free space below
    ui32 res_below;  // reserved below
#endif
} FDIR_T;

// RFS variation of file entry
typedef struct {
    char* name;         // file/directory name
    RFSEnt* next;       // next entry in parent directory
    RFSEnt* prev;       // prev entry in parent directory
    RCOM_T* comm;       // pointer to common entry
    RFSEnt* parent_dir; // pointer to parent directory
    uint open_cnt;      // number of file entry opens
} RFIL_T;

typedef RFIL_T RDIR_T;

// Represents the type for an entry in RAM (dir, file or link)
union f_file_entry {
    FDIR_T dir;
    FCOM_T comm;
    FFIL_T file;
};

// Represents the type for an entry for RFS (dif, file or link)
union r_file_entry {
    RDIR_T dir;
    RCOM_T comm;
    RFIL_T file;
};

// An entry in RAM consists of its type and value as a union
struct f_f_e {
    union f_file_entry entry;
    ui8 type;
    FFSEnts* tbl;
};

// An entry in RFS consists of its type and value as a union
struct r_f_e {
    union r_file_entry entry;
    ui8 type;
};

// Holds a table full of entries, pointers to next and prev, num free
struct flash_entries {
    FFSEnt tbl[FNUM_ENT];
    FFSEnts* next_tbl;
    FFSEnts* prev_tbl;
    ui32 free;
    ui32 num;
};

struct ram_entries {
    RFSEnt tbl[FNUM_ENT];
    RFSEnts* next_tbl;
    RFSEnts* prev_tbl;
    ui32 free;
};

// Structure to cast all volume structures to, to retrieve ioctl_func
typedef struct {
    CircLink link; /*lint !e601, !e10*/
    FileSys sys;
} FsVolume;

#if FS_CRYPT
// FS Crypt Type
typedef struct fscrypt FSCrypt;
struct fscrypt {
    // Public API
    void (*delete)(FSCrypt** fs_crypt);
    int (*decr)(const FSCrypt* fs_crypt, ui8* data, ui32 vpn, int n);
    int (*rd_decr)(const FSCrypt* fs_crypt, ui8* data, ui32 vpn, int n);
    int (*encr)(const FSCrypt* fs_crypt, ui8* data, ui32 vpn, int n);
    int (*encr_wr)(const FSCrypt* fs_crypt, const ui8* data, ui32 vpn, int n);
    ui32 (*ram_use)(const FSCrypt* fs_crypt);

    ui32 page_sz; // FS read/write page size
    ui32 buf_pgs; // encryption buffer size in pages
    ui8* buf;     // encryption buffer
    void* crypt;  // encryption/decryption engine
    void* fs_vol; // parameter passed to FS driver functions

    // FS driver read/write
    int (*fs_wr)(const void* buf, ui32 frst, int n, void* fs_vol);
    int (*fs_rd)(void* buf, ui32 frst, int n, void* fs_vol);
};
#endif // FS_CRYPT

/***********************************************************************/
/* Variable Declarations                                               */
/***********************************************************************/
extern int CurrFixedVols;
extern FILE_TFS Files[FOPEN_MAX + 1];
extern HeadEntry MountedList;
extern FileSys TtySys;
extern int FsMaxOpen; // maximum # of concurrently open files
extern void* FSIterCurrVol;

// FS Volumes Lists
extern CircLink FatVols;
extern CircLink FfsVols;
extern CircLink XfsVols;
extern CircLink RfsVols;
extern CircLink ZfsVols;

/***********************************************************************/
/* Function Prototypes                                                 */
/***********************************************************************/
// File/directory path lookup related functions
int FsInvalName(const char* name, int ignore_last);
int FsIsLast(const char* name, ui32* lenp, ui32* incp);
const char* FsSkipSeparators(const char* path);
void* FSearch(void* hndl, const char** path, int dir_lookup, uint flags);
char* FSearchFSUID(ui32 fsuid, char* buf, size_t size, int lookup);
FileSys* FsResolvePath(const char* path);

// General system/root level functions
int IsFreeFCB(const FILE_TFS* file);
void FsInitFCB(FILE_TFS* file, ui32 type);
void FsCopyFCB(FILE_TFS* dst, const FILE_TFS* src);
int DirFileWrite(FILE_TFS* stream, const ui8* buf, ui32 len);
int DirFileRead(FILE_TFS* stream, ui8* buf, ui32 len);
void* RootIoctl(DIR_TFS* dir, int code, ...);
int FsError(int err_code);
int FsIoError(FileSys* file_sys);
int FsSetErrno(int flags);

// Auxiliary functions
void QuickSort(RFSEnt** head, RFSEnt** tail, DirEntry* e1, DirEntry* e2,
               int (*cmp)(const DirEntry*, const DirEntry*));
int FNameEqu(const char* s1, const char* s2);
int FNameEquN(const char* s1, const char* s2, size_t n);
void DNameCpy(char* dst, const char* src);
int DNameEqu(const char* s1, const char* s2);
int DNameEquCase(const char* s1, const char* s2);
size_t DNameLen(const char* s);
void FsAddMount(FileSys* volume);
void FsDelMount(FileSys* volume);
void* FtlNdmAddFatFTL(FtlNdmVol* ftl_dvr, FatVol* fat_dvr);
void* FtlNdmAddXfsFTL(FtlNdmVol* ftl_dvr, XfsVol* xfs_dvr);
void FtlnFreeFTL(void* ftl);
int FsConvOpenMode(const char* mode);
int FsCheckPerm(mode_t mode, uid_t uid, gid_t gid, uint permissions);
void FsSetFl(FILE_TFS* fcb, int oflag);
void* FsGetFl(const FILE_TFS* fcb);
int FsFormatResetWc(const char* name);
int FatGetSectSize(const FatVol* fat_vol);
ui32 UDiv64(ui64 dividend, ui32 divisor);
void FsMemPrn(void);
ui32 FsMemPeakRst(void);

#if FS_CRYPT
// FS Crypt Initialization
FSCrypt* FsCryptNew(FSCryptDrvr* fs_crypt);
int FsCryptSetBufSz(FSCrypt* fs_crypt, ui32 buf_pgs);
#endif

// FCB recorded file offsets interface
fpos_t_TFS FOffGet(const FILE_TFS* fcbp, ui32 sect_off, ui32 frst_sect);
void FOffSet(FILE_TFS* fcbp, const fpos_t_TFS* new_pos, ui32 f_sects);
void FOffUpdate(const FILE_TFS* fcbp, ui32 old_sect, ui32 new_sect);
void FOffDel(FILE_TFS* fcbp);

// Asynchronous I/O
void aioInit(void);
int aioOpen(FILE_TFS* file, int sect_size);
void aioSetIdle(FILE_TFS* file, uint code);
void aioVolInit(FileSys* vol);

// File System Memory Allocation Wrapper Functions
void* FsCalloc(size_t nmemb, size_t size);
void* FsMalloc(size_t size);
void* FsAalloc(size_t size);
void FsFreeClear(void* ptr_ptr);
void FsAfreeClear(void* ptr_ptr);
void FsFree(void* ptr);

#ifdef __cplusplus
}
#endif
