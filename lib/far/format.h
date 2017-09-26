// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FAR_FORMAT_H_
#define GARNET_LIB_FAR_FORMAT_H_

#include <stdint.h>

namespace archive {

constexpr uint64_t kMagic = 0x11c5abad480bbfc8;
constexpr uint64_t kDirType = 0x2d2d2d2d2d524944;
constexpr uint64_t kDirnamesType = 0x53454d414e524944;

constexpr uint32_t kHashAlgorithm = 1;
constexpr uint32_t kHashLength = 32;

struct IndexChunk {
  uint64_t magic = kMagic;
  uint64_t length = 0;
  // Index entries
};

struct IndexEntry {
  uint64_t type = 0;
  uint64_t offset = 0;
  uint64_t length = 0;
};

struct HashChunk {
  uint32_t algorithm = kHashAlgorithm;
  uint32_t hash_length = kHashLength;
  uint8_t hash_data[kHashLength] = {};
};

struct DirectoryTableEntry {
  uint32_t name_offset = 0;
  uint16_t name_length = 0;
  uint16_t reserved0 = 0;
  uint64_t data_offset = 0;
  uint64_t data_length = 0;
  uint64_t reserved1 = 0;
};

struct DirectoryHashChunk {
  uint32_t algorithm = kHashAlgorithm;
  uint32_t hash_length = kHashLength;
  // Hashes
};

}  // namespace archive

#endif  // GARNET_LIB_FAR_FORMAT_H_
