// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/packet_queue.h"

#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

#include <fbl/ref_ptr.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_core_clock_factory.h"

namespace media::audio {
namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

class PacketQueueTest : public gtest::TestLoopFixture {
 protected:
  std::shared_ptr<PacketQueue> CreatePacketQueue() {
    // Use a simple transform of one frame per millisecond to make validations simple in the test
    // (ex: frame 1 will be consumed after 1ms).
    auto one_frame_per_ms = fbl::MakeRefCounted<VersionedTimelineFunction>(
        TimelineFunction(TimelineRate(Fixed(1).raw_value(), 1'000'000)));

    return std::make_shared<PacketQueue>(
        Format::Create({
                           .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                           .channels = 2,
                           .frames_per_second = 48000,
                       })
            .take_value(),
        std::move(one_frame_per_ms),
        ::media::audio::testing::FakeAudioCoreClockFactory::DefaultClock());
  }

  fbl::RefPtr<Packet> CreatePacket(uint32_t payload_buffer_id, int64_t start = 0,
                                   uint32_t length = 0) {
    auto it = payload_buffers_.find(payload_buffer_id);
    if (it == payload_buffers_.end()) {
      auto vmo_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
      zx_status_t res =
          vmo_mapper->CreateAndMap(zx_system_get_page_size(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
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
    return allocator_.New(it->second, 0, length, Fixed(start), dispatcher(), callback);
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
  std::optional<size_t> flushed_after;

  // Enqueue a packet.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket(0));
  RunLoopUntilIdle();
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Flush queue (discard all packets), then enqueue another packet.
  // This should release the first packet only.
  packet_queue->Flush(PendingFlushToken::Create(dispatcher(), [&flushed_after, this]() mutable {
    flushed_after = this->released_packets().size();
  }));
  packet_queue->PushPacket(CreatePacket(1));
  RunLoopUntilIdle();

  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0}), released_packets());

  // Must have released the flush after the packet was released.
  ASSERT_TRUE(flushed_after.has_value());
  EXPECT_EQ(*flushed_after, 1u);
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
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Now pop off the packets in FIFO order.
  //
  // Packet #0:
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(20, buffer->end());
    EXPECT_EQ(packet0->payload(), buffer->payload());
    EXPECT_FALSE(packet_queue->empty());
    RunLoopUntilIdle();
    EXPECT_EQ(std::vector<int64_t>(), released_packets());
    packet0 = nullptr;
  }
  RunLoopUntilIdle();
  EXPECT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0}), released_packets());

  // Packet #1
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(20), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(20, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(40, buffer->end());
    EXPECT_EQ(packet1->payload(), buffer->payload());
    packet1 = nullptr;
  }
  RunLoopUntilIdle();
  EXPECT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1}), released_packets());

  // ...and #2
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(40), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(40, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(60, buffer->end());
    EXPECT_EQ(packet2->payload(), buffer->payload());
    packet2 = nullptr;
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2}), released_packets());
}

TEST_F(PacketQueueTest, LockUnlockMultipleReadsPerPacket) {
  auto packet_queue = CreatePacketQueue();
  const auto bytes_per_frame = packet_queue->format().bytes_per_frame();

  // Enqueue some packets.
  ASSERT_TRUE(packet_queue->empty());
  auto packet = CreatePacket(0, 0, 20);
  packet_queue->PushPacket(packet);
  ASSERT_FALSE(packet_queue->empty());

  // Read the first 10 bytes of the packet.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 10);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(10, buffer->length());
    EXPECT_EQ(10, buffer->end());
    EXPECT_EQ(packet->payload(), buffer->payload());
    EXPECT_FALSE(packet_queue->empty());
  }
  RunLoopUntilIdle();
  EXPECT_FALSE(packet_queue->empty());

  // Read the next 10 bytes of the packet.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(10), 10);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(10, buffer->start());
    EXPECT_EQ(10, buffer->length());
    EXPECT_EQ(20, buffer->end());
    EXPECT_EQ(static_cast<char*>(packet->payload()) + 10 * bytes_per_frame, buffer->payload());
    EXPECT_FALSE(packet_queue->empty());
  }
  RunLoopUntilIdle();

  // Now that the packet has been fully consumed, it should be dropped.
  EXPECT_TRUE(packet_queue->empty());
}

