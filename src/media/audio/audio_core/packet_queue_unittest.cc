// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/packet_queue.h"

#include <lib/gtest/test_loop_fixture.h>

#include <unordered_map>

#include <fbl/ref_ptr.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {
namespace {

class PacketQueueTest : public gtest::TestLoopFixture {
 protected:
  std::unique_ptr<PacketQueue> CreatePacketQueue() {
    // Use a simple transform of one frame per millisecond to make validations simple in the test
    // (ex: frame 1 will be consumed after 1ms).
    auto one_frame_per_ms = fbl::MakeRefCounted<VersionedTimelineFunction>(
        TimelineFunction(TimelineRate(FractionalFrames<uint32_t>(1).raw_value(), 1'000'000)));
    return std::make_unique<PacketQueue>(
        Format{{
            .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
            .channels = 2,
            .frames_per_second = 48000,
        }},
        std::move(one_frame_per_ms));
  }

  fbl::RefPtr<Packet> CreatePacket(
      uint32_t payload_buffer_id, FractionalFrames<int64_t> start = FractionalFrames<int64_t>(0),
      FractionalFrames<uint32_t> length = FractionalFrames<uint32_t>(0),
      bool* release_flag = nullptr) {
    if (release_flag) {
      *release_flag = false;
    }
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
    auto callback = [this, release_flag] {
      ++released_packet_count_;
      if (release_flag) {
        *release_flag = true;
      }
    };
    return fbl::MakeRefCounted<Packet>(it->second, 0, length, start, dispatcher(), callback);
  }

  size_t released_packet_count() const { return released_packet_count_; }

  zx::time time_after(zx::duration duration) { return zx::time(duration.to_nsecs()); }

 private:
  size_t released_packet_count_ = 0;
  std::unordered_map<uint32_t, fbl::RefPtr<RefCountedVmoMapper>> payload_buffers_;
};

TEST_F(PacketQueueTest, PushPacket) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue a packet.
  ASSERT_TRUE(packet_queue->empty());

  packet_queue->PushPacket(CreatePacket(0));
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());
}

TEST_F(PacketQueueTest, Flush) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue a packet.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket(0));
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());

  // Flush queue (discard all packets). Expect to see one packet released back to us.
  packet_queue->Flush(nullptr);
  RunLoopUntilIdle();

  ASSERT_TRUE(packet_queue->empty());
  ASSERT_EQ(1u, released_packet_count());
}

// Simulate the packet sink popping packets off the queue.
TEST_F(PacketQueueTest, LockUnlockBuffer) {
  auto packet_queue = CreatePacketQueue();

  auto packet_frame = FractionalFrames<int64_t>(0);
  auto packet_length = FractionalFrames<uint32_t>(20);

  // Enqueue some packets.
  ASSERT_TRUE(packet_queue->empty());
  auto packet0 = CreatePacket(0, packet_frame, packet_length);
  packet_frame += packet_length;
  auto packet1 = CreatePacket(1, packet_frame, packet_length);
  packet_frame += packet_length;
  auto packet2 = CreatePacket(2, packet_frame, packet_length);

  packet_queue->PushPacket(packet0);
  packet_queue->PushPacket(packet1);
  packet_queue->PushPacket(packet2);
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());

  // Now pop off the packets in FIFO order.
  //
  // Packet #0:
  auto buffer = packet_queue->LockBuffer(Now(), 0, 0);
  ASSERT_TRUE(buffer);
  ASSERT_FALSE(buffer->is_continuous());
  ASSERT_EQ(0, buffer->start());
  ASSERT_EQ(20u, buffer->length());
  ASSERT_EQ(20, buffer->end());
  ASSERT_EQ(packet0->payload(), buffer->payload());
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());
  packet0 = nullptr;
  packet_queue->UnlockBuffer(true);
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(1u, released_packet_count());

  // Packet #1
  buffer = packet_queue->LockBuffer(Now(), 0, 0);
  ASSERT_TRUE(buffer);
  ASSERT_TRUE(buffer->is_continuous());
  ASSERT_EQ(20, buffer->start());
  ASSERT_EQ(20u, buffer->length());
  ASSERT_EQ(40, buffer->end());
  ASSERT_EQ(packet1->payload(), buffer->payload());
  packet1 = nullptr;
  packet_queue->UnlockBuffer(true);
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(2u, released_packet_count());

  // ...and #2
  buffer = packet_queue->LockBuffer(Now(), 0, 0);
  ASSERT_TRUE(buffer);
  ASSERT_TRUE(buffer->is_continuous());
  ASSERT_EQ(40, buffer->start());
  ASSERT_EQ(20u, buffer->length());
  ASSERT_EQ(60, buffer->end());
  ASSERT_EQ(packet2->payload(), buffer->payload());
  packet2 = nullptr;
  packet_queue->UnlockBuffer(true);
  RunLoopUntilIdle();
  ASSERT_TRUE(packet_queue->empty());
  ASSERT_EQ(3u, released_packet_count());
}

TEST_F(PacketQueueTest, Trim) {
  auto packet_queue = CreatePacketQueue();

  auto packet_frame = FractionalFrames<int64_t>(0);
  auto packet_length = FractionalFrames<uint32_t>(20);

  // Enqueue some packets.
  bool packet0_released, packet1_released, packet2_released, packet3_released;
  {
    ASSERT_TRUE(packet_queue->empty());
    auto packet0 = CreatePacket(0, packet_frame, packet_length, &packet0_released);
    packet_frame += packet_length;
    auto packet1 = CreatePacket(0, packet_frame, packet_length, &packet1_released);
    packet_frame += packet_length;
    auto packet2 = CreatePacket(0, packet_frame, packet_length, &packet2_released);
    packet_frame += packet_length;
    auto packet3 = CreatePacket(0, packet_frame, packet_length, &packet3_released);

    packet_queue->PushPacket(packet0);
    packet_queue->PushPacket(packet1);
    packet_queue->PushPacket(packet2);
    packet_queue->PushPacket(packet3);
  }
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());
  ASSERT_FALSE(packet0_released);
  ASSERT_FALSE(packet1_released);
  ASSERT_FALSE(packet2_released);
  ASSERT_FALSE(packet3_released);

  // The first packet should be trimmed after 20ms. Verify we haven't released at the instant
  // before.
  packet_queue->Trim(time_after(zx::msec(20) - zx::nsec(1)));
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());

  // Trim again with the same deadline just to verify Trim is idempotent.
  packet_queue->Trim(time_after(zx::msec(20) - zx::nsec(1)));
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());

  // Now trim |packet0|
  packet_queue->Trim(time_after(zx::msec(20)));
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(1u, released_packet_count());
  ASSERT_TRUE(packet0_released);

  // Now trim |packet1| and |packet2| in one go (run until just before |packet3| should be released.
  packet_queue->Trim(time_after(zx::msec(80) - zx::nsec(1)));
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(3u, released_packet_count());
  ASSERT_TRUE(packet1_released);
  ASSERT_TRUE(packet2_released);

  // Now trim past the end of all packets
  packet_queue->Trim(time_after(zx::sec(1)));
  RunLoopUntilIdle();
  ASSERT_TRUE(packet_queue->empty());
  ASSERT_EQ(4u, released_packet_count());
  ASSERT_TRUE(packet3_released);
}

}  // namespace
}  // namespace media::audio
