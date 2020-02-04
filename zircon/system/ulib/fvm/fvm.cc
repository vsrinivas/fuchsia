// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/assert.h>

#ifdef __Fuchsia__
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>
#endif

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fvm/format.h>

#define ZXDEBUG 0

namespace {

constexpr size_t MetadataSizeOrZero(size_t disk_size, size_t slice_size) {
  if (disk_size == 0 || slice_size == 0) {
    return 0;
  }
  return fvm::MetadataSize(disk_size, slice_size);
}

constexpr size_t UsableSlicesCountOrZero(size_t fvm_partition_size, size_t metadata_allocated_size,
                                         size_t slice_size) {
  if (slice_size == 0) {
    return 0;
  }

  int64_t delta = (fvm_partition_size - 2 * metadata_allocated_size);
  size_t slice_count = (delta > 0 ? static_cast<size_t>(delta) : 0) / slice_size;

  // Because the allocation table is 1-indexed and pslices are 0 indexed on disk,
  // if the number of slices fit perfectly in the metadata, the allocated buffer won't be big
  // enough to address them all. This only happens when the rounded up block value happens to
  // match the disk size.
  // TODO(gevalentino): Fix underlying cause and remove workaround.
  if ((fvm::AllocationTable::kOffset + slice_count * sizeof(fvm::SliceEntry)) ==
      metadata_allocated_size) {
    slice_count--;
  }
  return slice_count;
}

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
  ZX_DEBUG_ASSERT(metadata_size >= sizeof(fvm::Header));
  const fvm::Header* header = static_cast<const fvm::Header*>(metadata);
  const void* metadata_after_hash =
      reinterpret_cast<const void*>(header->hash + sizeof(header->hash));
  uint8_t empty_hash[sizeof(header->hash)];
  memset(empty_hash, 0, sizeof(empty_hash));

  digest::Digest digest;
  digest.Init();
  digest.Update(metadata, offsetof(fvm::Header, hash));
  digest.Update(empty_hash, sizeof(empty_hash));
  digest.Update(metadata_after_hash,
                metadata_size - (offsetof(fvm::Header, hash) + sizeof(header->hash)));
  digest.Final();
  return digest == header->hash;
}

}  // namespace

#ifdef __cplusplus

fvm::FormatInfo fvm::FormatInfo::FromSuperBlock(const Header& superblock) {
  fvm::FormatInfo format_info;
  format_info.metadata_allocated_size_ = superblock.allocation_table_size + kAllocTableOffset;
  format_info.metadata_size_ =
      MetadataSizeOrZero(superblock.fvm_partition_size, superblock.slice_size);
  format_info.slice_size_ = superblock.slice_size;
  format_info.slice_count_ =
      UsableSlicesCountOrZero(superblock.fvm_partition_size, format_info.metadata_allocated_size(),
                              format_info.slice_size());
  return format_info;
}

fvm::FormatInfo fvm::FormatInfo::FromPreallocatedSize(size_t initial_size, size_t max_size,
                                                      size_t slice_size) {
  fvm::FormatInfo format_info;
  format_info.metadata_allocated_size_ = MetadataSizeOrZero(max_size, slice_size);
  format_info.metadata_size_ = MetadataSizeOrZero(initial_size, slice_size);
  format_info.slice_size_ = slice_size;
  format_info.slice_count_ = UsableSlicesCountOrZero(
      initial_size, format_info.metadata_allocated_size(), format_info.slice_size());
  return format_info;
}

fvm::FormatInfo fvm::FormatInfo::FromDiskSize(size_t disk_size, size_t slice_size) {
  return FromPreallocatedSize(disk_size, disk_size, slice_size);
}

#endif  // __cplusplus

void fvm_update_hash(void* metadata, size_t metadata_size) {
  fvm::Header* header = static_cast<fvm::Header*>(metadata);
  memset(header->hash, 0, sizeof(header->hash));
  digest::Digest digest;
  const uint8_t* hash = digest.Hash(metadata, metadata_size);
  memcpy(header->hash, hash, sizeof(header->hash));
}

