// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_free_ptr.h>
#include <fbl/vector.h>

#include <zircon/types.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

// clang-format off

namespace blobstore {

constexpr uint64_t kBlobstoreMagic0  = (0xac2153479e694d21ULL);
constexpr uint64_t kBlobstoreMagic1  = (0x985000d4d4d3d314ULL);
constexpr uint32_t kBlobstoreVersion = 0x00000004;

constexpr uint32_t kBlobstoreFlagClean      = 1;
constexpr uint32_t kBlobstoreFlagDirty      = 2;
constexpr uint32_t kBlobstoreFlagFVM        = 4;
constexpr uint32_t kBlobstoreBlockSize      = 8192;
constexpr uint32_t kBlobstoreBlockBits      = (kBlobstoreBlockSize * 8);
constexpr uint32_t kBlobstoreBlockMapStart  = 1;
constexpr uint32_t kBlobstoreInodeSize      = 64;
constexpr uint32_t kBlobstoreInodesPerBlock = (kBlobstoreBlockSize / kBlobstoreInodeSize);

constexpr size_t kFVMBlockMapStart  = 0x10000;
constexpr size_t kFVMNodeMapStart   = 0x20000;
constexpr size_t kFVMDataStart      = 0x30000;

// Notes:
// - block 0 is always allocated
// - inode 0 is never used, should be marked allocated but ignored

typedef struct {
    uint64_t magic0;
    uint64_t magic1;
    uint32_t version;
    uint32_t flags;
    uint32_t block_size;       // 8K typical
    uint64_t block_count;      // Number of data blocks in this area
    uint64_t inode_count;      // Number of blobs in this area
    uint64_t alloc_block_count; // Total number of allocated blocks
    uint64_t alloc_inode_count; // Total number of allocated blobs
    uint64_t blob_header_next; // Block containing next blobstore, or zero if this is the last one
    // The following flags are only valid with (flags & kBlobstoreFlagFVM):
    uint64_t slice_size;    // Underlying slice size
    uint64_t vslice_count;  // Number of underlying slices
    uint32_t abm_slices;    // Slices allocated to block bitmap
    uint32_t ino_slices;    // Slices allocated to node map
    uint32_t dat_slices;    // Slices allocated to file data section
} blobstore_info_t;

constexpr uint64_t BlockMapStartBlock(const blobstore_info_t& info) {
    if (info.flags & kBlobstoreFlagFVM) {
        return kFVMBlockMapStart;
    } else {
        return kBlobstoreBlockMapStart;
    }
}

constexpr uint64_t BlockMapBlocks(const blobstore_info_t& info) {
    return fbl::round_up(info.block_count, kBlobstoreBlockBits) / kBlobstoreBlockBits;
}

constexpr uint64_t NodeMapStartBlock(const blobstore_info_t& info) {
    // Node map immediately follows the block map
    if (info.flags & kBlobstoreFlagFVM) {
        return kFVMNodeMapStart;
    } else {
        return BlockMapStartBlock(info) + BlockMapBlocks(info);
    }
}

constexpr uint64_t NodeMapBlocks(const blobstore_info_t& info) {
    return fbl::round_up(info.inode_count, kBlobstoreInodesPerBlock) / kBlobstoreInodesPerBlock;
}

constexpr uint64_t DataStartBlock(const blobstore_info_t& info) {
    // Data immediately follows the node map
    if (info.flags & kBlobstoreFlagFVM) {
        return kFVMDataStart;
    } else {
        return NodeMapStartBlock(info) + NodeMapBlocks(info);
    }
}

constexpr uint64_t DataBlocks(const blobstore_info_t& info) {
    return info.block_count;
}

constexpr uint64_t TotalBlocks(const blobstore_info_t& info) {
    return BlockMapStartBlock(info) + BlockMapBlocks(info) + NodeMapBlocks(info) + DataBlocks(info);
}

// States of 'Blob' identified via start block.
constexpr uint64_t kStartBlockFree     = 0;
constexpr uint64_t kStartBlockReserved = 1;
constexpr uint64_t kStartBlockMinimum  = 2; // Smallest 'data' block possible

using digest::Digest;
typedef struct {
    uint8_t  merkle_root_hash[Digest::kLength];
    uint64_t start_block;
    uint64_t num_blocks;
    uint64_t blob_size;
    uint64_t reserved;
} blobstore_inode_t;

static_assert(sizeof(blobstore_inode_t) == kBlobstoreInodeSize,
              "Blobstore Inode size is wrong");
static_assert(kBlobstoreBlockSize % kBlobstoreInodeSize == 0,
              "Blobstore Inodes should fit cleanly within a blobstore block");

// Number of blocks reserved for the blob itself
constexpr uint64_t BlobDataBlocks(const blobstore_inode_t& blobNode) {
    return fbl::round_up(blobNode.blob_size, kBlobstoreBlockSize) / kBlobstoreBlockSize;
}

void* GetBlock(const RawBitmap& bitmap, uint32_t blkno);
void* GetBitBlock(const RawBitmap& bitmap, uint32_t* blkno_out, uint32_t bitno);

zx_status_t readblk(int fd, uint64_t bno, void* data);
zx_status_t writeblk(int fd, uint64_t bno, const void* data);
zx_status_t blobstore_check_info(const blobstore_info_t* info, uint64_t max);
zx_status_t blobstore_get_blockcount(int fd, uint64_t* out);
int blobstore_mkfs(int fd, uint64_t block_count);

#ifndef __Fuchsia__
typedef union {
    uint8_t block[kBlobstoreBlockSize];
    blobstore_info_t info;
} info_block_t;

// Stores pointer to an inode's metadata and the matching block number
class InodeBlock {
public:
    InodeBlock(size_t bno, blobstore_inode_t* inode, const Digest& digest)
              : bno_(bno) {
        inode_ = inode;
        digest.CopyTo(inode_->merkle_root_hash, sizeof(inode_->merkle_root_hash));
    }

