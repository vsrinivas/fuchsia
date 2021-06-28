// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/minidump_memory.h"

#include <string.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "lib/syslog/cpp/macros.h"
#include "src/developer/debug/unwinder/error.h"

namespace zxdb {

namespace {

class MockMemoryRegion : public MinidumpMemory::Region {
 public:
  MockMemoryRegion(uint64_t load_address, std::vector<uint8_t> data)
      : load_address_(load_address), data_(std::move(data)) {}
  unwinder::Error ReadBytes(uint64_t addr, uint64_t size, void* dst) override {
    FX_CHECK(addr >= load_address_);
    FX_CHECK(addr + size <= load_address_ + data_.size());

    memcpy(dst, data_.data() + (addr - load_address_), size);
    return unwinder::Success();
  }

 private:
  uint64_t load_address_;
  std::vector<uint8_t> data_;
};

TEST(MinidumpMemory, ReadMemoryBlocks) {
  MinidumpMemory memory({
      {0x1000, 0x1100,
       std::make_shared<MockMemoryRegion>(0x1000, std::vector<uint8_t>(0x100, 0xCC))},
      {0x2000, 0x2100,
       std::make_shared<MockMemoryRegion>(0x2000, std::vector<uint8_t>(0x100, 0xCC))},
  });

  // Read empty.
  auto res = memory.ReadMemoryBlocks(0x1000, 0);
  ASSERT_EQ(0u, res.size());
  res = memory.ReadMemoryBlocks(0x0200, 0);
  ASSERT_EQ(0u, res.size());

  // Read valid region.
  res = memory.ReadMemoryBlocks(0x1000, 0x10);
  ASSERT_EQ(1u, res.size());
  ASSERT_EQ(0x1000u, res[0].address);
  ASSERT_EQ(0x10u, res[0].size);
  ASSERT_EQ(true, res[0].valid);
  ASSERT_EQ(0x10u, res[0].data.size());

  // Read invalid region.
  res = memory.ReadMemoryBlocks(0x0200, 0x10);
  ASSERT_EQ(1u, res.size());
  ASSERT_EQ(0x0200u, res[0].address);
  ASSERT_EQ(0x10u, res[0].size);
  ASSERT_EQ(false, res[0].valid);
  ASSERT_EQ(0u, res[0].data.size());

  // Read invalid region.
  res = memory.ReadMemoryBlocks(0x0200, 0x10);
  ASSERT_EQ(1u, res.size());
  ASSERT_EQ(0x0200u, res[0].address);
  ASSERT_EQ(0x10u, res[0].size);
  ASSERT_EQ(false, res[0].valid);
  ASSERT_EQ(0u, res[0].data.size());

  // Read invalid region + valid region.
  res = memory.ReadMemoryBlocks(0x0FF0, 0x20);
  ASSERT_EQ(2u, res.size());
  ASSERT_EQ(0x0FF0u, res[0].address);
  ASSERT_EQ(0x10u, res[0].size);
  ASSERT_EQ(false, res[0].valid);
  ASSERT_EQ(0x1000u, res[1].address);
  ASSERT_EQ(0x10u, res[1].size);
  ASSERT_EQ(true, res[1].valid);

  // Read one valid region + one invalid region.
  res = memory.ReadMemoryBlocks(0x10F0, 0x20);
  ASSERT_EQ(2u, res.size());
  ASSERT_EQ(0x10F0u, res[0].address);
  ASSERT_EQ(0x10u, res[0].size);
  ASSERT_EQ(true, res[0].valid);
  ASSERT_EQ(0x1100u, res[1].address);
  ASSERT_EQ(0x10u, res[1].size);
  ASSERT_EQ(false, res[1].valid);

  // Read invalid region + valid region + invalid region.
  res = memory.ReadMemoryBlocks(0x1FF0, 0x120);
  ASSERT_EQ(3u, res.size());
  ASSERT_EQ(0x1FF0u, res[0].address);
  ASSERT_EQ(0x10u, res[0].size);
  ASSERT_EQ(false, res[0].valid);
  ASSERT_EQ(0x2000u, res[1].address);
  ASSERT_EQ(0x100u, res[1].size);
  ASSERT_EQ(true, res[1].valid);
  ASSERT_EQ(0x2100u, res[2].address);
  ASSERT_EQ(0x10u, res[2].size);
  ASSERT_EQ(false, res[2].valid);

  // Read valid region + invalid region + valid region.
  res = memory.ReadMemoryBlocks(0x1000, 0x1100);
  ASSERT_EQ(3u, res.size());
  ASSERT_EQ(0x1000u, res[0].address);
  ASSERT_EQ(0x100u, res[0].size);
  ASSERT_EQ(true, res[0].valid);
  ASSERT_EQ(0x1100u, res[1].address);
  ASSERT_EQ(0xF00u, res[1].size);
  ASSERT_EQ(false, res[1].valid);
  ASSERT_EQ(0x2000u, res[2].address);
  ASSERT_EQ(0x100u, res[2].size);
  ASSERT_EQ(true, res[2].valid);
}

}  // namespace

}  // namespace zxdb
