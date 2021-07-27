// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the on-disk structure of Blobfs.

#ifndef SRC_STORAGE_BLOBFS_FORMAT_H_
#define SRC_STORAGE_BLOBFS_FORMAT_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <algorithm>
#include <limits>
#include <ostream>

#include <fbl/algorithm.h>
#include <fbl/macros.h>

#include "src/lib/storage/vfs/cpp/journal/format.h"

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

#include "src/lib/digest/digest.h"
#include "src/lib/digest/merkle-tree.h"

namespace blobfs {

// clang-format off
constexpr uint64_t kBlobfsMagic0  = (0xac2153479e694d21ULL);
constexpr uint64_t kBlobfsMagic1  = (0x985000d4d4d3d314ULL);

// Current version of the format. The major version determines backwards-compatibility. The minor
// version can be freely incremented at any time and does not impact backwards-compatibility; the
// more often it is updated, the more granularly we can find out what the oldest driver that has
// touched a filesystem instance.
//
// Minimally, the minor version should be incremented whenever a (backwards-compatible) format
// change is made, but it can also be incremented when major logic changes are made in case there is
// chance of bugs being introduced and we would like to be able to detect if the filesystem has been
// touched by a potentially buggy driver. The kBlobfsCurrentMinorVersion is used to updated the
// oldest_minor_version field in the header when it is opened.
//
// See //src/storage/docs/versioning.md for more.
//
// *************************************************************************************************
//
// IMPORTANT: When changing either kBlobfsCurrentMajorVersion or kBlobfsCurrentMinorVersion:
//
//   * Update //third_party/cobalt_config/fuchsia/local_storage/versions.txt
//     (submission order does not matter).
//
//   * Update //src/storage/blobfs/README.md with what changed.
//
// *************************************************************************************************

constexpr uint32_t kBlobfsCurrentMajorVersion = 0x00000009;

// When this next changes, consider enabling the OldestMinorVersionNotUpdated test.
constexpr uint64_t kBlobfsCurrentMinorVersion = 0x00000004;

// Version 9 introduced a compact merkle tree version. Version 8 uses padded merkle trees. This
// format is controlled by a build flag so version 9 is not necessarily newer (see README.md).
constexpr uint32_t kBlobfsCompactMerkleTreeVersion = 0x00000009;

// Revision 2: introduced a backup superblock.
constexpr uint64_t kBlobfsMinorVersionBackupSuperblock = 0x00000002;
// Revision 3: migrated away from old compression formats.
constexpr uint64_t kBlobfsMinorVersionNoOldCompressionFormats = 0x00000003;
// Revision 4: fixed host-side tool bug which generated a zero-length extent for the null blob.
constexpr uint64_t kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly = 0x00000004;

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

// Blobfs has a backup superblock but only with FVM.  The backup superblock only needs to be
// sufficient to get to the journal, since once there, either the primary superblock is valid or
// there's a pending write in the journal to the primary superblock.  Note that the backup
// superblock is not there for the purpose of solving random corruption issues i.e. a random
// corruption of the primary superblock will still render the volume unusable.  It only exists to
// guard against the potential for corruption whilst updating the primary superblock.  In practice,
// after a write to a device but before a successful flush, we will either see the data before the
// write or after the write and not some indeterminate state between so it's unlikely that the
// primary superblock wouldn't be readable, but if it's easy to do so, we should be resilient to the
// case where data is in an indeterminate state between write and flush, and we should have tests
// for this.  A backup superblock solves this issue.  Regarding random corruption: there are other
// blocks within a filesystem that could be corrupted and would have equally serious consequences,
// so if this is something to be addressed, it might benefit from a holistic solution, although one
// could argue that corruption of the first block on the device is more likely than other blocks.
// Note that there's no need to update the superblock when the primary superblock changes since its
// only purpose is to help locate the journal, so some aspects of the backup superblock are likely
// to be inconsistent.
constexpr size_t kFVMBackupSuperblockOffset = 1;

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

struct __PACKED alignas(8) Superblock {
  uint64_t magic0;
  uint64_t magic1;
  uint32_t major_version;
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

  // The following 6 fields are only valid with (flags & kBlobFlagFVM):
  uint64_t slice_size;      // Underlying slice size.
  uint64_t deprecated1;     // Unused but not necessarily 0 (saved total vslices in old vers.).
  uint32_t abm_slices;      // Slices allocated to block bitmap.
  uint32_t ino_slices;      // Slices allocated to node map.
  uint32_t dat_slices;      // Slices allocated to file data section.
  uint32_t journal_slices;  // Slices allocated to journal section.
  // End FVM-specific fields

  uint8_t zeroes[8];  // Padding. Set to zeroes, can be reclaimed.

  // The oldest minor version corresponding to the kBlobfsCurrentMinorVersion of the software that
  // has written to this blobfs instance. When opening for writes, the driver should check this and
  // lower it if the current revision is lower than the one stored in this header. This does not say
  // anything about backwards-compatibility, that is determined by major_version above.
  //
  // See //src/storage/docs/versioning.md for more.
  uint64_t oldest_minor_version;

