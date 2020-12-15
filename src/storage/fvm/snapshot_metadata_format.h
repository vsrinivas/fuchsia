// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_SNAPSHOT_METADATA_FORMAT_H_
#define SRC_STORAGE_FVM_SNAPSHOT_METADATA_FORMAT_H_

#include <stdint.h>

#include <string>

#include <digest/digest.h>

#include "src/storage/fvm/format.h"

// FVM supports A/B copies of metadata within FVM-managed partitions (see
// https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0005_blobfs_snapshots). This header
// defines the FVM-internal format which manages this A/B functionality.
//
// FVM stores this metadata within an internally defined vpartition. With the default configuration,
// this vpartition needs to be at least 32KiB large.
//
// The format of the metadata is as follows (with default offset values given on the left, although
// the offsets should be read from the header itself):
//
// 0x000  +----------------------------------
//        | Header (primary)
//        |   PartitionStateTableOffset ---------+
//        |   ExtentTypeTableOffset -------------|--+
//        | <padding>                            |  |
// 0x400  +----------------------------------    |  |
//        | Partition state table (primary) <----+  |
//        |   ... Table of PartitionState ...       |
//        | <padding>                               |
// 0xc00  +----------------------------------       |
//        | Extent type table (primary) <-----------+
//        |   ... Table of ExtentType ...
//        | <padding>
// 0x2000 +----------------------------------
//        | Secondary copy of all above
//        |   ...
// 0x8000 +----------------------------------
//
// Two copies of the metadata are stored for resilience. Similarly to how the primary FVM metadata
// is managed, the metadata is updated in an A/B fashion, with the header having the greatest
// generation number being the active copy.

namespace fvm {

static constexpr uint64_t kSnapshotMetadataMagic = 0x3573a2537a40b5b9;

// Current version of the format and the revision of the software. The format version determines
// backwards-compatibility. The revision should be incremented for any minor change in how data is
// stored (including format versions but also for anything minor) and does not imply anything about
// backwards compatibility. The revision is used to updated the oldest_revision field in the
// header.
//
// See //src/storage/docs/versioning.md for more.
static constexpr uint64_t kSnapshotMetadataCurrentFormatVersion = 0x1;
static constexpr uint64_t kSnapshotMetadataCurrentRevision = 0x1;

static constexpr size_t kSnapshotMetadataHeaderMaxSize = 1024;

// The second header is at a static, fixed offset. This is necessary so that we can find the second
// metadata copy even if the primary copy is corrupt.
static constexpr size_t kSnapshotMetadataSecondHeaderOffset = 16384;

// Min/max number of partition state entries.
// For now we clamp this to the max FVM partition table size, so the snapshot metadata can
// accommodate any FVM partition table. This occupies exactly 2KiB.
static constexpr uint32_t kSnapshotMetadataHeaderMinPartitions = fvm::kMaxVPartitions;
static constexpr uint32_t kSnapshotMetadataHeaderMaxPartitions = fvm::kMaxVPartitions;

// Min/max number of extent type entries.
// About 5KiB & 13KiB respectively, which would result in roughly 8KiB or 16KiB of total metadata
// including the header and a maximal partition state table.
static constexpr uint32_t kSnapshotMetadataHeaderMinExtentTypes = 213;  // About 5KiB
static constexpr uint32_t kSnapshotMetadataHeaderMaxExtentTypes = 554;  // About 13KiB

// Selects the copy of the metadata.
enum class SnapshotMetadataCopy {
  kPrimary,
  kSecondary,
};

struct SnapshotMetadataHeader {
  // Constructs a SnapshotMetadataHeader with minimum table sizes.
  SnapshotMetadataHeader();

  // Constructs a SnapshotMetadataHeader with the configured table sizes.
  // If either value exceeds the min/max, they will be clamped.
  // Some amount of space will be unused for padding.
  SnapshotMetadataHeader(uint32_t partition_state_table_entries,
                         uint32_t extent_type_table_entries);

  // Checks if the header is valid.
  bool IsValid(std::string& out_error) const;

  // Returns an offset relative to the header where the PartitionState table starts.
  uint64_t PartitionStateTableOffset() const { return partition_state_table_offset; }

  // Returns the number of entries in the PartitionState table.
  size_t PartitionStateTableNumEntries() const { return partition_state_table_entry_count; }

  // Returns the size of the PartitionState table in bytes.
  size_t PartitionStateTableSizeBytes() const;

  // Returns an offset relative to the header where the ExtentType table starts.
  uint64_t ExtentTypeTableOffset() const { return extent_type_table_offset; }

  // Returns the number of entries in the ExtentType table.
  size_t ExtentTypeTableNumEntries() const { return extent_type_table_entry_count; }

  // Returns the size of the ExtentType table in bytes.
  size_t ExtentTypeTableSizeBytes() const;

  // Returns the size, in bytes, of metadata allocated, including the header and both tables.
  size_t AllocatedMetadataBytes() const;

  // Returns the offset of the primary or secondary copy of the header, relative to the start of the
  // vpartition storing the metadata.
  static uint64_t HeaderOffset(SnapshotMetadataCopy copy);

  // Returns a stringified representation of the header, useful for debugging.
  std::string ToString() const;

  // Data ------------------------------------------------------------------------------------------

