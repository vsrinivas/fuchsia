// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the on-disk structure of Blobfs.

#ifndef BLOBFS_FORMAT_H_
#define BLOBFS_FORMAT_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <algorithm>
#include <limits>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

// clang-format off

namespace blobfs {
constexpr uint64_t kBlobfsMagic0  = (0xac2153479e694d21ULL);
constexpr uint64_t kBlobfsMagic1  = (0x985000d4d4d3d314ULL);
constexpr uint32_t kBlobfsVersion = 0x00000007;

constexpr uint32_t kBlobFlagClean        = 1;
constexpr uint32_t kBlobFlagDirty        = 2;
constexpr uint32_t kBlobFlagFVM          = 4;
constexpr uint32_t kBlobfsBlockSize      = 8192;
constexpr uint32_t kBlobfsBlockBits      = (kBlobfsBlockSize * 8);
constexpr uint32_t kBlobfsSuperblockBlocks = 1;
constexpr uint32_t kBlobfsBlockMapStart  = 1;
constexpr uint32_t kBlobfsInodeSize      = 64;
constexpr uint32_t kBlobfsInodesPerBlock = (kBlobfsBlockSize / kBlobfsInodeSize);

// Known Blobfs metadata locations. Unit of the location is blobfs block.
constexpr size_t kSuperblockOffset  = 0;

// Blobfs block offset of various filesystem structures, when using the FVM.
constexpr size_t kFVMBlockMapStart  = 0x10000;
constexpr size_t kFVMNodeMapStart   = 0x20000;
constexpr size_t kFVMJournalStart   = 0x30000;
constexpr size_t kFVMDataStart      = 0x40000;

// Number of metadata blocks allocated for the whole journal: 1 info block.
constexpr uint32_t kJournalMetadataBlocks = 1;
// Number of metadata blocks allocated for each entry: 2 (header block, commit block).
constexpr uint32_t kJournalEntryHeaderBlocks = 1;
constexpr uint32_t kJournalEntryCommitBlocks = 1;
constexpr uint32_t kEntryMetadataBlocks = kJournalEntryHeaderBlocks + kJournalEntryCommitBlocks;
// Maximum number of data blocks possible for a single entry:
// - Blobfs Superblock
// - Inode Table Blocks
// - Block Bitmap Blocks
// TODO(ZX-3076): Calculate the actual upper bound here; this number is not
// necessarily considering the worst cases of fragmentation.
constexpr uint32_t kMaxEntryDataBlocks = 64;
// Minimum possible size for the journal, allowing the maximum size for one entry.
constexpr size_t kMinimumJournalBlocks = kJournalMetadataBlocks + kEntryMetadataBlocks +
                                         kMaxEntryDataBlocks;
constexpr size_t kDefaultJournalBlocks = std::max(kMinimumJournalBlocks, static_cast<size_t>(256));

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
    uint32_t block_size;          // 8K typical.
    uint64_t data_block_count;    // Number of data blocks in this area.
    uint64_t journal_block_count; // Number of journal blocks in this area.
    uint64_t inode_count;         // Number of blobs in this area.
    uint64_t alloc_block_count;   // Total number of allocated blocks.
    uint64_t alloc_inode_count;   // Total number of allocated blobs and container nodes.
    uint64_t blob_header_next;    // Block containing next blobfs, or zero if this is the last one.
    // The following fields are only valid with (flags & kBlobFlagFVM):
    uint64_t slice_size;          // Underlying slice size.
    uint64_t vslice_count;        // Number of underlying slices.
    uint32_t abm_slices;          // Slices allocated to block bitmap.
    uint32_t ino_slices;          // Slices allocated to node map.
    uint32_t dat_slices;          // Slices allocated to file data section.
    uint32_t journal_slices;      // Slices allocated to journal section.
    uint8_t reserved[8080];
};

static_assert(sizeof(Superblock) == kBlobfsBlockSize, "Invalid blobfs superblock size");