  uint8_t reserved[8064];
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

using BlockOffsetType = uint64_t;
constexpr size_t kBlockOffsetBits = 48;
constexpr BlockOffsetType kBlockOffsetMax = (1LLU << kBlockOffsetBits) - 1;
constexpr uint64_t kBlockOffsetMask = kBlockOffsetMax;

using BlockCountType = uint16_t;
constexpr size_t kBlockCountBits = 16;
constexpr size_t kBlockCountMax = (1LLU << kBlockCountBits) - 1;
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

// This is inlined because format.cc only compiles on Fuchsia builds (not host).
inline std::ostream& operator<<(std::ostream& stream, const Extent& extent) {
  stream << "{start:" << extent.Start() << ", len:" << extent.Length() << "}";
  return stream;
}

template <size_t N>
inline std::ostream& operator<<(std::ostream& stream, const Extent (&extents)[N]) {
  stream << "[";
  for (size_t i = 0; i < N; ++i) {
    if (i > 0) {
      stream << ", ";
    }
    stream << extents[i];
  }
  stream << "]";
  return stream;
}

static_assert(sizeof(Extent) == sizeof(uint64_t), "Extent class should only contain data");

// The number of extents within a single blob.
using ExtentCountType = uint16_t;

// The largest number of extents which can compose a blob.
constexpr size_t kMaxBlobExtents = std::numeric_limits<ExtentCountType>::max();

// The largest node id representable in a node list.
constexpr uint32_t kMaxNodeId = 0xffffffffu;

// Identifies that the node is allocated.
// Both inodes and extent containers can be allocated.
constexpr uint16_t kBlobFlagAllocated = 1 << 0;

// Bits 1, 3, and 4 were used for obsolete compression flags. They were set only for old internal
// test builds and no devices were released with these flags. They can be assumed to be 0 if needed
// for future uses.

// Identifies that this node is a container for extents.
constexpr uint16_t kBlobFlagExtentContainer = 1 << 2;

// Identifies that the on-disk storage of the blob is chunk-compression compressed.
constexpr uint16_t kBlobFlagChunkCompressed = 1 << 5;
// When adding another compression flag, it must be added to kBlobFlagMaskAnyCompression below.

// Bitmask of all compression flags (this allows additional flags to be added more easily).
constexpr uint16_t kBlobFlagMaskAnyCompression = kBlobFlagChunkCompressed;

// This mask is the mask of all valid flag bits, but we should be tolerant if we encounter bits set
// on a filesystem in case the filesystem has been touched by a future version of blobfs.
constexpr uint16_t kBlobFlagMaskValid =
    kBlobFlagAllocated | kBlobFlagExtentContainer | kBlobFlagMaskAnyCompression;

// The number of extents within a normal inode.
constexpr uint32_t kInlineMaxExtents = 1;
// The number of extents within an extent container node.
constexpr uint32_t kContainerMaxExtents = 6;

constexpr uint16_t kBlobNodeVersion = 0;

struct __PACKED NodePrelude {
  uint16_t flags = 0;
  uint16_t version = kBlobNodeVersion;
  // The next node containing this blob's extents.
  // Should not be used or read if there are no more extents.
  uint32_t next_node = 0;

  bool IsAllocated() const { return flags & kBlobFlagAllocated; }

  bool IsExtentContainer() const { return flags & kBlobFlagExtentContainer; }

  bool IsInode() const { return !IsExtentContainer(); }
};

// This is inlined because format.cc only compiles on Fuchsia builds (not host).
inline std::ostream& operator<<(std::ostream& stream, const NodePrelude& prelude) {
  stream << "Node {allocated:" << prelude.IsAllocated() << " is_inode:" << prelude.IsInode()
         << " version:" << prelude.version << " next_node:" << prelude.next_node << "}";
  return stream;
}

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

// This is inlined because format.cc only compiles on Fuchsia builds (not host).
inline std::ostream& operator<<(std::ostream& stream, const Inode& inode) {
  digest::Digest d(inode.merkle_root_hash);
  stream << "Inode {header:" << inode.header << " merkle:" << d.ToString()
         << " blob_size:" << inode.blob_size << " block_count:" << inode.block_count
         << " extent_count:" << inode.extent_count << " extents:" << inode.extents << "}";
  return stream;
}

struct __PACKED alignas(8) ExtentContainer {
  NodePrelude header;

  // The map index of the previous node.
  uint32_t previous_node;

  // The number of extents within this container.
  ExtentCountType extent_count;

  uint16_t reserved;
  Extent extents[kContainerMaxExtents];
};

// This is inlined because format.cc only compiles on Fuchsia builds (not host).
inline std::ostream& operator<<(std::ostream& stream, const ExtentContainer& container) {
  stream << "ExtentContainer {header:" << container.header
         << " prev_node:" << container.previous_node << " extent_count:" << container.extent_count
         << " extents:" << container.extents << "}";
  return stream;
}

static_assert(sizeof(Inode) == sizeof(ExtentContainer), "Extent nodes must be as large as inodes");
static_assert(sizeof(Inode) == kBlobfsInodeSize, "Blobfs Inode size is wrong");
static_assert(kBlobfsBlockSize % kBlobfsInodeSize == 0,
              "Blobfs Inodes should fit cleanly within a blobfs block");

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_FORMAT_H_
