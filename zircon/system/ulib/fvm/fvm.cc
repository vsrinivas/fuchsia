// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <limits>

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

std::optional<SuperblockType> PickValidHeader(const void* primary_metadata,
                                              const void* secondary_metadata,
                                              size_t metadata_size) {
  return PickValidHeader(std::numeric_limits<uint64_t>::max(), kBlockSize, primary_metadata,
                         secondary_metadata, metadata_size);
}

std::optional<SuperblockType> PickValidHeader(uint64_t disk_size, uint64_t disk_block_size,
                                              const void* primary_metadata,
                                              const void* secondary_metadata,
                                              size_t metadata_size) {
  std::string header_error;

  // Validate primary header.
  const Header* primary_header = static_cast<const Header*>(primary_metadata);
  bool primary_valid = false;
  if (primary_header->IsValid(disk_size, disk_block_size, header_error)) {
    if (CheckHash(primary_metadata, primary_header->GetMetadataUsedBytes())) {
      primary_valid = true;
    } else {
      fprintf(stderr, "fvm: Primary metadata has invalid content hash\n");
    }
  } else {
    fprintf(stderr, "fvm: Primary metadata invalid: %s\n", header_error.c_str());
  }

  // Validate secondary header.
  const Header* secondary_header = static_cast<const Header*>(secondary_metadata);
  header_error.clear();
  bool secondary_valid = false;
  if (secondary_header->IsValid(disk_size, disk_block_size, header_error)) {
    if (CheckHash(secondary_metadata, secondary_header->GetMetadataUsedBytes())) {
      secondary_valid = true;
    } else {
      fprintf(stderr, "fvm: Secondary metadata has invalid content hash\n");
    }
  } else {
    fprintf(stderr, "fvm: Secondary metadata invalid: %s\n", header_error.c_str());
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
