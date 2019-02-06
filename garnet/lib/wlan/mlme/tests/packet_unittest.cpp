// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/packet.h>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace wlan {
namespace {

class PacketQueueTest : public ::testing::Test {
   protected:
    PacketQueue queue_;
};

TEST(PacketTest, BufferAlloc) {
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

TEST(PacketTest, BufferMaxOut) {
    constexpr size_t buffer_cnt_max = ::wlan::kHugeSlabs * ::wlan::kHugeBuffers;
    fbl::unique_ptr<Buffer> buffers[buffer_cnt_max + 1];

    for (uint32_t i = 0; i < buffer_cnt_max; i++) {
        buffers[i] = GetBuffer(kHugeBufferSize);
        ASSERT_NE(buffers[i], nullptr);
    }
    buffers[buffer_cnt_max] = GetBuffer(kHugeBufferSize);
    EXPECT_EQ(buffers[buffer_cnt_max], nullptr);
}

TEST(PacketTest, BufferFallback) {
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

TEST_F(PacketQueueTest, Empty) {
    ASSERT_TRUE(queue_.is_empty());
    ASSERT_EQ(queue_.size(), 0ULL);
}

TEST_F(PacketQueueTest, EnqueueAndDequeue) {
    queue_.Enqueue(GetWlanPacket(1));
    ASSERT_EQ(queue_.size(), 1ULL);
    queue_.Enqueue(GetEthPacket(2));
    ASSERT_EQ(queue_.size(), 2ULL);

    auto packet = queue_.Dequeue();
    ASSERT_EQ(packet->peer(), Packet::Peer::kWlan);
    packet = queue_.Dequeue();
    ASSERT_EQ(packet->peer(), Packet::Peer::kEthernet);
    ASSERT_TRUE(queue_.is_empty());

    packet = queue_.Dequeue();
    ASSERT_EQ(packet, nullptr);
    ASSERT_TRUE(queue_.is_empty());
}

TEST_F(PacketQueueTest, EnqueueAndUndoEnqueue) {
    queue_.Enqueue(GetWlanPacket(1));
    queue_.Enqueue(GetEthPacket(2));

    queue_.UndoEnqueue();
    auto packet = queue_.Dequeue();
    ASSERT_EQ(packet->peer(), Packet::Peer::kWlan);
    ASSERT_TRUE(queue_.is_empty());
}

TEST_F(PacketQueueTest, Move) {
    queue_.Enqueue(GetWlanPacket(1));
    auto queue = std::move(queue_);
    ASSERT_EQ(queue.size(), 1ULL);
    ASSERT_TRUE(queue_.is_empty());

    auto packet = queue.Dequeue();
    ASSERT_EQ(packet->peer(), Packet::Peer::kWlan);
    ASSERT_EQ(packet->size(), 1ULL);
}

TEST_F(PacketQueueTest, Drain) {
    queue_.Enqueue(GetWlanPacket(1));
    auto queue = queue_.Drain();
    ASSERT_EQ(queue.size(), 1ULL);
    ASSERT_TRUE(queue_.is_empty());

    auto packet = queue.Dequeue();
    ASSERT_EQ(packet->peer(), Packet::Peer::kWlan);
    ASSERT_EQ(packet->size(), 1ULL);
}
}  // namespace
}  // namespace wlan
