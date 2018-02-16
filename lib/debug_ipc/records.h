// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace debug_ipc {

#pragma pack(push, 8)

// Note: see "ps" source:
// https://fuchsia.googlesource.com/zircon/+/master/system/uapp/psutils/ps.c
struct ProcessTreeRecord {
  enum class Type : uint32_t { kJob, kProcess };

  Type type = Type::kJob;
  uint64_t koid = 0;
  std::string name;

  std::vector<ProcessTreeRecord> children;
};

struct ThreadRecord {
  uint64_t koid = 0;
  std::string name;
};

struct MemoryBlock {
  // Begin address of this memory.
  uint64_t address = 0;

  // When true, indicates this is valid memory, with the data containing the
  // memory. False means
  // that this range is not mapped in the process and the data will be empty.
  bool valid = false;

  // Length of this range. When valid == true, this will be the same as
  // data.size().
  uint64_t size = 0;

  // The actual memory. Filled in only if valid == true.
  std::vector<uint8_t> data;
};

#pragma pack(pop)

}  // namespace debug_ipc
