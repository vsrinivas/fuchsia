// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __LIB_FVM_FORMAT_H__
#define __LIB_FVM_FORMAT_H__

#include <digest/digest.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus

#include <limits>
#include <type_traits>

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
} // namespace internal

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
    uint8_t hash[SHA256_DIGEST_LENGTH];
    // Fill remainder of the block.
    uint8_t reserved[0];
};

// TODO(gevalentino): Remove once all callsites have been updated.
using fvm_t = Header;

static_assert(internal::is_persistable<Header>::value && sizeof(Header) <= kBlockSize,
              "fvm::Header must fit within one block, be trivial and match standard layout.");

// Represent an entry in the FVM Partition table, which is fixed size contiguous flat buffer.
struct VPartitionEntry {

    // Returns a new blank entry.
    static VPartitionEntry Create() {
        VPartitionEntry entry;
        entry.Release();
        return entry;
    }

    // Returns a new entry with a the respective |type|, |guid|, |name|, |slices| and |flags|.
    // Note: This is subject to NRVO.
    static VPartitionEntry Create(const uint8_t* type, const uint8_t* guid, uint32_t slices,
                                  const char* name, uint32_t flags);

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

    // Mirrors GPT value.
    uint8_t type[kGuidSize];

    // Mirrors GPT value.
    uint8_t guid[kGuidSize];

    // Number of allocated slices.
    uint32_t slices;

    uint32_t flags;

    // Partition name.
    uint8_t name[fvm::kMaxVPartitionNameLength];
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
//
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
    constexpr uint64_t Length(size_t fvm_disk_size, size_t slice_size) {
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

// Helper class for obtaining information about the format of a FVM, such as superblock offsets,
// metadata size, allocated sizes, etc.
//
// This class is copyable and moveable.
class FormatInfo {
public:
    // Returns a FormatInfo from a Fvm header and a disk size.
    static FormatInfo FromSuperBlock(const fvm_t& superblock);

    // Assumes a superblock created from the given disk for the given disk size,
    // with slice size. (No Preallocated metadata headers for future growth).
    static FormatInfo FromDiskSize(size_t disk_size, size_t slice_size);

    // Without instantiating a SuperBlock, assumes that an fvm will be formatted initially with
    // |initial_size| and eventually will grow up to |max_size| with |slice_size|.
    static FormatInfo FromPreallocatedSize(size_t initial_size, size_t max_size, size_t slice_size);

    FormatInfo() = default;
    FormatInfo(const FormatInfo&) = default;
    FormatInfo(FormatInfo&&) = default;
    FormatInfo& operator=(const FormatInfo&) = default;
    FormatInfo& operator=(FormatInfo&&) = default;
    ~FormatInfo() = default;

    // Returns the size of the addressable metadata in a FVM header.
    size_t metadata_size() const { return metadata_size_; }

    // Returns the size of the allocated metadata SuperBlock. This may be bigger than
    // |metadata_size_| if there was extra space pre allocated for allowing fvm growing.
    size_t metadata_allocated_size() const { return metadata_allocated_size_; }

    // Returns the number of addressable slices for the superblock, this is the number
    // of physical slices.
    size_t slice_count() const { return slice_count_; }

    // Returns the size of each slice in the described block.
    size_t slice_size() const { return slice_size_; }

    // Returns the offset of the given superblock. The first superblock is considered primary,
    // in terms of position.
    size_t GetSuperblockOffset(SuperblockType type) const {
        return (type == SuperblockType::kPrimary) ? 0 : metadata_allocated_size();
    }

    // Returns the offset from the start of the disk to beginning of |pslice| physical slice.
    // Note: pslice is 1-indexed.
    size_t GetSliceStart(size_t pslice) const {
        return 2 * metadata_allocated_size_ + (pslice - 1) * slice_size_;
    }

    // Returns the maximum number of slices that can be addressed from the maximum possible size
    // of the metatadata.
    size_t GetMaxAllocatableSlices() const {
        return (metadata_allocated_size() - kAllocTableOffset) / sizeof(SliceEntry);
    }

    // Returns the maximum number of slices that the allocated metadata can address for a given
    // |disk_size|.
    size_t GetMaxAddressableSlices(uint64_t disk_size) const {
        size_t slice_count =
            std::min(GetMaxAllocatableSlices(), UsableSlicesCount(disk_size, slice_size_));
        // Because the allocation table is 1-indexed and pslices are 0 indexed on disk,
        // if the number of slices fit perfectly in the metadata, the allocated buffer won't be big
        // enough to address them all. This only happens when the rounded up block value happens to
        // match the disk size.
        // TODO(gevalentino): Fix underlying cause and remove workaround.
        if ((AllocationTable::kOffset + slice_count * sizeof(SliceEntry)) ==
            metadata_allocated_size()) {
            slice_count--;
        }
        return slice_count;
    }

    // Returns the maximum partition size the current metadata can grow to.
    size_t GetMaxPartitionSize() const {
        return GetSliceStart(1) + GetMaxAllocatableSlices() * slice_size_;
    }

private:
    // Size in bytes of addressable metadata.
    size_t metadata_size_ = 0;

    // Size in bytes of the allocated size of metadata.
    size_t metadata_allocated_size_ = 0;

    // Number of addressable slices.
    size_t slice_count_ = 0;

    // Size of the slices.
    size_t slice_size_ = 0;
};

} // namespace fvm

#endif //  __cplusplus

__BEGIN_CDECLS

// Update's the metadata's hash field to accurately reflect
// the contents of metadata.
void fvm_update_hash(void* metadata, size_t metadata_size);

// Validate the FVM header information, and identify which
// copy of metadata (primary or backup) should be used for
// initial reading, if either.
//
// "out" is an optional output parameter which is equal to a
// valid copy of either metadata or backup on success.
zx_status_t fvm_validate_header(const void* metadata, const void* backup, size_t metadata_size,
                                const void** out);

__END_CDECLS

#endif //  __LIB_FVM_FORMAT_H__
