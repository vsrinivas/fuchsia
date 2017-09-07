// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/type_support.h>
#include <fbl/unique_free_ptr.h>

#include <magenta/types.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "misc.h"

#ifdef __Fuchsia__
#include <block-client/client.h>
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

// clang-format off

namespace minfs {

// Type of a reference to block number, either absolute (able to index
// into disk directly) or relative to some entity (such as a file).
typedef uint32_t blk_t;

// The type of an inode number, which may be used as an
// index into the inode table.
typedef uint32_t ino_t;

constexpr uint64_t kMinfsMagic0         = (0x002153466e694d21ULL);
constexpr uint64_t kMinfsMagic1         = (0x385000d3d3d3d304ULL);
constexpr uint32_t kMinfsVersion        = 0x00000005;

constexpr ino_t kMinfsRootIno           = 1;
constexpr uint32_t kMinfsFlagClean      = 0x00000001; // Currently unused
constexpr uint32_t kMinfsFlagFVM        = 0x00000002; // Mounted on FVM
constexpr uint32_t kMinfsBlockSize      = 8192;
constexpr uint32_t kMinfsBlockBits      = (kMinfsBlockSize * 8);
constexpr uint32_t kMinfsInodeSize      = 256;
constexpr uint32_t kMinfsInodesPerBlock = (kMinfsBlockSize / kMinfsInodeSize);

constexpr uint32_t kMinfsDirect         = 16;
constexpr uint32_t kMinfsIndirect       = 31;
constexpr uint32_t kMinfsDoublyIndirect = 1;

constexpr uint32_t kMinfsDirectPerIndirect = (kMinfsBlockSize / sizeof(blk_t));
// not possible to have a block at or past this one
// due to the limitations of the inode and indirect blocks
constexpr uint64_t kMinfsMaxFileBlock = (kMinfsDirect + (kMinfsIndirect * kMinfsDirectPerIndirect)
                                        + (kMinfsDoublyIndirect * kMinfsDirectPerIndirect
                                        * kMinfsDirectPerIndirect));
constexpr uint64_t kMinfsMaxFileSize  = kMinfsMaxFileBlock * kMinfsBlockSize;

constexpr uint32_t kMinfsTypeFile = 8;
constexpr uint32_t kMinfsTypeDir  = 4;

constexpr uint32_t MinfsMagic(uint32_t T) { return 0xAA6f6e00 | T; }
constexpr uint32_t kMinfsMagicDir  = MinfsMagic(kMinfsTypeDir);
constexpr uint32_t kMinfsMagicFile = MinfsMagic(kMinfsTypeFile);
constexpr uint32_t MinfsMagicType(uint32_t n) { return n & 0xFF; }

constexpr size_t kFVMBlockInodeBmStart = 0x10000;
constexpr size_t kFVMBlockDataBmStart  = 0x20000;
constexpr size_t kFVMBlockInodeStart   = 0x30000;
constexpr size_t kFVMBlockDataStart    = 0x40000;

typedef struct {
    uint64_t magic0;
    uint64_t magic1;
    uint32_t version;
    uint32_t flags;
    uint32_t block_size;    // 8K typical
    uint32_t inode_size;    // 256
    uint32_t block_count;   // total number of data blocks
    uint32_t inode_count;   // total number of inodes
    uint32_t alloc_block_count; // total number of allocated data blocks
    uint32_t alloc_inode_count; // total number of allocated inodes
    blk_t ibm_block;     // first blockno of inode allocation bitmap
    blk_t abm_block;     // first blockno of block allocation bitmap
    blk_t ino_block;     // first blockno of inode table
    blk_t dat_block;     // first blockno available for file data
    // The following flags are only valid with (flags & kMinfsFlagFVM):
    uint64_t slice_size;    // Underlying slice size
    uint64_t vslice_count;  // Number of allocated underlying slices
    uint32_t ibm_slices;    // Slices allocated to inode bitmap
    uint32_t abm_slices;    // Slices allocated to block bitmap
    uint32_t ino_slices;    // Slices allocated to inode table
    uint32_t dat_slices;    // Slices allocated to file data section
} minfs_info_t;

// Notes:
// - the ibm, abm, ino, and dat regions must be in that order
//   and may not overlap
// - the abm has an entry for every block on the volume, including
//   the info block (0), the bitmaps, etc
// - data blocks referenced from direct and indirect block tables
//   in inodes are also relative to (0), but it is not legal for
//   a block number of less than dat_block (start of data blocks)
//   to be used
// - inode numbers refer to the inode in block:
//     ino_block + ino / kMinfsInodesPerBlock
//   at offset: ino % kMinfsInodesPerBlock
// - inode 0 is never used, should be marked allocated but ignored

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t block_count;
    uint32_t link_count;
    uint64_t create_time;
    uint64_t modify_time;
    uint32_t seq_num;               // bumped when modified
    uint32_t gen_num;               // bumped when deleted
    uint32_t dirent_count;          // for directories
    uint32_t rsvd[5];
    blk_t dnum[kMinfsDirect];    // direct blocks
    blk_t inum[kMinfsIndirect];  // indirect blocks
    blk_t dinum[kMinfsDoublyIndirect]; // doubly indirect blocks
} minfs_inode_t;

