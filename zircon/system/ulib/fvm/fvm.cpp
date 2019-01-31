// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#include <lib/zx/vmo.h>
#endif

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include "fvm/fvm.h"

#define ZXDEBUG 0

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
    ZX_DEBUG_ASSERT(metadata_size >= sizeof(fvm::fvm_t));
    const fvm::fvm_t* header = static_cast<const fvm::fvm_t*>(metadata);
    const void* metadata_after_hash =
        reinterpret_cast<const void*>(header->hash + sizeof(header->hash));
    uint8_t empty_hash[sizeof(header->hash)];
    memset(empty_hash, 0, sizeof(empty_hash));

    digest::Digest digest;
    digest.Init();
    digest.Update(metadata, offsetof(fvm::fvm_t, hash));
    digest.Update(empty_hash, sizeof(empty_hash));
    digest.Update(metadata_after_hash,
                  metadata_size - (offsetof(fvm::fvm_t, hash) + sizeof(header->hash)));
    digest.Final();
    return digest == header->hash;
}

} // namespace anonymous

#ifdef __cplusplus

uint64_t fvm::slice_entry::Vpart() const {
    uint64_t result = data & VPART_MASK;
    ZX_DEBUG_ASSERT(result < VPART_MAX);
    return result;
}

void fvm::slice_entry::SetVpart(uint64_t vpart) {
    ZX_DEBUG_ASSERT(vpart < VPART_MAX);
    data = (data & ~VPART_MASK) | (vpart & VPART_MASK);
}

uint64_t fvm::slice_entry::Vslice() const {
    uint64_t result = (data & VSLICE_MASK) >> VPART_BITS;
    ZX_DEBUG_ASSERT(result < VSLICE_MAX);
    return result;
}

void fvm::slice_entry::SetVslice(uint64_t vslice) {
    ZX_DEBUG_ASSERT(vslice < VSLICE_MAX);
    data = (data & ~VSLICE_MASK) | ((vslice & VSLICE_MAX) << VPART_BITS);
}

#endif // __cplusplus

void fvm_update_hash(void* metadata, size_t metadata_size) {
    fvm::fvm_t* header = static_cast<fvm::fvm_t*>(metadata);
    memset(header->hash, 0, sizeof(header->hash));
    digest::Digest digest;
    const uint8_t* hash = digest.Hash(metadata, metadata_size);
    memcpy(header->hash, hash, sizeof(header->hash));
}

zx_status_t fvm_validate_header(const void* metadata, const void* backup,
                                size_t metadata_size, const void** out) {
    const fvm::fvm_t* primary_header = static_cast<const fvm::fvm_t*>(metadata);
    const fvm::fvm_t* backup_header = static_cast<const fvm::fvm_t*>(backup);

    bool primary_valid = fvm_check_hash(metadata, metadata_size);
    bool backup_valid = fvm_check_hash(backup, metadata_size);

    // Decide if we should use the primary or the backup copy of metadata
    // for reading.
    bool use_primary;
    if (!primary_valid && !backup_valid) {
        return ZX_ERR_BAD_STATE;
    } else if (primary_valid && !backup_valid) {
        use_primary = true;
    } else if (!primary_valid && backup_valid) {
        use_primary = false;
    } else {
        use_primary = generation_ge(primary_header->generation, backup_header->generation);
    }

    const fvm::fvm_t* header = use_primary ? primary_header : backup_header;
    if (header->magic != FVM_MAGIC) {
        fprintf(stderr, "fvm: Bad magic\n");
        return ZX_ERR_BAD_STATE;
    }
    if (header->version > FVM_VERSION) {
        fprintf(stderr, "fvm: Header Version does not match fvm driver\n");
        return ZX_ERR_BAD_STATE;
    }

    // TODO(smklein): Additional validation....

    if (out) {
        *out = use_primary ? metadata : backup;
    }
    return ZX_OK;
}
