// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/memory_dump.h"

#include <limits>

#include "gtest/gtest.h"

namespace zxdb {

TEST(MemoryDump, Empty) {
  MemoryDump empty;
  EXPECT_EQ(0u, empty.address());
  EXPECT_EQ(0u, empty.size());

  uint8_t byte = 65;
  EXPECT_FALSE(empty.GetByte(0u, &byte));
  EXPECT_EQ(0, byte);

  byte = 65;
  EXPECT_FALSE(empty.GetByte(0x1234556, &byte));
  EXPECT_EQ(0, byte);
}

TEST(MemoryDump, Valid) {
  std::vector<debug_ipc::MemoryBlock> input;
  input.resize(3);

  uint64_t begin1 = 0x1000;
  uint64_t begin2 = 0x2000;
  uint64_t begin3 = 0x3000;
  uint64_t end = 0x4000;

  // Invalid block.
  input[0].address = begin1;
  input[0].size = begin2 - begin1;
  input[0].valid = false;

  // Valid block filled with cycling bytes.
  input[1].address = begin2;
  input[1].size = begin3 - begin2;
  input[1].valid = true;
  for (uint64_t i = 0; i < input[1].size; i++) {
    input[1].data.push_back(static_cast<uint8_t>(i % 0x100));
  }

  // Invalid block.
  input[2].address = begin3;
  input[2].size = end - begin3;
  input[2].valid = false;

  MemoryDump dump(std::move(input));

  // Read from before begin.
  uint8_t byte;
  EXPECT_FALSE(dump.GetByte(0x100, &byte));
  EXPECT_EQ(0u, byte);

  // Read from first invalid block.
  EXPECT_FALSE(dump.GetByte(begin1, &byte));
  EXPECT_FALSE(dump.GetByte(begin1 + 10, &byte));
  EXPECT_FALSE(dump.GetByte(begin2 - 1, &byte));

  // Read from valid block.
  EXPECT_TRUE(dump.GetByte(begin2, &byte));
  EXPECT_EQ(0u, byte);
  EXPECT_TRUE(dump.GetByte(begin2 + 10, &byte));
  EXPECT_EQ(10u, byte);
  EXPECT_TRUE(dump.GetByte(begin3 - 1, &byte));
  EXPECT_EQ((begin3 - 1) % 0x100, byte);

  // Read from third invalid block.
  EXPECT_FALSE(dump.GetByte(begin3, &byte));
  EXPECT_FALSE(dump.GetByte(begin3 + 10, &byte));
  EXPECT_FALSE(dump.GetByte(end - 1, &byte));

  // Read from past the end.
  EXPECT_FALSE(dump.GetByte(end, &byte));
  EXPECT_FALSE(dump.GetByte(end + 1000, &byte));
}

TEST(MemoryDump, Limits) {
  uint64_t max = std::numeric_limits<uint64_t>::max();

  std::vector<debug_ipc::MemoryBlock> block;
  block.resize(1);
  block[0].size = 0x1000;
  block[0].address = max - block[0].size + 1;
  block[0].valid = true;
  for (uint64_t i = 0; i < block[0].size; i++) {
    block[0].data.push_back(static_cast<uint8_t>(i % 0x100));
  }

  MemoryDump dump(std::move(block));

  // Query last byte.
  uint8_t byte = 10;
  EXPECT_TRUE(dump.GetByte(max, &byte));
  EXPECT_EQ(max % 0x100, byte);
}

}  // namespace zxdb
