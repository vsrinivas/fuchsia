// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/demux/sliding_buffer.h"

#include "gtest/gtest.h"

namespace media_player {
namespace {

void FillBlock(SlidingBuffer::Block block) {
  for (unsigned int i = 0; i < block.size; ++i) {
    block.buffer[i] = uint8_t((i + block.start) % 256);
  }
}

void FillBlocks(std::vector<SlidingBuffer::Block> blocks) {
  for (const auto& block : blocks) {
    FillBlock(block);
  }
}

void CheckRange(SlidingBuffer& buffer, size_t start, size_t size,
                size_t expected_read_size) {
  std::vector<uint8_t> store(expected_read_size, 0);
  size_t bytes_read = buffer.Read(start, store.data(), size);
  EXPECT_EQ(bytes_read, expected_read_size);
  for (unsigned int i = 0; i < bytes_read; ++i) {
    EXPECT_EQ(store[i], (i + start) % 256);
  }
}

TEST(SlidingBuffer, Sanity) {
  SlidingBuffer under_test(100);

  auto blocks = under_test.Slide(0, 50);
  EXPECT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].start, 0u);
  EXPECT_EQ(blocks[0].size, 50u);
}

TEST(SlidingBuffer, SlideForward) {
  SlidingBuffer under_test(100);

  auto blocks = under_test.Slide(400, 50);
  EXPECT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].start, 400u);
  EXPECT_EQ(blocks[0].size, 50u);
}

TEST(SlidingBuffer, SlideReverse) {
  SlidingBuffer under_test(100);

  under_test.Slide(400, 50);
  auto blocks = under_test.Slide(300, 50);
  EXPECT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].start, 300u);
  EXPECT_EQ(blocks[0].size, 50u);
}

TEST(SlidingBuffer, SlideReverseWithGap) {
  SlidingBuffer under_test(100);

  under_test.Slide(400, 50);
  auto blocks = under_test.Slide(390, 50);
  EXPECT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].start, 390u);
  EXPECT_EQ(blocks[0].size, 10u);
}

TEST(SlidingBuffer, SlideForwardWithGap) {
  SlidingBuffer under_test(100);

  under_test.Slide(400, 50);
  auto blocks = under_test.Slide(410, 50);
  EXPECT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].start, 450u);
  EXPECT_EQ(blocks[0].size, 10u);
}

TEST(SlidingBuffer, Integrity) {
  SlidingBuffer under_test(100);
  FillBlocks(under_test.Slide(400, 100));
  FillBlocks(under_test.Slide(500, 10));
  CheckRange(under_test, 420, 520, 90);
}

}  // namespace
}  // namespace media_player
