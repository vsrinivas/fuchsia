// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes host-side functionality for accessing Blobfs.

#pragma once

#ifdef __Fuchsia__
#error Host-only Header
#endif

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_free_ptr.h>
#include <fbl/vector.h>
#include <zircon/types.h>

#include <assert.h>
#include <limits.h>
#include <mutex>
#include <stdbool.h>
#include <stdint.h>

#include <blobfs/common.h>
#include <blobfs/format.h>

namespace blobfs {

typedef union {
    uint8_t block[kBlobfsBlockSize];
    blobfs_info_t info;
} info_block_t;

// Stores pointer to an inode's metadata and the matching block number
class InodeBlock {
public:
    InodeBlock(size_t bno, blobfs_inode_t* inode, const Digest& digest)
        : bno_(bno) {
        inode_ = inode;
        digest.CopyTo(inode_->merkle_root_hash, sizeof(inode_->merkle_root_hash));
    }

    size_t GetBno() const {
        return bno_;
    }

    blobfs_inode_t* GetInode() {
        return inode_;
    }

    void SetSize(size_t size);

private:
    size_t bno_;
    blobfs_inode_t* inode_;
};

class Blobfs : public fbl::RefCounted<Blobfs> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);

    // Creates an instance of Blobfs from the file at |blockfd|.
    // The blobfs partition is expected to start at |offset| bytes into the file.
    static zx_status_t Create(fbl::unique_fd blockfd, off_t offset, const info_block_t& info_block,
                              const fbl::Array<size_t>& extent_lengths,
                              fbl::RefPtr<Blobfs>* out);

    ~Blobfs() {}

    // Checks to see if a blob already exists, and if not allocates a new node
    zx_status_t NewBlob(const Digest& digest, fbl::unique_ptr<InodeBlock>* out);

    // Allocate |nblocks| starting at |*blkno_out| in memory
    zx_status_t AllocateBlocks(size_t nblocks, size_t* blkno_out);

    zx_status_t WriteData(blobfs_inode_t* inode, void* merkle_data, void* blob_data);
    zx_status_t WriteBitmap(size_t nblocks, size_t start_block);
    zx_status_t WriteNode(fbl::unique_ptr<InodeBlock> ino_block);
    zx_status_t WriteInfo();

private:
    typedef struct {
        size_t bno;
        uint8_t blk[kBlobfsBlockSize];
    } block_cache_t;

    friend class BlobfsChecker;

    Blobfs(fbl::unique_fd fd, off_t offset, const info_block_t& info_block,
           const fbl::Array<size_t>& extent_lengths);
    zx_status_t LoadBitmap();

    // Access the |index|th inode
    blobfs_inode_t* GetNode(size_t index);

    // Read data from block |bno| into the block cache.
    // If the block cache already contains data from the specified bno, nothing happens.
    // Cannot read while a dirty block is pending.
    zx_status_t ReadBlock(size_t bno);

    // Write |data| into block |bno|
    zx_status_t WriteBlock(size_t bno, const void* data);

    zx_status_t ResetCache();

    zx_status_t VerifyBlob(size_t node_index);

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
        blobfs_info_t info_;
        uint8_t info_block_[kBlobfsBlockSize];
    };

    // Caches the most recent block read from disk
    block_cache_t cache_;
};

zx_status_t blobfs_create(fbl::RefPtr<Blobfs>* out, fbl::unique_fd blockfd);

// blobfs_add_blob may be called by multiple threads to gain concurrent
// merkle tree generation. No other methods are thread safe.
zx_status_t blobfs_add_blob(Blobfs* bs, int data_fd);
zx_status_t blobfs_fsck(fbl::unique_fd fd, off_t start, off_t end,
                        const fbl::Vector<size_t>& extent_lengths);

// Create a blobfs from a sparse file
// |start| indicates where the blobfs partition starts within the file (in bytes)
// |end| indicates the end of the blobfs partition (in bytes)
// |extent_lengths| contains the length (in bytes) of each blobfs extent: currently this includes
// the superblock, block bitmap, inode table, and data blocks.
zx_status_t blobfs_create_sparse(fbl::RefPtr<Blobfs>* out, fbl::unique_fd fd, off_t start,
                                 off_t end, const fbl::Vector<size_t>& extent_lengths);
} // namespace blobfs
