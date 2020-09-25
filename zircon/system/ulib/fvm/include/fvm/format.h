// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_FORMAT_H_
#define FVM_FORMAT_H_

#include <stdlib.h>
#include <string.h>

#include <array>
#include <limits>
#include <string>
#include <type_traits>

#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <gpt/gpt.h>

// FVM is a virtual volume manager that allows partitions inside of it to be dynamically sized. It
// allocates data to partitions in units of fixed-size "slices".
//
// It maintains two copies of its data so there is a previous version to roll-back to in case of
// corruption. The latest one is determined by looking at the generation numbers stored in the
// headers (see Header::generation).
//
// There are two main structures which follow the header: the partition table contains the
// information for each virtual partition, and the allocation table maps which slice of the device
// is allocated to which partition.
//
// It's possible for the FVM partition to be smaller than the underlying physical device. The
// current size used by FVM is stored in |fvm_partition_size| (the header does not store the size of
// the underlying device). As long as the allocation table has enough entries, the FVM partition may
// be dynamically expanded to use more of the underlying device.
//
//             +----------------------------------
// Block 0  -> | Header (primary)
//             | <padding>
//             +----------------------------------
// Block 1  -> | Partition table (starts at block boundary)
//       .     |
//       .     |   ... Table of VPartitionEntry ...
//       .     |
//             +----------------------------------
// Block X  -> | Allocation table (starts at block boundary)
//       .     |
//       .     |   ... Table of SliceEntry[pslice_count] ...
//       .     |
//             | <padding to next block boundary>
// Block Y  -> +----------------------------------     <- Metadata used bytes (block boundary).
//       .     |
//       .     |   <unused allocation table space>
//       .     |
//             +==================================     <- Metadata allocated bytes (block boundary).
// Block Z  -> | Header (secondary, starts at block boundary)
//             +----------------------------------
//             | Partition table (secondary)
//             +----------------------------------
//             | Allocation table (secondary)
//             +==================================
//             | Physical slice "1" data...

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

// Provides a placeholder instange GUID, that will be updated when a driver loads a partition with
// such instance GUID.
static constexpr std::array<uint8_t, kGuidSize> kPlaceHolderInstanceGuid = {0};

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
  // The partition table always starts at a block offset, and is always a multiple of blocks
  // long in bytes.
  size_t GetPartitionTableOffset() const;
  size_t GetPartitionTableEntryCount() const;
  size_t GetPartitionTableByteSize() const;

  // The allocation table begins on a block boundary after the partition table. It has a "used"
  // portion which are available for use by partitions (though they may not be used yet). Then it
  // has a potentially-larger "allocated" portion which allows FVM to grow assuming the underlying
  // device has room.
  size_t GetAllocationTableOffset() const;
  size_t GetAllocationTableUsedEntryCount() const;
  size_t GetAllocationTableUsedByteSize() const;
  size_t GetAllocationTableAllocatedEntryCount() const;
  size_t GetAllocationTableAllocatedByteSize() const;

  // Byte offset from the beginning of the Header to the allocation table entry (SliceEntry struct)
  // with the given ID. Physical slice IDs are 1-based so valid input is:
  //   1 <= pslice <= pslice_count
  // To get the actual slice data, see GetSliceOffset().
  size_t GetSliceEntryOffset(size_t pslice) const;

  // The metadata counts the header, partition table, and allocation table. The allocation table
  // may contain unused entries (allowing FVM to grow as long as there is space on the underlying
  // device), in which case the used bytes will be less than the allocated bytes.
  //
  // When the Header is default-initialized and the disk or slice size is 0, the metadata size
  // is defined to be 0.
  size_t GetMetadataUsedBytes() const;
  size_t GetMetadataAllocatedBytes() const;

  // Byte offset from the beginning of the device to the slice data. Physical slice IDs are 1-based
  // so valid input is:
  //   1 <= pslice <= pslice_count
  // This gets the actual slice data. To get the slice's allocation table entry, see
  // GetSliceEntryOffset().
  size_t GetSliceDataOffset(size_t pslice) const;

  // Data ------------------------------------------------------------------------------------------

  // Unique identifier for this format type. Expected to be kMagic.
  uint64_t magic;

  // Version of the format. The current version is kVersion.
  uint64_t version;

  // The number of physical slices which can be addressed and allocated by the virtual parititons.
  // This is the number of slices that will fit in the current fvm_partition_size, minus the size
  // of the two copies of the metadata at the beginning of the device.
  //
  // Physical slices are 1-indexed (0 means "none"). Therefore:
  //   1 <= |maximum valid pslice| <= pslice_count
  //
  // IMPORTANT NOTE: Due to fxbug.dev/59980, this value is one less than the number of entries worth of
  // space in the allocation table because there is an unused 0 entry. Always compute with
  // UsableSlicesCountOrZero() in fvm.cc to account for some edge conditions. See also
  // allocation_table_size below.
  uint64_t pslice_count;

  // Size of the each slice in bytes. Must be a multiple of kBlockSize.
  uint64_t slice_size;

  // Current size of the volume the fvm described by this header. This might be smaller than the
  // size of the underlying device (see comments at the top of the file). Must be a multiple of
  // kBlockSize.
  uint64_t fvm_partition_size;

  // Size in bytes of the partition table of the superblock the header describes. Must be a
  // multiple of kBlockSize.
  //
  // Currently this is fixed to be the size required to hold exactly kMaxVPartitions and various
  // code assumes this constant.
  // TODO(fxbug.dev/40192): Use this value so the partition table can have different sizes.
  uint64_t vpartition_table_size;

  // Size in bytes reserved for the allocation table. Must be a multiple of kBlockSize. This
  // includes extra space allowing the fvm to grow as the underlying volume grows. The
  // currently-used allocation table size, which defines the number of slices that can be addressed,
  // is derived from |pslice_count|.
  uint64_t allocation_table_size;

  // Use to determine over two copies(primary, secondary) of superblock, which one is the latest
  // one. This is incremented for each metadata change so the valid metadata with the largest
  // generation is the one to use.
  uint64_t generation;

  // Integrity check of the entire metadata (one copy). When computing the hash (use
  // fvm::UpdateHash() to compute), this field is is considered to be 0-filled.
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
  return fbl::round_up(sizeof(SliceEntry) * (total_size / slice_size), fvm::kBlockSize);
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

