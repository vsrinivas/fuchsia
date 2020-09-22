// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the on-disk structure of Blobfs.

#ifndef SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_FORMAT_H_
#define SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_FORMAT_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <algorithm>
#include <limits>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fs/journal/format.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

namespace blobfs {

// clang-format off
constexpr uint64_t kBlobfsMagic0  = (0xac2153479e694d21ULL);
constexpr uint64_t kBlobfsMagic1  = (0x985000d4d4d3d314ULL);
constexpr uint32_t kBlobfsVersion = 0x00000008;

constexpr uint32_t kBlobFlagClean          = 1;
constexpr uint32_t kBlobFlagFVM            = 4;
constexpr uint32_t kBlobfsBlockSize        = 8192;
constexpr uint32_t kBlobfsBlockBits        = (kBlobfsBlockSize * 8);
constexpr uint32_t kBlobfsSuperblockBlocks = 1;
constexpr uint32_t kBlobfsBlockMapStart    = 1;
constexpr uint32_t kBlobfsInodeSize        = 64;
constexpr uint32_t kBlobfsInodesPerBlock   = (kBlobfsBlockSize / kBlobfsInodeSize);
constexpr uint64_t kBlobfsMaxFileSize      = kBlobfsBlockSize * sizeof(uint32_t);

// Known Blobfs metadata locations. Unit of the location is blobfs block.
constexpr size_t kSuperblockOffset = 0;

// Blobfs block offset of various filesystem structures, when using the FVM.
constexpr size_t kFVMBlockMapStart = 0x10000;
constexpr size_t kFVMNodeMapStart  = 0x20000;
constexpr size_t kFVMJournalStart  = 0x30000;
constexpr size_t kFVMDataStart     = 0x40000;
// clang-format on

// Maximum number of data blocks possible for a single entry:
// - Blobfs Superblock
// - Inode Table Blocks
// - Block Bitmap Blocks
// TODO(fxbug.dev/32911): Calculate the actual upper bound here; this number is not
// necessarily considering the worst cases of fragmentation.
constexpr uint32_t kMaxEntryDataBlocks = 64;

// Minimum possible size for the journal, allowing the maximum size for one entry.
constexpr size_t kMinimumJournalBlocks =
    fs::kJournalMetadataBlocks + fs::kEntryMetadataBlocks + kMaxEntryDataBlocks;

// This serves as both default journal size and as minimum journal size.
// This value is somewhat arbitrarily chosen. It is large enough to allow
// us to run transactions and still small so that resources spent on
// journals are limited. Mkfs can override this value.
constexpr size_t kDefaultJournalBlocks = std::max(kMinimumJournalBlocks, static_cast<size_t>(16));

// This serves as both default inode count when mkfs arguments do not specify
// inode count and as absolute minimum inodes allowed in the fs.
// This value is somewhat arbitrarily chosen. It is large enough to allow us
// to create a few blobs and still small so that resources spent on inodes
// are limited. Mkfs can override this value.
constexpr uint64_t kBlobfsDefaultInodeCount = 10240;

constexpr size_t kMinimumDataBlocks = 2;

#ifdef __Fuchsia__
// Returns the size of the writeback buffer in filesystem blocks.
inline size_t WriteBufferSize() {
  // Hardcoded to 10 MB; may be replaced by a more device-specific option
  // in the future.
  return 10 * (1 << 20) / kBlobfsBlockSize;
}
#endif

struct __PACKED Superblock {
  uint64_t magic0;
  uint64_t magic1;
  uint32_t version;
  uint32_t flags;
  uint32_t block_size;           // 8K typical.
  uint32_t reserved1;            // Unused, reserved (for padding).
  uint64_t data_block_count;     // Number of data blocks in this area.
  uint64_t journal_block_count;  // Number of journal blocks in this area.
  uint64_t inode_count;          // Number of blobs in this area.
  uint64_t alloc_block_count;    // Total number of allocated blocks.
  uint64_t alloc_inode_count;    // Total number of allocated blobs and container nodes.
  // NOTE: prior to https://fuchsia-review.googlesource.com/c/fuchsia/+/404619, |reserved2| was
  // explicitly required to be zero. This field may be used for other purposes, but doing so is a
  // backwards-incompatible change.
  uint64_t reserved2;  // Unused.