    size_t GetBno() const {
        return bno_;
    }

    blobstore_inode_t* GetInode() {
        return inode_;
    }

    void SetSize(size_t size);

private:
    size_t bno_;
    blobstore_inode_t* inode_;
};

class Blobstore : public fbl::RefCounted<Blobstore> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Blobstore);

    // Creates an instance of Blobstore from the file at |blockfd|.
    // The blobstore partition is expected to start at |offset| bytes into the file.
    static zx_status_t Create(fbl::unique_fd blockfd, off_t offset, const info_block_t& info_block,
                              const fbl::Array<size_t>& extent_lengths,
                              fbl::RefPtr<Blobstore>* out);

    ~Blobstore() {}

    // Checks to see if a blob already exists, and if not allocates a new node
    zx_status_t NewBlob(const Digest& digest, fbl::unique_ptr<InodeBlock>* out);

    // Allocate |nblocks| starting at |*blkno_out| in memory
    zx_status_t AllocateBlocks(size_t nblocks, size_t* blkno_out);

    zx_status_t WriteData(blobstore_inode_t* inode, void* merkle_data, void* blob_data);
    zx_status_t WriteBitmap(size_t nblocks, size_t start_block);
    zx_status_t WriteNode(fbl::unique_ptr<InodeBlock> ino_block);
    zx_status_t WriteInfo();

private:
    typedef struct {
        size_t bno;
        uint8_t blk[kBlobstoreBlockSize];
    } block_cache_t;

    friend class BlobstoreChecker;

    Blobstore(fbl::unique_fd fd, off_t offset, const info_block_t& info_block,
              const fbl::Array<size_t>& extent_lengths);
    zx_status_t LoadBitmap();

    // Access the |index|th inode
    blobstore_inode_t* GetNode(size_t index);

    // Read data from block |bno| into the block cache.
    // If the block cache already contains data from the specified bno, nothing happens.
    // Cannot read while a dirty block is pending.
    zx_status_t ReadBlock(size_t bno);

    // Write |data| into block |bno|
    zx_status_t WriteBlock(size_t bno, const void* data);

    zx_status_t ResetCache();

    RawBitmap block_map_{};

    fbl::unique_fd blockfd_;
    bool dirty_;
    off_t offset_;

    size_t block_map_start_block_;
    size_t node_map_start_block_;
    size_t data_start_block_;

    size_t block_map_block_count_;
    size_t node_map_block_count_;
    size_t data_block_count_;

    union {
        blobstore_info_t info_;
        uint8_t info_block_[kBlobstoreBlockSize];
    };

    // Caches the most recent block read from disk
    block_cache_t cache_;
};

zx_status_t blobstore_create(fbl::RefPtr<Blobstore>* out, int blockfd);
zx_status_t blobstore_add_blob(Blobstore* bs, int data_fd);
zx_status_t blobstore_fsck(fbl::unique_fd fd, off_t start, off_t end,
                           const fbl::Vector<size_t>& extent_lengths);
#endif
} // namespace blobstore