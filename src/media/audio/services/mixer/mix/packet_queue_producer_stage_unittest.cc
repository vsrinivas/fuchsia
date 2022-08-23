// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/packet_queue_producer_stage.h"

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
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::ElementsAre;

const Format kFormat = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 48000});

class PacketQueueProducerStageTest : public ::testing::Test {
 public:
  PacketQueueProducerStageTest()
      : command_queue_(std::make_shared<PacketQueueProducerStage::CommandQueue>()),
        packet_queue_producer_stage_({
            .format = kFormat,
            .reference_clock_koid = DefaultClockKoid(),
            .command_queue = command_queue_,
        }) {
    packet_queue_producer_stage_.UpdatePresentationTimeToFracFrame(
        DefaultPresentationTimeToFracFrame(kFormat));
  }

  const void* SendPushPacketCommand(uint32_t packet_id, int64_t start = 0, int64_t length = 1) {
    auto& packet = NewPacket(packet_id, start, length);
    command_queue_->push(PacketQueueProducerStage::PushPacketCommand{
        .packet = packet.view,
        .fence = packet.fence.Take(),
    });
    return packet.payload.data();
  }

  void SendClearCommand(zx::eventpair fence) {
    command_queue_->push(PacketQueueProducerStage::ClearCommand{.fence = std::move(fence)});
  }

  void SendStartCommand(zx::time start_presentation_time, Fixed start_frame,
                        std::function<void()> callback = nullptr) {
    command_queue_->push(PacketQueueProducerStage::StartCommand{
        .start_presentation_time = start_presentation_time,
        .start_frame = start_frame,
        .callback = std::move(callback),
    });
  }

  void SendStopCommand(Fixed stop_frame, std::function<void()> callback = nullptr) {
    command_queue_->push(PacketQueueProducerStage::StopCommand{
        .stop_frame = stop_frame,
        .callback = std::move(callback),
    });
  }

  PacketQueueProducerStage& packet_queue_producer_stage() { return packet_queue_producer_stage_; }

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

  std::shared_ptr<PacketQueueProducerStage::CommandQueue> command_queue_;
  PacketQueueProducerStage packet_queue_producer_stage_;
  std::map<int32_t, Packet> packets_;  // ordered map so iteration is deterministic
  std::vector<uint32_t> released_packets_;
};

TEST_F(PacketQueueProducerStageTest, ReadWhileStarted) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
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
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(20, buffer->end());
    EXPECT_EQ(payload_0, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0));

  {
    // Packet #1
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(20), 20);
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
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(40), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(40, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(60, buffer->end());
    EXPECT_EQ(payload_2, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1, 2));
}

TEST_F(PacketQueueProducerStageTest, ReadAfterClearWhileStarted) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
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
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(40), 40);
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
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(80), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(80, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(100, buffer->end());
    EXPECT_EQ(payload_4, buffer->payload());
  }
}

TEST_F(PacketQueueProducerStageTest, AdvanceWhileStarted) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
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
  packet_queue.Advance(DefaultCtx(), Fixed(40));
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));

  // Finally advance past the third packet.
  packet_queue.Advance(DefaultCtx(), Fixed(60));
  EXPECT_THAT(released_packets(), ElementsAre(0, 1, 2));
}

TEST_F(PacketQueueProducerStageTest, ReadWhileStopped) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Push a packets onto the command queue.
  // Since we never sent a Start command, this packet won't be read.
  SendPushPacketCommand(0, 0, 20);
  EXPECT_TRUE(released_packets().empty());

  {
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_FALSE(buffer);
  }
  EXPECT_TRUE(released_packets().empty());
}

TEST_F(PacketQueueProducerStageTest, AdvanceWhileStopped) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Push a packet onto the command queue.
  SendPushPacketCommand(0, 0, 20);
  EXPECT_TRUE(released_packets().empty());

  // Advance past that packet.
  // Since we never sent a Start command, that packet wasn't released.
  packet_queue.Advance(DefaultCtx(), Fixed(20));
  EXPECT_TRUE(released_packets().empty());

  // Now start the Producer at t=0.
  // The internal and downstream frame timelines are identical.
  SendStartCommand(zx::time(0), Fixed(0));

  // Advance again. Since the Producer is started, the packet should be released.
  packet_queue.Advance(DefaultCtx(), Fixed(21));
  EXPECT_THAT(released_packets(), ElementsAre(0));

  // Now that we're started, we can Push and Read.
  SendPushPacketCommand(1, 20, 20);
  {
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(21), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(21, buffer->start());
    EXPECT_EQ(19, buffer->length());
    EXPECT_EQ(40, buffer->end());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));
}