  // The following fields are only valid with (flags & kBlobFlagFVM):
  uint64_t slice_size;      // Underlying slice size.
  uint64_t vslice_count;    // Number of underlying slices.
  uint32_t abm_slices;      // Slices allocated to block bitmap.
  uint32_t ino_slices;      // Slices allocated to node map.
  uint32_t dat_slices;      // Slices allocated to file data section.
  uint32_t journal_slices;  // Slices allocated to journal section.
  uint8_t reserved[8080];
};

static_assert(sizeof(Superblock) == kBlobfsBlockSize, "Invalid blobfs superblock size");

constexpr uint64_t SuperblockBlocks(const Superblock& info) { return kBlobfsSuperblockBlocks; }

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

constexpr uint64_t JournalBlocks(const Superblock& info) { return info.journal_block_count; }

constexpr uint64_t DataStartBlock(const Superblock& info) {
  if (info.flags & kBlobFlagFVM) {
    return kFVMDataStart;
  }

  // Data immediately follows the journal.
  return JournalStartBlock(info) + JournalBlocks(info);
}

constexpr uint64_t DataBlocks(const Superblock& info) { return info.data_block_count; }

constexpr uint64_t TotalNonDataBlocks(const Superblock& info) {
  return SuperblockBlocks(info) + BlockMapBlocks(info) + NodeMapBlocks(info) + JournalBlocks(info);
}

constexpr uint64_t TotalBlocks(const Superblock& info) {
  return TotalNonDataBlocks(info) + DataBlocks(info);
}

// States of 'Blob' identified via start block.
constexpr uint64_t kStartBlockMinimum = 1;  // Smallest 'data' block possible.

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
  Extent() = default;
  Extent(BlockOffsetType start, BlockCountType length) {
    SetStart(start);
    SetLength(length);
  }

  BlockOffsetType Start() const { return data_ & kBlockOffsetMask; }

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
  uint64_t data_ = 0;
};

static_assert(sizeof(Extent) == sizeof(uint64_t), "Extent class should only contain data");

// The number of extents within a single blob.
typedef uint16_t ExtentCountType;

// The largest number of extents which can compose a blob.
constexpr size_t kMaxBlobExtents = std::numeric_limits<ExtentCountType>::max();

// The largest node id representable in a node list.
constexpr uint32_t kMaxNodeId = 0xffffffffu;

// Identifies that the node is allocated.
// Both inodes and extent containers can be allocated.
constexpr uint16_t kBlobFlagAllocated = 1 << 0;

// Identifies that the on-disk storage of the blob is LZ4 compressed.
constexpr uint16_t kBlobFlagLZ4Compressed = 1 << 1;

// Identifies that this node is a container for extents.
constexpr uint16_t kBlobFlagExtentContainer = 1 << 2;

// Identifies that the on-disk storage of the blob is ZSTD compressed.
constexpr uint16_t kBlobFlagZSTDCompressed = 1 << 3;

// Identifies that the on-disk storage of the blob is ZSTD-seekable compressed.
constexpr uint16_t kBlobFlagZSTDSeekableCompressed = 1 << 4;

// Identifies that the on-disk storage of the blob is chunk-compression compressed.
constexpr uint16_t kBlobFlagChunkCompressed = 1 << 5;
// When adding another compression flag, it must be added to
// kBlobFlagMaskAnyCompression below.

// Bitmask of all compression flags.
constexpr uint16_t kBlobFlagMaskAnyCompression =
    (kBlobFlagLZ4Compressed | kBlobFlagZSTDCompressed | kBlobFlagZSTDSeekableCompressed |
     kBlobFlagChunkCompressed);

// The number of extents within a normal inode.
constexpr uint32_t kInlineMaxExtents = 1;
// The number of extents within an extent container node.
constexpr uint32_t kContainerMaxExtents = 6;

struct __PACKED NodePrelude {
  uint16_t flags;
  uint16_t version;
  // The next node containing this blob's extents.
  // Should not be used or read if there are no more extents.
  uint32_t next_node;

  bool IsAllocated() const { return flags & kBlobFlagAllocated; }

  bool IsExtentContainer() const { return flags & kBlobFlagExtentContainer; }

  bool IsInode() const { return !IsExtentContainer(); }
};

struct ExtentContainer;

struct __PACKED alignas(8) Inode {
  NodePrelude header;
  uint8_t merkle_root_hash[digest::kSha256Length];
  uint64_t blob_size;

  // The total number of Blocks used to represent this blob.
  uint32_t block_count;
  // The total number of Extent objects necessary to represent this blob.
  // Identifies when to stop iterating through the node list.
  ExtentCountType extent_count;
  uint16_t reserved;
  Extent extents[kInlineMaxExtents];

  ExtentContainer* AsExtentContainer() { return reinterpret_cast<ExtentContainer*>(this); }

  bool IsCompressed() const { return header.flags & kBlobFlagMaskAnyCompression; }
};

struct __PACKED alignas(8) ExtentContainer {
  NodePrelude header;
  // The map index of the previous node.
  uint32_t previous_node;
  // The number of extents within this container.
  ExtentCountType extent_count;
  uint16_t reserved;
  Extent extents[kContainerMaxExtents];
};

static_assert(sizeof(Inode) == sizeof(ExtentContainer), "Extent nodes must be as large as inodes");
static_assert(sizeof(Inode) == kBlobfsInodeSize, "Blobfs Inode size is wrong");
static_assert(kBlobfsBlockSize % kBlobfsInodeSize == 0,
              "Blobfs Inodes should fit cleanly within a blobfs block");

// Number of blocks reserved for the blob itself
constexpr uint64_t BlobDataBlocks(const Inode& blobNode) {
  return fbl::round_up(blobNode.blob_size, kBlobfsBlockSize) / kBlobfsBlockSize;
}

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_FORMAT_H_
