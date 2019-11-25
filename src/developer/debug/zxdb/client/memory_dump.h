// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MEMORY_DUMP_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MEMORY_DUMP_H_

#include <stdint.h>

#include <vector>

#include "src/developer/debug/ipc/records.h"

namespace zxdb {

// Memory in a debugged process can be mapped or not mapped. This dump object represents a view into
// memory consisting of a sequence of these blocks.
class MemoryDump {
 public:
  MemoryDump();
  MemoryDump(std::vector<debug_ipc::MemoryBlock>&& blocks);
  ~MemoryDump();

  // Returns the begin address of this dump.
  uint64_t address() const {
    if (blocks_.empty())
      return 0;
    return blocks_[0].address;
  }

  // Returns the total size covered by this memory dump.
  uint64_t size() const {
    if (blocks_.empty())
      return 0;
    return blocks_.back().address + blocks_.back().size - blocks_.front().address;
  }

  // Returns true if every block in this memory dump is valid.
  bool AllValid() const;

  // The blocks in the memory dump will be contiguous. Anything not mapped will be represented by a
  // block marked not valid.
  const std::vector<debug_ipc::MemoryBlock>& blocks() const { return blocks_; }

  // Helper function to read out of the memory. If the given address is outside the range or is not
  // mapped, returns false. Otherwise fills it into the given output.
  bool GetByte(uint64_t address, uint8_t* byte) const;

 private:
  std::vector<debug_ipc::MemoryBlock> blocks_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MEMORY_DUMP_H_