static_assert(sizeof(minfs_inode_t) == kMinfsInodeSize,
              "minfs inode size is wrong");

typedef struct {
    ino_t ino;                      // inode number
    uint32_t reclen;                // Low 28 bits: Length of record
                                    // High 4 bits: Flags
    uint8_t namelen;                // length of the filename
    uint8_t type;                   // kMinfsType*
    char name[];                    // name does not have trailing \0
} minfs_dirent_t;

constexpr uint32_t MINFS_DIRENT_SIZE = sizeof(minfs_dirent_t);

constexpr uint32_t DirentSize(uint8_t namelen) {
    return MINFS_DIRENT_SIZE + ((namelen + 3) & (~3));
}

constexpr uint8_t kMinfsMaxNameSize       = 255;
constexpr uint32_t kMinfsMaxDirentSize    = DirentSize(kMinfsMaxNameSize);
constexpr uint32_t kMinfsMaxDirectorySize = (((1 << 20) - 1) & (~3));

static_assert(kMinfsMaxNameSize >= NAME_MAX,
              "MinFS names must be large enough to hold NAME_MAX characters");

constexpr uint32_t kMinfsReclenMask = 0x0FFFFFFF;
constexpr uint32_t kMinfsReclenLast = 0x80000000;

constexpr uint32_t MinfsReclen(minfs_dirent_t* de, size_t off) {
    return (de->reclen & kMinfsReclenLast) ?
           kMinfsMaxDirectorySize - static_cast<uint32_t>(off) :
           de->reclen & kMinfsReclenMask;
}

static_assert(kMinfsMaxDirectorySize <= kMinfsReclenMask,
              "MinFS directory size must be smaller than reclen mask");

// Notes:
// - dirents with ino of 0 are free, and skipped over on lookup
// - reclen must be a multiple of 4
// - the last record in a directory has the "kMinfsReclenLast" flag set. The
//   actual size of this record can be computed from the offset at which this
//   record starts. If the MAX_DIR_SIZE is increased, this 'last' record will
//   also increase in size.


// blocksize   8K    16K    32K
// 16 dir =  128K   256K   512K
// 32 ind =  512M  1024M  2048M

//  1GB ->  128K blocks ->  16K bitmap (2K qword)
//  4GB ->  512K blocks ->  64K bitmap (8K qword)
// 32GB -> 4096K blocks -> 512K bitmap (64K qwords)

// Block Cache (bcache.c)
constexpr uint32_t kMinfsHashBits = (8);

class Bcache {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Bcache);
    friend class BlockNode;

    static mx_status_t Create(fbl::unique_ptr<Bcache>* out, int fd, uint32_t blockmax);

    // Raw block read functions.
    // These do not track blocks (or attempt to access the block cache)
    mx_status_t Readblk(blk_t bno, void* data);
    mx_status_t Writeblk(blk_t bno, const void* data);

    // Returns the maximum number of available blocks,
    // assuming the filesystem is non-resizable.
    uint32_t Maxblk() const { return blockmax_; };

#ifdef __Fuchsia__
    ssize_t GetDevicePath(char* out, size_t out_len);
    mx_status_t AttachVmo(mx_handle_t vmo, vmoid_t* out);
    mx_status_t Txn(block_fifo_request_t* requests, size_t count) {
        return block_fifo_txn(fifo_client_, requests, count);
    }
    txnid_t TxnId() const { return txnid_; }

    mx_status_t FVMQuery(fvm_info_t* info) {
        ssize_t r = ioctl_block_fvm_query(fd_, info);
        if (r < 0) {
            return static_cast<mx_status_t>(r);
        }
        return MX_OK;
    }

    mx_status_t FVMExtend(const extend_request_t* request) {
        ssize_t r = ioctl_block_fvm_extend(fd_, request);
        if (r < 0) {
            return static_cast<mx_status_t>(r);
        }
        return MX_OK;
    }

    mx_status_t FVMShrink(const extend_request_t* request) {
        ssize_t r = ioctl_block_fvm_shrink(fd_, request);
        if (r < 0) {
            return static_cast<mx_status_t>(r);
        }
        return MX_OK;
    }
#endif

    int Sync();

    ~Bcache();

private:
    Bcache(int fd, uint32_t blockmax);

#ifdef __Fuchsia__
    fifo_client_t* fifo_client_{}; // Fast path to interact with block device
    txnid_t txnid_{}; // TODO(smklein): One per thread
#endif
    int fd_ = -1;
    uint32_t blockmax_{};
};

} // namespace minfs
