// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/producer_stage.h"

#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/simple_ring_buffer_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using RealTime = StartStopControl::RealTime;
using WhichClock = StartStopControl::WhichClock;
using ::fuchsia_audio::SampleType;
using ::testing::ElementsAre;

const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});

// The majority of test cases use a packet queue for the internal source because packet queues are
// easier to setup than ring buffers.
class ProducerStageTestWithPacketQueue : public ::testing::Test {
 public:
  ProducerStageTestWithPacketQueue()
      : start_stop_command_queue_(std::make_shared<ProducerStage::CommandQueue>()),
        packet_command_queue_(std::make_shared<SimplePacketQueueProducerStage::CommandQueue>()),
        producer_stage_({
            .format = kFormat,
            .reference_clock = DefaultUnreadableClock(),
            .command_queue = start_stop_command_queue_,
            .internal_source = std::make_shared<SimplePacketQueueProducerStage>(
                SimplePacketQueueProducerStage::Args{
                    .format = kFormat,
                    .reference_clock = DefaultUnreadableClock(),
                    .command_queue = packet_command_queue_,
                }),
        }) {
    producer_stage_.UpdatePresentationTimeToFracFrame(DefaultPresentationTimeToFracFrame(kFormat));
  }

  const void* SendPushPacketCommand(uint32_t packet_id, int64_t start = 0, int64_t length = 1) {
    auto& packet = NewPacket(packet_id, start, length);
    packet_command_queue_->push(SimplePacketQueueProducerStage::PushPacketCommand{
        .packet = packet.view,
        .fence = packet.fence.Take(),
    });
    return packet.payload.data();
  }

  void SendClearCommand(zx::eventpair fence) {
    packet_command_queue_->push(
        SimplePacketQueueProducerStage::ClearCommand{.fence = std::move(fence)});
  }

  void SendStartCommand(zx::time start_presentation_time, Fixed start_frame) {
    start_stop_command_queue_->push(ProducerStage::StartCommand{
        .start_time = RealTime{.clock = WhichClock::Reference, .time = start_presentation_time},
        .start_frame = start_frame,
    });
  }

  void SendStopCommand(Fixed stop_frame) {
    start_stop_command_queue_->push(ProducerStage::StopCommand{
        .when = stop_frame,
    });
  }

  ProducerStage& producer_stage() { return producer_stage_; }

  const std::vector<uint32_t>& released_packets() {
    for (auto& [id, packet] : packets_) {
      if (!packet.released && packet.fence.Done()) {
        released_packets_.push_back(id);
        packet.released = true;
      }
    }
    return released_packets_;
  }

  void TestInternalFramesOffsetBehind();
  void TestInternalFramesOffsetAhead();

 private:
  struct Packet {
    explicit Packet(int64_t start, int64_t length)
        : payload(length, 0.0f),
          view({
              .format = kFormat,
              .start = Fixed(start),
              .length = length,
              .payload = payload.data(),
          }) {}

    std::vector<float> payload;
    PacketView view;
    TestFence fence;
    bool released = false;
  };

  Packet& NewPacket(uint32_t packet_id, int64_t start, int64_t length) {
    auto [it, inserted] = packets_.try_emplace(packet_id, start, length);
    FX_CHECK(inserted) << "duplicate packet with id " << packet_id;
    return it->second;
  }

  std::shared_ptr<ProducerStage::CommandQueue> start_stop_command_queue_;
  std::shared_ptr<SimplePacketQueueProducerStage::CommandQueue> packet_command_queue_;
  ProducerStage producer_stage_;
  std::map<int32_t, Packet> packets_;  // ordered map so iteration is deterministic
  std::vector<uint32_t> released_packets_;
};

