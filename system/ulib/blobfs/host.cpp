// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/new.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/debug.h>
#include <fs/block-txn.h>

#define ZXDEBUG 0

#include <blobfs/format.h>
#include <blobfs/fsck.h>
#include <blobfs/host.h>
#include <blobfs/lz4.h>

using digest::Digest;
using digest::MerkleTree;

namespace blobfs {

#define EXTENT_COUNT 4

zx_status_t readblk_offset(int fd, uint64_t bno, off_t offset, void* data) {
    off_t off = offset + bno * kBlobfsBlockSize;
    if (lseek(fd, off, SEEK_SET) < 0) {
        fprintf(stderr, "blobfs: cannot seek to block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    if (read(fd, data, kBlobfsBlockSize) != kBlobfsBlockSize) {
        fprintf(stderr, "blobfs: cannot read block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t writeblk_offset(int fd, uint64_t bno, off_t offset, const void* data) {
    off_t off = offset + bno * kBlobfsBlockSize;
    if (lseek(fd, off, SEEK_SET) < 0) {
        fprintf(stderr, "blobfs: cannot seek to block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    if (write(fd, data, kBlobfsBlockSize) != kBlobfsBlockSize) {
        fprintf(stderr, "blobfs: cannot write block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t blobfs_create(fbl::unique_ptr<Blobfs>* out, fbl::unique_fd fd) {
    info_block_t info_block;

    if (readblk(fd.get(), 0, (void*)info_block.block) < 0) {
        return ZX_ERR_IO;
    }
    uint64_t blocks;
    zx_status_t status;
    if ((status = blobfs_get_blockcount(fd.get(), &blocks)) != ZX_OK) {
        fprintf(stderr, "blobfs: cannot find end of underlying device\n");
        return status;
    } else if ((status = blobfs_check_info(&info_block.info, blocks)) != ZX_OK) {
        fprintf(stderr, "blobfs: Info check failed\n");
        return status;
    }

    fbl::AllocChecker ac;
    fbl::Array<size_t> extent_lengths(new (&ac) size_t[EXTENT_COUNT], EXTENT_COUNT);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    extent_lengths[0] = BlockMapStartBlock(info_block.info) * kBlobfsBlockSize;
    extent_lengths[1] = BlockMapBlocks(info_block.info) * kBlobfsBlockSize;
    extent_lengths[2] = NodeMapBlocks(info_block.info) * kBlobfsBlockSize;
    extent_lengths[3] = DataBlocks(info_block.info) * kBlobfsBlockSize;

    if ((status = Blobfs::Create(fbl::move(fd), 0, info_block, extent_lengths, out)) != ZX_OK) {
        fprintf(stderr, "blobfs: mount failed; could not create blobfs\n");
        return status;
    }

    return ZX_OK;
}

zx_status_t blobfs_create_sparse(fbl::unique_ptr<Blobfs>* out, fbl::unique_fd fd, off_t start,
                                 off_t end, const fbl::Vector<size_t>& extent_vector) {
    if (start >= end) {
        fprintf(stderr, "blobfs: Insufficient space allocated\n");
        return ZX_ERR_INVALID_ARGS;
    }
    if (extent_vector.size() != EXTENT_COUNT) {
        fprintf(stderr, "blobfs: Incorrect number of extents\n");
        return ZX_ERR_INVALID_ARGS;
    }

    info_block_t info_block;

    struct stat s;
    if (fstat(fd.get(), &s) < 0) {
        return ZX_ERR_BAD_STATE;
    }

    if (s.st_size < end) {
        fprintf(stderr, "blobfs: Invalid file size\n");
        return ZX_ERR_BAD_STATE;
    } else if (readblk_offset(fd.get(), 0, start, (void*)info_block.block) < 0) {
        return ZX_ERR_IO;
    }

    zx_status_t status;
    if ((status = blobfs_check_info(&info_block.info, (end - start) / kBlobfsBlockSize)) != ZX_OK) {
        fprintf(stderr, "blobfs: Info check failed\n");
        return status;
    }

    fbl::AllocChecker ac;
    fbl::Array<size_t> extent_lengths(new (&ac) size_t[EXTENT_COUNT], EXTENT_COUNT);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    extent_lengths[0] = extent_vector[0];
    extent_lengths[1] = extent_vector[1];
    extent_lengths[2] = extent_vector[2];
    extent_lengths[3] = extent_vector[3];

    if ((status = Blobfs::Create(fbl::move(fd), start, info_block, extent_lengths, out)) != ZX_OK) {
        fprintf(stderr, "blobfs: mount failed; could not create blobfs\n");
        return status;
    }

    return ZX_OK;
}

std::mutex add_blob_mutex_;

zx_status_t blobfs_add_blob(Blobfs* bs, int data_fd) {
    // Mmap user-provided file, create the corresponding merkle tree
    struct stat s;
    if (fstat(data_fd, &s) < 0) {
        return ZX_ERR_BAD_STATE;
    }
    void* blob_data = mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, data_fd, 0);
    if (blob_data == nullptr) {
        return ZX_ERR_BAD_STATE;
    }

    auto auto_unmap = fbl::MakeAutoCall([blob_data, s]() {
        munmap(blob_data, s.st_size);
    });

    size_t data_blocks = fbl::round_up((size_t) s.st_size, kBlobfsBlockSize) / kBlobfsBlockSize;

    Compressor compressor;
    size_t max = compressor.BufferMax(s.st_size);
    auto compressed_data = fbl::unique_ptr<uint8_t[]>(new uint8_t[max]);
    bool compressed = false;
    if ((s.st_size >= kCompressionMinBytesSaved) &&
        (compressor.Initialize(compressed_data.get(), max) == ZX_OK) &&
        (compressor.Update(blob_data, s.st_size) == ZX_OK) &&
        (compressor.End() == ZX_OK) &&
        (s.st_size - kCompressionMinBytesSaved >= compressor.Size())) {
        compressed = true;
        data_blocks = fbl::round_up(compressor.Size(), kBlobfsBlockSize) / kBlobfsBlockSize;
    }

    const void* data = compressed ? compressed_data.get() : blob_data;

    zx_status_t status;
    digest::Digest digest;
    fbl::AllocChecker ac;
    size_t merkle_size = MerkleTree::GetTreeLength(s.st_size);
    auto merkle_tree = fbl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[merkle_size]);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    } else if ((status = MerkleTree::Create(blob_data, s.st_size, merkle_tree.get(),
                                            merkle_size, &digest)) != ZX_OK) {
        return status;
    }

    std::lock_guard<std::mutex> lock(add_blob_mutex_);
    fbl::unique_ptr<InodeBlock> inode_block;
    if ((status = bs->NewBlob(digest, &inode_block)) < 0) {
        return status;
    }
    if (inode_block == nullptr) {
        fprintf(stderr, "error: No nodes available on blobfs image\n");
        return ZX_ERR_NO_RESOURCES;
    }

    blobfs_inode_t* inode = inode_block->GetInode();
    inode->blob_size = s.st_size;
    inode->num_blocks = MerkleTreeBlocks(*inode) + data_blocks;
    inode->flags |= (compressed ? kBlobFlagLZ4Compressed : 0);

    if ((status = bs->AllocateBlocks(inode->num_blocks,
                                     reinterpret_cast<size_t*>(&inode->start_block))) != ZX_OK) {
        fprintf(stderr, "error: No blocks available\n");
        return status;
    } else if ((status = bs->WriteData(inode, merkle_tree.get(), data)) != ZX_OK) {
        return status;
    } else if ((status = bs->WriteBitmap(inode->num_blocks, inode->start_block)) != ZX_OK) {
        return status;
    } else if ((status = bs->WriteNode(fbl::move(inode_block))) != ZX_OK) {
        return status;
    } else if ((status = bs->WriteInfo()) != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

zx_status_t blobfs_fsck(fbl::unique_fd fd, off_t start, off_t end,
                        const fbl::Vector<size_t>& extent_lengths) {
    fbl::unique_ptr<Blobfs> blob;
    zx_status_t status;
    if ((status = blobfs_create_sparse(&blob, fbl::move(fd), start, end, extent_lengths)) != ZX_OK) {
        return status;
    } else if ((status = blobfs_check(fbl::move(blob))) != ZX_OK) {
        return status;
    }
    return ZX_OK;
}

Blobfs::Blobfs(fbl::unique_fd fd, off_t offset, const info_block_t& info_block,
               const fbl::Array<size_t>& extent_lengths)
    : blockfd_(fbl::move(fd)),
      dirty_(false), offset_(offset) {
    ZX_ASSERT(extent_lengths.size() == EXTENT_COUNT);
    memcpy(&info_block_, info_block.block, kBlobfsBlockSize);
    cache_.bno = 0;

    block_map_start_block_ = extent_lengths[0] / kBlobfsBlockSize;
    block_map_block_count_ = extent_lengths[1] / kBlobfsBlockSize;
    node_map_start_block_ = block_map_start_block_ + block_map_block_count_;
    node_map_block_count_ = extent_lengths[2] / kBlobfsBlockSize;
    data_start_block_ = node_map_start_block_ + node_map_block_count_;
    data_block_count_ = extent_lengths[3] / kBlobfsBlockSize;
}

zx_status_t Blobfs::Create(fbl::unique_fd blockfd_, off_t offset, const info_block_t& info_block,
                           const fbl::Array<size_t>& extent_lengths,
                           fbl::unique_ptr<Blobfs>* out) {
    zx_status_t status = blobfs_check_info(&info_block.info, TotalBlocks(info_block.info));
    if (status < 0) {
        fprintf(stderr, "blobfs: Check info failure\n");
        return status;
    }

    ZX_ASSERT(extent_lengths.size() == EXTENT_COUNT);

    for (unsigned i = 0; i < 3; i++) {
        if (extent_lengths[i] % kBlobfsBlockSize) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    auto fs = fbl::unique_ptr<Blobfs>(new Blobfs(fbl::move(blockfd_), offset,
                                                 info_block, extent_lengths));

    if ((status = fs->LoadBitmap()) < 0) {
        fprintf(stderr, "blobfs: Failed to load bitmaps\n");
        return status;
    }

    *out = fbl::move(fs);
    return ZX_OK;
}

zx_status_t Blobfs::LoadBitmap() {
    zx_status_t status;
    if ((status = block_map_.Reset(block_map_block_count_ * kBlobfsBlockBits)) != ZX_OK) {
        return status;
    } else if ((status = block_map_.Shrink(info_.block_count)) != ZX_OK) {
        return status;
    }
    const void* bmstart = block_map_.StorageUnsafe()->GetData();

    for (size_t n = 0; n < block_map_block_count_; n++) {
        void* bmdata = fs::GetBlock(kBlobfsBlockSize, bmstart, n);

        if (n >= node_map_start_block_) {
            memset(bmdata, 0, kBlobfsBlockSize);
        } else if ((status = ReadBlock(block_map_start_block_ + n)) != ZX_OK) {
            return status;
        } else {
            memcpy(bmdata, cache_.blk, kBlobfsBlockSize);
        }
    }
    return ZX_OK;
}

zx_status_t Blobfs::NewBlob(const Digest& digest, fbl::unique_ptr<InodeBlock>* out) {
    size_t ino = info_.inode_count;

    for (size_t i = 0; i < info_.inode_count; ++i) {
        size_t bno = (i / kBlobfsInodesPerBlock) + node_map_start_block_;

        zx_status_t status;
        if ((status = ReadBlock(bno)) != ZX_OK) {
            return status;
        }

        auto iblk = reinterpret_cast<const blobfs_inode_t*>(cache_.blk);
        auto observed_inode = &iblk[i % kBlobfsInodesPerBlock];
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

    size_t bno = (ino / kBlobfsInodesPerBlock) + NodeMapStartBlock(info_);
    zx_status_t status;
    if ((status = ReadBlock(bno)) != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    blobfs_inode_t* inodes = reinterpret_cast<blobfs_inode_t*>(cache_.blk);

    fbl::unique_ptr<InodeBlock> ino_block(
        new (&ac) InodeBlock(bno, &inodes[ino % kBlobfsInodesPerBlock], digest));

    if (!ac.check()) {
        return ZX_ERR_INTERNAL;
    }

    dirty_ = true;
    info_.alloc_inode_count++;
    *out = fbl::move(ino_block);
    return ZX_OK;
}

zx_status_t Blobfs::AllocateBlocks(size_t nblocks, size_t* blkno_out) {
    zx_status_t status;
    if ((status = block_map_.Find(false, 0, block_map_.size(), nblocks, blkno_out)) != ZX_OK) {
        return status;
    } else if ((status = block_map_.Set(*blkno_out, *blkno_out + nblocks)) != ZX_OK) {
        return status;
    }

    info_.alloc_block_count += nblocks;
    return ZX_OK;
}

zx_status_t Blobfs::WriteBitmap(size_t nblocks, size_t start_block) {
    uint64_t bbm_start_block = start_block / kBlobfsBlockBits;
    uint64_t bbm_end_block = fbl::round_up(start_block + nblocks, kBlobfsBlockBits) / kBlobfsBlockBits;
    const void* bmstart = block_map_.StorageUnsafe()->GetData();
    for (size_t n = bbm_start_block; n < bbm_end_block; n++) {
        const void* data = fs::GetBlock(kBlobfsBlockSize, bmstart, n);
        uint64_t bno = block_map_start_block_ + n;
        zx_status_t status;
        if ((status = WriteBlock(bno, data)) != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

zx_status_t Blobfs::WriteNode(fbl::unique_ptr<InodeBlock> ino_block) {
    if (ino_block->GetBno() != cache_.bno) {
        return ZX_ERR_ACCESS_DENIED;
    }

    dirty_ = false;
    return WriteBlock(cache_.bno, cache_.blk);
}

zx_status_t Blobfs::WriteData(blobfs_inode_t* inode, const void* merkle_data, const void* blob_data) {
    const size_t merkle_blocks = MerkleTreeBlocks(*inode);
    const size_t data_blocks = inode->num_blocks - merkle_blocks;
    for (size_t n = 0; n < merkle_blocks; n++) {
        const void* data = fs::GetBlock(kBlobfsBlockSize, merkle_data, n);
        uint64_t bno = data_start_block_ + inode->start_block + n;
        zx_status_t status;
        if ((status = WriteBlock(bno, data)) != ZX_OK) {
            return status;
        }
    }

    for (size_t n = 0; n < data_blocks; n++) {
        const void* data = fs::GetBlock(kBlobfsBlockSize, blob_data, n);

        // If we try to write a block, will it be reaching beyond the end of the
        // mapped file?
        size_t off = n * kBlobfsBlockSize;
        uint8_t last_data[kBlobfsBlockSize];
        if (inode->blob_size < off + kBlobfsBlockSize) {
            // Read the partial block from a block-sized buffer which zero-pads the data.
            memset(last_data, 0, kBlobfsBlockSize);
            memcpy(last_data, data, inode->blob_size - off);
            data = last_data;
        }

        uint64_t bno = data_start_block_ + inode->start_block + merkle_blocks + n;
        zx_status_t status;
        if ((status = WriteBlock(bno, data)) != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

zx_status_t Blobfs::WriteInfo() {
    return WriteBlock(0, info_block_);
}

zx_status_t Blobfs::ReadBlock(size_t bno) {
    if (dirty_) {
        return ZX_ERR_ACCESS_DENIED;
    }

    zx_status_t status;
    if ((cache_.bno != bno) && ((status = readblk_offset(blockfd_.get(), bno, offset_, &cache_.blk)) != ZX_OK)) {
        return status;
    }

    cache_.bno = bno;
    return ZX_OK;
}

zx_status_t Blobfs::WriteBlock(size_t bno, const void* data) {
    return writeblk_offset(blockfd_.get(), bno, offset_, data);
}

zx_status_t Blobfs::ResetCache() {
    if (dirty_) {
        return ZX_ERR_ACCESS_DENIED;
    }

    if (cache_.bno != 0) {
        memset(cache_.blk, 0, kBlobfsBlockSize);
        cache_.bno = 0;
    }
    return ZX_OK;
}

blobfs_inode_t* Blobfs::GetNode(size_t index) {
    size_t bno = node_map_start_block_ + index / kBlobfsInodesPerBlock;

    if (bno >= data_start_block_) {
        // Set cache to 0 so we can return a pointer to an empty inode
        if (ResetCache() != ZX_OK) {
            return nullptr;
        }
    } else if (ReadBlock(bno) < 0) {
        return nullptr;
    }

    auto iblock = reinterpret_cast<blobfs_inode_t*>(cache_.blk);
    return &iblock[index % kBlobfsInodesPerBlock];
}

zx_status_t Blobfs::VerifyBlob(size_t node_index) {
    blobfs_inode_t inode = *GetNode(node_index);

    // Determine size for (uncompressed) data buffer.
    uint64_t data_blocks = BlobDataBlocks(inode);
    uint64_t merkle_blocks = MerkleTreeBlocks(inode);
    uint64_t num_blocks = data_blocks + merkle_blocks;
    size_t target_size;
    if (mul_overflow(num_blocks, kBlobfsBlockSize, &target_size)) {
        fprintf(stderr, "Multiplication overflow");
        return ZX_ERR_OUT_OF_RANGE;
    }

    // Create data buffer.
    fbl::unique_ptr<uint8_t[]> data(new uint8_t[target_size]);

    if (inode.flags & kBlobFlagLZ4Compressed) {
        // Read in uncompressed merkle blocks.
        for (unsigned i = 0; i < merkle_blocks; i++) {
            ReadBlock(data_start_block_ + inode.start_block + i);
            memcpy(data.get() + (i * kBlobfsBlockSize), cache_.blk, kBlobfsBlockSize);
        }

        // Determine size for compressed data buffer.
        size_t compressed_blocks = (inode.num_blocks - merkle_blocks);
        size_t compressed_size;
        if (mul_overflow(compressed_blocks, kBlobfsBlockSize, &compressed_size)) {
            fprintf(stderr, "Multiplication overflow");
            return ZX_ERR_OUT_OF_RANGE;
        }

        // Create compressed data buffer.
        fbl::unique_ptr<uint8_t[]> compressed_data(new uint8_t[compressed_size]);

        // Read in all compressed blob data.
        for (unsigned i = 0; i < compressed_blocks; i++) {
            ReadBlock(data_start_block_ + inode.start_block + i + merkle_blocks);
            memcpy(compressed_data.get() + (i * kBlobfsBlockSize), cache_.blk, kBlobfsBlockSize);
        }

        // Decompress the compressed data into the target buffer.
        zx_status_t status;
        target_size = inode.blob_size;
        uint8_t* data_ptr = data.get() + (merkle_blocks * kBlobfsBlockSize);
        if ((status = Decompressor::Decompress(data_ptr, &target_size, compressed_data.get(),
                                               &compressed_size)) != ZX_OK) {
            return status;
        }
        if (target_size != inode.blob_size) {
            fprintf(stderr, "Failed to fully decompress blob (%zu of %" PRIu64 " expected)\n",
                    target_size, inode.blob_size);
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
    } else {
        // For uncompressed blobs, read entire blob straight into the data buffer.
        for (unsigned i = 0; i < inode.num_blocks; i++) {
            ReadBlock(data_start_block_ + inode.start_block + i);
            memcpy(data.get() + (i * kBlobfsBlockSize), cache_.blk, kBlobfsBlockSize);
        }
    }

    // Verify the contents of the blob.
    uint8_t* data_ptr = data.get() + (merkle_blocks * kBlobfsBlockSize);
    Digest digest(&inode.merkle_root_hash[0]);
    return MerkleTree::Verify(data_ptr, inode.blob_size, data.get(),
                              MerkleTree::GetTreeLength(inode.blob_size), 0,
                              inode.blob_size, digest);
}
} // namespace blobfs

// This is used by the ioctl wrappers in magenta/device/device.h. It's not
// called by host tools, so just satisfy the linker with a stub.
ssize_t fdio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf,
                   size_t out_len) {
    return -1;
}
