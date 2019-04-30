// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <limits>
#include <safemath/checked_math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fs/block-txn.h>
#include <fs/trace.h>

#ifdef __Fuchsia__
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <fvm/client.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/fdio.h>
#endif

#include <blobfs/common.h>

using digest::Digest;
using digest::MerkleTree;

namespace blobfs {

namespace {
// Dumps the content of superblock to |out|. Does nothing if |out| is nullptr.
void DumpSuperblock(const Superblock& info, FILE* out) {
    if (out == nullptr) {
        return;
    }

    fprintf(out,
            "info.magic0: %" PRIu64 "\n"
            "info.magic1: %" PRIu64 "\n"
            "info.version: %" PRIu32 "\n"
            "info.flags: %" PRIu32 "\n"
            "info.block_size: %" PRIu32 "\n"
            "info.data_block_count: %" PRIu64 "\n"
            "info.journal_block_count: %" PRIu64 "\n"
            "info.inode_count: %" PRIu64 "\n"
            "info.alloc_block_count: %" PRIu64 "\n"
            "info.alloc_inode_count: %" PRIu64 "\n"
            "info.blob_header_next: %" PRIu64 "\n"
            "info.slice_size: %" PRIu64 "\n"
            "info.vslice_count: %" PRIu64 "\n"
            "info.abm_slices: %" PRIu32 "\n"
            "info.ino_slices: %" PRIu32 "\n"
            "info.dat_slices: %" PRIu32 "\n"
            "info.journal_slices: %" PRIu32 "\n",
            info.magic0, info.magic1, info.version, info.flags, info.block_size,
            info.data_block_count, info.journal_block_count, info.inode_count,
            info.alloc_block_count, info.alloc_inode_count, info.blob_header_next, info.slice_size,
            info.vslice_count, info.abm_slices, info.ino_slices, info.dat_slices,
            info.journal_slices);
}

} // namespace

// Number of blocks reserved for the Merkle Tree
uint32_t MerkleTreeBlocks(const Inode& blobNode) {
    uint64_t size_merkle = MerkleTree::GetTreeLength(blobNode.blob_size);
    ZX_DEBUG_ASSERT(size_merkle <= std::numeric_limits<uint32_t>::max());
    return fbl::round_up(static_cast<uint32_t>(size_merkle), kBlobfsBlockSize) / kBlobfsBlockSize;
}

// Sanity check the metadata for the blobfs, given a maximum number of
// available blocks.
zx_status_t CheckSuperblock(const Superblock* info, uint64_t max) {
    if ((info->magic0 != kBlobfsMagic0) ||
        (info->magic1 != kBlobfsMagic1)) {
        FS_TRACE_ERROR("blobfs: bad magic\n");
        return ZX_ERR_INVALID_ARGS;
    }
    if (info->version != kBlobfsVersion) {
        FS_TRACE_ERROR("blobfs: FS Version: %08x. Driver version: %08x\n", info->version,
                       kBlobfsVersion);
        DumpSuperblock(*info, stderr);
        return ZX_ERR_INVALID_ARGS;
    }
    if (info->block_size != kBlobfsBlockSize) {
        FS_TRACE_ERROR("blobfs: bsz %u unsupported\n", info->block_size);
        DumpSuperblock(*info, stderr);
        return ZX_ERR_INVALID_ARGS;
    }
    if ((info->flags & kBlobFlagFVM) == 0) {
        if (TotalBlocks(*info) > max) {
            FS_TRACE_ERROR("blobfs: too large for device\n");
            DumpSuperblock(*info, stderr);
            return ZX_ERR_INVALID_ARGS;
        }
    } else {
        const size_t blocks_per_slice = info->slice_size / info->block_size;

        size_t abm_blocks_needed = BlockMapBlocks(*info);
        size_t abm_blocks_allocated = info->abm_slices * blocks_per_slice;
        if (abm_blocks_needed > abm_blocks_allocated) {
            FS_TRACE_ERROR("blobfs: Not enough slices for block bitmap\n");
            DumpSuperblock(*info, stderr);
            return ZX_ERR_INVALID_ARGS;
        } else if (abm_blocks_allocated + BlockMapStartBlock(*info) >= NodeMapStartBlock(*info)) {
            FS_TRACE_ERROR("blobfs: Block bitmap collides into node map\n");
            DumpSuperblock(*info, stderr);
            return ZX_ERR_INVALID_ARGS;
        }

        size_t ino_blocks_needed = NodeMapBlocks(*info);
        size_t ino_blocks_allocated = info->ino_slices * blocks_per_slice;
        if (ino_blocks_needed > ino_blocks_allocated) {
            FS_TRACE_ERROR("blobfs: Not enough slices for node map\n");
            DumpSuperblock(*info, stderr);
            return ZX_ERR_INVALID_ARGS;
        } else if (ino_blocks_allocated + NodeMapStartBlock(*info) >= DataStartBlock(*info)) {
            FS_TRACE_ERROR("blobfs: Node bitmap collides into data blocks\n");
            DumpSuperblock(*info, stderr);
            return ZX_ERR_INVALID_ARGS;
        }

        size_t dat_blocks_needed = DataBlocks(*info);
        size_t dat_blocks_allocated = info->dat_slices * blocks_per_slice;
        if (dat_blocks_needed < kStartBlockMinimum) {
            FS_TRACE_ERROR("blobfs: Partition too small; no space left for data blocks\n");
            DumpSuperblock(*info, stderr);
            return ZX_ERR_INVALID_ARGS;
        } else if (dat_blocks_needed > dat_blocks_allocated) {
            FS_TRACE_ERROR("blobfs: Not enough slices for data blocks\n");
            DumpSuperblock(*info, stderr);
            return ZX_ERR_INVALID_ARGS;
        } else if (dat_blocks_allocated + DataStartBlock(*info) >
                   std::numeric_limits<uint32_t>::max()) {
            FS_TRACE_ERROR("blobfs: Data blocks overflow uint32\n");
            DumpSuperblock(*info, stderr);
            return ZX_ERR_INVALID_ARGS;
        }
    }
    if (info->blob_header_next != 0) {
        FS_TRACE_ERROR("blobfs: linked blob headers not yet supported\n");
        DumpSuperblock(*info, stderr);
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

zx_status_t GetBlockCount(int fd, uint64_t* out) {
#ifdef __Fuchsia__
    fzl::UnownedFdioCaller caller(fd);
    fuchsia_hardware_block_BlockInfo info;
    zx_status_t status;
    zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &status,
                                                                &info);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
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
        FS_TRACE_ERROR("blobfs: cannot seek to block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    if (read(fd, data, kBlobfsBlockSize) != kBlobfsBlockSize) {
        FS_TRACE_ERROR("blobfs: cannot read block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t writeblk(int fd, uint64_t bno, const void* data) {
    off_t off = bno * kBlobfsBlockSize;
    if (lseek(fd, off, SEEK_SET) < 0) {
        FS_TRACE_ERROR("blobfs: cannot seek to block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    if (write(fd, data, kBlobfsBlockSize) != kBlobfsBlockSize) {
        FS_TRACE_ERROR("blobfs: cannot write block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

uint32_t BlocksRequiredForInode(uint64_t inode_count) {
    return safemath::checked_cast<uint32_t>(fbl::round_up(inode_count, kBlobfsInodesPerBlock) /
                                            kBlobfsInodesPerBlock);
}

uint32_t BlocksRequiredForBits(uint64_t bit_count) {
    return safemath::checked_cast<uint32_t>(fbl::round_up(bit_count, kBlobfsBlockBits) /
                                            kBlobfsBlockBits);
}

uint32_t SuggestJournalBlocks(uint32_t current, uint32_t available) {
    return current + available;
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

    zx_status_t status;
#ifdef __Fuchsia__
    fuchsia_hardware_block_volume_VolumeInfo fvm_info;
    fzl::UnownedFdioCaller caller(fd);

    // Querying may be used to confirm if the underlying connection is capable of
    // communicating the FVM protocol. Clone the connection, since if the block
    // device does NOT speak the Volume protocol, the connection is terminated.
    zx::channel connection(fdio_service_clone(caller.borrow_channel()));

    zx_status_t io_status;
    io_status = fuchsia_hardware_block_volume_VolumeQuery(connection.get(), &status, &fvm_info);
    if (io_status == ZX_OK && status == ZX_OK) {
        info.slice_size = fvm_info.slice_size;
        info.flags |= kBlobFlagFVM;

        if (info.slice_size % kBlobfsBlockSize) {
            FS_TRACE_ERROR("blobfs mkfs: Slice size not multiple of blobfs block\n");
            return -1;
        }

        if (fvm::ResetAllSlices(fd) != ZX_OK) {
            FS_TRACE_ERROR("blobfs mkfs: Failed to reset slices\n");
            return -1;
        }

        const size_t kBlocksPerSlice = info.slice_size / kBlobfsBlockSize;

        uint64_t offset = kFVMBlockMapStart / kBlocksPerSlice;
        uint64_t length = 1;
        io_status = fuchsia_hardware_block_volume_VolumeExtend(caller.borrow_channel(), offset,
                                                               length, &status);
        if (io_status != ZX_OK || status != ZX_OK) {
            FS_TRACE_ERROR("blobfs mkfs: Failed to allocate block map\n");
            return -1;
        }

        offset = kFVMNodeMapStart / kBlocksPerSlice;
        io_status = fuchsia_hardware_block_volume_VolumeExtend(caller.borrow_channel(), offset,
                                                               length, &status);
        if (io_status != ZX_OK || status != ZX_OK) {
            FS_TRACE_ERROR("blobfs mkfs: Failed to allocate node map\n");
            return -1;
        }

        // Allocate the minimum number of journal blocks in FVM.
        offset = kFVMJournalStart / kBlocksPerSlice;
        length = fbl::round_up(kDefaultJournalBlocks, kBlocksPerSlice) / kBlocksPerSlice;
        info.journal_slices = static_cast<uint32_t>(length);
        io_status = fuchsia_hardware_block_volume_VolumeExtend(caller.borrow_channel(), offset,
                                                               length, &status);
        if (io_status != ZX_OK || status != ZX_OK) {
            FS_TRACE_ERROR("blobfs mkfs: Failed to allocate journal blocks\n");
            return -1;
        }

        // Allocate the minimum number of data blocks in the FVM.
        offset = kFVMDataStart / kBlocksPerSlice;
        length = fbl::round_up(kMinimumDataBlocks, kBlocksPerSlice) / kBlocksPerSlice;
        info.dat_slices = static_cast<uint32_t>(length);
        io_status = fuchsia_hardware_block_volume_VolumeExtend(caller.borrow_channel(), offset,
                                                               length, &status);
        if (io_status != ZX_OK || status != ZX_OK) {
            FS_TRACE_ERROR("blobfs mkfs: Failed to allocate data blocks\n");
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

    FS_TRACE_DEBUG("Blobfs Mkfs\n");
    FS_TRACE_DEBUG("Disk size  : %" PRIu64 "\n", block_count * kBlobfsBlockSize);
    FS_TRACE_DEBUG("Block Size : %u\n", kBlobfsBlockSize);
    FS_TRACE_DEBUG("Block Count: %" PRIu64 "\n", TotalBlocks(info));
    FS_TRACE_DEBUG("Inode Count: %" PRIu64 "\n", inodes);
    FS_TRACE_DEBUG("FVM-aware: %s\n", (info.flags & kBlobFlagFVM) ? "YES" : "NO");

    if (info.data_block_count < kMinimumDataBlocks) {
        FS_TRACE_ERROR("blobfs mkfs: Not enough space for minimum data partition\n");
        return -1;
    }

    if (info.journal_block_count < kMinimumJournalBlocks) {
        FS_TRACE_ERROR("blobfs mkfs: Not enough space for minimum journal partition\n");
        return -1;
    }

    // Determine the number of blocks necessary for the block map and node map.
    uint64_t bbm_blocks = BlockMapBlocks(info);
    uint64_t nbm_blocks = NodeMapBlocks(info);

    RawBitmap abm;
    if (abm.Reset(bbm_blocks * kBlobfsBlockBits)) {
        FS_TRACE_ERROR("Couldn't allocate blobfs block map\n");
        return -1;
    } else if (abm.Shrink(info.data_block_count)) {
        FS_TRACE_ERROR("Couldn't shrink blobfs block map\n");
        return -1;
    }

    // Reserve first |kStartBlockMinimum| data blocks
    abm.Set(0, kStartBlockMinimum);
    info.alloc_block_count += kStartBlockMinimum;

    if (info.inode_count * sizeof(Inode) != nbm_blocks * kBlobfsBlockSize) {
        FS_TRACE_ERROR("For simplicity, inode table block must be entirely filled\n");
        return -1;
    }

    // All in-memory structures have been created successfully. Dump everything to disk.
    char block[kBlobfsBlockSize];
    memset(block, 0, sizeof(block));

    JournalInfo* journal_info = reinterpret_cast<JournalInfo*>(block);
    journal_info->magic = kJournalMagic;
    if ((status = writeblk(fd, JournalStartBlock(info), block)) != ZX_OK) {
        FS_TRACE_ERROR("Failed to write journal block\n");
        return status;
    }

    // write the root block to disk
    memset(block, 0, sizeof(journal_info));
    memcpy(block, &info, sizeof(info));
    if ((status = writeblk(fd, 0, block)) != ZX_OK) {
        FS_TRACE_ERROR("Failed to write root block\n");
        return status;
    }

    // write allocation bitmap to disk
    for (uint64_t n = 0; n < bbm_blocks; n++) {
        void* bmdata = GetRawBitmapData(abm, n);
        if ((status = writeblk(fd, BlockMapStartBlock(info) + n, bmdata)) < 0) {
            FS_TRACE_ERROR("Failed to write blockmap block %" PRIu64 "\n", n);
            return status;
        }
    }

    // write node map to disk
    for (uint64_t n = 0; n < nbm_blocks; n++) {
        memset(block, 0, sizeof(block));
        if (writeblk(fd, NodeMapStartBlock(info) + n, block)) {
            FS_TRACE_ERROR("blobfs: failed writing inode map\n");
            return ZX_ERR_IO;
        }
    }

    FS_TRACE_DEBUG("BLOBFS: mkfs success\n");
    return 0;
}

} // namespace blobfs
