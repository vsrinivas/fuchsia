// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/alloc_checker.h>
#include <fbl/limits.h>
#include <lib/fdio/debug.h>
#include <fs/block-txn.h>
#include <fs/trace.h>

#ifdef __Fuchsia__
#include <fs/fvm.h>
#endif

#define ZXDEBUG 0

#include <blobfs/common.h>

using digest::Digest;
using digest::MerkleTree;

namespace blobfs {

// Number of blocks reserved for the Merkle Tree
uint64_t MerkleTreeBlocks(const Inode& blobNode) {
    uint64_t size_merkle = MerkleTree::GetTreeLength(blobNode.blob_size);
    return fbl::round_up(size_merkle, kBlobfsBlockSize) / kBlobfsBlockSize;
}

// Sanity check the metadata for the blobfs, given a maximum number of
// available blocks.
zx_status_t CheckSuperblock(const Superblock* info, uint64_t max) {
    if ((info->magic0 != kBlobfsMagic0) ||
        (info->magic1 != kBlobfsMagic1)) {
        fprintf(stderr, "blobfs: bad magic\n");
        return ZX_ERR_INVALID_ARGS;
    }
    if (info->version != kBlobfsVersion) {
        fprintf(stderr, "blobfs: FS Version: %08x. Driver version: %08x\n", info->version,
                kBlobfsVersion);
        return ZX_ERR_INVALID_ARGS;
    }
    if (info->block_size != kBlobfsBlockSize) {
        fprintf(stderr, "blobfs: bsz %u unsupported\n", info->block_size);
        return ZX_ERR_INVALID_ARGS;
    }
    if ((info->flags & kBlobFlagFVM) == 0) {
        if (TotalBlocks(*info) > max) {
            fprintf(stderr, "blobfs: too large for device\n");
            return ZX_ERR_INVALID_ARGS;
        }
    } else {
        const size_t blocks_per_slice = info->slice_size / info->block_size;

        size_t abm_blocks_needed = BlockMapBlocks(*info);
        size_t abm_blocks_allocated = info->abm_slices * blocks_per_slice;
        if (abm_blocks_needed > abm_blocks_allocated) {
            FS_TRACE_ERROR("blobfs: Not enough slices for block bitmap\n");
            return ZX_ERR_INVALID_ARGS;
        } else if (abm_blocks_allocated + BlockMapStartBlock(*info) >= NodeMapStartBlock(*info)) {
            FS_TRACE_ERROR("blobfs: Block bitmap collides into node map\n");
            return ZX_ERR_INVALID_ARGS;
        }

        size_t ino_blocks_needed = NodeMapBlocks(*info);
        size_t ino_blocks_allocated = info->ino_slices * blocks_per_slice;
        if (ino_blocks_needed > ino_blocks_allocated) {
            FS_TRACE_ERROR("blobfs: Not enough slices for node map\n");
            return ZX_ERR_INVALID_ARGS;
        } else if (ino_blocks_allocated + NodeMapStartBlock(*info) >= DataStartBlock(*info)) {
            FS_TRACE_ERROR("blobfs: Node bitmap collides into data blocks\n");
            return ZX_ERR_INVALID_ARGS;
        }

        size_t dat_blocks_needed = DataBlocks(*info);
        size_t dat_blocks_allocated = info->dat_slices * blocks_per_slice;
        if (dat_blocks_needed < kStartBlockMinimum) {
            FS_TRACE_ERROR("blobfs: Partition too small; no space left for data blocks\n");
            return ZX_ERR_INVALID_ARGS;
        } else if (dat_blocks_needed > dat_blocks_allocated) {
            FS_TRACE_ERROR("blobfs: Not enough slices for data blocks\n");
            return ZX_ERR_INVALID_ARGS;
        } else if (dat_blocks_allocated + DataStartBlock(*info) >
                   fbl::numeric_limits<uint32_t>::max()) {
            FS_TRACE_ERROR("blobfs: Data blocks overflow uint32\n");
            return ZX_ERR_INVALID_ARGS;
        }
    }
    if (info->blob_header_next != 0) {
        fprintf(stderr, "blobfs: linked blob headers not yet supported\n");
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

zx_status_t GetBlockCount(int fd, uint64_t* out) {
#ifdef __Fuchsia__
    block_info_t info;
    ssize_t r;
    if ((r = ioctl_block_get_info(fd, &info)) < 0) {
        return static_cast<zx_status_t>(r);
    }
    *out = (info.block_size * info.block_count) / kBlobfsBlockSize;
#else
    struct stat s;
    if (fstat(fd, &s) < 0) {
        return ZX_ERR_BAD_STATE;
    }
    *out = s.st_size / kBlobfsBlockSize;
#endif
    return ZX_OK;
}

zx_status_t readblk(int fd, uint64_t bno, void* data) {
    off_t off = bno * kBlobfsBlockSize;
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

zx_status_t writeblk(int fd, uint64_t bno, const void* data) {
    off_t off = bno * kBlobfsBlockSize;
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

int Mkfs(int fd, uint64_t block_count) {
    uint64_t inodes = kBlobfsDefaultInodeCount;

    Superblock info;
    memset(&info, 0x00, sizeof(info));
    info.magic0 = kBlobfsMagic0;
    info.magic1 = kBlobfsMagic1;
    info.version = kBlobfsVersion;
    info.flags = kBlobFlagClean;
    info.block_size = kBlobfsBlockSize;
    //TODO(planders): Consider modifying the inode count if we are low on space.
    //                It doesn't make sense to have fewer data blocks than inodes.
    info.inode_count = inodes;
    info.alloc_block_count = 0;
    info.alloc_inode_count = 0;
    info.blob_header_next = 0; // TODO(smklein): Allow chaining

    // Temporarily set the data_block_count to the total block_count so we can estimate the number
    // of pre-data blocks.
    info.data_block_count = block_count;

    // The result of DataStartBlock(info) is based on the current value of info.data_block_count.
    // As a result, the block bitmap may have slightly more space allocated than is necessary.
    size_t usable_blocks = JournalStartBlock(info) < block_count
                           ? block_count - JournalStartBlock(info)
                           : 0;

    // Determine allocation for the journal vs. data blocks based on the number of blocks remaining.
    if (usable_blocks >= kDefaultJournalBlocks * 2) {
        // Regular-sized partition, capable of fitting a data region
        // at least as large as the journal. Give all excess blocks
        // to the data region.
        info.journal_block_count = kDefaultJournalBlocks;
        info.data_block_count = usable_blocks - kDefaultJournalBlocks;
    } else if (usable_blocks >= kMinimumDataBlocks + kMinimumJournalBlocks) {
        // On smaller partitions, give both regions the minimum amount of space,
        // and split the remainder. The choice of where to allocate the "remainder"
        // is arbitrary.
        const size_t remainder_blocks = usable_blocks -
                                        (kMinimumDataBlocks + kMinimumJournalBlocks);
        const size_t remainder_for_journal = remainder_blocks / 2;
        const size_t remainder_for_data = remainder_blocks - remainder_for_journal;
        info.journal_block_count = kMinimumJournalBlocks + remainder_for_journal;
        info.data_block_count = kMinimumDataBlocks + remainder_for_data;
    } else {
        // Error, partition too small.
        info.journal_block_count = 0;
        info.data_block_count = 0;
    }

#ifdef __Fuchsia__
    fvm_info_t fvm_info;

    if (ioctl_block_fvm_query(fd, &fvm_info) >= 0) {
        info.slice_size = fvm_info.slice_size;
        info.flags |= kBlobFlagFVM;

        if (info.slice_size % kBlobfsBlockSize) {
            fprintf(stderr, "blobfs mkfs: Slice size not multiple of blobfs block\n");
            return -1;
        }

        if (fs::fvm_reset_volume_slices(fd) != ZX_OK) {
            fprintf(stderr, "blobfs mkfs: Failed to reset slices\n");
            return -1;
        }

        const size_t kBlocksPerSlice = info.slice_size / kBlobfsBlockSize;

        extend_request_t request;
        request.length = 1;
        request.offset = kFVMBlockMapStart / kBlocksPerSlice;
        if (ioctl_block_fvm_extend(fd, &request) < 0) {
            fprintf(stderr, "blobfs mkfs: Failed to allocate block map\n");
            return -1;
        }

        request.offset = kFVMNodeMapStart / kBlocksPerSlice;
        if (ioctl_block_fvm_extend(fd, &request) < 0) {
            fprintf(stderr, "blobfs mkfs: Failed to allocate node map\n");
            return -1;
        }

        // Allocate the minimum number of journal blocks in FVM.
        request.offset = kFVMJournalStart / kBlocksPerSlice;
        request.length = fbl::round_up(kDefaultJournalBlocks, kBlocksPerSlice) / kBlocksPerSlice;
        info.journal_slices = static_cast<uint32_t>(request.length);
        if (ioctl_block_fvm_extend(fd, &request) < 0) {
            fprintf(stderr, "blobfs mkfs: Failed to allocate journal blocks\n");
            return -1;
        }

        // Allocate the minimum number of data blocks in the FVM.
        request.offset = kFVMDataStart / kBlocksPerSlice;
        request.length = fbl::round_up(kMinimumDataBlocks, kBlocksPerSlice) / kBlocksPerSlice;
        info.dat_slices = static_cast<uint32_t>(request.length);
        if (ioctl_block_fvm_extend(fd, &request) < 0) {
            fprintf(stderr, "blobfs mkfs: Failed to allocate data blocks\n");
            return -1;
        }

        info.abm_slices = 1;
        info.ino_slices = 1;

        info.vslice_count = info.abm_slices + info.ino_slices + info.dat_slices +
                            info.journal_slices + 1;

        info.inode_count = static_cast<uint32_t>(info.ino_slices * info.slice_size
                                                 / kBlobfsInodeSize);

        info.data_block_count = static_cast<uint32_t>(info.dat_slices * info.slice_size
                                                      / kBlobfsBlockSize);
        info.journal_block_count = static_cast<uint32_t>(info.journal_slices * info.slice_size
                                                         / kBlobfsBlockSize);
    }
#endif

    xprintf("Blobfs Mkfs\n");
    xprintf("Disk size  : %" PRIu64 "\n", block_count * kBlobfsBlockSize);
    xprintf("Block Size : %u\n", kBlobfsBlockSize);
    xprintf("Block Count: %" PRIu64 "\n", TotalBlocks(info));
    xprintf("Inode Count: %" PRIu64 "\n", inodes);
    xprintf("FVM-aware: %s\n", (info.flags & kBlobFlagFVM) ? "YES" : "NO");

    if (info.data_block_count < kMinimumDataBlocks) {
        fprintf(stderr, "blobfs mkfs: Not enough space for minimum data partition\n");
        return -1;
    }

    if (info.journal_block_count < kMinimumJournalBlocks) {
        fprintf(stderr, "blobfs mkfs: Not enough space for minimum journal partition\n");
        return -1;
    }

    // Determine the number of blocks necessary for the block map and node map.
    uint64_t bbm_blocks = BlockMapBlocks(info);
    uint64_t nbm_blocks = NodeMapBlocks(info);

    RawBitmap abm;
    if (abm.Reset(bbm_blocks * kBlobfsBlockBits)) {
        fprintf(stderr, "Couldn't allocate blobfs block map\n");
        return -1;
    } else if (abm.Shrink(info.data_block_count)) {
        fprintf(stderr, "Couldn't shrink blobfs block map\n");
        return -1;
    }

    // Reserve first |kStartBlockMinimum| data blocks
    abm.Set(0, kStartBlockMinimum);
    info.alloc_block_count += kStartBlockMinimum;

    if (info.inode_count * sizeof(Inode) != nbm_blocks * kBlobfsBlockSize) {
        fprintf(stderr, "For simplicity, inode table block must be entirely filled\n");
        return -1;
    }

    // All in-memory structures have been created successfully. Dump everything to disk.
    zx_status_t status;
    char block[kBlobfsBlockSize];
    memset(block, 0, sizeof(block));

    JournalInfo* journal_info = reinterpret_cast<JournalInfo*>(block);
    journal_info->magic = kJournalMagic;
    if ((status = writeblk(fd, JournalStartBlock(info), block)) != ZX_OK) {
        fprintf(stderr, "Failed to write journal block\n");
        return status;
    }

    // write the root block to disk
    memset(block, 0, sizeof(journal_info));
    memcpy(block, &info, sizeof(info));
    if ((status = writeblk(fd, 0, block)) != ZX_OK) {
        fprintf(stderr, "Failed to write root block\n");
        return status;
    }

    // write allocation bitmap to disk
    for (uint64_t n = 0; n < bbm_blocks; n++) {
        void* bmdata = GetRawBitmapData(abm, n);
        if ((status = writeblk(fd, BlockMapStartBlock(info) + n, bmdata)) < 0) {
            fprintf(stderr, "Failed to write blockmap block %" PRIu64 "\n", n);
            return status;
        }
    }

    // write node map to disk
    for (uint64_t n = 0; n < nbm_blocks; n++) {
        memset(block, 0, sizeof(block));
        if (writeblk(fd, NodeMapStartBlock(info) + n, block)) {
            fprintf(stderr, "blobfs: failed writing inode map\n");
            return ZX_ERR_IO;
        }
    }

    xprintf("BLOBFS: mkfs success\n");
    return 0;
}

} // namespace blobfs