TEST_F(ProducerStageTestWithPacketQueue, ReadWhileStarted) {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Start the Producer at t=0.
  // The internal and downstream frame timelines are identical.
  SendStartCommand(zx::time(0), Fixed(0));

  // Push two packets onto the command queue.
  const void* payload_0 = SendPushPacketCommand(0, 0, 20);
  const void* payload_1 = SendPushPacketCommand(1, 20, 20);
  EXPECT_TRUE(released_packets().empty());

  // Pop the first two packets.
  {
    // Packet #0
    const auto buffer = producer.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(20, buffer->end());
    EXPECT_EQ(payload_0, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0));

  {
    // Packet #1
    const auto buffer = producer.Read(DefaultCtx(), Fixed(20), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(20, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(40, buffer->end());
    EXPECT_EQ(payload_1, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));

  // Push one more packet.
  const void* payload_2 = SendPushPacketCommand(2, 40, 20);

  {
    // Packet #2
    const auto buffer = producer.Read(DefaultCtx(), Fixed(40), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(40, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(60, buffer->end());
    EXPECT_EQ(payload_2, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1, 2));
}

TEST_F(ProducerStageTestWithPacketQueue, ReadAfterClearWhileStarted) {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Start the Producer at t=0.
  // The internal and downstream frame timelines are identical.
  SendStartCommand(zx::time(0), Fixed(0));

  // Push some packets, then a clear command, then payload_3 and payload_4.
  // This should drop everything before payload_3.
  SendPushPacketCommand(0, 0, 20);
  SendPushPacketCommand(1, 20, 20);
  SendPushPacketCommand(2, 40, 20);
  TestFence clear_fence;
  SendClearCommand(clear_fence.Take());
  const void* payload_3 = SendPushPacketCommand(3, 60, 20);
  const void* payload_4 = SendPushPacketCommand(4, 80, 20);

  {
    // Start reading at packet #2 but allow up through packet #3.
    // This should return packet #3.
    const auto buffer = producer.Read(DefaultCtx(), Fixed(40), 40);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(60, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(80, buffer->end());
    EXPECT_EQ(payload_3, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1, 2, 3));
  EXPECT_TRUE(clear_fence.Done());

  {
    // Packet #5.
    const auto buffer = producer.Read(DefaultCtx(), Fixed(80), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(80, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(100, buffer->end());
    EXPECT_EQ(payload_4, buffer->payload());
  }
}

TEST_F(ProducerStageTestWithPacketQueue, AdvanceWhileStarted) {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Start the Producer at t=0.
  // The internal and downstream frame timelines are identical.
  SendStartCommand(zx::time(0), Fixed(0));

  // Push some packets onto the command queue.
  SendPushPacketCommand(0, 0, 20);
  SendPushPacketCommand(1, 20, 20);
  SendPushPacketCommand(2, 40, 20);

  // The packet queue is still empty because we haven't processed those commands yet.
  EXPECT_TRUE(released_packets().empty());

  // Advancing past the second packet should release the first two packets.
  producer.Advance(DefaultCtx(), Fixed(40));
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));

  // Finally advance past the third packet.
  producer.Advance(DefaultCtx(), Fixed(60));
  EXPECT_THAT(released_packets(), ElementsAre(0, 1, 2));
}

TEST_F(ProducerStageTestWithPacketQueue, ReadWhileStopped) {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Push a packets onto the command queue.
  // Since we never sent a Start command, this packet won't be read.
  SendPushPacketCommand(0, 0, 20);
  EXPECT_TRUE(released_packets().empty());

  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_FALSE(buffer);
  }
  EXPECT_TRUE(released_packets().empty());
}

TEST_F(ProducerStageTestWithPacketQueue, AdvanceWhileStopped) {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Push a packet onto the command queue.
  SendPushPacketCommand(0, 0, 20);
  EXPECT_TRUE(released_packets().empty());

  // Advance past that packet.
  // Since we never sent a Start command, that packet wasn't released.
  producer.Advance(DefaultCtx(), Fixed(20));
  EXPECT_TRUE(released_packets().empty());

  // Now start the Producer at t=0.
  // The internal and downstream frame timelines are identical.
  SendStartCommand(zx::time(0), Fixed(0));

  // Advance again. Since the Producer is started, the packet should be released.
  producer.Advance(DefaultCtx(), Fixed(21));
  EXPECT_THAT(released_packets(), ElementsAre(0));

  // Now that we're started, we can Push and Read.
  SendPushPacketCommand(1, 20, 20);
  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(21), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(21, buffer->start());
    EXPECT_EQ(19, buffer->length());
    EXPECT_EQ(40, buffer->end());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));
}

TEST_F(ProducerStageTestWithPacketQueue, StartAfterRead) {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Start the Producer at t=1ms. For this test case, we want the internal
  // and downstream frame timelines to be identical, so start the internal
  // frames at 48 (1ms at 48kHz) so that t=0 corresponds to frame 0.
  SendStartCommand(zx::time(0) + zx::msec(1), Fixed(48));

  // Push and read a packet. Since the Producer starts at frame 48, the
  // first 48 frames of this packet should be ignored.
  SendPushPacketCommand(0, 0, 100);
  EXPECT_TRUE(released_packets().empty());
  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(0), 100);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(48, buffer->start());
    EXPECT_EQ(52, buffer->length());
    EXPECT_EQ(100, buffer->end());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0));
}

