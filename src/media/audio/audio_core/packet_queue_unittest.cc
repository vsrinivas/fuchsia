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

  fbl::RefPtr<Packet> CreatePacket(uint32_t payload_buffer_id = 0) {
    auto vmo_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
    zx_status_t res = vmo_mapper->CreateAndMap(PAGE_SIZE, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (res != ZX_OK) {
      FX_PLOGS(ERROR, res) << "Failed to map payload buffer";
      return nullptr;
    }
    fuchsia::media::StreamPacket packet;
    packet.payload_buffer_id = payload_buffer_id;
    packet.payload_offset = 0;
    packet.payload_size = PAGE_SIZE;
    return fbl::MakeRefCounted<Packet>(
        std::move(vmo_mapper), dispatcher(), [this] { ++released_packet_count_; },
        std::move(packet), 0, 0);
  }

  size_t released_packet_count() const { return released_packet_count_; }

 private:
  size_t released_packet_count_ = 0;
};

TEST_F(PacketQueueTest, PushPacket) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue a packet.
  ASSERT_TRUE(packet_queue->empty());

  packet_queue->PushPacket(CreatePacket());
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());
}

TEST_F(PacketQueueTest, Flush) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue a packet.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket());
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());

  // Flush queue (discard all packets). Expect to see one packet released back to us.
  packet_queue->Flush(nullptr);
  RunLoopUntilIdle();

  ASSERT_TRUE(packet_queue->empty());
  ASSERT_EQ(1u, released_packet_count());
}

// Simulate the packet sink popping packets off the queue.
TEST_F(PacketQueueTest, LockUnlockPacket) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue some packets.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket(0));
  packet_queue->PushPacket(CreatePacket(1));
  packet_queue->PushPacket(CreatePacket(2));
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());

  // Now pop off the packets in FIFO order.
  //
  // Packet #0:
  bool was_flushed = false;
  auto packet = packet_queue->LockPacket(&was_flushed);
  ASSERT_TRUE(was_flushed);
  ASSERT_NE(nullptr, packet);
  ASSERT_EQ(0u, packet->payload_buffer_id());
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(0u, released_packet_count());
  packet = nullptr;
  packet_queue->UnlockPacket(true);
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(1u, released_packet_count());

  // Packet #1
  packet = packet_queue->LockPacket(&was_flushed);
  ASSERT_FALSE(was_flushed);
  ASSERT_NE(nullptr, packet);
  ASSERT_EQ(1u, packet->payload_buffer_id());
  packet = nullptr;
  packet_queue->UnlockPacket(true);
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  ASSERT_EQ(2u, released_packet_count());

  // ...and #2
  packet = packet_queue->LockPacket(&was_flushed);
  ASSERT_FALSE(was_flushed);
  ASSERT_NE(nullptr, packet);
  ASSERT_EQ(2u, packet->payload_buffer_id());
  packet = nullptr;
  packet_queue->UnlockPacket(true);
  RunLoopUntilIdle();
  ASSERT_TRUE(packet_queue->empty());
  ASSERT_EQ(3u, released_packet_count());
}

}  // namespace
}  // namespace media::audio
