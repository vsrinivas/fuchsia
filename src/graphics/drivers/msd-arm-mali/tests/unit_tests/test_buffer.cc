// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "msd_arm_buffer.h"

class TestMsdArmBuffer {
 public:
  static void TestFlush() {
    auto buffer = MsdArmBuffer::Create(1024, "test-buffer");
    ASSERT_NE(nullptr, buffer);
    EXPECT_TRUE(buffer->EnsureRegionFlushed(100, 200));
    EXPECT_EQ(100u, buffer->flushed_region_.start());
    EXPECT_EQ(200u, buffer->flushed_region_.end());
    EXPECT_TRUE(buffer->EnsureRegionFlushed(0, 300));
    EXPECT_EQ(0u, buffer->flushed_region_.start());
    EXPECT_EQ(300u, buffer->flushed_region_.end());
    EXPECT_TRUE(buffer->EnsureRegionFlushed(0, 0));
    EXPECT_EQ(0u, buffer->flushed_region_.start());
    EXPECT_EQ(300u, buffer->flushed_region_.end());
  }
};

TEST(MsdArmBuffer, Flush) {
  TestMsdArmBuffer test;
  test.TestFlush();
}
