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
#include <mxtl/alloc_checker.h>

#define MXDEBUG 0

#include "blobstore-private.h"

using digest::Digest;
using digest::MerkleTree;

namespace blobstore {

// Number of blocks reserved for the Merkle Tree
uint64_t MerkleTreeBlocks(const blobstore_inode_t& blobNode) {
    uint64_t size_merkle = MerkleTree::GetTreeLength(blobNode.blob_size);
    return mxtl::roundup(size_merkle, kBlobstoreBlockSize) / kBlobstoreBlockSize;
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
    if (info->block_count > max) {
        fprintf(stderr, "blobstore: too large for device\n");
        return MX_ERR_INVALID_ARGS;
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
    info.block_count = block_count;
    info.inode_count = inodes;
    info.alloc_block_count = 0;
    info.alloc_inode_count = 0;
    info.blob_header_next = 0; // TODO(smklein): Allow chaining

    xprintf("Blobstore Mkfs\n");
    xprintf("Disk size  : %" PRIu64 "\n", block_count * kBlobstoreBlockSize);
    xprintf("Block Size : %u\n", kBlobstoreBlockSize);
    xprintf("Block Count: %" PRIu64 "\n", block_count);
    xprintf("Inode Count: %" PRIu64 "\n", inodes);

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

    // update block bitmap:
    // reserve all blocks before the data storage area.
    abm.Set(0, DataStartBlock(info));

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
        if ((status = writeblk(fd, BlockMapStartBlock() + n, bmdata)) < 0) {
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
