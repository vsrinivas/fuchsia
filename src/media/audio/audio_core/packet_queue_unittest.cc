// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/packet_queue.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/lib/clock/clone_mono.h"

namespace media::audio {
namespace {

class PacketQueueTest : public gtest::TestLoopFixture {
 protected:
  std::unique_ptr<PacketQueue> CreatePacketQueue() {
    // Use a simple transform of one frame per millisecond to make validations simple in the test
    // (ex: frame 1 will be consumed after 1ms).
    auto one_frame_per_ms = fbl::MakeRefCounted<VersionedTimelineFunction>(
        TimelineFunction(TimelineRate(Fixed(1).raw_value(), 1'000'000)));

    return std::make_unique<PacketQueue>(
        Format::Create({
                           .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                           .channels = 2,
                           .frames_per_second = 48000,
                       })
            .take_value(),
        std::move(one_frame_per_ms),
        AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic()));
  }

  fbl::RefPtr<Packet> CreatePacket(uint32_t payload_buffer_id, int64_t start = 0,
                                   uint32_t length = 0) {
    auto it = payload_buffers_.find(payload_buffer_id);
    if (it == payload_buffers_.end()) {
      auto vmo_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
      zx_status_t res = vmo_mapper->CreateAndMap(PAGE_SIZE, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
      if (res != ZX_OK) {
        FX_PLOGS(ERROR, res) << "Failed to map payload buffer";
        return nullptr;
      }
      auto result = payload_buffers_.emplace(payload_buffer_id, std::move(vmo_mapper));
      FX_CHECK(result.second);
      it = result.first;
    }
    auto callback = [this, payload_buffer_id] {
      ++released_packet_count_;
      released_packets_.push_back(payload_buffer_id);
    };
    return allocator_.New(it->second, 0, Fixed(length), Fixed(start), dispatcher(), callback);
  }

  std::vector<int64_t> released_packets() const { return released_packets_; }

 private:
  Packet::Allocator allocator_{1, true};
  size_t released_packet_count_ = 0;
  std::unordered_map<uint32_t, fbl::RefPtr<RefCountedVmoMapper>> payload_buffers_;
  std::vector<int64_t> released_packets_;
};

TEST_F(PacketQueueTest, PushPacket) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue a packet.
  ASSERT_TRUE(packet_queue->empty());

  packet_queue->PushPacket(CreatePacket(0));
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());
}

TEST_F(PacketQueueTest, Flush) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue a packet.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket(0));
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Flush queue (discard all packets), then enqueue another packet.
  // This should release the first packet only.
  packet_queue->Flush(nullptr);
  packet_queue->PushPacket(CreatePacket(1));
  RunLoopUntilIdle();

  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0}), released_packets());
}

TEST_F(PacketQueueTest, LockUnlock) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue some packets.
  ASSERT_TRUE(packet_queue->empty());
  auto packet0 = CreatePacket(0, 0, 20);
  auto packet1 = CreatePacket(1, 20, 20);
  auto packet2 = CreatePacket(2, 40, 20);

  packet_queue->PushPacket(packet0);
  packet_queue->PushPacket(packet1);
  packet_queue->PushPacket(packet2);
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Now pop off the packets in FIFO order.
  //
  // Packet #0:
  {
    auto buffer = packet_queue->ReadLock(0, 0);
    ASSERT_TRUE(buffer);
    ASSERT_FALSE(buffer->is_continuous());
    ASSERT_EQ(0, buffer->start());
    ASSERT_EQ(20, buffer->length());
    ASSERT_EQ(20, buffer->end());
    ASSERT_EQ(packet0->payload(), buffer->payload());
    ASSERT_FALSE(packet_queue->empty());
    EXPECT_EQ(std::vector<int64_t>(), released_packets());
    packet0 = nullptr;
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0}), released_packets());

  // Packet #1
  {
    auto buffer = packet_queue->ReadLock(0, 0);
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(buffer->is_continuous());
    ASSERT_EQ(20, buffer->start());
    ASSERT_EQ(20, buffer->length());
    ASSERT_EQ(40, buffer->end());
    ASSERT_EQ(packet1->payload(), buffer->payload());
    packet1 = nullptr;
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1}), released_packets());

  // ...and #2
  {
    auto buffer = packet_queue->ReadLock(0, 0);
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(buffer->is_continuous());
    ASSERT_EQ(40, buffer->start());
    ASSERT_EQ(20, buffer->length());
    ASSERT_EQ(60, buffer->end());
    ASSERT_EQ(packet2->payload(), buffer->payload());
    packet2 = nullptr;
  }
  RunLoopUntilIdle();
  ASSERT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2}), released_packets());
}