TEST_F(PacketQueueProducerStageTest, StartAfterRead) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
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
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(0), 100);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(48, buffer->start());
    EXPECT_EQ(52, buffer->length());
    EXPECT_EQ(100, buffer->end());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0));
}

TEST_F(PacketQueueProducerStageTest, StopAfterRead) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Start the Producer at t=0.
  // The internal and downstream frame timelines are identical.
  SendStartCommand(zx::time(0), Fixed(0));

  // Push a packet and a stop command. When reading the packet, we should
  // get just the first 50 frames since the Producer stops at frame 50.
  SendPushPacketCommand(0, 0, 100);
  SendStopCommand(Fixed(50));
  EXPECT_TRUE(released_packets().empty());
  {
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(0), 100);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(50, buffer->length());
    EXPECT_EQ(50, buffer->end());
  }

  // Nothing released because the packet was not fully consumed.
  EXPECT_TRUE(released_packets().empty());
}

TEST_F(PacketQueueProducerStageTest, StartAndStopAfterRead) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Start the Producer at t=1ms. For this test case, we want the internal
  // and downstream frame timelines to be identical, so start the internal
  // frames at 48 (1ms at 48kHz) so that t=0 corresponds to frame 0.
  SendStartCommand(zx::time(0) + zx::msec(1), Fixed(48));

  // Push a packet and a stop command. Since the Producer starts at frame 48,
  // but stops again at frame 50, we should read just 2 frames.
  SendPushPacketCommand(0, 0, 100);
  SendStopCommand(Fixed(50));
  EXPECT_TRUE(released_packets().empty());
  {
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(0), 100);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(48, buffer->start());
    EXPECT_EQ(2, buffer->length());
    EXPECT_EQ(50, buffer->end());
  }

  // Nothing released because the packet was not fully consumed.
  EXPECT_TRUE(released_packets().empty());
}

TEST_F(PacketQueueProducerStageTest, StartStopCallbacks) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  bool done1 = false;
  bool done2 = false;
  bool done3 = false;
  bool done4 = false;

  // Sequence of:
  //   start @ 0
  //   stop  @ 5
  //   start @ 48 (= 1ms @ 48kHz)
  //   stop  @ 100
  //
  // Throughout this test, the internal and downstream frame timelines are identical.
  SendStartCommand(zx::time(0), Fixed(0), [&done1]() mutable { done1 = true; });
  SendStopCommand(Fixed(5), [&done1, &done2]() mutable {
    EXPECT_TRUE(done1);
    done2 = true;
  });

  SendStartCommand(zx::time(0) + zx::msec(1), Fixed(48), [&done3]() mutable { done3 = true; });
  SendStopCommand(Fixed(100), [&done4]() mutable { done4 = true; });

  // Advancing to frame 10 should Start and Stop.
  packet_queue.Advance(DefaultCtx(), Fixed(10));
  EXPECT_TRUE(done1);
  EXPECT_TRUE(done2);

  // Advancing to frame 48 should Start.
  packet_queue.Advance(DefaultCtx(), Fixed(48));
  EXPECT_TRUE(done3);

  // Advancing to frame 100 should Stop.
  packet_queue.Advance(DefaultCtx(), Fixed(100));
  EXPECT_TRUE(done4);
}

TEST_F(PacketQueueProducerStageTest, InternalFramesOffsetBehind) {
  // Start the Producer at t=1ms with internal frame 0. This makes the
  // internal and downstream frame timelines offset by 1ms, or 48 frames
  // at 48kHz. Reading downstream frame 48 is equivalent to reading
  // internal frame 0.
  SendStartCommand(zx::time(0) + zx::msec(1), Fixed(0));
  TestInternalFramesOffsetBehind();
}

