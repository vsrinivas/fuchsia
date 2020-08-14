// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_FORMAT_H_
#define FVM_FORMAT_H_

#include <stdlib.h>
#include <string.h>

#include <limits>
#include <string>
#include <type_traits>

#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <gpt/gpt.h>

namespace fvm {

// Unique identifier mapped to a GPT partition that contains a FVM.
static constexpr uint64_t kMagic = 0x54524150204d5646;

// Current version of the FVM format being handled by this library.
static constexpr uint64_t kVersion = 0x00000001;

// Defines the block size of that the FVM driver exposes.
static constexpr uint64_t kBlockSize = 8192;

// Maximum number of virtual partitions that can be created.
static constexpr uint64_t kMaxVPartitions = 1024;

// Maximum size for a partition GUID.
static constexpr uint64_t kGuidSize = GPT_GUID_LEN;

// Maximum string length for virtual partition GUID.
static constexpr uint64_t kGuidStrLen = GPT_GUID_STRLEN;

// Maximum length allowed for a virtual partition name.
static constexpr uint16_t kMaxVPartitionNameLength = 24;

// Number of bits required for the VSlice address space.
static constexpr uint64_t kSliceEntryVSliceBits = 32;

// Number of bits required for the VPartition address space.
static constexpr uint64_t kSliceEntryVPartitionBits = 16;

// Maximum number of VSlices that can be addressed.
static constexpr uint64_t kMaxVSlices = 1ull << (kSliceEntryVSliceBits - 1);

namespace internal {
// Minimal safety checks for persisted data structures. Currently they are required to be trivial
// and standard layout.
template <typename T>
struct is_persistable {
  static constexpr bool value = std::is_standard_layout<T>::value && std::is_trivial<T>::value;
};

// FVM block alignment properties for a given type.
template <typename T>
struct block_alignment {
  static constexpr bool may_cross_boundary = (kBlockSize % sizeof(T)) != 0;
  static constexpr bool ends_at_boundary = (sizeof(T) % kBlockSize);
};
}  // namespace internal

// FVM header which describes the contents and layout of the volume manager.
struct Header {
  // Unique identifier for this format type.
  uint64_t magic;
  // Version of the format.
  uint64_t version;
  // Number of slices which can be addressed and allocated by the virtual parititons.
  uint64_t pslice_count;
  // Size of the each slice in size.
  uint64_t slice_size;
  // Size of the volume the fvm described by this header is expecting to interact with.
  uint64_t fvm_partition_size;
  // Size of the partition table of the superblock the header describes, which contains
  // partition metadata.
  uint64_t vpartition_table_size;
  // Size of the allocation table allocated size. This includes extra space allowing the
  // fvm to grow as the underlying volume grows. The actual allocation table size, which defines
  // the number of slices that can be addressed, is determined by |fvm_partition_size|.
  uint64_t allocation_table_size;
  // Use to determine over two copies(primary, secondary) of superblock, which one is the latest
  // one.
  uint64_t generation;
  // Integrity check.
  uint8_t hash[digest::kSha256Length];
  // Fill remainder of the block.
  uint8_t reserved[0];
};

static_assert(internal::is_persistable<Header>::value && sizeof(Header) <= kBlockSize,
              "fvm::Header must fit within one block, be trivial and match standard layout.");

// Represent an entry in the FVM Partition table, which is fixed size contiguous flat buffer.
struct VPartitionEntry {
  // std::string_view's constructor is not explicit and because of past confusion, we want to make
  // sure nobody accidentally initialises with a char* that isn't NULL terminated.
  struct Name {
    explicit Name(std::string_view name) : name(name) {}

    template <size_t N>
    explicit constexpr Name(const uint8_t (&array)[N])
        : name(std::string_view(reinterpret_cast<const char*>(&array[0]),
                                std::find(&array[0], &array[N], 0) - &array[0])) {}

    std::string_view name;
  };

