// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fs/mapped-vmo.h>
#include <magenta/device/block.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

#include "fvm/fvm.h"

namespace {

// Return true if g1 is greater than or equal to g2.
// Safe against integer overflow.
bool generation_ge(uint64_t g1, uint64_t g2) {
    if (g1 == UINT64_MAX && g2 == 0) {
        return false;
    } else if (g1 == 0 && g2 == UINT64_MAX) {
        return true;
    }
    return g1 >= g2;
}

// Validate the metadata's hash value.
// Returns 'true' if it matches, 'false' otherwise.
bool fvm_check_hash(const void* metadata, size_t metadata_size) {
    MX_DEBUG_ASSERT(metadata_size >= sizeof(fvm_t));
    const fvm_t* header = static_cast<const fvm_t*>(metadata);
    const void* metadata_after_hash =
        reinterpret_cast<const void*>(header->hash + sizeof(header->hash));
    uint8_t empty_hash[sizeof(header->hash)];
    memset(empty_hash, 0, sizeof(empty_hash));

    digest::Digest digest;
    digest.Init();
    digest.Update(metadata, offsetof(fvm_t, hash));
    digest.Update(empty_hash, sizeof(empty_hash));
    digest.Update(metadata_after_hash,
                  metadata_size - (offsetof(fvm_t, hash) + sizeof(header->hash)));
    digest.Final();
    return digest == header->hash;
}

} // namespace anonymous

void fvm_update_hash(void* metadata, size_t metadata_size) {
    fvm_t* header = static_cast<fvm_t*>(metadata);
    memset(header->hash, 0, sizeof(header->hash));
    digest::Digest digest;
    const uint8_t* hash = digest.Hash(metadata, metadata_size);
    memcpy(header->hash, hash, sizeof(header->hash));
}

mx_status_t fvm_validate_header(const void* metadata, const void* backup,
                                size_t metadata_size, const void** out) {
    const fvm_t* primary_header = static_cast<const fvm_t*>(metadata);
    const fvm_t* backup_header = static_cast<const fvm_t*>(backup);

    bool primary_valid = fvm_check_hash(metadata, metadata_size);
    bool backup_valid = fvm_check_hash(backup, metadata_size);

    // Decide if we should use the primary or the backup copy of metadata
    // for reading.
    bool use_primary;
    if (!primary_valid && !backup_valid) {
        fprintf(stderr, "fvm: Neither copy of metadata is valid\n");
        return MX_ERR_BAD_STATE;
    } else if (primary_valid && !backup_valid) {
        use_primary = true;
    } else if (!primary_valid && backup_valid) {
        use_primary = false;
    } else {
        use_primary = generation_ge(primary_header->generation, backup_header->generation);
    }

    const fvm_t* header = use_primary ? primary_header : backup_header;
    if (header->magic != FVM_MAGIC) {
        fprintf(stderr, "fvm: Bad magic\n");
        return MX_ERR_BAD_STATE;
    }
    if (header->version > FVM_VERSION) {
        fprintf(stderr, "fvm: Header Version does not match fvm driver\n");
        return MX_ERR_BAD_STATE;
    }

    // TODO(smklein): Additional validation....

    if (out) {
        *out = use_primary ? metadata : backup;
    }
    return MX_OK;
}

mx_status_t fvm_init(int fd) {
    // The metadata layout of the FVM is dependent on the
    // size of the FVM's underlying partition.
    block_info_t block_info;
    ssize_t rc = ioctl_block_get_info(fd, &block_info);
    if (rc < 0) {
        return static_cast<mx_status_t>(rc);
    } else if (rc != sizeof(block_info)) {
        return MX_ERR_BAD_STATE;
    } else if (FVM_SLICE_SIZE % block_info.block_size) {
        return MX_ERR_BAD_STATE;
    }

    size_t disk_size = block_info.block_count * block_info.block_size;
    size_t metadata_size = FVM_METADATA_SIZE(disk_size);

    mxtl::unique_ptr<MappedVmo> mvmo;
    mx_status_t status = MappedVmo::Create(metadata_size * 2, "fvm-meta", &mvmo);
    if (status != MX_OK) {
        return status;
    }

    // Clear entire primary copy of metadata
    memset(mvmo->GetData(), 0, metadata_size);

    // Superblock
    fvm_t* sb = static_cast<fvm_t*>(mvmo->GetData());
    sb->magic = FVM_MAGIC;
    sb->version = FVM_VERSION;
    sb->slice_count = (disk_size - metadata_size * 2) / FVM_SLICE_SIZE;
    sb->slice_size = FVM_SLICE_SIZE;
    sb->fvm_partition_size = disk_size;
    sb->vpartition_table_size = FVM_VPART_TABLE_LENGTH;
    sb->allocation_table_size = FVM_ALLOC_TABLE_LENGTH(disk_size);
    sb->generation = 0;

    if (sb->slice_count == 0) {
        return MX_ERR_NO_SPACE;
    }

    fvm_update_hash(mvmo->GetData(), metadata_size);

    const void* backup = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mvmo->GetData()) +
                                                 metadata_size);
    status = fvm_validate_header(mvmo->GetData(), backup, metadata_size, nullptr);
    if (status != MX_OK) {
        return status;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return MX_ERR_BAD_STATE;
    }
    // Write to primary copy.
    if (write(fd, mvmo->GetData(), metadata_size) != static_cast<ssize_t>(metadata_size)) {
        return MX_ERR_BAD_STATE;
    }
    // Write to secondary copy, to overwrite any previous FVM metadata copy that
    // could be here.
    if (write(fd, mvmo->GetData(), metadata_size) != static_cast<ssize_t>(metadata_size)) {
        return MX_ERR_BAD_STATE;
    }

    printf("fvm_init: Success\n");
    printf("fvm_init: Slice Count: %zu, size: %zu\n", sb->slice_count, sb->slice_size);
    printf("fvm_init: Vpart offset: %zu, length: %zu\n",
           FVM_VPART_TABLE_OFFSET, FVM_VPART_TABLE_LENGTH);
    printf("fvm_init: Atable offset: %zu, length: %zu\n",
           FVM_ALLOC_TABLE_OFFSET, FVM_ALLOC_TABLE_LENGTH(disk_size));
    printf("fvm_init: Backup meta starts at: %zu\n", FVM_BACKUP_START(disk_size));
    printf("fvm_init: Slices start at %zu, there are %zu of them\n",
           FVM_SLICES_START(disk_size), FVM_USABLE_SLICES_COUNT(disk_size));

    return MX_OK;
}
