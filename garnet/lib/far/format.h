// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FAR_FORMAT_H_
#define GARNET_LIB_FAR_FORMAT_H_

#include <stdint.h>

namespace archive {

constexpr uint64_t kMagic = 0x11c5abad480bbfc8;
constexpr uint64_t kMagicLength = 8u;
constexpr uint64_t kDirType = 0x2d2d2d2d2d524944;
constexpr uint64_t kDirnamesType = 0x53454d414e524944;

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

struct DirectoryTableEntry {
  uint32_t name_offset = 0;
  uint16_t name_length = 0;
  uint16_t reserved0 = 0;
  uint64_t data_offset = 0;
  uint64_t data_length = 0;
  uint64_t reserved1 = 0;
};

}  // namespace archive

#endif  // GARNET_LIB_FAR_FORMAT_H_