  // Returns a new blank entry.
  static VPartitionEntry Create() {
    VPartitionEntry entry;
    entry.Release();
    return entry;
  }

  // Returns a new entry with a the respective |type|, |guid|, |name|, |slices| and |flags|.
  // Note: This is subject to NRVO.
  static VPartitionEntry Create(const uint8_t* type, const uint8_t* guid, uint32_t slices,
                                Name name, uint32_t flags);

  // Returns the allowed set of flags in |raw_flags|.
  static uint32_t ParseFlags(uint32_t raw_flags);

  // Returns true if the entry is allocated.
  bool IsAllocated() const;

  // Returns true if the entry is free.
  bool IsFree() const;

  // Releases the partition, marking it as free.
  void Release();

  // Returns true if the partition is should be treated as active..
  bool IsActive() const;

  // Returns true if the partition is flagged as inactive.
  bool IsInactive() const;

  // Marks this entry active status as |is_active|.
  void SetActive(bool is_active);

  std::string name() const { return std::string(Name(unsafe_name).name); }

  // Mirrors GPT value.
  uint8_t type[kGuidSize];

  // Mirrors GPT value.
  uint8_t guid[kGuidSize];

  // Number of allocated slices.
  uint32_t slices;

  uint32_t flags;

  // Partition name. This is not necessarily NULL terminated. Prefer to use the name() accessor
  // above.
  uint8_t unsafe_name[fvm::kMaxVPartitionNameLength];
};

// TODO(gevalentino): remove after updating callsites.
using vpart_entry_t = VPartitionEntry;

static_assert(sizeof(VPartitionEntry) == 64, "Unchecked VPartitionEntry size change.");
static_assert(internal::is_persistable<VPartitionEntry>::value,
              "VPartitionEntry must be standard layout compilant and trivial.");
static_assert(!internal::block_alignment<VPartitionEntry>::may_cross_boundary,
              "VPartitionEntry must not cross block boundary.");
static_assert(!internal::block_alignment<VPartitionEntry[kMaxVPartitions]>::ends_at_boundary,
              "VPartitionEntry table max size must end at block boundary.");

// A Slice Entry represents the allocation of a slice.
//
// Slice Entries are laid out in an array on disk.  The index into this array
// determines the "physical slice" being accessed, where physical slices consist
// of all disk space immediately following the FVM metadata on an FVM partition.
struct SliceEntry {
  // Returns a new blank entry.
  static SliceEntry Create() {
    SliceEntry entry;
    entry.Release();
    return entry;
  }

  // Returns a slice entry with vpartition and vslice set.
  static SliceEntry Create(uint64_t vpartition, uint64_t vslice);

  // Returns true if this slice is assigned to a partition.
  bool IsAllocated() const;

  // Returns true if this slice is unassigned.
  bool IsFree() const;

  // Resets the slice entry, marking it as Free.
  void Release();

  // Returns the |vpartition| that owns this slice.
  uint64_t VPartition() const;

  // Returns the |vslice| of this slice. This represents the relative order of the slices
  // assigned to |vpartition|. This is, the block device exposed to |partition| sees an array of
  // all slices assigned to it, sorted by |vslice|.
  uint64_t VSlice() const;

  // Sets the contents of the slice entry to |partition| and |slice|.
  void Set(uint64_t vpartition, uint64_t vslice);

  // Packed entry, the format must remain obscure to the user.
  uint64_t data;
};

using slice_entry_t = SliceEntry;

static_assert(sizeof(SliceEntry) == 8, "Unchecked SliceEntry size change.");
static_assert(internal::is_persistable<SliceEntry>::value,
              "VSliceEntry must meet persistable constraints.");
static_assert(!internal::block_alignment<SliceEntry>::may_cross_boundary,
              "VSliceEntry must not cross block boundary.");

// Partition Table.
// TODO(gevalentino): Upgrade this into a class that provides a view into a an unowned buffer, so
// the logic for calculating offsets and accessing respective entries is hidden.
struct PartitionTable {
  // The Partition table starts at the next block after the respective header.
  static constexpr uint64_t kOffset = kBlockSize;

