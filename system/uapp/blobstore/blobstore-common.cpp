// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fs/block-txn.h>
#include <mxio/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/limits.h>

#define MXDEBUG 0

#include "blobstore-private.h"

using digest::Digest;
using digest::MerkleTree;

namespace blobstore {

// Number of blocks reserved for the Merkle Tree
uint64_t MerkleTreeBlocks(const blobstore_inode_t& blobNode) {
    uint64_t size_merkle = MerkleTree::GetTreeLength(blobNode.blob_size);
    return fbl::roundup(size_merkle, kBlobstoreBlockSize) / kBlobstoreBlockSize;
}

// Sanity check the metadata for the blobstore, given a maximum number of
// available blocks.
mx_status_t blobstore_check_info(const blobstore_info_t* info, uint64_t max) {
    if ((info->magic0 != kBlobstoreMagic0) ||
        (info->magic1 != kBlobstoreMagic1)) {
        fprintf(stderr, "blobstore: bad magic\n");
        return MX_ERR_INVALID_ARGS;
    }
    if (info->version != kBlobstoreVersion) {
        fprintf(stderr, "blobstore: FS Version: %08x. Driver version: %08x\n", info->version,
                kBlobstoreVersion);
        return MX_ERR_INVALID_ARGS;
    }
    if (info->block_size != kBlobstoreBlockSize) {
        fprintf(stderr, "blobstore: bsz %u unsupported\n", info->block_size);
        return MX_ERR_INVALID_ARGS;
    }
    if ((info->flags & kBlobstoreFlagFVM) == 0) {
        if (info->block_count + DataStartBlock(*info) > max) {
            fprintf(stderr, "blobstore: too large for device\n");
            return MX_ERR_INVALID_ARGS;
        }
    } else {
        const size_t blocks_per_slice = info->slice_size / info->block_size;

        size_t abm_blocks_needed = BlockMapBlocks(*info);
        size_t abm_blocks_allocated = info->abm_slices * blocks_per_slice;
        if (abm_blocks_needed > abm_blocks_allocated) {
            FS_TRACE_ERROR("blobstore: Not enough slices for block bitmap\n");
            return MX_ERR_INVALID_ARGS;
        } else if (abm_blocks_allocated + BlockMapStartBlock(*info) >= NodeMapStartBlock(*info)) {
            FS_TRACE_ERROR("blobstore: Block bitmap collides into node map\n");
            return MX_ERR_INVALID_ARGS;
        }

        size_t ino_blocks_needed = NodeMapBlocks(*info);
        size_t ino_blocks_allocated = info->ino_slices * blocks_per_slice;
        if (ino_blocks_needed > ino_blocks_allocated) {
            FS_TRACE_ERROR("blobstore: Not enough slices for node map\n");
            return MX_ERR_INVALID_ARGS;
        } else if (ino_blocks_allocated + NodeMapStartBlock(*info) >= DataStartBlock(*info)) {
            FS_TRACE_ERROR("blobstore: Node bitmap collides into data blocks\n");
            return MX_ERR_INVALID_ARGS;
        }

        size_t dat_blocks_needed = DataBlocks(*info);
        size_t dat_blocks_allocated = info->dat_slices * blocks_per_slice;
        if (dat_blocks_needed > dat_blocks_allocated) {
            FS_TRACE_ERROR("blobstore: Not enough slices for data blocks\n");
            return MX_ERR_INVALID_ARGS;
        } else if (dat_blocks_allocated + DataStartBlock(*info) >
                   fbl::numeric_limits<uint32_t>::max()) {
            FS_TRACE_ERROR("blobstore: Data blocks overflow uint32\n");
            return MX_ERR_INVALID_ARGS;
        }
    }
    if (info->blob_header_next != 0) {
        fprintf(stderr, "blobstore: linked blob headers not yet supported\n");
        return MX_ERR_INVALID_ARGS;
    }
    return MX_OK;
}

mx_status_t blobstore_get_blockcount(int fd, uint64_t* out) {
#ifdef __Fuchsia__
    block_info_t info;
    ssize_t r;
    if ((r = ioctl_block_get_info(fd, &info)) < 0) {
        return static_cast<mx_status_t>(r);
    }
    *out = (info.block_size * info.block_count) / kBlobstoreBlockSize;
#else
    struct stat s;
    if (fstat(fd, &s) < 0) {
        return MX_ERR_BAD_STATE;
    }
    *out = s.st_size / kBlobstoreBlockSize;
#endif
    return MX_OK;
}

mx_status_t readblk(int fd, uint64_t bno, void* data) {
    off_t off = bno * kBlobstoreBlockSize;
    if (lseek(fd, off, SEEK_SET) < 0) {
        fprintf(stderr, "blobstore: cannot seek to block %" PRIu64 "\n", bno);
        return MX_ERR_IO;
    }
    if (read(fd, data, kBlobstoreBlockSize) != kBlobstoreBlockSize) {
        fprintf(stderr, "blobstore: cannot read block %" PRIu64 "\n", bno);
        return MX_ERR_IO;
    }
    return MX_OK;
}

mx_status_t writeblk(int fd, uint64_t bno, const void* data) {
    off_t off = bno * kBlobstoreBlockSize;
    if (lseek(fd, off, SEEK_SET) < 0) {
        fprintf(stderr, "blobstore: cannot seek to block %" PRIu64 "\n", bno);
        return MX_ERR_IO;
    }
    if (write(fd, data, kBlobstoreBlockSize) != kBlobstoreBlockSize) {
        fprintf(stderr, "blobstore: cannot write block %" PRIu64 "\n", bno);
        return MX_ERR_IO;
    }
    return MX_OK;
}

int blobstore_mkfs(int fd, uint64_t block_count) {
    uint64_t inodes = 32768;

    blobstore_info_t info;
    memset(&info, 0x00, sizeof(info));
    info.magic0 = kBlobstoreMagic0;
    info.magic1 = kBlobstoreMagic1;
    info.version = kBlobstoreVersion;
    info.flags = kBlobstoreFlagClean;
    info.block_size = kBlobstoreBlockSize;
    // Set block_count to max blocks so we can calculate block map blocks
    info.block_count = block_count;
    info.inode_count = inodes;
    info.alloc_block_count = 0;
    info.alloc_inode_count = 0;
    info.blob_header_next = 0; // TODO(smklein): Allow chaining
    // The result of DataStartBlock(info) is based on the current value of info.block_count. As a
    // result, the block bitmap may have slightly more space allocated than is necessary.
    info.block_count -= DataStartBlock(info); // Set block_count to number of data blocks


#ifdef __Fuchsia__
    fvm_info_t fvm_info;

    if (ioctl_block_fvm_query(fd, &fvm_info) >= 0) {
        info.slice_size = fvm_info.slice_size;
        info.flags |= kBlobstoreFlagFVM;

        if (info.slice_size % kBlobstoreBlockSize) {
            fprintf(stderr, "blobstore mkfs: Slice size not multiple of blobstore block\n");
            return -1;
        }

        const size_t kBlocksPerSlice = info.slice_size / kBlobstoreBlockSize;

        extend_request_t request;
        request.length = 1;
        request.offset = kFVMBlockMapStart / kBlocksPerSlice;
        if (ioctl_block_fvm_extend(fd, &request) < 0) {
            fprintf(stderr, "blobstore mkfs: Failed to allocate block map\n");
            return -1;
        }
        request.offset = kFVMNodeMapStart / kBlocksPerSlice;
        if (ioctl_block_fvm_extend(fd, &request) < 0) {
            fprintf(stderr, "blobstore mkfs: Failed to allocate node map\n");
            return -1;
        }
        request.offset = kFVMDataStart / kBlocksPerSlice;
        if (ioctl_block_fvm_extend(fd, &request) < 0) {
            fprintf(stderr, "blobstore mkfs: Failed to allocate data blocks\n");
            return -1;
        }

        info.abm_slices = 1;
        info.ino_slices = 1;
        info.dat_slices = 1;
        info.vslice_count = info.abm_slices + info.ino_slices + info.dat_slices + 1;

        info.inode_count = static_cast<uint32_t>(info.ino_slices * info.slice_size
                                                 / kBlobstoreInodeSize);
        info.block_count = static_cast<uint32_t>(info.dat_slices * info.slice_size
                                                 / kBlobstoreBlockSize);
    } else {
        info.block_count -= DataStartBlock(info);
    }
#endif

    xprintf("Blobstore Mkfs\n");
    xprintf("Disk size  : %" PRIu64 "\n", block_count * kBlobstoreBlockSize);
    xprintf("Block Size : %u\n", kBlobstoreBlockSize);
    xprintf("Block Count: %" PRIu64 "\n", TotalBlocks(info));
    xprintf("Inode Count: %" PRIu64 "\n", inodes);
    xprintf("FVM-aware: %s\n", (info.flags & kBlobstoreFlagFVM) ? "YES" : "NO");

    // Determine the number of blocks necessary for the block map and node map.
    uint64_t bbm_blocks = BlockMapBlocks(info);
    uint64_t nbm_blocks = NodeMapBlocks(info);

    RawBitmap abm;
    if (abm.Reset(bbm_blocks * kBlobstoreBlockBits)) {
        fprintf(stderr, "Couldn't allocate blobstore block map\n");
        return -1;
    } else if (abm.Shrink(info.block_count)) {
        fprintf(stderr, "Couldn't shrink blobstore block map\n");
        return -1;
    }

    if (info.inode_count * sizeof(blobstore_inode_t) != nbm_blocks * kBlobstoreBlockSize) {
        fprintf(stderr, "For simplicity, inode table block must be entirely filled\n");
        return -1;
    }

    // All in-memory structures have been created successfully. Dump everything to disk.
    char block[kBlobstoreBlockSize];
    mx_status_t status;

    // write the root block to disk
    memset(block, 0, sizeof(block));
    memcpy(block, &info, sizeof(info));
    if ((status = writeblk(fd, 0, block)) != MX_OK) {
        fprintf(stderr, "Failed to write root block\n");
        return status;
    }

    // write allocation bitmap to disk
    for (uint64_t n = 0; n < bbm_blocks; n++) {
        void* bmdata = get_raw_bitmap_data(abm, n);
        if ((status = writeblk(fd, BlockMapStartBlock(info) + n, bmdata)) < 0) {
            fprintf(stderr, "Failed to write blockmap block %" PRIu64 "\n", n);
            return status;
        }
    }

    // write node map to disk
    for (uint64_t n = 0; n < nbm_blocks; n++) {
        memset(block, 0, sizeof(block));
        if (writeblk(fd, NodeMapStartBlock(info) + n, block)) {
            fprintf(stderr, "blobstore: failed writing inode map\n");
            return MX_ERR_IO;
        }
    }

    xprintf("BLOBSTORE: mkfs success\n");
    return 0;
}

} // namespace blobstore

#ifndef __Fuchsia__
// This is used by the ioctl wrappers in magenta/device/device.h. It's not
// called by host tools, so just satisfy the linker with a stub.
ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    return -1;
}
#endif