struct JournalInfo {
    uint64_t magic;
    uint64_t start_block; // Block of first journal entry (relative to entries start).
    uint64_t num_blocks; // TODO(smklein): This will become unused with the new implementation.
    uint64_t timestamp; // Timestamp at which the info block was last written.
    uint32_t checksum; // crc32 checksum of the preceding contents of the info block.
};

static_assert(sizeof(JournalInfo) <= kBlobfsBlockSize, "Journal info size is too large");

// Journal V1 Structures:

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

// End Journal V1 Structures.

// Journal V2 Structures:

constexpr uint64_t kJournalEntryMagic = 0x696d616a75726e6cULL;

constexpr uint64_t kJournalPrefixFlagHeader = 1;
constexpr uint64_t kJournalPrefixFlagCommit = 2;
constexpr uint64_t kJournalPrefixFlagRevocation = 3;
constexpr uint64_t kJournalPrefixFlagMask = 0xF;

enum class JournalObjectType {
    kUnknown = 0,
    kHeader,
    kCommit,
    kRevocation,
};

// The prefix structure on both header blocks and commit blocks.
struct JournalPrefix {
    JournalObjectType ObjectType() const {
        switch (flags & kJournalPrefixFlagMask) {
        case kJournalPrefixFlagHeader:
            return JournalObjectType::kHeader;
        case kJournalPrefixFlagCommit:
            return JournalObjectType::kCommit;
        case kJournalPrefixFlagRevocation:
            return JournalObjectType::kRevocation;
        default:
            return JournalObjectType::kUnknown;
        }
    }

    // Must be |kJournalMagic|.
    uint64_t magic;
    // A monotonically increasing value. This entry will only be replayed if the JournalInfo
    // block contains a sequence number less than or equal to this value.
    uint64_t sequence_number;
    // Identifies the type of this journal object. See |GetJournalObjectType()|.
    uint64_t flags;
    uint64_t reserved;
};

// The maximum number of blocks which fit within a |JournalHeaderBlock|.
constexpr uint32_t kMaxBlockDescriptors = 679;

// Flags for JournalHeaderBlock.target_flags:

// Identifies that the journaled block begins with |kJournalEntryMagic|, which are replaced
// with zeros to avoid confusing replay logic.
constexpr uint32_t kJournalBlockDescriptorFlagEscapedBlock = 1;

struct JournalHeaderBlock {
    JournalPrefix prefix;
    // The number of blocks between this header and the following commit block.
    // [0, payload_blocks) are valid indices for |target_blocks| and |target_flags|.
    uint64_t payload_blocks;
    // The final location of the blocks within the payload.
    uint64_t target_blocks[kMaxBlockDescriptors];
    // Flags about each block within the payload.
    uint32_t target_flags[kMaxBlockDescriptors];
    uint32_t reserved;
};
static_assert(sizeof(JournalHeaderBlock) == kBlobfsBlockSize, "Invalid Header Block size");

struct JournalCommitBlock {
    JournalPrefix prefix;
    // CRC32 checksum of all prior blocks (not including commit block itself).
    uint32_t checksum;
};
static_assert(sizeof(JournalCommitBlock) <= kBlobfsBlockSize, "Commit Block is too large");

// End Journal V2 Structures.

constexpr uint64_t SuperblockBlocks(const Superblock& info) {
    return kBlobfsSuperblockBlocks;
}

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

constexpr uint64_t TotalNonDataBlocks(const Superblock& info) {
    return SuperblockBlocks(info) + BlockMapBlocks(info) + NodeMapBlocks(info)
           + JournalBlocks(info);
}

constexpr uint64_t TotalBlocks(const Superblock& info) {
    return TotalNonDataBlocks(info) + DataBlocks(info);
}

// States of 'Blob' identified via start block.
constexpr uint64_t kStartBlockMinimum  = 1; // Smallest 'data' block possible.

using digest::Digest;

typedef uint64_t BlockOffsetType;
constexpr size_t kBlockOffsetBits = 48;
constexpr BlockOffsetType kBlockOffsetMax = (1LLU << kBlockOffsetBits) - 1;
constexpr uint64_t kBlockOffsetMask = kBlockOffsetMax;

