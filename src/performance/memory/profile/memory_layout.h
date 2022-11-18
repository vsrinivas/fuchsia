// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_MEMORY_PROFILE_MEMORY_LAYOUT_H_
#define SRC_PERFORMANCE_MEMORY_PROFILE_MEMORY_LAYOUT_H_

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// Unique identifier of a completed build object.
// This is used to identify the debug symbols for a binary.
struct BuildId {
  // Variable size identifier.
  std::vector<uint8_t> value;

  // Deserialize from the specified stream.
  void Read(std::istream& is);
  std::string ToHex();
};

// Placement of executable code in memory.
// This is used to interpret the code pointers of backtraces.
// The addresses makes possible to compute the offset in the executable binary.
struct Mmap {
  uint64_t starting_address;
  uint64_t size;
  uint16_t module_index;
  bool readable;
  bool writable;
  bool executable;
  uint64_t relative_addr;

  // Deserialize from the specified stream.
  void Read(std::istream& is);
};

// Information required to symbolized a trace trace.
struct Layout {
  std::vector<BuildId> modules;
  std::vector<Mmap> mmaps;

  // Deserialize from the specified stream.
  void Read(std::istream& is);
};

#endif  // SRC_PERFORMANCE_MEMORY_PROFILE_MEMORY_LAYOUT_H_