inline size_t Header::GetPartitionTableOffset() const {
  // The partition table starts at the first block after the header.
  return kBlockSize;
}

inline size_t Header::GetPartitionTableEntryCount() const {
  // Currently we expect the partition table count and size to be constant.
  // TODO(bug 40192): Derive this from the header so we can have different sizes.
  return kMaxVPartitions;
}

inline size_t Header::GetPartitionTableByteSize() const {
  // Currently we expect the partition table count and size to be constant.
  // TODO(bug 40192): Derive this from the header so we can have different sizes.
  return sizeof(VPartitionEntry) * kMaxVPartitions;
}

inline size_t Header::GetAllocationTableOffset() const {
  // The allocation table follows the partition table immediately.
  return GetPartitionTableOffset() + GetPartitionTableByteSize();
}

inline size_t Header::GetAllocationTableUsedEntryCount() const { return pslice_count; }

inline size_t Header::GetAllocationTableUsedByteSize() const {
  // Ensure the used allocation table byte size is always on a multiple of the block suize.
  return fbl::round_up(sizeof(SliceEntry) * GetAllocationTableUsedEntryCount(), kBlockSize);
}

inline size_t Header::GetAllocationTableAllocatedEntryCount() const {
  // The "-1" here allows for the unused 0 indexed slice.
  // TODO(fxbug.dev/59980) the allocation table is 0-indexed (with the 0th entry not used) while the
  // allocation data itself is 1-indexed. This inconsistency should be fixed,
  return GetAllocationTableAllocatedByteSize() / sizeof(SliceEntry) - 1;
}

inline size_t Header::GetAllocationTableAllocatedByteSize() const { return allocation_table_size; }

inline size_t Header::GetSliceEntryOffset(size_t pslice) const {
  // TODO(fxbug.dev/59980) the allocation table is 0-indexed (with the 0th entry not used) while the
  // allocation data itself is 1-indexed. This inconsistency should be fixed,
  return GetAllocationTableOffset() + pslice * sizeof(SliceEntry);
}

inline size_t Header::GetMetadataUsedBytes() const {
  if (fvm_partition_size == 0 || slice_size == 0)
    return 0;  // Uninitialized header.

  // The used metadata ends after the used portion of the allocation table.
  return GetAllocationTableOffset() + GetAllocationTableUsedByteSize();
}

inline size_t Header::GetMetadataAllocatedBytes() const {
  if (fvm_partition_size == 0 || slice_size == 0)
    return 0;  // Uninitialized header.

  // The metadata ends after the allocation table.
  return GetAllocationTableOffset() + GetAllocationTableAllocatedByteSize();
}

inline size_t Header::GetSliceDataOffset(size_t pslice) const {
  return 2 * GetMetadataAllocatedBytes() +  // Skip two copies of metadata at beginning of device.
         (pslice - 1) * slice_size;         // pslice is 1-based and starts after the 2x metadata.
}

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
