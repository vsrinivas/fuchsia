// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the on-disk structure of Blobfs.

#pragma once

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <zircon/types.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

// clang-format off

namespace blobfs {
constexpr uint64_t kBlobfsMagic0  = (0xac2153479e694d21ULL);
constexpr uint64_t kBlobfsMagic1  = (0x985000d4d4d3d314ULL);
constexpr uint32_t kBlobfsVersion = 0x00000006;

constexpr uint32_t kBlobFlagClean        = 1;
constexpr uint32_t kBlobFlagDirty        = 2;
constexpr uint32_t kBlobFlagFVM          = 4;
constexpr uint32_t kBlobfsBlockSize      = 8192;
constexpr uint32_t kBlobfsBlockBits      = (kBlobfsBlockSize * 8);
constexpr uint32_t kBlobfsBlockMapStart  = 1;
constexpr uint32_t kBlobfsInodeSize      = 64;
constexpr uint32_t kBlobfsInodesPerBlock = (kBlobfsBlockSize / kBlobfsInodeSize);

constexpr size_t kFVMBlockMapStart  = 0x10000;
constexpr size_t kFVMNodeMapStart   = 0x20000;
constexpr size_t kFVMJournalStart   = 0x30000;
constexpr size_t kFVMDataStart      = 0x40000;

// Number of metadata blocks allocated for the whole journal - 1 info block.
constexpr uint32_t kJournalMetadataBlocks = 1;
// Number of metadata blocks allocated for each entry - 2 (header block, commit block).
constexpr uint32_t kEntryMetadataBlocks = 2;
// Maximum number of data blocks possible for a single entry - 4
// (blobfs superblock, inode table, block bitmap (up to 2)).
constexpr uint32_t kMaxEntryDataBlocks = 4;
// Minimum possible size for the journal, allowing the maximum size for one entry.
constexpr size_t kMinimumJournalBlocks = kJournalMetadataBlocks + kEntryMetadataBlocks +
                                         kMaxEntryDataBlocks;
constexpr size_t kDefaultJournalBlocks = 256;

constexpr uint64_t kBlobfsDefaultInodeCount = 32768;

constexpr size_t kMinimumDataBlocks = 2;

#ifdef __Fuchsia__
// Use a heuristics-based approach based on physical RAM size to
// determine the size of the writeback buffer.
//
// Currently, we set the writeback buffer size to 2% of physical
// memory.
//
// Should be invoked with caution; the size of the system's total
// memory may eventually change after boot.
inline size_t WriteBufferSize() {
    return fbl::round_up((zx_system_get_physmem() * 2) / 100, kBlobfsBlockSize);
}
#endif

constexpr uint64_t kJournalMagic = (0x626c6f626a726e6cULL);

// Notes:
// - block 0 is always allocated
// - inode 0 is never used, should be marked allocated but ignored

struct Superblock {
    uint64_t magic0;
    uint64_t magic1;
    uint32_t version;
    uint32_t flags;
    uint32_t block_size;          // 8K typical
    uint64_t data_block_count;    // Number of data blocks in this area
    uint64_t journal_block_count; // Number of journal blocks in this area
    uint64_t inode_count;         // Number of blobs in this area
    uint64_t alloc_block_count;   // Total number of allocated blocks
    uint64_t alloc_inode_count;   // Total number of allocated blobs
    uint64_t blob_header_next;    // Block containing next blobfs, or zero if this is the last one
    // The following fields are only valid with (flags & kBlobFlagFVM):
    uint64_t slice_size;          // Underlying slice size
    uint64_t vslice_count;        // Number of underlying slices
    uint32_t abm_slices;          // Slices allocated to block bitmap
    uint32_t ino_slices;          // Slices allocated to node map
    uint32_t dat_slices;          // Slices allocated to file data section
    uint32_t journal_slices;      // Slices allocated to journal section
};

static_assert(sizeof(Superblock) <= kBlobfsBlockSize, "Invalid blobfs superblock size");

// TODO(ZX-2729): Use counter instead of timestamp (for journal info block and entries).
struct JournalInfo {
    uint64_t magic;
    uint64_t start_block; // Block at which the first journal entry starts.
    uint64_t num_blocks; // Number of valid blocks currently contained in the journal.
    uint64_t timestamp; // Timestamp (in ticks) at which the info block was last written.
    uint32_t checksum; // crc32 checksum of the preceding contents of the info block.
};

static_assert(sizeof(JournalInfo) <= kBlobfsBlockSize, "Journal info size is too large");

struct HeaderBlock {
    uint64_t magic;
    uint64_t timestamp;
    uint64_t reserved;
    uint64_t num_blocks;
    uint64_t target_blocks[kMaxEntryDataBlocks];
};

static_assert(sizeof(HeaderBlock) <= kBlobfsBlockSize, "HeaderBlock size is too large");

struct CommitBlock {
    uint64_t magic;
    uint64_t timestamp; // Timestamp (in ticks) at which the journal entry was written.
    uint32_t checksum; // crc32 checksum of all preceding blocks in the entry.
};

static_assert(sizeof(CommitBlock) <= kBlobfsBlockSize, "CommitBlock size is too large");

constexpr uint64_t BlockMapStartBlock(const Superblock& info) {
    if (info.flags & kBlobFlagFVM) {
        return kFVMBlockMapStart;
    } else {
        return kBlobfsBlockMapStart;
    }
}

constexpr uint64_t BlockMapBlocks(const Superblock& info) {
    return fbl::round_up(info.data_block_count, kBlobfsBlockBits) / kBlobfsBlockBits;
}

constexpr uint64_t NodeMapStartBlock(const Superblock& info) {
    // Node map immediately follows the block map
    if (info.flags & kBlobFlagFVM) {
        return kFVMNodeMapStart;
    } else {
        // Node map immediately follows the block map.
        return BlockMapStartBlock(info) + BlockMapBlocks(info);
    }
}

constexpr uint64_t NodeBitmapBlocks(const Superblock& info) {
    return fbl::round_up(info.inode_count, kBlobfsBlockBits) / kBlobfsBlockBits;
}

constexpr uint64_t NodeMapBlocks(const Superblock& info) {
    return fbl::round_up(info.inode_count, kBlobfsInodesPerBlock) / kBlobfsInodesPerBlock;
}

constexpr uint64_t JournalStartBlock(const Superblock& info) {
    if (info.flags & kBlobFlagFVM) {
        return kFVMJournalStart;
    }

    // Journal immediately follows the node map.
    return NodeMapStartBlock(info) + NodeMapBlocks(info);
}

constexpr uint64_t JournalBlocks(const Superblock& info) {
    return info.journal_block_count;
}

constexpr uint64_t DataStartBlock(const Superblock& info) {
    if (info.flags & kBlobFlagFVM) {
        return kFVMDataStart;
    }

    // Data immediately follows the journal.
    return JournalStartBlock(info) + JournalBlocks(info);
}

constexpr uint64_t DataBlocks(const Superblock& info) {
    return info.data_block_count;
}

constexpr uint64_t TotalBlocks(const Superblock& info) {
    return BlockMapStartBlock(info) + BlockMapBlocks(info) + NodeMapBlocks(info)
           + JournalBlocks(info) + DataBlocks(info);
}

// States of 'Blob' identified via start block.
constexpr uint64_t kStartBlockFree     = 0;
constexpr uint64_t kStartBlockMinimum  = 1; // Smallest 'data' block possible.

// Identifies that the on-disk storage of the blob is LZ4 compressed.
constexpr uint32_t kBlobFlagLZ4Compressed = 0x00000001;

using digest::Digest;

struct Inode {
    uint8_t  merkle_root_hash[Digest::kLength];
    uint64_t start_block;
    uint64_t num_blocks;
    uint64_t blob_size;
    uint32_t flags;
    uint32_t reserved;
};

static_assert(sizeof(Inode) == kBlobfsInodeSize,
              "Blobfs Inode size is wrong");
static_assert(kBlobfsBlockSize % kBlobfsInodeSize == 0,
              "Blobfs Inodes should fit cleanly within a blobfs block");

// Number of blocks reserved for the blob itself
constexpr uint64_t BlobDataBlocks(const Inode& blobNode) {
    return fbl::round_up(blobNode.blob_size, kBlobfsBlockSize) / kBlobfsBlockSize;
}

} // namespace blobfs
