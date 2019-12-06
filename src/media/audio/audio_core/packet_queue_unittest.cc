// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/packet_queue.h"

#include <lib/gtest/test_loop_fixture.h>

#include <fbl/ref_ptr.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {
namespace {

class PacketQueueTest : public gtest::TestLoopFixture {
 protected:
  fbl::RefPtr<PacketQueue> CreatePacketQueue() {
    return fbl::MakeRefCounted<PacketQueue>(Format{{
        .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
        .channels = 2,
        .frames_per_second = 48000,
    }});
  }

  fbl::RefPtr<Packet> CreatePacket(
      uint32_t payload_buffer_id, FractionalFrames<int64_t> start = FractionalFrames<int64_t>(0),
      FractionalFrames<uint32_t> length = FractionalFrames<uint32_t>(0)) {
    auto vmo_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
    zx_status_t res = vmo_mapper->CreateAndMap(PAGE_SIZE, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (res != ZX_OK) {
      FX_PLOGS(ERROR, res) << "Failed to map payload buffer";
      return nullptr;
    }
    return fbl::MakeRefCounted<Packet>(std::move(vmo_mapper), 0, length, start, dispatcher(),
                                       [this] { ++released_packet_count_; });
  }

  size_t released_packet_count() const { return released_packet_count_; }

 private:
  size_t released_packet_count_ = 0;
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
  auto buffer = packet_queue->LockBuffer();
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
  buffer = packet_queue->LockBuffer();
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
  buffer = packet_queue->LockBuffer();
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

}  // namespace
}  // namespace media::audio