TEST_F(PacketQueueTest, LockUnlockNotFullyConsumed) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue some packets.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket(0, 0, 20));
  packet_queue->PushPacket(CreatePacket(1, 20, 20));
  packet_queue->PushPacket(CreatePacket(2, 40, 20));
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Pop, consume 0/20 bytes.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    buffer->set_frames_consumed(0);
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Pop, consume 5/20 bytes.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    buffer->set_frames_consumed(5);
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Pop again, consume 10/15 bytes.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(5), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(5, buffer->start());
    EXPECT_EQ(15, buffer->length());
    buffer->set_frames_consumed(10);
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Pop again, this time consume it fully.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(15), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(15, buffer->start());
    EXPECT_EQ(5, buffer->length());
    buffer->set_frames_consumed(5);
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0}), released_packets());
}

TEST_F(PacketQueueTest, LockUnlockSkipsOverPacket) {
  auto packet_queue = CreatePacketQueue();

  // Enqueue some packets.
  packet_queue->PushPacket(CreatePacket(0, 0, 20));
  packet_queue->PushPacket(CreatePacket(1, 20, 20));
  packet_queue->PushPacket(CreatePacket(2, 40, 20));
  ASSERT_FALSE(packet_queue->empty());

  // Ask for packet 0.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(20, buffer->end());
  }
  RunLoopUntilIdle();
  EXPECT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0}), released_packets());

  // Ask for packet 2, skipping over packet 1.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(40), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(40, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(60, buffer->end());
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2}), released_packets());
}

TEST_F(PacketQueueTest, LockFlushUnlock) {
  auto packet_queue = CreatePacketQueue();
  std::optional<size_t> flushed_after;

  // Enqueue some packets.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket(0, 0, 20));
  packet_queue->PushPacket(CreatePacket(1, 20, 20));
  packet_queue->PushPacket(CreatePacket(2, 40, 20));
  RunLoopUntilIdle();
  EXPECT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  {
    // Pop packet #0.
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 20);
    ASSERT_TRUE(buffer);
    ASSERT_EQ(0, buffer->start());
    ASSERT_EQ(20, buffer->length());
    ASSERT_EQ(20, buffer->end());

    // This should flush 0-3 but not 4.
    packet_queue->PushPacket(CreatePacket(3, 60, 20));
    packet_queue->Flush(PendingFlushToken::Create(dispatcher(), [&flushed_after, this]() mutable {
      flushed_after = this->released_packets().size();
    }));
    packet_queue->PushPacket(CreatePacket(4, 80, 20));

    // Now unlock the buffer.
    buffer = std::nullopt;
  }

  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2, 3}), released_packets());

  // Must have released the flush after the first 4 packets were released.
  ASSERT_TRUE(flushed_after.has_value());
  EXPECT_EQ(*flushed_after, 4u);

  {
    // Pop the remaining packet.
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(80), 20);
    ASSERT_TRUE(buffer);
    ASSERT_EQ(80, buffer->start());
  }

  RunLoopUntilIdle();
  ASSERT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2, 3, 4}), released_packets());
}

