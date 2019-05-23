// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_MOCK_MEMORY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_MOCK_MEMORY_H_

#include <stdint.h>

#include <map>
#include <vector>

namespace zxdb {

// This helper class keeps blocks of memory that have been manually added and
// can reply with subsets of those blocks. This is in turn used by other mocks
// that need to respond with memory queries.
class MockMemory {
 public:
  // Sets a memory block that will be returned.
  void AddMemory(uint64_t address, std::vector<uint8_t> data);

  // Query for memory. This will do short reads if the requested size goes
  // beyond a valid block, and will return an empty vector if the requested
  // address isn't set.
  std::vector<uint8_t> ReadMemory(uint64_t address, uint32_t size) const;

 private:
  // Registered memory blocks indexed by address.
  using RegisteredMemory = std::map<uint64_t, std::vector<uint8_t>>;

  // Returns the memory block that contains the given address, or mem_.end()
  // if not found.
  RegisteredMemory::const_iterator FindBlockForAddress(uint64_t address) const;

  RegisteredMemory mem_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_MOCK_MEMORY_H_
