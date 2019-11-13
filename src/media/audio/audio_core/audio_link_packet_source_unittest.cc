// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_link_packet_source.h"

#include <lib/gtest/test_loop_fixture.h>

#include "src/media/audio/audio_core/audio_object.h"

namespace media::audio {
namespace {

class FakeAudioObject : public AudioObject {
 public:
  FakeAudioObject(AudioObject::Type type) : AudioObject(type) {}
};

class AudioLinkPacketSourceTest : public gtest::TestLoopFixture {
 protected:
  // Creates a new AudioLinkPacketSource.
  fbl::RefPtr<AudioLinkPacketSource> CreateAudioLinkPacketSource() {
    auto input = fbl::AdoptRef(new FakeAudioObject(AudioObject::Type::AudioRenderer));
    auto output = fbl::AdoptRef(new FakeAudioObject(AudioObject::Type::Output));
    // The values chosen here are not important. The AudioLinkPacketSource doesn't actualy use this
    // internally so anything will do here for the purposes of these tests. It just has to be sane
    // enough to pass the validation done in |Format|.
    auto stream_type = fuchsia::media::AudioStreamType{
        .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
        .channels = 2,
        .frames_per_second = 100,
    };
    return AudioLinkPacketSource::Create(input, output, Format::Create(stream_type));
  }

  fbl::RefPtr<AudioPacketRef> CreateAudioPacketRef(uint32_t payload_buffer_id = 0) {
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
    return fbl::MakeRefCounted<AudioPacketRef>(
        std::move(vmo_mapper), dispatcher(), [this] { ++released_packet_count_; },
        std::move(packet), 0, 0);
  }

  size_t released_packet_count() const { return released_packet_count_; }

 private:
  size_t released_packet_count_ = 0;
};

TEST_F(AudioLinkPacketSourceTest, PushToPendingQueue) {
  auto link = CreateAudioLinkPacketSource();

  // Enqueue a packet.
  ASSERT_TRUE(link->pending_queue_empty());

  link->PushToPendingQueue(CreateAudioPacketRef());
  ASSERT_FALSE(link->pending_queue_empty());
  ASSERT_EQ(0u, released_packet_count());
}

TEST_F(AudioLinkPacketSourceTest, FlushPendingQueue) {
  auto link = CreateAudioLinkPacketSource();

  // Enqueue a packet.
  ASSERT_TRUE(link->pending_queue_empty());
  link->PushToPendingQueue(CreateAudioPacketRef());
  ASSERT_FALSE(link->pending_queue_empty());
  ASSERT_EQ(0u, released_packet_count());

  // Flush queue (discard all packets). Expect to see one packet released back to us.
  link->FlushPendingQueue(nullptr);
  RunLoopUntilIdle();

  ASSERT_TRUE(link->pending_queue_empty());
  ASSERT_EQ(1u, released_packet_count());
}

// Simulate the packet sink popping packets off the pending queue.
TEST_F(AudioLinkPacketSourceTest, LockUnlockPendingQueue) {
  auto link = CreateAudioLinkPacketSource();

  // Enqueue some packets.
  ASSERT_TRUE(link->pending_queue_empty());
  link->PushToPendingQueue(CreateAudioPacketRef(0));
  link->PushToPendingQueue(CreateAudioPacketRef(1));
  link->PushToPendingQueue(CreateAudioPacketRef(2));
  ASSERT_FALSE(link->pending_queue_empty());
  ASSERT_EQ(0u, released_packet_count());

  // Now pop off the packets in FIFO order.
  //
  // Packet #0:
  bool was_flushed = false;
  auto packet = link->LockPendingQueueFront(&was_flushed);
  ASSERT_TRUE(was_flushed);
  ASSERT_NE(nullptr, packet);
  ASSERT_EQ(0u, packet->payload_buffer_id());
  ASSERT_FALSE(link->pending_queue_empty());
  ASSERT_EQ(0u, released_packet_count());
  packet = nullptr;
  link->UnlockPendingQueueFront(true);
  RunLoopUntilIdle();
  ASSERT_FALSE(link->pending_queue_empty());
  ASSERT_EQ(1u, released_packet_count());

  // Packet #1
  packet = link->LockPendingQueueFront(&was_flushed);
  ASSERT_FALSE(was_flushed);
  ASSERT_NE(nullptr, packet);
  ASSERT_EQ(1u, packet->payload_buffer_id());
  packet = nullptr;
  link->UnlockPendingQueueFront(true);
  RunLoopUntilIdle();
  ASSERT_FALSE(link->pending_queue_empty());
  ASSERT_EQ(2u, released_packet_count());

  // ...and #2
  packet = link->LockPendingQueueFront(&was_flushed);
  ASSERT_FALSE(was_flushed);
  ASSERT_NE(nullptr, packet);
  ASSERT_EQ(2u, packet->payload_buffer_id());
  packet = nullptr;
  link->UnlockPendingQueueFront(true);
  RunLoopUntilIdle();
  ASSERT_TRUE(link->pending_queue_empty());
  ASSERT_EQ(3u, released_packet_count());
}

}  // namespace
}  // namespace media::audio
