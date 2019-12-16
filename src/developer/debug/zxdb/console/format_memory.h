// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_MEMORY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_MEMORY_H_

#include <stdint.h>

#include <string>

#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class MemoryDump;

struct MemoryFormatOptions {
  enum AddressMode {
    kNoAddresses,  // Don't show location information on the left.
    kAddresses,    // Show absolute hex addresses: "0x000012360"
    kOffsets,      // Show offsets from the beginning of the dump: "+0x10".
  };

  AddressMode address_mode = kNoAddresses;

  // Shows printable characters on the right.
  bool show_ascii = false;

  int values_per_line = 16;

  // Instead of a space, every this many values on a line will use a hyphen instead. 0 means no
  // separators.
  int separator_every = 0;
};

OutputBuffer FormatMemory(const MemoryDump& dump, uint64_t begin, uint32_t size,
                          const MemoryFormatOptions& opts);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_MEMORY_H_
