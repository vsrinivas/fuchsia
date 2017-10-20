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
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/new.h>
#include <fbl/unique_ptr.h>
#include <fdio/debug.h>

#define MXDEBUG 0

#include <blobstore/blobstore.h>
#include "blobstore-private.h"

using digest::Digest;
using digest::MerkleTree;

namespace blobstore {

int blobstore_create(fbl::RefPtr<Blobstore>* out, int fd) {
    info_block_t info_block;

    if (readblk(fd, 0, (void*)info_block.block) < 0) {
        return -1;
    }
    uint64_t blocks;
    if (blobstore_get_blockcount(fd, &blocks) < 0) {
        fprintf(stderr, "blobstore: cannot find end of underlying device\n");
        return -1;
    } else if (blobstore_check_info(&info_block.info, blocks) < 0) {
        fprintf(stderr, "blobstore: Info check failed\n");
        return -1;
    } else if (Blobstore::Create(fbl::unique_fd(fd), info_block, out) < 0) {
        fprintf(stderr, "blobstore: mount failed; could not create blobstore\n");
        return -1;
    }

    return 0;
}

int blobstore_add_blob(Blobstore* bs, int data_fd) {
    // Mmap user-provided file, create the corresponding merkle tree
    struct stat s;
    if (fstat(data_fd, &s) < 0) {
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

    fbl::unique_ptr<InodeBlock> inode_block;
    if (bs->NewBlob(digest, &inode_block) < 0) {
        return -1;
    }
    if (inode_block == nullptr) {
        fprintf(stderr, "error: No nodes available on blobstore image\n");
        return -1;
    }

    inode_block->SetSize(s.st_size);
    blobstore_inode_t* inode = inode_block->GetInode();

    if (bs->AllocateBlocks(inode->num_blocks, reinterpret_cast<size_t*>(&inode->start_block)) < 0) {
        fprintf(stderr, "error: No blocks available\n");
        return -1;
    } else if (bs->WriteData(inode, merkle_tree.get(), blob_data) < 0) {
        return -1;
    } else if (bs->WriteBitmap(inode->num_blocks, inode->start_block) < 0) {
        return -1;
    } else if (bs->WriteNode(fbl::move(inode_block)) < 0) {
        return -1;
    } else if (bs->WriteInfo() < 0) {
        return -1;
    }

    return 0;
}

void InodeBlock::SetSize(size_t size) {
    inode_->blob_size = size;
    inode_->num_blocks = MerkleTreeBlocks(*inode_) + BlobDataBlocks(*inode_);
}

Blobstore::Blobstore(fbl::unique_fd fd, const info_block_t& info_block) : blockfd_(fbl::move(fd)),
                                                                          dirty_(false) {
    memcpy(&info_block_, info_block.block, kBlobstoreBlockSize);
    cache_.bno = 0;
}

zx_status_t Blobstore::Create(fbl::unique_fd blockfd_, const info_block_t& info_block,
                              fbl::RefPtr<Blobstore>* out) {
    zx_status_t status = blobstore_check_info(&info_block.info, TotalBlocks(info_block.info));
    if (status < 0) {
        fprintf(stderr, "blobstore: Check info failure\n");
        return status;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<Blobstore> fs = fbl::AdoptRef(new (&ac) Blobstore(fbl::move(blockfd_), info_block));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = fs->LoadBitmap()) < 0) {
        fprintf(stderr, "blobstore: Failed to load bitmaps\n");
        return status;
    }

    *out = fs;
    return ZX_OK;
}

zx_status_t Blobstore::LoadBitmap() {
    zx_status_t status;
    if ((status = block_map_.Reset(BlockMapBlocks(info_) * kBlobstoreBlockBits)) != ZX_OK) {
        return status;
    } else if ((status = block_map_.Shrink(info_.block_count)) != ZX_OK) {
        return status;
    }
    const void* bmstart = block_map_.StorageUnsafe()->GetData();
    for (size_t n = 0; n < BlockMapBlocks(info_); n++) {
        void* bmdata = fs::GetBlock<kBlobstoreBlockSize>(bmstart, n);
        if (ReadBlock(BlockMapStartBlock(info_) + n) < 0) {
            return ZX_ERR_IO;
        }
        memcpy(bmdata, cache_.blk, kBlobstoreBlockSize);
    }
    return ZX_OK;
}

zx_status_t Blobstore::NewBlob(const Digest& digest, fbl::unique_ptr<InodeBlock>* out) {
    size_t ino = info_.inode_count;

    for (size_t i = 0; i < info_.inode_count; ++i) {
        size_t bno = (i / kBlobstoreInodesPerBlock) + NodeMapStartBlock(info_);

        zx_status_t status;
        if ((status = ReadBlock(bno)) != ZX_OK) {
            return status;
        }

        auto iblk = reinterpret_cast<const blobstore_inode_t*>(cache_.blk);
        auto observed_inode = &iblk[i % kBlobstoreInodesPerBlock];
        if (observed_inode->start_block >= kStartBlockMinimum) {
            if (digest == observed_inode->merkle_root_hash) {
                return ZX_ERR_ALREADY_EXISTS;
            }
        } else if (ino >= info_.inode_count) {
            // If |ino| has not already been set to a valid value, set it to the
            // first free value we find.
            // We still check all the remaining inodes to avoid adding a duplicate blob.
            ino = i;
        }
    }

    if (ino >= info_.inode_count) {
        return ZX_ERR_NO_RESOURCES;
    }

    size_t bno = (ino / kBlobstoreInodesPerBlock) + NodeMapStartBlock(info_);
    zx_status_t status;
    if ((status = ReadBlock(bno)) != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    blobstore_inode_t* inodes = reinterpret_cast<blobstore_inode_t*>(cache_.blk);

    fbl::unique_ptr<InodeBlock> ino_block(
        new (&ac) InodeBlock(bno, &inodes[ino % kBlobstoreInodesPerBlock], digest));

    if (!ac.check()) {
        return ZX_ERR_INTERNAL;
    }

    dirty_ = true;
    info_.alloc_inode_count++;
    *out = fbl::move(ino_block);
    return ZX_OK;
}

zx_status_t Blobstore::AllocateBlocks(size_t nblocks, size_t* blkno_out) {
    zx_status_t status;
    if ((status = block_map_.Find(false, 0, block_map_.size(), nblocks, blkno_out)) != ZX_OK) {
        return status;
    } else if ((status = block_map_.Set(*blkno_out, *blkno_out + nblocks)) != ZX_OK) {
        return status;
    }

    info_.alloc_block_count += nblocks;
    return 0;
}

zx_status_t Blobstore::WriteBitmap(size_t nblocks, size_t start_block) {
    uint64_t bbm_start_block = start_block / kBlobstoreBlockBits;
    uint64_t bbm_end_block = fbl::round_up(start_block + nblocks, kBlobstoreBlockBits)
                             / kBlobstoreBlockBits;
    const void* bmstart = block_map_.StorageUnsafe()->GetData();
    for (size_t n = bbm_start_block; n < bbm_end_block; n++) {
        const void* data = fs::GetBlock<kBlobstoreBlockSize>(bmstart, n);
        uint64_t bno = BlockMapStartBlock(info_) + n;
        zx_status_t status;
        if ((status = WriteBlock(bno, data)) != ZX_OK) {
            return status;
        }
    }

    return 0;
}

zx_status_t Blobstore::WriteNode(fbl::unique_ptr<InodeBlock> ino_block) {
    if (ino_block->GetBno() != cache_.bno) {
        return ZX_ERR_ACCESS_DENIED;
    }

    dirty_ = false;
    return WriteBlock(cache_.bno, cache_.blk);
}

zx_status_t Blobstore::WriteData(blobstore_inode_t* inode, void* merkle_data, void* blob_data) {
    for (size_t n = 0; n < MerkleTreeBlocks(*inode); n++) {
        const void* data = fs::GetBlock<kBlobstoreBlockSize>(merkle_data, n);
        uint64_t bno = DataStartBlock(info_) + inode->start_block + n;
        zx_status_t status;
        if ((status = WriteBlock(bno, data)) != ZX_OK) {
            return status;
        }
    }

    for (size_t n = 0; n < BlobDataBlocks(*inode); n++) {
        const void* data = fs::GetBlock<kBlobstoreBlockSize>(blob_data, n);

        // If we try to write a block, will it be reaching beyond the end of the
        // mapped file?
        size_t off = n * kBlobstoreBlockSize;
        uint8_t last_data[kBlobstoreBlockSize];
        if (inode->blob_size < off + kBlobstoreBlockSize) {
            // Read the partial block from a block-sized buffer which zero-pads the data.
            memset(last_data, 0, kBlobstoreBlockSize);
            memcpy(last_data, data, inode->blob_size - off);
            data = last_data;
        }

        uint64_t bno = DataStartBlock(info_) + inode->start_block + MerkleTreeBlocks(*inode) + n;
        zx_status_t status;
        if ((status = WriteBlock(bno, data)) != ZX_OK) {
            return status;
        }
    }

    return 0;
}

zx_status_t Blobstore::WriteInfo() {
    return WriteBlock(0, info_block_);
}

zx_status_t Blobstore::ReadBlock(size_t bno) {
    if (dirty_) {
        return ZX_ERR_ACCESS_DENIED;
    }

    zx_status_t status;
    if ((cache_.bno != bno) && ((status = readblk(blockfd_.get(), bno, &cache_.blk)) != ZX_OK)) {
        return status;
    }

    cache_.bno = bno;
    return 0;
}

zx_status_t Blobstore::WriteBlock(size_t bno, const void* data) {
    return writeblk(blockfd_.get(), bno, data);
}

blobstore_inode_t* Blobstore::GetNode(size_t index)  {
    size_t bno = NodeMapStartBlock(info_) + index / kBlobstoreInodesPerBlock;

    if (bno < DataStartBlock(info_)) {
        if (ReadBlock(bno) < 0) {
            return nullptr;
        }
    } else {
        memset(cache_.blk, 0, kBlobstoreBlockSize);
    }

    auto iblock = reinterpret_cast<blobstore_inode_t*>(cache_.blk);
    return &iblock[index % kBlobstoreInodesPerBlock];
}
} // namespace blobstore

// This is used by the ioctl wrappers in magenta/device/device.h. It's not
// called by host tools, so just satisfy the linker with a stub.
ssize_t fdio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf,
                   size_t out_len) {
    return -1;
}