  // The Partition table size will finish at a block boundary, which is determined by the maximum
  // allowed number of partitions.
  static constexpr uint64_t kLength = sizeof(VPartitionEntry) * kMaxVPartitions;
};

// Allocation Table.
// TODO(gevalentino): Upgrade this into a class that provides a view into a an unowned buffer, so
// the logic for calculating offsets and accessing respective entries is hidden.
struct AllocationTable {
  // The allocation table offset with respect to the start of the header.
  static constexpr uint64_t kOffset = PartitionTable::kOffset + PartitionTable::kLength;

  // Returns an over estimation of size required to allocate all slices in a fvm_volume of
  // |fvm_disk_size| with a given |slice_size|. The returned value is always rounded to the next
  // block boundary.
  static constexpr uint64_t Length(size_t fvm_disk_size, size_t slice_size) {
    return fbl::round_up(sizeof(SliceEntry) * (fvm_disk_size / slice_size), kBlockSize);
  }
};

// Remove this.
constexpr size_t kVPartTableOffset = PartitionTable::kOffset;
constexpr size_t kVPartTableLength = PartitionTable::kLength;
constexpr size_t kAllocTableOffset = AllocationTable::kOffset;

constexpr size_t AllocTableLength(size_t total_size, size_t slice_size) {
  return fbl::round_up(sizeof(slice_entry_t) * (total_size / slice_size), fvm::kBlockSize);
}

constexpr size_t MetadataSize(size_t total_size, size_t slice_size) {
  return kAllocTableOffset + AllocTableLength(total_size, slice_size);
}

constexpr size_t BackupStart(size_t total_size, size_t slice_size) {
  return MetadataSize(total_size, slice_size);
}

constexpr size_t SlicesStart(size_t total_size, size_t slice_size) {
  return 2 * MetadataSize(total_size, slice_size);
}

constexpr size_t UsableSlicesCount(size_t total_size, size_t slice_size) {
  return (total_size - SlicesStart(total_size, slice_size)) / slice_size;
}

constexpr size_t BlocksToSlices(size_t slice_size, size_t block_size, size_t block_count) {
  if (slice_size == 0 || slice_size < block_size) {
    return 0;
  }

  const size_t kBlocksPerSlice = slice_size / block_size;
  return (block_count + kBlocksPerSlice - 1) / kBlocksPerSlice;
}

constexpr size_t SlicesToBlocks(size_t slice_size, size_t block_size, size_t slice_count) {
  return slice_count * slice_size / block_size;
}

// Defines the type of superblocks of an FVM. The key difference is how the offset from the
// beginning is calculated. They both share the same format.
enum class SuperblockType {
  kPrimary,
  kSecondary,
};

}  // namespace fvm

// Following describes the android sparse format

constexpr uint32_t kAndroidSparseHeaderMagic = 0xed26ff3a;

struct AndroidSparseHeader {
  const uint32_t kMagic = kAndroidSparseHeaderMagic;
  const uint16_t kMajorVersion = 0x1;
  const uint16_t kMinorVersion = 0x0;
  uint16_t file_header_size = 0;
  uint16_t chunk_header_size = 0;
  uint32_t block_size = 0;
  uint32_t total_blocks = 0;
  uint32_t total_chunks = 0;
  // CRC32 checksum of the original data, including dont-care chunk
  uint32_t image_checksum = 0;
};

enum AndroidSparseChunkType : uint16_t {
  kChunkTypeRaw = 0xCAC1,
  kChunkTypeFill = 0xCAC2,
  kChunkTypeDontCare = 0xCAC3,
};

struct AndroidSparseChunkHeader {
  AndroidSparseChunkType chunk_type;
  uint16_t reserved1 = 0;
  // In the unit of blocks
  uint32_t chunk_blocks = 0;
  // In the unit of bytes
  uint32_t total_size = 0;
};

#endif  //  FVM_FORMAT_H_