typedef uint16_t BlockCountType;
constexpr size_t kBlockCountBits = 16;
constexpr size_t kBlockCountMax = std::numeric_limits<BlockCountType>::max();
constexpr uint64_t kBlockCountMask = kBlockCountMax << kBlockOffsetBits;

class Extent {
public:
    Extent() : data_(0) {}
    Extent(BlockOffsetType start, BlockCountType length) : data_(0) {
        SetStart(start);
        SetLength(length);
    }

    BlockOffsetType Start() const {
        return data_ & kBlockOffsetMask;
    }

    void SetStart(BlockOffsetType start) {
        ZX_DEBUG_ASSERT(start <= kBlockOffsetMax);
        data_ = (data_ & ~kBlockOffsetMask) | (start & kBlockOffsetMask);
    }

    BlockCountType Length() const {
        return static_cast<BlockCountType>((data_ & kBlockCountMask) >> kBlockOffsetBits);
    }

    void SetLength(BlockCountType length) {
        data_ = (data_ & ~kBlockCountMask) | ((length & kBlockCountMax) << kBlockOffsetBits);
    }

    bool operator==(const Extent& rhs) const {
        return Start() == rhs.Start() && Length() == rhs.Length();
    }

private:
    uint64_t data_;
};

static_assert(sizeof(Extent) == sizeof(uint64_t), "Extent class should only contain data");

// The number of extents within a single blob.
typedef uint16_t ExtentCountType;

// The largest number of extents which can compose a blob.
constexpr size_t kMaxBlobExtents = std::numeric_limits<ExtentCountType>::max();

// Identifies that the node is allocated.
// Both inodes and extent containers can be allocated.
constexpr uint16_t kBlobFlagAllocated = 1 << 0;

// Identifies that the on-disk storage of the blob is LZ4 compressed.
constexpr uint16_t kBlobFlagLZ4Compressed = 1 << 1;

// Identifies that this node is a container for extents.
constexpr uint16_t kBlobFlagExtentContainer = 1 << 2;

// Identifies that the on-disk storage of the blob is ZSTD compressed.
constexpr uint16_t kBlobFlagZSTDCompressed = 1 << 3;

// The number of extents within a normal inode.
constexpr uint32_t kInlineMaxExtents = 1;
// The number of extents within an extent container node.
constexpr uint32_t kContainerMaxExtents = 6;

struct NodePrelude {
    uint16_t flags;
    uint16_t version;
    // The next node containing this blob's extents.
    // Zero if there are no more extents.
    uint32_t next_node;

    bool IsAllocated() const {
        return flags & kBlobFlagAllocated;
    }

    bool IsExtentContainer() const {
        return flags & kBlobFlagExtentContainer;
    }
};

struct ExtentContainer;

struct Inode {
    NodePrelude header;
    uint8_t  merkle_root_hash[Digest::kLength];
    uint64_t blob_size;

    // The total number of Blocks used to represent this blob.
    uint32_t block_count;
    // The total number of Extent objects necessary to represent this blob.
    ExtentCountType extent_count;
    uint16_t reserved;
    Extent extents[kInlineMaxExtents];

    ExtentContainer* AsExtentContainer() {
        return reinterpret_cast<ExtentContainer*>(this);
    }
};

struct ExtentContainer {
    NodePrelude header;
    // The map index of the previous node.
    uint32_t previous_node;
    // The number of extents within this container.
    ExtentCountType extent_count;
    uint16_t reserved;
    Extent extents[kContainerMaxExtents];
};

static_assert(sizeof(Inode) == sizeof(ExtentContainer),
              "Extent nodes must be as large as inodes");
static_assert(sizeof(Inode) == kBlobfsInodeSize,
              "Blobfs Inode size is wrong");
static_assert(kBlobfsBlockSize % kBlobfsInodeSize == 0,
              "Blobfs Inodes should fit cleanly within a blobfs block");

// Number of blocks reserved for the blob itself
constexpr uint64_t BlobDataBlocks(const Inode& blobNode) {
    return fbl::round_up(blobNode.blob_size, kBlobfsBlockSize) / kBlobfsBlockSize;
}

} // namespace blobfs

#endif  // BLOBFS_FORMAT_H_