TEST_F(PacketQueueTest, LockFlushUnlockWithDuplicateTrim) {
  auto packet_queue = CreatePacketQueue();
  std::optional<size_t> flushed_after;

  // Enqueue some packets.
  ASSERT_TRUE(packet_queue->empty());
  packet_queue->PushPacket(CreatePacket(0, 0, 20));
  packet_queue->PushPacket(CreatePacket(1, 20, 20));
  packet_queue->PushPacket(CreatePacket(2, 40, 20));
  RunLoopUntilIdle();
  EXPECT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Pop, consume 0/20 bytes.
  // This trims up to frame 0.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    buffer->set_frames_consumed(0);
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Pop, consume 0/20 bytes again.
  // We're already trimmed to frame 0, so the trim is a no-op.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 20);
    ASSERT_TRUE(buffer);
    ASSERT_EQ(0, buffer->start());
    ASSERT_EQ(20, buffer->length());
    ASSERT_EQ(20, buffer->end());
    buffer->set_frames_consumed(0);

    // This should flush 0-3 but not 4.
    packet_queue->PushPacket(CreatePacket(3, 60, 20));
    packet_queue->Flush(PendingFlushToken::Create(dispatcher(), [&flushed_after, this]() mutable {
      flushed_after = this->released_packets().size();
    }));
    packet_queue->PushPacket(CreatePacket(4, 80, 20));
  }

  // Must have flushed the first 4 packets.
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2, 3}), released_packets());

  // Read the 5th packet.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(80), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(80, buffer->start());
    EXPECT_EQ(20, buffer->length());
  }
}

TEST_F(PacketQueueTest, LockReturnsNullThenFlush) {
  auto packet_queue = CreatePacketQueue();
  ASSERT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Since the queue is empty, this should return null.
  auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 10);
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
  packet_queue->Trim(Fixed(19));
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Trim again with the same limit just to verify Trim is idempotent.
  packet_queue->Trim(Fixed(19));
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>(), released_packets());

  // Now trim |packet0|
  packet_queue->Trim(Fixed(20));
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0}), released_packets());

  // Now trim |packet1| and |packet2| in one go (run until just before |packet3| should be released.
  packet_queue->Trim(Fixed(79));
  RunLoopUntilIdle();
  ASSERT_FALSE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2}), released_packets());

  // Now trim past the end of all packets
  packet_queue->Trim(Fixed(1000));
  RunLoopUntilIdle();
  ASSERT_TRUE(packet_queue->empty());
  EXPECT_EQ(std::vector<int64_t>({0, 1, 2, 3}), released_packets());
}

TEST_F(PacketQueueTest, ReportUnderflow) {
  auto packet_queue = CreatePacketQueue();

  std::vector<zx::duration> reported_underflows;
  packet_queue->SetUnderflowReporter(
      [&reported_underflows](zx::duration duration) { reported_underflows.push_back(duration); });

  // This test uses 48k fps, so 10ms = 480 frames.
  constexpr int kPacketSize = 480;
  constexpr int kFrameAt05ms = kPacketSize / 2;
  constexpr int kFrameAt15ms = kPacketSize + kPacketSize / 2;
  constexpr int kFrameAt20ms = 2 * kPacketSize;

  // Advance to t=20ms.
  {
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(0), 2 * kPacketSize);
    ASSERT_FALSE(buffer);
    EXPECT_TRUE(reported_underflows.empty());
  }

  // Queue two packets, one that fully underflows and one that partially underflows.
  packet_queue->PushPacket(CreatePacket(0, kFrameAt05ms, kPacketSize));
  packet_queue->PushPacket(CreatePacket(0, kFrameAt15ms, kPacketSize));

  // The next ReadLock Advances to t=25ms, returning part of the queued packet.
  {
    reported_underflows.clear();
    auto buffer = packet_queue->ReadLock(rlctx, Fixed(kFrameAt20ms), kPacketSize);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(kFrameAt20ms, buffer->start());  // return half of the second packet
    EXPECT_EQ(kPacketSize / 2, buffer->length());
    ASSERT_EQ(reported_underflows.size(), 2u);
    EXPECT_EQ(reported_underflows[0].get(), zx::msec(15).get());  // 1st was needed by t=5ms
    EXPECT_EQ(reported_underflows[1].get(), zx::msec(5).get());   // 2nd was needed by t=15ms
  }

  // After unlocking, the queue should now be empty.
  EXPECT_TRUE(packet_queue->empty());
}

}  // namespace
}  // namespace media::audio