zx_status_t fvm_validate_header(const void* metadata, const void* backup, size_t metadata_size,
                                const void** out) {
  const fvm::Header* primary_header = static_cast<const fvm::Header*>(metadata);
  fvm::FormatInfo primary_info = fvm::FormatInfo::FromSuperBlock(*primary_header);
  size_t primary_metadata_size = primary_info.metadata_size();

  const fvm::Header* backup_header = static_cast<const fvm::Header*>(backup);
  fvm::FormatInfo backup_info = fvm::FormatInfo::FromSuperBlock(*backup_header);
  size_t backup_metadata_size = backup_info.metadata_size();

  auto check_value_consitency = [metadata_size](const fvm::Header* header,
                                                const fvm::FormatInfo& info) {
    // Check no overflow for each region of metadata.
    uint64_t calculated_metadata_size = 0;
    if (add_overflow(header->allocation_table_size, fvm::kAllocTableOffset,
                     &calculated_metadata_size)) {
      fprintf(stderr, "fvm: Calculated metadata size produces overflow(%" PRIu64 ", %zu).\n",
              header->allocation_table_size, fvm::kAllocTableOffset);
      return false;
    }

    // Check that the reported metadata size matches the calculated metadata size by format info.
    if (info.metadata_size() > metadata_size) {
      fprintf(stderr,
              "fvm: Reported metadata size of %zu is smaller than header buffer size %zu.\n",
              info.metadata_size(), metadata_size);
      return false;
    }

    // Check metadata size is as least as big as the header.
    if (info.metadata_size() < sizeof(fvm::Header)) {
      fprintf(stderr,
              "fvm: Reported metadata size of %zu is smaller than header buffer size %lu.\n",
              info.metadata_size(), sizeof(fvm::Header));
      return false;
    }

    // Check bounds of slice size and partition.
    if (header->fvm_partition_size < 2 * info.metadata_allocated_size()) {
      fprintf(stderr,
              "fvm: Partition of size %" PRIu64 " can't fit both metadata copies of size %zu.\n",
              header->fvm_partition_size, info.metadata_allocated_size());
      return false;
    }

    // Check that addressable slices fit in the partition.
    if (info.GetSliceStart(0) + info.slice_count() * info.slice_size() >
        header->fvm_partition_size) {
      fprintf(stderr,
              "fvm: Slice count %zu Slice Size %zu out of range for partition %" PRIu64 ".\n",
              info.slice_count(), info.slice_size(), header->fvm_partition_size);
      return false;
    }

    return true;
  };

  // Assume that the reported metadata size by each header is correct. This size must be smaller
  // than metadata buffer size(|metadata_size|. If this is the case, then check that the contents
  // from [start, reported_size] are valid.
  // The metadata size should always be at least the size of the header.
  bool primary_valid = check_value_consitency(primary_header, primary_info) &&
                       fvm_check_hash(metadata, primary_metadata_size);
  if (!primary_valid) {
    fprintf(stderr, "fvm: Primary metadata invalid\n");
  }
  bool backup_valid = check_value_consitency(backup_header, backup_info) &&
                      fvm_check_hash(backup, backup_metadata_size);
  if (!backup_valid) {
    fprintf(stderr, "fvm: Secondary metadata invalid\n");
  }

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

  const fvm::Header* header = use_primary ? primary_header : backup_header;
  if (header->magic != fvm::kMagic) {
    fprintf(stderr, "fvm: Bad magic\n");
    return ZX_ERR_BAD_STATE;
  }
  if (header->version > fvm::kVersion) {
    fprintf(stderr, "fvm: Header Version does not match fvm driver\n");
    return ZX_ERR_BAD_STATE;
  }

  // TODO(smklein): Additional validation....

  if (out) {
    *out = use_primary ? metadata : backup;
  }
  return ZX_OK;
}
