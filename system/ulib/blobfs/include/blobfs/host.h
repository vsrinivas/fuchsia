// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes host-side functionality for accessing Blobfs.

#pragma once

#ifdef __Fuchsia__
#error Host-only Header
#endif

#include <sys/mman.h>
#include <sys/stat.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
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

// Merkle Tree information associated with a file.
struct MerkleInfo {
    // Merkle-Tree related information.
    digest::Digest digest;
    fbl::Array<uint8_t> merkle;

    // The path which generated this file, and a cached file length.
    fbl::String path;
    uint64_t length = 0;

    // Compressed blob data, if the blob is compressible.
    fbl::unique_ptr<uint8_t[]> compressed_data;
    uint64_t compressed_length = 0;
    bool compressed = false;

    uint64_t GetDataBlocks() const {
        uint64_t blob_size = compressed ? compressed_length : length;
        return fbl::round_up(blob_size, kBlobfsBlockSize) / kBlobfsBlockSize;
    }
};

// A mapping of a file. Does not own the file.
class FileMapping {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(FileMapping);

    FileMapping() : data_(nullptr), length_(0) {}

    ~FileMapping() {
        reset();
    }

    void reset() {
        if (data_ != nullptr) {
            munmap(data_, length_);
            data_ = nullptr;
        }
    }

    zx_status_t Map(int fd) {
        reset();

        struct stat s;
        if (fstat(fd, &s) < 0) {
            return ZX_ERR_BAD_STATE;
        }
        data_ = mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data_ == nullptr) {
            return ZX_ERR_BAD_STATE;
        }
        length_ = s.st_size;
        return ZX_OK;
    }

    void* data() const {
        return data_;
    }

    uint64_t length() const {
        return length_;
    }

private:
    void* data_;
    uint64_t length_;
};

typedef union {
    uint8_t block[kBlobfsBlockSize];
    Superblock info;
} info_block_t;

// Stores pointer to an inode's metadata and the matching block number
class InodeBlock {
public:
    InodeBlock(size_t bno, Inode* inode, const Digest& digest)
        : bno_(bno) {
        inode_ = inode;
        digest.CopyTo(inode_->merkle_root_hash, sizeof(inode_->merkle_root_hash));
    }

    size_t GetBno() const {
        return bno_;
    }

    Inode* GetInode() {
        return inode_;
    }

private:
    size_t bno_;
    Inode* inode_;
};

class Blobfs : public fbl::RefCounted<Blobfs> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);

    // Creates an instance of Blobfs from the file at |blockfd|.
    // The blobfs partition is expected to start at |offset| bytes into the file.
    static zx_status_t Create(fbl::unique_fd blockfd, off_t offset, const info_block_t& info_block,
                              const fbl::Array<size_t>& extent_lengths,
                              fbl::unique_ptr<Blobfs>* out);

    ~Blobfs() {}

    // Checks to see if a blob already exists, and if not allocates a new node
    zx_status_t NewBlob(const Digest& digest, fbl::unique_ptr<InodeBlock>* out);

    // Allocate |nblocks| starting at |*blkno_out| in memory
    zx_status_t AllocateBlocks(size_t nblocks, size_t* blkno_out);

    zx_status_t WriteData(Inode* inode, const void* merkle_data,
                          const void* blob_data);
    zx_status_t WriteBitmap(size_t nblocks, size_t start_block);
    zx_status_t WriteNode(fbl::unique_ptr<InodeBlock> ino_block);
    zx_status_t WriteInfo();

private:
    struct BlockCache {
        size_t bno;
        uint8_t blk[kBlobfsBlockSize];
    };

    friend class BlobfsChecker;

    Blobfs(fbl::unique_fd fd, off_t offset, const info_block_t& info_block,
           const fbl::Array<size_t>& extent_lengths);
    zx_status_t LoadBitmap();

    // Access the |index|th inode
    Inode* GetNode(size_t index);

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
    size_t journal_start_block_;
    size_t data_start_block_;

    size_t block_map_block_count_;
    size_t node_map_block_count_;
    size_t journal_block_count_;
    size_t data_block_count_;

    union {
        Superblock info_;
        uint8_t info_block_[kBlobfsBlockSize];
    };

    // Caches the most recent block read from disk
    BlockCache cache_;
};

zx_status_t blobfs_create(fbl::unique_ptr<Blobfs>* out, fbl::unique_fd blockfd);

// Pre-process a blob by creating a merkle tree and digest from the supplied file.
// Also return the length of the file. If |compress| is true and we decide to compress the file,
// the compressed length and data are returned.
zx_status_t blobfs_preprocess(int data_fd, bool compress, MerkleInfo* out_info);

// blobfs_add_blob may be called by multiple threads to gain concurrent
// merkle tree generation. No other methods are thread safe.
zx_status_t blobfs_add_blob(Blobfs* bs, int data_fd);

// Identical to blobfs_add_blob, but uses a precomputed Merkle Tree and digest.
zx_status_t blobfs_add_blob_with_merkle(Blobfs* bs, int data_fd, const MerkleInfo& info);

zx_status_t blobfs_fsck(fbl::unique_fd fd, off_t start, off_t end,
                        const fbl::Vector<size_t>& extent_lengths);

// Create a blobfs from a sparse file
// |start| indicates where the blobfs partition starts within the file (in bytes)
// |end| indicates the end of the blobfs partition (in bytes)
// |extent_lengths| contains the length (in bytes) of each blobfs extent: currently this includes
// the superblock, block bitmap, inode table, and data blocks.
zx_status_t blobfs_create_sparse(fbl::unique_ptr<Blobfs>* out, fbl::unique_fd fd, off_t start,
                                 off_t end, const fbl::Vector<size_t>& extent_lengths);
} // namespace blobfs
