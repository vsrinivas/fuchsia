// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_MEMORY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_MEMORY_H_

#include <stdint.h>

#include <string>

namespace zxdb {

class MemoryDump;

struct MemoryFormatOptions {
  bool show_addrs = false;
  bool show_ascii = false;

  int values_per_line = 16;

  // Instead of a space, every this many values on a line will use a hyphen
  // instead. 0 means no separators.
  int separator_every = 0;
};

std::string FormatMemory(const MemoryDump& dump, uint64_t begin, uint32_t size,
                         const MemoryFormatOptions& opts);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_MEMORY_H_
