// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/packet.h>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace wlan {
namespace {

class PacketTest : public ::testing::Test {
   protected:
};

TEST_F(PacketTest, BufferAlloc) {
    auto buffer = GetBuffer(kSmallBufferSize - 1);
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->capacity(), kSmallBufferSize);

    buffer = GetBuffer(kSmallBufferSize);
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->capacity(), kSmallBufferSize);

    buffer = GetBuffer(kSmallBufferSize + 1);
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->capacity(), kLargeBufferSize);

    buffer = GetBuffer(kLargeBufferSize - 1);
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->capacity(), kLargeBufferSize);

    buffer = GetBuffer(kLargeBufferSize);
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->capacity(), kLargeBufferSize);

    buffer = GetBuffer(kLargeBufferSize + 1);
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->capacity(), kHugeBufferSize);

    buffer = GetBuffer(kHugeBufferSize - 1);
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->capacity(), kHugeBufferSize);

    buffer = GetBuffer(kHugeBufferSize);
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->capacity(), kHugeBufferSize);

    buffer = GetBuffer(kHugeBufferSize + 1);
    ASSERT_TRUE(buffer == nullptr);
}

TEST_F(PacketTest, BufferMaxOut) {
    constexpr size_t buffer_cnt_max = ::wlan::kHugeSlabs * ::wlan::kHugeBuffers;
    fbl::unique_ptr<Buffer> buffers[buffer_cnt_max + 1];

    for (uint32_t i = 0; i < buffer_cnt_max; i++) {
        buffers[i] = GetBuffer(kHugeBufferSize);
        ASSERT_NE(buffers[i], nullptr);
    }
    buffers[buffer_cnt_max] = GetBuffer(kHugeBufferSize);
    EXPECT_EQ(buffers[buffer_cnt_max], nullptr);
}

TEST_F(PacketTest, BufferFallback) {
    constexpr size_t buffer_cnt_max = ::wlan::kSmallSlabs * ::wlan::kSmallBuffers;
    fbl::unique_ptr<Buffer> buffers[buffer_cnt_max + 1];

    for (uint32_t i = 0; i < buffer_cnt_max; i++) {
        buffers[i] = GetBuffer(kSmallBufferSize);
        EXPECT_NE(buffers[i], nullptr);
        EXPECT_EQ(buffers[i]->capacity(), kSmallBufferSize);
    }
    buffers[buffer_cnt_max] = GetBuffer(kSmallBufferSize);
    ASSERT_NE(buffers[buffer_cnt_max], nullptr);
    EXPECT_EQ(buffers[buffer_cnt_max]->capacity(), kLargeBufferSize);
}

}  // namespace
}  // namespace wlan