TEST_F(PacketQueueProducerStageTest, DownstreamFramesOffsetAhead) {
  // Offset the downstream frame timeline so that t=0ms is downstream frame 48.
  packet_queue_producer_stage().UpdatePresentationTimeToFracFrame(
      TimelineFunction(Fixed(48).raw_value(), 0, kFormat.frac_frames_per_ns()));

  // Start the Producer at t=0ms with internal frame 0. This makes the
  // internal and downstream frame timelines offset by 48 frames.
  SendStartCommand(zx::time(0), Fixed(0));

  // The setup should be identical to InternalFramesOffsetBehind.
  TestInternalFramesOffsetBehind();
}

void PacketQueueProducerStageTest::TestInternalFramesOffsetBehind() {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // These push commands use internal frames.
  const void* payload_0 = SendPushPacketCommand(0, 0, 20);
  SendPushPacketCommand(1, 40, 20);
  EXPECT_TRUE(released_packets().empty());

  // Caller ensures that the Producer is started at downstream frame 0 and
  // that downstream frame 0 is equivalent to internal frame -48, so this
  // returns nothing.
  {
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_FALSE(buffer);
  }
  EXPECT_TRUE(released_packets().empty());

  // Downstream frame 48 is equivalent to internal frame 0, so this
  // returns the first packet.
  {
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(48), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(48, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(68, buffer->end());
    EXPECT_EQ(payload_0, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0));
}

TEST_F(PacketQueueProducerStageTest, InternalFramesOffsetAhead) {
  // Start the Producer at t=0ms with internal frame 48. This makes the
  // internal and downstream frame timelines offset by 48 frames, or 1ms
  // at 48kHz. Reading downstream frame 0 is equivalent to reading internal
  // frame 48.
  SendStartCommand(zx::time(0), Fixed(48));
  TestInternalFramesOffsetAhead();
}

TEST_F(PacketQueueProducerStageTest, DownstreamFramesOffsetBehind) {
  // Offset the downstream frame timeline so that t=0ms is downstream frame -48.
  packet_queue_producer_stage().UpdatePresentationTimeToFracFrame(
      TimelineFunction(Fixed(-48).raw_value(), 0, kFormat.frac_frames_per_ns()));

  // TestInternalFramesOffsetAhead requires that we start the Producer by
  // downstream frame 0. Since that will be equivalent to internal frame -48,
  // we need to start the Producer at internal frame -48.
  SendStartCommand(zx::time(0) - zx::msec(1), Fixed(-48));

  // The setup should be identical to InternalFramesOffsetAhead.
  TestInternalFramesOffsetAhead();
}

void PacketQueueProducerStageTest::TestInternalFramesOffsetAhead() {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // These push commands use internal frames.
  SendPushPacketCommand(0, 0, 20);
  const void* payload_1 = SendPushPacketCommand(1, 48, 20);
  EXPECT_TRUE(released_packets().empty());

  // Caller ensures that the Producer is started at downstream frame 0 and
  // that downstream frame 0 is equivalent to internal frame 48, so this
  // returns the second packet.
  {
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(20, buffer->end());
    EXPECT_EQ(payload_1, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));
}

TEST_F(PacketQueueProducerStageTest, DownstreamFramesUpdatedAfterPush) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // This test is equivalent to DownstreamFramesOffsetBehind except that the
  // UpdatePresentationTimeToFracFrame is called *after* a Start command is pushed
  // onto the command queue. This tests that PendingStartOrStop::downstream_frame
  // is correctly updated by UpdatePresentationTimeToFracFrame.
  SendStartCommand(zx::time(0) - zx::msec(1), Fixed(-48));
  SendPushPacketCommand(0, 0, 20);
  const void* payload_1 = SendPushPacketCommand(1, 48, 20);

  packet_queue.UpdatePresentationTimeToFracFrame(
      TimelineFunction(Fixed(-48).raw_value(), 0, kFormat.frac_frames_per_ns()));

  // Downstream frame 0 is equivalent to internal frame 48, so this
  // returns the second packet.
  {
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(20, buffer->end());
    EXPECT_EQ(payload_1, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));
}

}  // namespace
}  // namespace media_audio
