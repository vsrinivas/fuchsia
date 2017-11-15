// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes host-side functionality for accessing Blobstore.

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
#include <stdbool.h>
#include <stdint.h>

#include <blobstore/format.h>
#include <blobstore/common.h>

namespace blobstore {

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

// Create a blobstore from a sparse file
// |start| indicates where the blobstore partition starts within the file (in bytes)
// |end| indicates the end of the blobstore partition (in bytes)
// |extent_lengths| contains the length (in bytes) of each blobstore extent: currently this includes
// the superblock, block bitmap, inode table, and data blocks.
zx_status_t blobstore_create_sparse(fbl::RefPtr<Blobstore>* out, int fd, off_t start, off_t end,
                                    const fbl::Vector<size_t>& extent_lengths);

} // namespace blobstore