TEST_F(PacketQueueTest, LockUnlockNotFullyConsumed) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue some packets.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket(0, 0, 20));
  packet_queue->PushPacket(CreatePacket(1, 20, 20));
  packet_queue->PushPacket(CreatePacket(2, 40, 20));
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Pop but don't fully consume.
  {
    auto buffer = packet_queue->ReadLock(0, 0);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    buffer->set_is_fully_consumed(false);
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Pop again, this time consume it fully.
  {
    auto buffer = packet_queue->ReadLock(0, 0);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    buffer->set_is_fully_consumed(true);
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0}), released_packets());
}

TEST_F(PacketQueueTest, LockFlushUnlock) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue some packets.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket(0, 0, 20));
  packet_queue->PushPacket(CreatePacket(1, 20, 20));
  packet_queue->PushPacket(CreatePacket(2, 40, 20));
  EXPECT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  {
    // Pop packet #0.
    auto buffer = packet_queue->ReadLock(0, 0);
    ASSERT_TRUE(buffer);
    ASSERT_FALSE(buffer->is_continuous());
    ASSERT_EQ(0, buffer->start());
    ASSERT_EQ(20, buffer->length());
    ASSERT_EQ(20, buffer->end());

    // This should flush 0-3 but not 4.
    packet_queue->PushPacket(CreatePacket(3, 60, 20));
    packet_queue->Flush(nullptr);
    packet_queue->PushPacket(CreatePacket(4, 80, 20));

    // Now unlock the buffer.
    buffer = std::nullopt;
  }

  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2, 3}), released_packets());

  {
    // Pop the remaining packet.
    auto buffer = packet_queue->ReadLock(0, 0);
    ASSERT_TRUE(buffer);
    ASSERT_EQ(80, buffer->start());
  }

  RunLoopUntilIdle();
  ASSERT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2, 3, 4}), released_packets());
}

TEST_F(PacketQueueTest, LockReturnsNullThenFlush) {
  auto packet_queue = CreatePacketQueue();
  ASSERT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Since the queue is empty, this should return null.
  auto buffer = packet_queue->ReadLock(0, 10);
  EXPECT_FALSE(buffer);

  // Push some packets, then flush them immediately
  packet_queue->PushPacket(CreatePacket(0, 0, 20));
  packet_queue->PushPacket(CreatePacket(1, 20, 20));
  packet_queue->PushPacket(CreatePacket(2, 40, 20));
  packet_queue->Flush(nullptr);

  RunLoopUntilIdle();
  ASSERT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2}), released_packets());
}

TEST_F(PacketQueueTest, Trim) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue some packets.
  {
    ASSERT_TRUE(packet_queue->empty());
    auto packet0 = CreatePacket(0, 0, 20);
    auto packet1 = CreatePacket(1, 20, 20);
    auto packet2 = CreatePacket(2, 40, 20);
    auto packet3 = CreatePacket(3, 60, 20);

    packet_queue->PushPacket(packet0);
    packet_queue->PushPacket(packet1);
    packet_queue->PushPacket(packet2);
    packet_queue->PushPacket(packet3);
  }
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // The last frame in the first packet is frame 19.
  // Verify this trimming at that frame does not release the first packet.
  packet_queue->Trim(19);
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Trim again with the same limit just to verify Trim is idempotent.
  packet_queue->Trim(19);
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Now trim |packet0|
  packet_queue->Trim(20);
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0}), released_packets());

  // Now trim |packet1| and |packet2| in one go (run until just before |packet3| should be released.
  packet_queue->Trim(79);
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2}), released_packets());

  // Now trim past the end of all packets
  packet_queue->Trim(1000);
  RunLoopUntilIdle();
  ASSERT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2, 3}), released_packets());
}

}  // namespace
}  // namespace media::audio
