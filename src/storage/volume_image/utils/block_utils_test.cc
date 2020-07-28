// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/block_utils.h"

#include <gtest/gtest.h>

namespace storage::volume_image {
namespace {

TEST(BlockUtilsTest, GetOffsetFromBlockStartIsCorrect) {
  EXPECT_EQ(GetOffsetFromBlockStart(2048, 512), 0u);
  EXPECT_EQ(GetOffsetFromBlockStart(2049, 512), 1u);
  EXPECT_EQ(GetOffsetFromBlockStart(2047, 512), 511u);
}

TEST(BlockUtilsTest, GetRemainderFromBlockIsCorrect) {
  EXPECT_EQ(GetRemainderFromBlock(2048, 512), 0u);
  EXPECT_EQ(GetRemainderFromBlock(2049, 512), 511u);
  EXPECT_EQ(GetRemainderFromBlock(2047, 512), 1u);
}

TEST(BlockUtilsTest, GetBlockCountFromAlignedByteOffsetIsCorrect) {
  EXPECT_EQ(GetBlockCount(2048, 0, 512), 0u);
  EXPECT_EQ(GetBlockCount(2048, 1, 512), 1u);
  EXPECT_EQ(GetBlockCount(2048, 512, 512), 1u);
  EXPECT_EQ(GetBlockCount(2048, 513, 512), 2u);
  EXPECT_EQ(GetBlockCount(2048, 1023, 512), 2u);
  EXPECT_EQ(GetBlockCount(2048, 1024, 512), 2u);
}

TEST(BlockUtilsTest, GetBlockCountFromUnalignedByteOffsetIsCorrect) {
  EXPECT_EQ(GetBlockCount(2049, 0, 512), 0u);
  EXPECT_EQ(GetBlockCount(2049, 1, 512), 1u);
  EXPECT_EQ(GetBlockCount(2049, 512, 512), 2u);
  EXPECT_EQ(GetBlockCount(2049, 513, 512), 2u);
  EXPECT_EQ(GetBlockCount(2049, 1023, 512), 2u);
  EXPECT_EQ(GetBlockCount(2049, 1024, 512), 3u);
}

TEST(BlockUtilsTest, GetBlockOffsetIsCorrect) {
  EXPECT_EQ(GetBlockFromBytes(0, 512), 0u);
  EXPECT_EQ(GetBlockFromBytes(1, 512), 0u);
  EXPECT_EQ(GetBlockFromBytes(511, 512), 0u);
  EXPECT_EQ(GetBlockFromBytes(512, 512), 1u);
  EXPECT_EQ(GetBlockFromBytes(513, 512), 1u);
  EXPECT_EQ(GetBlockFromBytes(1023, 512), 1u);
  EXPECT_EQ(GetBlockFromBytes(1024, 512), 2u);
}

TEST(BlockUtilsTest, IsOffsetBlockAlignedIsCorrect) {
  EXPECT_TRUE(IsOffsetBlockAligned(0, 512));
  EXPECT_TRUE(IsOffsetBlockAligned(512, 512));
  EXPECT_TRUE(IsOffsetBlockAligned(1024, 512));

  EXPECT_FALSE(IsOffsetBlockAligned(1, 512));
  EXPECT_FALSE(IsOffsetBlockAligned(511, 512));
  EXPECT_FALSE(IsOffsetBlockAligned(1023, 512));
}

}  // namespace
}  // namespace storage::volume_image
