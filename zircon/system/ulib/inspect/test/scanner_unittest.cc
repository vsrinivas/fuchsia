// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/vmo/scanner.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

namespace {

using inspect::internal::Block;
using inspect::internal::BlockFields;
using inspect::internal::BlockIndex;
using inspect::internal::BlockType;
using inspect::internal::kMinOrderSize;
using inspect::internal::ScanBlocks;

TEST(Scanner, ReadEmpty) {
  uint8_t buf[1024];
  memset(buf, 0, 1024);

  size_t count = 0;
  EXPECT_TRUE(ZX_OK == ScanBlocks(buf, 1024, [&count](BlockIndex index, const Block* block) {
                count++;
                return true;
              }));
  EXPECT_EQ(1024 / kMinOrderSize, count);
}

TEST(Scanner, ReadCancel) {
  uint8_t buf[1024];
  memset(buf, 0, 1024);

  size_t count = 0;
  EXPECT_TRUE(ZX_OK == ScanBlocks(buf, 1024, [&count](BlockIndex index, const Block* block) {
                count++;
                return false;
              }));
  EXPECT_EQ(1, count);
}

TEST(Scanner, ReadMisaligned) {
  uint8_t buf[1020];
  memset(buf, 0, 1020);

  size_t count = 0;
  EXPECT_TRUE(ZX_ERR_OUT_OF_RANGE ==
              ScanBlocks(buf, 1020, [&count](BlockIndex index, const Block* block) {
                count++;
                return true;
              }));
  EXPECT_EQ(1024 / kMinOrderSize - 1, count);
}

TEST(Scanner, ReadSingle) {
  uint8_t buf[kMinOrderSize];
  memset(buf, 0, kMinOrderSize);

  size_t count = 0;
  BlockIndex last_index = 0xFFFFFF;
  EXPECT_TRUE(ZX_OK == ScanBlocks(buf, kMinOrderSize, [&](BlockIndex index, const Block* block) {
                count++;
                last_index = index;
                return true;
              }));
  EXPECT_EQ(1u, count);
  EXPECT_EQ(0u, last_index);
}

TEST(Scanner, ReadOutOfBounds) {
  uint8_t buf[kMinOrderSize];
  memset(buf, 0, kMinOrderSize);
  Block* block = reinterpret_cast<Block*>(buf);
  block->header = BlockFields::Order::Make(1);

  size_t count = 0;
  EXPECT_TRUE(ZX_ERR_OUT_OF_RANGE ==
              ScanBlocks(buf, kMinOrderSize, [&count](BlockIndex index, const Block* block) {
                count++;
                return true;
              }));
  EXPECT_EQ(0u, count);
}

}  // namespace