  // Unique identifier for the snapshot metadata header.
  uint64_t magic = kSnapshotMetadataMagic;

  // Version of the overall format. If this is larger than kSnapshotMetadataCurrentFormatVersion the
  // driver must not access the data. See also "oldest_revision" below and
  // //src/storage/docs/versioning.md.
  uint64_t format_version = kSnapshotMetadataCurrentFormatVersion;
  // The oldest revision of the software that has written to this FVM instance. When opening for
  // writes, the driver should check this and lower it if the current revision is lower than the
  // one stored in this header. This does not say anything about backwards-compatibility, that is
  // determined by format_version.
  //
  // See //src/storage/docs/versioning.md for more.
  uint64_t oldest_revision = kSnapshotMetadataCurrentRevision;

  // Use to determine over two copies(primary, secondary) of superblock, which one is the latest
  // one. This is incremented for each metadata write, so the valid metadata with the largest
  // generation is the one to use.
  uint64_t generation = 0;

  // Integrity check of the entire metadata (one copy). When computing the hash (use
  // fvm::UpdatSnapshotMetadataeHash() to compute), this field is is considered to be 0-filled.
  uint8_t hash[digest::kSha256Length] = {0};

  uint32_t partition_state_table_offset = 0;
  uint32_t partition_state_table_entry_count = 0;

  uint32_t extent_type_table_offset = 0;
  uint32_t extent_type_table_entry_count = 0;

  // Fill remainder of the block.
  uint8_t reserved[0];
};
static_assert(sizeof(SnapshotMetadataHeader) <= kSnapshotMetadataHeaderMaxSize);

// Per-partition snapshot state.
// For now, this struct is empty. This is intentional, we are simply reserving space for eventual
// flags and snapshot state to be stored on a per-partition level.
struct PartitionSnapshotState {
  PartitionSnapshotState() = default;

  // Marks the entry as unallocated.
  void Release();

  // Opaque data field.
  uint16_t data = 0;
};
static_assert(sizeof(PartitionSnapshotState) == 2);

enum class ExtentType : uint8_t {
  // Default type with implementation-defined semantics for A/B enabled partitions.
  kDefault = 0,

  // Slices in an A/B extent have two distinct copies. Both sub-partitions can write to their own
  // copy, but they can only read the other copy.
  kAB,

  // NOTE: As of 2020/11/19, this value is being preemptively defined to achieve future goals of the
  // Blobfs Snapshots RFC
  // (https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0005_blobfs_snapshots), but its
  // semantics will be the same as |kAB| for now. The rest of this comment describes the *future*
  // semantics of this value.
  // Slices in an A/B bitmap manage the allocation of an extent of type |kSharedData|. Both
  // sub-partitions can write to their own copy, but they can only read the other copy.
  // FVM will use these slices to determine if a write to a kSharedData slice is allowed, by
  // interpreting the data held in these slices as a block bitmap managing the allocation of blocks
  // in the corresponding |kSharedData| section (i.e., the i'th bit is set if the i'th block is
  // allocated). Note that interpreting this bitmap requires FVM knowing the block sizes of the data
  // in the partition, which has not been sorted out in the design yet.
  // At most one extent of type |kABBitmap| can exist per vpartition.
  kABBitmap,

  // Slices which are shared between both sub-partitions. Writes are permitted from either
  // sub-partition.
  kShared,

  // NOTE: As of 2020/11/19, this value is being preemptively defined to achieve future goals of the
  // Blobfs Snapshots RFC
  // (https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0005_blobfs_snapshots), but its
  // semantics will be the same as |kShared| for now. The rest of this comment describes the
  // *future* semantics of this value.
  // Slices which are shared between both sub-partitions. Writes are permitted from either
  // sub-partition, however FVM will prevent each sub-partition from overwriting a block that the
  // other sub-partition claims to have allocated as well (as determined by the |kABBitmap| extent).
  // At most one extent of type |kSharedData| can exist per vpartition, and a corresponding
  // |kABBitmap| must be defined.
  kSharedData,
  kMax = kSharedData,
};

// A description of the type of an extent of vslices in a vpartition.
struct SnapshotExtentType {
  SnapshotExtentType() = default;

  SnapshotExtentType(uint64_t vpart, uint64_t vslice_offset, uint64_t extent_length_slices,
                     ExtentType type);

  // If |extent_length_slices| is set to |kEndless|, the extent covers every slice after the offset.
  // At most one entry per partition can have an endless extent, and it should have the highest
  // offset.
  static constexpr uint64_t kEndless = 0;

  // Returns if the entry is free.
  bool IsFree() const;

  // Marks the entry as unallocated.
  void Release();

  // Offset into the vpartition where the extent begins.
  uint64_t vslice_offset = 0;

  // Length, in slices, of the extent. (|kEndless| means the extent is unbounded.)
  uint64_t extent_length_slices = kEndless;

  // Index of the partition the extent applies to.
  // This index matches both the PartitionSnapshotState table and the main FVM partition table.
  uint16_t vpartition_index = 0;

  // Type of the extent.
  ExtentType type = ExtentType::kDefault;

  // Unused padding. Set to zero.
  uint8_t padding[5];
};
static_assert(sizeof(SnapshotExtentType) == 24);

}  // namespace fvm

#endif  // SRC_STORAGE_FVM_SNAPSHOT_METADATA_FORMAT_H_
