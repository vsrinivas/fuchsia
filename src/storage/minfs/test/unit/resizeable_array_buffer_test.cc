// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/resizeable_array_buffer.h"

#include <zxtest/zxtest.h>

namespace minfs {
namespace {

const uint32_t kBlockSize = 8192;

TEST(ResizeableArrayBufferTest, Grow) {
  ResizeableArrayBuffer buffer(kBlockSize);
  ASSERT_OK(buffer.Grow(2));
  EXPECT_EQ(2, buffer.capacity());
  char buf[kBlockSize];
  memset(buf, 'a', sizeof(buf));
  memcpy(buffer.Data(1), buf, kBlockSize);
  ASSERT_OK(buffer.Grow(50));
  // Check that after growing, the data is still there.
  EXPECT_BYTES_EQ(buf, buffer.Data(1), kBlockSize);
  EXPECT_EQ(50, buffer.capacity());
}

TEST(ResizeableArrayBufferTest, Shrink) {
  ResizeableArrayBuffer buffer(5, kBlockSize);
  char buf[kBlockSize];
  memset(buf, 'a', sizeof(buf));
  memcpy(buffer.Data(1), buf, kBlockSize);
  ASSERT_OK(buffer.Shrink(2));
  EXPECT_BYTES_EQ(buf, buffer.Data(1), kBlockSize);
  EXPECT_EQ(2, buffer.capacity());
}

TEST(ResizeableArrayBufferTest, Zero) {
  constexpr int kBlocks = 5;
  ResizeableArrayBuffer buffer(kBlocks, kBlockSize);
  memset(buffer.Data(0), 'a', kBlocks * kBlockSize);
  buffer.Zero(1, 2);
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
  ASSERT_DEATH([&]() { [[maybe_unused]] zx_status_t status = buffer.Grow(4); });
}

TEST(ResizeableArrayBufferDeathTest, BadShrink) {
  ResizeableArrayBuffer buffer(10, kBlockSize);
  ASSERT_DEATH([&]() { [[maybe_unused]] zx_status_t status = buffer.Shrink(15); });
}

TEST(ResizeableArrayBufferDeathTest, BadShrink2) {
  ResizeableArrayBuffer buffer(10, kBlockSize);
  ASSERT_DEATH([&]() { [[maybe_unused]] zx_status_t status = buffer.Shrink(0); });
}

}  // namespace
}  // namespace minfs
