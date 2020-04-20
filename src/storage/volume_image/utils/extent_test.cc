// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/extent.h"

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

namespace storage::volume_image {
namespace {

TEST(ExtentTest, ConstructorParametersInitializedCorrectly) {
  constexpr uint64_t kOffset = 1234;
  constexpr uint64_t kCount = 5678;
  constexpr uint64_t kBlockSize = 91011;
  // Trivial test, but let's make sure that we are doing the right mapping.
  Extent extent(kOffset, kCount, kBlockSize);

  EXPECT_EQ(kOffset, extent.offset());
  EXPECT_EQ(kCount, extent.count());
  EXPECT_EQ(kBlockSize, extent.block_size());
  EXPECT_EQ(kOffset, extent.begin());
  EXPECT_EQ(kOffset + kCount, extent.end());
  EXPECT_FALSE(extent.empty());
}

TEST(ExtentTest, ConstructorWithDefaultIsEmpty) {
  Extent extent;

  EXPECT_EQ(0u, extent.offset());
  EXPECT_EQ(0u, extent.count());
  EXPECT_EQ(0u, extent.block_size());
  EXPECT_EQ(0u, extent.begin());
  EXPECT_EQ(0u, extent.end());
  EXPECT_TRUE(extent.empty());
}

TEST(ExtentTest, ConvertToBiggerBlockSizeWithAlignedBoundariesReturnsEmptyTail) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kOffset = 3 * kBlockSize;
  constexpr uint64_t kCount = 200;
  constexpr uint64_t kTargetBlockSize = kBlockSize * 10;
  constexpr uint64_t kTargetOffset = kTargetBlockSize * 10;

  Extent extent(kOffset, kCount, kBlockSize);

  auto [target_extent, target_tail] = extent.Convert(kTargetOffset, kTargetBlockSize);

  EXPECT_EQ((kCount * kBlockSize) / kTargetBlockSize, target_extent.count());
  EXPECT_EQ(kTargetOffset, target_extent.offset());
  EXPECT_EQ(kTargetBlockSize, target_extent.block_size());
  EXPECT_TRUE(target_tail.empty());
}

TEST(ExtentTest, ConvertToBiggerBlockSizeWithUnalignedBoundariesReturnsTail) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kOffset = 3 * kBlockSize;
  constexpr uint64_t kCount = 199;
  constexpr uint64_t kTargetBlockSize = kBlockSize * 10;
  constexpr uint64_t kTargetOffset = kTargetBlockSize * 10;

  Extent extent(kOffset, kCount, kBlockSize);

  auto [target_extent, target_tail] = extent.Convert(kTargetOffset, kTargetBlockSize);

  EXPECT_EQ(fbl::round_up(kCount * kBlockSize, kTargetBlockSize) / kTargetBlockSize,
            target_extent.count());
  EXPECT_EQ(kTargetOffset, target_extent.offset());
  EXPECT_EQ(kTargetBlockSize, target_extent.block_size());
  EXPECT_FALSE(target_tail.empty());
  uint64_t tail_offset =
      (kBlockSize * kCount) - (target_extent.count() - 1) * target_extent.block_size();
  EXPECT_EQ(tail_offset, target_tail.offset);
  EXPECT_EQ(kTargetBlockSize - tail_offset, target_tail.count);
}

TEST(ExtentTest, ConvertToSmallerBlockSizeWithAlignedBoundariesReturnsEmptyTail) {
  constexpr uint64_t kBlockSize = 5120;
  constexpr uint64_t kOffset = 3 * kBlockSize;
  constexpr uint64_t kCount = 200;
  constexpr uint64_t kTargetBlockSize = kBlockSize / 10;
  constexpr uint64_t kTargetOffset = kTargetBlockSize * 10;

  Extent extent(kOffset, kCount, kBlockSize);

  auto [target_extent, target_tail] = extent.Convert(kTargetOffset, kTargetBlockSize);

  EXPECT_EQ(fbl::round_up(kCount * kBlockSize, kTargetBlockSize) / kTargetBlockSize,
            target_extent.count());
  EXPECT_EQ(kTargetOffset, target_extent.offset());
  EXPECT_EQ(kTargetBlockSize, target_extent.block_size());
  EXPECT_TRUE(target_tail.empty());
}

TEST(ExtentTest, ConvertToSmallerBlockSizeWithUnalignedBoundariesReturnsTail) {
  constexpr uint64_t kBlockSize = 5120;
  constexpr uint64_t kOffset = 3 * kBlockSize;
  constexpr uint64_t kCount = 100;
  constexpr uint64_t kTargetBlockSize = kBlockSize / 5 - 1;
  constexpr uint64_t kTargetOffset = kTargetBlockSize * 10;

  Extent extent(kOffset, kCount, kBlockSize);

  auto [target_extent, target_tail] = extent.Convert(kTargetOffset, kTargetBlockSize);

  EXPECT_EQ(fbl::round_up(kCount * kBlockSize, kTargetBlockSize) / kTargetBlockSize,
            target_extent.count());
  EXPECT_EQ(kTargetOffset, target_extent.offset());
  EXPECT_EQ(kTargetBlockSize, target_extent.block_size());

  uint64_t tail_offset =
      (kBlockSize * kCount) - (target_extent.count() - 1) * target_extent.block_size();
  EXPECT_EQ(tail_offset, target_tail.offset);
  EXPECT_EQ(kTargetBlockSize - tail_offset, target_tail.count);
}

TEST(ExtentTest, ConvertEmptyExtentReturnsEmptyExtentAndEmptyTail) {
  constexpr uint64_t kBlockSize = 5120;
  constexpr uint64_t kOffset = 3 * kBlockSize;
  constexpr uint64_t kCount = 0;
  constexpr uint64_t kTargetBlockSize = kBlockSize / 5 - 1;
  constexpr uint64_t kTargetOffset = kTargetBlockSize * 10;

  Extent extent(kOffset, kCount, kBlockSize);

  auto [target_extent, target_tail] = extent.Convert(kTargetOffset, kTargetBlockSize);

  EXPECT_EQ(fbl::round_up(kCount * kBlockSize, kTargetBlockSize) / kTargetBlockSize,
            target_extent.count());
  EXPECT_EQ(kTargetOffset, target_extent.offset());
  EXPECT_EQ(kTargetBlockSize, target_extent.block_size());
  EXPECT_TRUE(target_extent.empty());
  EXPECT_TRUE(target_tail.empty());
}

}  // namespace
}  // namespace storage::volume_image
