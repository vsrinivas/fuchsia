// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/mock_memory.h"

#include "src/developer/debug/zxdb/common/largest_less_or_equal.h"

namespace zxdb {

void MockMemory::AddMemory(uint64_t address, std::vector<uint8_t> data) {
  mem_[address] = std::move(data);
}

std::vector<uint8_t> MockMemory::ReadMemory(uint64_t address, uint32_t size) const {
  auto found = FindBlockForAddress(address);
  if (found == mem_.end())
    return {};

  size_t offset = address - found->first;

  uint32_t size_to_return = std::min(size, static_cast<uint32_t>(found->second.size() - offset));

  std::vector<uint8_t> subset;
  subset.resize(size_to_return);
  memcpy(&subset[0], &found->second[offset], size_to_return);
  return subset;
}

MockMemory::RegisteredMemory::const_iterator MockMemory::FindBlockForAddress(
    uint64_t address) const {
  // Locates the potential map entry covering this address.
  auto found = LargestLessOrEqual(
      mem_.begin(), mem_.end(), address,
      [](const RegisteredMemory::value_type& v, uint64_t a) { return v.first < a; },
      [](const RegisteredMemory::value_type& v, uint64_t a) { return v.first == a; });

  if (found == mem_.end())
    return mem_.end();

  // Validate the address is within the data range.
  if (address >= found->first + found->second.size())
    return mem_.end();  // Address is after this range.
  return found;
}

}  // namespace zxdb
