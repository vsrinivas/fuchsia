// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TOOLS_TARGET_ARCHIVE_INCLUDE_TARGET_ARCHIVE_TARGET_ARCHIVE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TOOLS_TARGET_ARCHIVE_INCLUDE_TARGET_ARCHIVE_TARGET_ARCHIVE_H_

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

#include <stdint.h>

//
//
//

#define TARGET_ARCHIVE_MAGIC 0x54475254  // "TRGT"

//
//
//

struct target_archive_entry
{
  uint64_t offset;
  uint64_t size;
  uint32_t data[];
};

struct target_archive_header
{
  uint32_t                    magic;
  uint32_t                    count;
  struct target_archive_entry entries[];
};

//
// Concatenates one or more binaries prefixed by a table containing the number
// of binaries and the offset and size of each binary.
//
//   - Offsets are relative to the end of the entries[] table.
//   - Offsets and sizes are in bytes.
//   - Offsets and sizes are 64-bit.
//   - Binaries and their offsets are 4-byte aligned.
//
// Target memory map:
//
//   +-----------------------------------------+ 0
//   | alignas(8) struct target_archive_header |
//   +-----------------------------------------+ 8
//   | struct target_archive_entry[0]          |
//   | struct target_archive_entry[1]          |
//   | ...                                     |
//   | struct target_archive_entry[count-1]    |
//   +-----------------------------------------+ 8 + 16 * count
//   | alignas(4) data_(0)                     |
//   | alignas(4) data_(1)                     |
//   | ...                                     |
//   | alignas(4) data_(count-1)               |
//   +-----------------------------------------+
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TOOLS_TARGET_ARCHIVE_INCLUDE_TARGET_ARCHIVE_TARGET_ARCHIVE_H_