TEST_F(ProducerStageTestWithPacketQueue, StopAfterRead) {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Start the Producer at t=0.
  // The internal and downstream frame timelines are identical.
  SendStartCommand(zx::time(0), Fixed(0));
  producer.Advance(DefaultCtx(), Fixed(0));

  // Push a packet and a stop command. When reading the packet, we should
  // get just the first 50 frames since the Producer stops at frame 50.
  SendPushPacketCommand(0, 0, 100);
  SendStopCommand(Fixed(50));
  EXPECT_TRUE(released_packets().empty());
  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(0), 100);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(50, buffer->length());
    EXPECT_EQ(50, buffer->end());
  }

  // Nothing released because the packet was not fully consumed.
  EXPECT_TRUE(released_packets().empty());
}

TEST_F(ProducerStageTestWithPacketQueue, InternalFramesOffsetBehind) {
  // Start the Producer at t=1ms with internal frame 0. This makes the
  // internal and downstream frame timelines offset by 1ms, or 48 frames
  // at 48kHz. Reading downstream frame 48 is equivalent to reading
  // internal frame 0.
  SendStartCommand(zx::time(0) + zx::msec(1), Fixed(0));
  TestInternalFramesOffsetBehind();
}

TEST_F(ProducerStageTestWithPacketQueue, DownstreamFramesOffsetAhead) {
  // Offset the downstream frame timeline so that t=0ms is downstream frame 48.
  producer_stage().UpdatePresentationTimeToFracFrame(
      TimelineFunction(Fixed(48).raw_value(), 0, kFormat.frac_frames_per_ns()));

  // Start the Producer at t=0ms with internal frame 0. This makes the
  // internal and downstream frame timelines offset by 48 frames.
  SendStartCommand(zx::time(0), Fixed(0));

  // The setup should be identical to InternalFramesOffsetBehind.
  TestInternalFramesOffsetBehind();
}

void ProducerStageTestWithPacketQueue::TestInternalFramesOffsetBehind() {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // These push commands use internal frames.
  const void* payload_0 = SendPushPacketCommand(0, 0, 20);
  SendPushPacketCommand(1, 40, 20);
  EXPECT_TRUE(released_packets().empty());

  // Caller ensures that the Producer is started at downstream frame 0 and
  // that downstream frame 0 is equivalent to internal frame -48, so this
  // returns nothing.
  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_FALSE(buffer);
  }
  EXPECT_TRUE(released_packets().empty());

  // Downstream frame 48 is equivalent to internal frame 0, so this
  // returns the first packet.
  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(48), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(48, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(68, buffer->end());
    EXPECT_EQ(payload_0, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0));
}

TEST_F(ProducerStageTestWithPacketQueue, InternalFramesOffsetAhead) {
  // Start the Producer at t=0ms with internal frame 48. This makes the
  // internal and downstream frame timelines offset by 48 frames, or 1ms
  // at 48kHz. Reading downstream frame 0 is equivalent to reading internal
  // frame 48.
  SendStartCommand(zx::time(0), Fixed(48));
  TestInternalFramesOffsetAhead();
}

TEST_F(ProducerStageTestWithPacketQueue, DownstreamFramesOffsetBehind) {
  // Offset the downstream frame timeline so that t=0ms is downstream frame -48.
  producer_stage().UpdatePresentationTimeToFracFrame(
      TimelineFunction(Fixed(-48).raw_value(), 0, kFormat.frac_frames_per_ns()));

  // TestInternalFramesOffsetAhead requires that we start the Producer by
  // downstream frame 0. Since that will be equivalent to internal frame -48,
  // we need to start the Producer at internal frame -48.
  SendStartCommand(zx::time(0) - zx::msec(1), Fixed(-48));

  // The setup should be identical to InternalFramesOffsetAhead.
  TestInternalFramesOffsetAhead();
}

void ProducerStageTestWithPacketQueue::TestInternalFramesOffsetAhead() {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // These push commands use internal frames.
  SendPushPacketCommand(0, 0, 20);
  const void* payload_1 = SendPushPacketCommand(1, 48, 20);
  EXPECT_TRUE(released_packets().empty());

  // Caller ensures that the Producer is started at downstream frame 0 and
  // that downstream frame 0 is equivalent to internal frame 48, so this
  // returns the second packet.
  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(20, buffer->end());
    EXPECT_EQ(payload_1, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));
}

TEST_F(ProducerStageTestWithPacketQueue, DownstreamFramesUpdatedAfterPush) {
  ProducerStage& producer = producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // This test is equivalent to DownstreamFramesOffsetBehind except that the
  // UpdatePresentationTimeToFracFrame is called *after* a Start command is pushed
  // onto the command queue. This tests that PendingStartOrStop::downstream_frame
  // is correctly updated by UpdatePresentationTimeToFracFrame.
  SendStartCommand(zx::time(0) - zx::msec(1), Fixed(-48));
  SendPushPacketCommand(0, 0, 20);
  const void* payload_1 = SendPushPacketCommand(1, 48, 20);

  producer.UpdatePresentationTimeToFracFrame(
      TimelineFunction(Fixed(-48).raw_value(), 0, kFormat.frac_frames_per_ns()));

  // Downstream frame 0 is equivalent to internal frame 48, so this
  // returns the second packet.
  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(20, buffer->end());
    EXPECT_EQ(payload_1, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));
}

