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

#include <fvm/fvm.h>

#ifdef __Fuchsia__
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>
#endif

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fvm/format.h>

namespace fvm {

namespace {

// Return true if g1 is greater than or equal to g2.
// Safe against integer overflow.
bool GenerationGE(uint64_t g1, uint64_t g2) {
  if (g1 == UINT64_MAX && g2 == 0) {
    return false;
  } else if (g1 == 0 && g2 == UINT64_MAX) {
    return true;
  }
  return g1 >= g2;
}

// Validate the metadata's hash value.
// Returns 'true' if it matches, 'false' otherwise.
bool CheckHash(const void* metadata, size_t metadata_size) {
  ZX_ASSERT(metadata_size >= sizeof(Header));
  const Header* header = static_cast<const Header*>(metadata);
  const void* metadata_after_hash =
      reinterpret_cast<const void*>(header->hash + sizeof(header->hash));
  uint8_t empty_hash[sizeof(header->hash)];
  memset(empty_hash, 0, sizeof(empty_hash));

  digest::Digest digest;
  digest.Init();
  digest.Update(metadata, offsetof(Header, hash));
  digest.Update(empty_hash, sizeof(empty_hash));
  digest.Update(metadata_after_hash,
                metadata_size - (offsetof(Header, hash) + sizeof(header->hash)));
  digest.Final();
  return digest == header->hash;
}

}  // namespace

void UpdateHash(void* metadata, size_t metadata_size) {
  Header* header = static_cast<Header*>(metadata);
  memset(header->hash, 0, sizeof(header->hash));
  digest::Digest digest;
  const uint8_t* hash = digest.Hash(metadata, metadata_size);
  memcpy(header->hash, hash, sizeof(header->hash));
}

std::optional<SuperblockType> ValidateHeader(const void* primary_metadata,
                                             const void* secondary_metadata, size_t metadata_size) {
  const Header* primary_header = static_cast<const Header*>(primary_metadata);
  size_t primary_metadata_size = primary_header->GetMetadataUsedBytes();

  const Header* secondary_header = static_cast<const Header*>(secondary_metadata);
  size_t secondary_metadata_size = secondary_header->GetMetadataUsedBytes();

  auto check_value_consitency = [metadata_size](const Header& header) {
    // Check header signature and version.
    if (header.magic != kMagic) {
      fprintf(stderr, "fvm: Bad magic\n");
      return false;
    }
    if (header.version > kVersion) {
      fprintf(stderr, "fvm: Header Version does not match fvm driver\n");
      return false;
    }

    // Check no overflow for each region of metadata.
    uint64_t calculated_metadata_size = 0;
    if (add_overflow(header.allocation_table_size, header.GetAllocationTableOffset(),
                     &calculated_metadata_size)) {
      fprintf(stderr, "fvm: Calculated metadata size produces overflow(%" PRIu64 ", %zu).\n",
              header.allocation_table_size, header.GetAllocationTableOffset());
      return false;
    }

    // Check that the reported metadata size matches the calculated metadata size by format info.
    // This may be slightly redundant since presumably the caller who passed in the metadata_size
    // computed it from the header values, but it's useful to check for the backup copy.
    if (header.GetMetadataUsedBytes() > metadata_size) {
      fprintf(stderr,
              "fvm: Reported metadata size of %zu is smaller than header buffer size %zu.\n",
              header.GetMetadataUsedBytes(), metadata_size);
      return false;
    }

    // Check metadata size is as least as big as the header.
    if (header.GetMetadataUsedBytes() < sizeof(Header)) {
      fprintf(stderr,
              "fvm: Reported metadata size of %zu is smaller than header buffer size %lu.\n",
              header.GetMetadataUsedBytes(), sizeof(Header));
      return false;
    }

    // Check bounds of slice size and partition.
    if (header.fvm_partition_size < 2 * header.GetMetadataAllocatedBytes()) {
      fprintf(stderr,
              "fvm: Partition of size %" PRIu64 " can't fit both metadata copies of size %zu.\n",
              header.fvm_partition_size, header.GetMetadataAllocatedBytes());
      return false;
    }

    // Check that addressable slices fit in the partition.
    if (header.GetDataStartOffset() +
            header.GetAllocationTableUsedEntryCount() * header.slice_size >
        header.fvm_partition_size) {
      fprintf(
          stderr,
          "fvm: Slice count %zu Slice Size %" PRIu64 " out of range for partition %" PRIu64 ".\n",
          header.GetAllocationTableUsedEntryCount(), header.slice_size, header.fvm_partition_size);
      return false;
    }

    return true;
  };

  // Assume that the reported metadata size by each header is correct. This size must be smaller
  // than metadata buffer size(|metadata_size|. If this is the case, then check that the contents
  // from [start, reported_size] are valid.
  // The metadata size should always be at least the size of the header.
  bool primary_valid =
      check_value_consitency(*primary_header) && CheckHash(primary_metadata, primary_metadata_size);
  if (!primary_valid) {
    fprintf(stderr, "fvm: Primary metadata invalid\n");
  }
  bool secondary_valid = check_value_consitency(*secondary_header) &&
                         CheckHash(secondary_metadata, secondary_metadata_size);
  if (!secondary_valid) {
    fprintf(stderr, "fvm: Secondary metadata invalid\n");
  }

  // Decide if we should use the primary or the secondary copy of metadata for reading.
  if (!primary_valid && !secondary_valid) {
    return std::nullopt;
  }

  if (primary_valid && !secondary_valid) {
    return SuperblockType::kPrimary;
  }
  if (!primary_valid && secondary_valid) {
    return SuperblockType::kSecondary;
  }

  // Both valid, pick the newest.
  return GenerationGE(primary_header->generation, secondary_header->generation)
             ? SuperblockType::kPrimary
             : SuperblockType::kSecondary;
}

}  // namespace fvm
