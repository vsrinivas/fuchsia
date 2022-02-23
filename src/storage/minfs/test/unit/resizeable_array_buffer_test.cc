// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/resizeable_array_buffer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace minfs {
namespace {

using ::testing::_;

const uint32_t kBlockSize = 8192;

TEST(ResizeableArrayBufferTest, Grow) {
  ResizeableArrayBuffer buffer(kBlockSize);
  ASSERT_TRUE(buffer.Grow(2).is_ok());
  EXPECT_EQ(buffer.capacity(), 2ul);
  char buf[kBlockSize];
  memset(buf, 'a', sizeof(buf));
  memcpy(buffer.Data(1), buf, kBlockSize);
  ASSERT_TRUE(buffer.Grow(50).is_ok());
  // Check that after growing, the data is still there.
  EXPECT_EQ(memcmp(buf, buffer.Data(1), kBlockSize), 0);
  EXPECT_EQ(buffer.capacity(), 50ul);
}

TEST(ResizeableArrayBufferTest, Shrink) {
  ResizeableArrayBuffer buffer(5, kBlockSize);
  char buf[kBlockSize];
  memset(buf, 'a', sizeof(buf));
  memcpy(buffer.Data(1), buf, kBlockSize);
  ASSERT_TRUE(buffer.Shrink(2).is_ok());
  EXPECT_EQ(memcmp(buf, buffer.Data(1), kBlockSize), 0);
  EXPECT_EQ(buffer.capacity(), 2ul);
}

TEST(ResizeableArrayBufferTest, Zero) {
  constexpr int kBlocks = 5;
  ResizeableArrayBuffer buffer(kBlocks, kBlockSize);
  memset(buffer.Data(0), 'a', kBlocks * kBlockSize);
  ASSERT_EQ(buffer.Zero(1, 2), ZX_OK);
  const char* p = reinterpret_cast<const char*>(buffer.Data(0));
  for (unsigned i = 0; i < kBlocks * kBlockSize; ++i) {
    if (i < 1 * kBlockSize || i >= 3 * kBlockSize) {
      EXPECT_EQ('a', p[i]);
    } else {
      EXPECT_EQ(0, p[i]);
    }
  }
}

TEST(ResizeableArrayBufferDeathTest, BadGrow) {
  ResizeableArrayBuffer buffer(10, kBlockSize);
  ASSERT_DEATH({ [[maybe_unused]] auto status = buffer.Grow(4); }, _);
}

TEST(ResizeableArrayBufferDeathTest, BadShrink) {
  ResizeableArrayBuffer buffer(10, kBlockSize);
  ASSERT_DEATH({ [[maybe_unused]] auto status = buffer.Shrink(15); }, _);
}

TEST(ResizeableArrayBufferDeathTest, BadShrink2) {
  ResizeableArrayBuffer buffer(10, kBlockSize);
  ASSERT_DEATH({ [[maybe_unused]] auto status = buffer.Shrink(0); }, _);
}

}  // namespace
}  // namespace minfs
