// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/vmo/scanner.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

namespace {

using inspect::Block;
using inspect::BlockIndex;
using inspect::BlockType;
using inspect::ScanBlocks;

TEST(Scanner, ReadEmpty) {
  uint8_t buf[1024];
  memset(buf, 0, 1024);

  size_t count = 0;
  EXPECT_TRUE(ZX_OK ==
              ScanBlocks(buf, 1024, [&count](BlockIndex index, const Block* block) { count++; }));
  EXPECT_EQ(1024 / inspect::kMinOrderSize, count);
}

TEST(Scanner, ReadMisaligned) {
  uint8_t buf[1020];
  memset(buf, 0, 1020);

  size_t count = 0;
  EXPECT_TRUE(ZX_ERR_OUT_OF_RANGE ==
              ScanBlocks(buf, 1020, [&count](BlockIndex index, const Block* block) { count++; }));
  EXPECT_EQ(1024 / inspect::kMinOrderSize - 1, count);
}

TEST(Scanner, ReadSingle) {
  uint8_t buf[inspect::kMinOrderSize];
  memset(buf, 0, inspect::kMinOrderSize);

  size_t count = 0;
  BlockIndex last_index = 0xFFFFFF;
  EXPECT_TRUE(ZX_OK ==
              ScanBlocks(buf, inspect::kMinOrderSize, [&](BlockIndex index, const Block* block) {
                count++;
                last_index = index;
              }));
  EXPECT_EQ(1u, count);
  EXPECT_EQ(0u, last_index);
}

TEST(Scanner, ReadOutOfBounds) {
  uint8_t buf[inspect::kMinOrderSize];
  memset(buf, 0, inspect::kMinOrderSize);
  Block* block = reinterpret_cast<Block*>(buf);
  block->header = inspect::BlockFields::Order::Make(1);

  size_t count = 0;
  EXPECT_TRUE(ZX_ERR_OUT_OF_RANGE ==
              ScanBlocks(buf, inspect::kMinOrderSize,
                         [&count](BlockIndex index, const Block* block) { count++; }));
  EXPECT_EQ(0u, count);
}

}  // namespace
