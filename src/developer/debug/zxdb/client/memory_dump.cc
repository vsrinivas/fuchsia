// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/memory_dump.h"

namespace zxdb {

MemoryDump::MemoryDump() {}
MemoryDump::MemoryDump(std::vector<debug_ipc::MemoryBlock>&& blocks) : blocks_(std::move(blocks)) {}
MemoryDump::~MemoryDump() = default;

bool MemoryDump::AllValid() const {
  if (blocks_.empty())
    return false;

  for (const auto& block : blocks_) {
    if (!block.valid)
      return false;
  }
  return true;
}

bool MemoryDump::GetByte(uint64_t address, uint8_t* byte) const {
  *byte = 0;

  // Address math needs to be careful to avoid overflow.
  if (blocks_.empty() || address < blocks_.front().address ||
      address > blocks_.back().address + (blocks_.back().size - 1)) {
    return false;
  }

  // It's expected the set of blocks will be in the 1-3 block making a brute-force search for the
  // block containing the address more efficient than a binary search.
  for (const auto& block : blocks_) {
    uint64_t last_addr = block.address + (block.size - 1);  // Watch out for overflow.
    if (address >= block.address && address <= last_addr) {
      if (!block.valid)
        return false;
      *byte = block.data[address - block.address];
      return true;
    }
  }
  return false;
}

}  // namespace zxdb
