// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fs/block-txn.h>
#include <fbl/auto_call.h>
#include <fbl/new.h>
#include <fbl/unique_ptr.h>
#include <fdio/debug.h>

#define MXDEBUG 0

#include "blobstore.h"
#include "blobstore-private.h"

using digest::Digest;
using digest::MerkleTree;

namespace blobstore {
namespace {

const void* readblk_cached(int fd, uint64_t bno) {
    static uint64_t last_bno = 0;
    static char block[kBlobstoreBlockSize];
    if ((last_bno != bno) && (readblk(fd, bno, &block) != ZX_OK)) {
        return nullptr;
    }
    return &block[0];
}

int load_info(int fd, void* info_block) {
    uint64_t block_count;
    blobstore_info_t* info = &reinterpret_cast<blobstore_info_t*>(info_block)[0];
    if (readblk(fd, 0, info_block) < 0) {
        return -1;
    } else if (blobstore_get_blockcount(fd, &block_count) != ZX_OK) {
        return -1;
    } else if (blobstore_check_info(info, block_count) != ZX_OK) {
        return -1;
    }
    return 0;
}

int load_block_bitmap(int fd, const blobstore_info_t* info, RawBitmap* block_map) {
    if (block_map->Reset(BlockMapBlocks(*info) * kBlobstoreBlockBits) != ZX_OK) {
        return -1;
    } else if (block_map->Shrink(info->block_count) != ZX_OK) {
        return -1;
    }
    const void* bmstart = block_map->StorageUnsafe()->GetData();
    for (size_t n = 0; n < BlockMapBlocks(*info); n++) {
        void* bmdata = fs::GetBlock<kBlobstoreBlockSize>(bmstart, n);
        if (readblk(fd, BlockMapStartBlock(*info) + n, bmdata) < 0) {
            return -1;
        }
    }
    return 0;
}

int add_blob_commit(int fd, const blobstore_inode_t* inode, size_t ino_bno,
                    const void* ino_block, const void* merkle_tree, const void*
                    blob_data, const RawBitmap& block_map, void* info_block) {
    blobstore_info_t* info = &reinterpret_cast<blobstore_info_t*>(info_block)[0];
    // Write back merkle tree and data
    for (size_t n = 0; n < MerkleTreeBlocks(*inode); n++) {
        const void* data = fs::GetBlock<kBlobstoreBlockSize>(merkle_tree, n);
        uint64_t bno = DataStartBlock(*info) + inode->start_block + n;
        if (writeblk(fd, bno, data) < 0) {
            return -1;
        }
    }
    for (size_t n = 0; n < BlobDataBlocks(*inode); n++) {
        const void* data = fs::GetBlock<kBlobstoreBlockSize>(blob_data, n);
        uint64_t bno = DataStartBlock(*info) + inode->start_block + MerkleTreeBlocks(*inode) + n;
        if (writeblk(fd, bno, data) < 0) {
            return -1;
        }
    }

    // Write back inode and block bitmap
    if (writeblk(fd, ino_bno, ino_block) < 0) {
        return -1;
    }

    uint64_t bbm_start_block = inode->start_block / kBlobstoreBlockBits;
    uint64_t bbm_end_block = fbl::round_up(inode->start_block + inode->num_blocks,
                                           kBlobstoreBlockBits) / kBlobstoreBlockBits;
    const void* bmstart = block_map.StorageUnsafe()->GetData();
    for (size_t n = bbm_start_block; n < bbm_end_block; n++) {
        const void* data = fs::GetBlock<kBlobstoreBlockSize>(bmstart, n);
        uint64_t bno = BlockMapStartBlock(*info) + n;
        if (writeblk(fd, bno, data) < 0) {
            return -1;
        }
    }

    // Update global info
    info->alloc_block_count += inode->num_blocks;
    info->alloc_inode_count++;
    if (writeblk(fd, 0, info_block) < 0) {
        return -1;
    }
    return 0;
}

}  // namespace anonymous

int blobstore_add_blob(int fd, int data_fd) {

    // Mmap user-provided file, create the corresponding merkle tree
    struct stat s;
    if (fstat(data_fd, &s)) {
        return -1;
    }
    void* blob_data = mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, data_fd, 0);
    if (blob_data == nullptr) {
        return -1;
    }

    auto auto_unmap = fbl::MakeAutoCall([blob_data, s]() {
        munmap(blob_data, s.st_size);
    });

    size_t merkle_size = MerkleTree::GetTreeLength(s.st_size);
    digest::Digest digest;
    fbl::AllocChecker ac;
    auto merkle_tree = fbl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[merkle_size]);
    if (!ac.check()) {
        return -1;
    } else if (MerkleTree::Create(blob_data, s.st_size, merkle_tree.get(),
                                  merkle_size, &digest) != ZX_OK) {
        return -1;
    }

    char info_block[kBlobstoreBlockSize];
    blobstore_info_t* info = reinterpret_cast<blobstore_info_t*>(&info_block[0]);
    if (load_info(fd, info_block)) {
        return -1;
    }
    RawBitmap block_map;
    if (load_block_bitmap(fd, info, &block_map)) {
        return -1;
    }

    // Ensure the digest doesn't already exist.
    // Also, find a free spot in which the new node can reside.
    const size_t kInodesPerBlock = kBlobstoreBlockSize / sizeof(blobstore_inode_t);
    blobstore_inode_t ino_block[kInodesPerBlock];
    size_t ino_bno;
    blobstore_inode_t* inode = nullptr;
    for (size_t i = 0; i < info->inode_count; ++i) {
        uint64_t bno = (i / kInodesPerBlock) + NodeMapStartBlock(*info);
        const void* blk = readblk_cached(fd, bno);
        if (blk == nullptr) {
            return -1;
        }
        auto iblk = reinterpret_cast<const blobstore_inode_t*>(blk);
        auto observed_inode = &iblk[i % kInodesPerBlock];
        if (observed_inode->start_block >= kStartBlockMinimum) {
            if (digest == observed_inode->merkle_root_hash) {
                fprintf(stderr, "error: Blob already exists in blobstore\n");
                return -1;
            }
        } else if (inode == nullptr) {
            memcpy(ino_block, iblk, kBlobstoreBlockSize);
            inode = &ino_block[i % kInodesPerBlock];
            digest.CopyTo(inode->merkle_root_hash, sizeof(inode->merkle_root_hash));
            inode->blob_size = s.st_size;
            inode->num_blocks = MerkleTreeBlocks(*inode) + BlobDataBlocks(*inode);
            ino_bno = bno;
        }
    }

    // Find blocks and an inode for the blob
    if (inode == nullptr) {
        fprintf(stderr, "error: No nodes available on blobstore image\n");
        return -1;
    }
    size_t blkno;
    if (block_map.Find(false, 0, block_map.size(), inode->num_blocks,
                              &blkno) != ZX_OK) {
        fprintf(stderr, "error: Not enough contiguous space for blob\n");
        return -1;
    }
    if (block_map.Set(blkno, blkno + inode->num_blocks) != ZX_OK) {
        return -1;
    }
    inode->start_block = blkno;
    if (add_blob_commit(fd, inode, ino_bno, ino_block, merkle_tree.get(),
                        blob_data, block_map, info_block)) {
        fprintf(stderr, "error: Could not commit blob update to disk\n");
        return -1;
    }

    return 0;
}

} // namespace blobstore

// This is used by the ioctl wrappers in magenta/device/device.h. It's not
// called by host tools, so just satisfy the linker with a stub.
ssize_t fdio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    return -1;
}
