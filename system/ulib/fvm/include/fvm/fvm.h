// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <digest/digest.h>
#include <gpt/gpt.h>
#include <stdlib.h>

#define FVM_MAGIC (0x54524150204d5646ull) // 'FVM PART'
#define FVM_VERSION 0x00000001
#define FVM_SLICE_FREE 0
#define FVM_BLOCK_SIZE 8192lu
#define FVM_GUID_LEN GPT_GUID_LEN
#define FVM_GUID_STRLEN GPT_GUID_STRLEN
#define FVM_NAME_LEN 24

#ifdef __cplusplus

#include <mxtl/algorithm.h>

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

typedef struct {
    void init(const uint8_t* type_, const uint8_t* guid_, uint32_t slices_, const char* name_) {
        slices = slices_;
        memcpy(type, type_, FVM_GUID_LEN);
        memcpy(guid, guid_, FVM_GUID_LEN);
        memcpy(name, name_, FVM_NAME_LEN);
    }

    void clear() {
        memset(this, 0, sizeof(*this));
    }

    uint8_t type[FVM_GUID_LEN]; // Mirroring GPT value
    uint8_t guid[FVM_GUID_LEN]; // Mirroring GPT value
    uint32_t slices;            // '0' if unallocated
    uint32_t reserved;
    uint8_t name[FVM_NAME_LEN];
} vpart_entry_t;

static_assert(sizeof(vpart_entry_t) == 64, "Unexpected VPart entry size");
static_assert(FVM_BLOCK_SIZE % sizeof(vpart_entry_t) == 0,
              "VPart entries might cross block");
static_assert(sizeof(vpart_entry_t) * FVM_MAX_ENTRIES % FVM_BLOCK_SIZE == 0,
              "VPart entries don't cleanly fit within block");

typedef struct slice_entry {
    uint32_t vpart; // '0' if unallocated
    uint32_t vslice;
} slice_entry_t;

static_assert(sizeof(slice_entry_t) == 8, "Unexpected FVM slice entry size");
static_assert(FVM_BLOCK_SIZE % sizeof(slice_entry_t) == 0,
              "FVM slice entry might cross block");

constexpr size_t kVPartTableOffset = FVM_BLOCK_SIZE;
constexpr size_t kVPartTableLength = (sizeof(vpart_entry_t) * FVM_MAX_ENTRIES);
constexpr size_t kAllocTableOffset = kVPartTableOffset + kVPartTableLength;

constexpr size_t AllocTableLength(size_t total_size, size_t slice_size) {
    return mxtl::roundup(sizeof(slice_entry_t) * (total_size / slice_size),
                         FVM_BLOCK_SIZE);
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

constexpr size_t SliceStart(size_t total_size, size_t slice_size, size_t pslice) {
    return SlicesStart(total_size, slice_size) + (pslice - 1) * slice_size;
}

} // namespace fvm

// Update's the metadata's hash field to accurately reflect
// the contents of metadata.
void fvm_update_hash(void* metadata, size_t metadata_size);

// Validate the FVM header information, and identify which
// copy of metadata (primary or backup) should be used for
// initial reading, if either.
//
// "out" is an optional output parameter which is equal to a
// valid copy of either metadata or backup on success.
mx_status_t fvm_validate_header(const void* metadata, const void* backup,
                                size_t metadata_size, const void** out);

// Format a block device to be an empty FVM.
mx_status_t fvm_init(int fd, size_t slice_size);

#endif //  __cplusplus
