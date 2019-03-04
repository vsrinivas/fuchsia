// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <digest/digest.h>
#include <stdlib.h>
#include <string.h>

#define FVM_MAGIC (0x54524150204d5646ull) // 'FVM PART'
#define FVM_VERSION 0x00000001
#define FVM_SLICE_ENTRY_FREE 0
#define FVM_BLOCK_SIZE 8192lu
#define FVM_NAME_LEN 24

#ifdef __cplusplus

#include <fbl/algorithm.h>
#include <gpt/gpt.h>

#define FVM_GUID_LEN GPT_GUID_LEN
#define FVM_GUID_STRLEN GPT_GUID_STRLEN

namespace fvm {

typedef struct {
    uint64_t magic;
    uint64_t version;
    uint64_t pslice_count; // Slices which can be used by vpartitions
    uint64_t slice_size;   // All sizes in bytes
    uint64_t fvm_partition_size;
    uint64_t vpartition_table_size;
    uint64_t allocation_table_size;
    uint64_t generation;
    uint8_t hash[SHA256_DIGEST_LENGTH];
    uint8_t reserved[0]; // Up to the rest of the block
} fvm_t;

static_assert(sizeof(fvm_t) <= FVM_BLOCK_SIZE, "FVM Superblock too large");

#define FVM_MAX_ENTRIES 1024

// Identifies that the partition is inactive, and should be destroyed on
// reboot (unless activated before rebinding the FVM).
constexpr uint32_t kVPartFlagInactive = 0x00000001;
constexpr uint32_t kVPartAllocateMask = 0x00000001; // All acceptable flags to pass to allocate.

typedef struct {
    void init(const uint8_t* type_, const uint8_t* guid_, uint32_t slices_, const char* name_,
              uint32_t flags_) {
        slices = slices_;
        memcpy(type, type_, FVM_GUID_LEN);
        memcpy(guid, guid_, FVM_GUID_LEN);
        memcpy(name, name_, FVM_NAME_LEN);
        flags = flags_;
    }

    void clear() { memset(this, 0, sizeof(*this)); }

    uint8_t type[FVM_GUID_LEN]; // Mirroring GPT value
    uint8_t guid[FVM_GUID_LEN]; // Mirroring GPT value
    uint32_t slices;            // '0' if unallocated
    uint32_t flags;
    uint8_t name[FVM_NAME_LEN];
} vpart_entry_t;

static_assert(sizeof(vpart_entry_t) == 64, "Unexpected VPart entry size");
static_assert(FVM_BLOCK_SIZE % sizeof(vpart_entry_t) == 0, "VPart entries might cross block");
static_assert(sizeof(vpart_entry_t) * FVM_MAX_ENTRIES % FVM_BLOCK_SIZE == 0,
              "VPart entries don't cleanly fit within block");

#define VPART_BITS 16
#define VPART_MAX ((1UL << VPART_BITS) - 1)
#define VPART_MASK VPART_MAX

#define VSLICE_BITS 32
#define VSLICE_MAX ((1UL << VSLICE_BITS) - 1)
#define VSLICE_MASK ((VSLICE_MAX) << VPART_BITS)

#define RESERVED_BITS 16

#define PSLICE_UNALLOCATED 0

// A Slice Entry represents the allocation of a slice.
//
// Slice Entries are laid out in an array on disk.  The index into this array
// determines the "physical slice" being accessed, where physical slices consist
// of all disk space immediately following the FVM metadata on an FVM partition.
//
// The "Vpart" field describes which virtual partition allocated the slice.
// If this field is set to FVM_SLICE_ENTRY_FREE, the slice is not allocated.
//
// If the slice is allocated, the "Vslice" field describes which virtual slice
// within the virtual partition is using this slice.
typedef struct slice_entry {
    uint64_t data;

    // Vpart is set to 'FVM_SLICE_ENTRY_FREE' if unallocated.
    uint64_t Vpart() const;
    void SetVpart(uint64_t vpart);

    // Vslice is only valid if Vpart is not set to 'FVM_SLICE_ENTRY_FREE'.
    uint64_t Vslice() const;
    void SetVslice(uint64_t vslice);
} slice_entry_t;

static_assert(FVM_MAX_ENTRIES <= VPART_MAX, "vpart address space too small");
static_assert(sizeof(slice_entry_t) == 8, "Unexpected FVM slice entry size");
static_assert(FVM_BLOCK_SIZE % sizeof(slice_entry_t) == 0, "FVM slice entry might cross block");

constexpr size_t kVPartTableOffset = FVM_BLOCK_SIZE;
constexpr size_t kVPartTableLength = (sizeof(vpart_entry_t) * FVM_MAX_ENTRIES);
constexpr size_t kAllocTableOffset = kVPartTableOffset + kVPartTableLength;

constexpr size_t AllocTableLength(size_t total_size, size_t slice_size) {
    return fbl::round_up(sizeof(slice_entry_t) * (total_size / slice_size), FVM_BLOCK_SIZE);
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