// To ensure compatibility, we also run a few simple tests using a ring buffer internal source.
class ProducerStageTestWithRingBuffer : public ::testing::Test {
 public:
  static inline const int64_t kRingBufferFrames = 100;

  ProducerStageTestWithRingBuffer()
      : start_stop_command_queue_(std::make_shared<ProducerStage::CommandQueue>()),
        buffer_(
            MemoryMappedBuffer::CreateOrDie(kRingBufferFrames * kFormat.bytes_per_frame(), true)),
        ring_buffer_(RingBuffer::Create({
            .format = kFormat,
            .reference_clock = DefaultUnreadableClock(),
            .buffer = buffer_,
            .producer_frames = kRingBufferFrames / 2,
            .consumer_frames = kRingBufferFrames / 2,
        })),
        producer_stage_({
            .format = kFormat,
            .reference_clock = DefaultUnreadableClock(),
            .command_queue = start_stop_command_queue_,
            .internal_source =
                std::make_shared<SimpleRingBufferProducerStage>("InternalSource", ring_buffer_),
        }) {
    producer_stage_.UpdatePresentationTimeToFracFrame(DefaultPresentationTimeToFracFrame(kFormat));
  }

  void SendStartCommand(zx::time start_presentation_time, Fixed start_frame) {
    start_stop_command_queue_->push(ProducerStage::StartCommand{
        .start_time = RealTime{.clock = WhichClock::Reference, .time = start_presentation_time},
        .start_frame = start_frame,
    });
  }

  void SendStopCommand(Fixed stop_frame) {
    start_stop_command_queue_->push(ProducerStage::StopCommand{
        .when = stop_frame,
    });
  }

  ProducerStage& producer_stage() { return producer_stage_; }

  void* RingBufferAt(int64_t frame) {
    return buffer_->offset((frame % kRingBufferFrames) * kFormat.bytes_per_frame());
  }

  void set_safe_read_frame(int64_t frame) { safe_read_frame_ = frame; }

 private:
  std::shared_ptr<ProducerStage::CommandQueue> start_stop_command_queue_;
  std::shared_ptr<MemoryMappedBuffer> buffer_;
  std::shared_ptr<RingBuffer> ring_buffer_;
  ProducerStage producer_stage_;
  int64_t safe_read_frame_ = kRingBufferFrames;
};

TEST_F(ProducerStageTestWithRingBuffer, ReadWhileStarted) {
  ProducerStage& producer = producer_stage();

  // The internal frame timeline is ahead by 10 frames.
  SendStartCommand(zx::time(0), Fixed(10));

  // Requesting dowstream frame 0 should return internal frame 10.
  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(0), 10);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), 0);
    EXPECT_EQ(buffer->length(), 10);
    EXPECT_EQ(buffer->end(), 10);
    EXPECT_EQ(buffer->payload(), RingBufferAt(10));
  }

  // Requesting dowstream frame 110 should return internal frame 120.
  {
    set_safe_read_frame(130);
    const auto buffer = producer.Read(DefaultCtx(), Fixed(110), 10);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), 110);
    EXPECT_EQ(buffer->length(), 10);
    EXPECT_EQ(buffer->end(), 120);
    EXPECT_EQ(buffer->payload(), RingBufferAt(120));
    // This is intended to test wrap-around.
    static_assert(110 > kRingBufferFrames);
  }
}

TEST_F(ProducerStageTestWithRingBuffer, StartAfterRead) {
  ProducerStage& producer = producer_stage();

  // Start the Producer at t=1ms. For this test case, we want the internal
  // and downstream frame timelines to be identical, so start the internal
  // frames at 48 (1ms at 48kHz) so that t=0 corresponds to frame 0.
  SendStartCommand(zx::time(0) + zx::msec(1), Fixed(48));

  // Push and read a packet. Since the Producer starts at frame 48, the
  // first 48 frames should be ignored.
  {
    const auto buffer = producer.Read(DefaultCtx(), Fixed(0), 50);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start(), 48);
    EXPECT_EQ(buffer->length(), 2);
    EXPECT_EQ(buffer->end(), 50);
    EXPECT_EQ(buffer->payload(), RingBufferAt(48));
  }
}

}  // namespace
}  // namespace media_audio
