// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/stream_sink_server.h"

#include <lib/async-testing/test_loop.h>
#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>

#include <condition_variable>
#include <mutex>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/testing/test_stream_sink_server_and_client.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using ::fuchsia_audio::wire::Timestamp;
using ::fuchsia_media2::ConsumerClosedReason;

using CommandQueue = SimplePacketQueueProducerStage::CommandQueue;
using PushPacketCommand = SimplePacketQueueProducerStage::PushPacketCommand;
using ReleasePacketsCommand = SimplePacketQueueProducerStage::ReleasePacketsCommand;

// These tests work best if we use a format with >= 2 bytes per frame to ensure we compute frame
// counts correctly. Other than that constraint, the specific choice of format does not matter.
const auto kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});
const auto kMediaTicksPerNs = TimelineRate(1, 10'000'000);  // 1 tick per 10ms
constexpr uint32_t kBufferId = 0;
constexpr uint64_t kBufferSize = 4096;

MATCHER_P(PushPacketCommandEq, want_packet, "") {
  if (!std::holds_alternative<PushPacketCommand>(arg)) {
    *result_listener << "not a PushPacketCommand";
    return false;
  }

  auto& got_packet = std::get<PushPacketCommand>(arg).packet;
  if (got_packet.format() != want_packet.format()) {
    *result_listener << "expected format: " << want_packet.format() << " "
                     << "actual format: " << got_packet.format();
    return false;
  }
  if (got_packet.start_frame() != want_packet.start_frame()) {
    *result_listener << ffl::String::DecRational << ""
                     << "expected start_frame " << want_packet.start_frame() << " "
                     << "actual start_frame " << got_packet.start_frame();
    return false;
  }
  if (got_packet.frame_count() != want_packet.frame_count()) {
    *result_listener << "expected frame_count " << want_packet.frame_count() << " "
                     << "actual frame_count " << got_packet.frame_count();
    return false;
  }

  return true;
}

struct TestHarness {
  void RunLoopUntilIdle() { loop->RunUntilIdle(); }

  std::unique_ptr<fidl::Arena<>> arena;
  std::unique_ptr<async::TestLoop> loop;
  std::unique_ptr<TestStreamSinkServerAndClient> stream_sink;
};

TestHarness MakeTestHarness() {
  TestHarness h;
  h.arena = std::make_unique<fidl::Arena<>>();
  h.loop = std::make_unique<async::TestLoop>();
  h.stream_sink = std::make_unique<TestStreamSinkServerAndClient>(*h.loop, kBufferId, kBufferSize,
                                                                  kFormat, kMediaTicksPerNs);
  return h;
}

TEST(StreamSinkServerTest, ExplicitTimestamp) {
  auto h = MakeTestHarness();
  auto queue = h.stream_sink->server().command_queue();

  // This timestamp is equivalent to 1s, since there is 1 media tick per 10ms reference time.
  // See kMediaTicksPerNs.
  const int64_t packet0_ts = 100;
  TestFence packet0_fence;
  TestFence packet1_fence;

  {
    SCOPED_TRACE("send a 10ms packet with an explicit timestamp");
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(480 * kFormat.bytes_per_frame()),
        },
        Timestamp::WithSpecified(*h.arena, packet0_ts), packet0_fence.Take()));
    h.RunLoopUntilIdle();
  }

  {
    SCOPED_TRACE("send a 1-frame packet with a 'continuous' timestamp");
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        Timestamp::WithUnspecifiedContinuous({}), packet1_fence.Take()));
    h.RunLoopUntilIdle();
  }

  // First command should push a packet with frame timestamp 48000, since packet0_ts = 1s.
  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  EXPECT_THAT(*cmd0, PushPacketCommandEq(PacketView({
                         .format = kFormat,
                         .start_frame = Fixed(48000),
                         .frame_count = 480,
                         .payload = nullptr,  // ignored
                     })));

  // Second command should push a packet with frame timestamp 48480, since the second packet
  // is continuous with the first packet.
  auto cmd1 = queue->pop();
  ASSERT_TRUE(cmd1);
  EXPECT_THAT(*cmd1, PushPacketCommandEq(PacketView({
                         .format = kFormat,
                         .start_frame = Fixed(48480),
                         .frame_count = 1,
                         .payload = nullptr,  // ignored
                     })));

  // Check that the fences work.
  cmd0 = std::nullopt;
  ASSERT_TRUE(packet0_fence.Wait(zx::sec(5)));

  cmd1 = std::nullopt;
  ASSERT_TRUE(packet1_fence.Wait(zx::sec(5)));
}

TEST(StreamSinkServerTest, ContinuousTimestamps) {
  auto h = MakeTestHarness();
  auto queue = h.stream_sink->server().command_queue();

  TestFence packet0_fence;
  TestFence packet1_fence;

  {
    SCOPED_TRACE("send first 'continuous' packet");
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        Timestamp::WithUnspecifiedContinuous({}), packet0_fence.Take()));
    h.RunLoopUntilIdle();
  }

  {
    SCOPED_TRACE("send second 'continuous' packet");
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        Timestamp::WithUnspecifiedContinuous({}), packet1_fence.Take()));
    h.RunLoopUntilIdle();
  }

  // First command should push a packet with frame timestamp 0.
  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  EXPECT_THAT(*cmd0, PushPacketCommandEq(PacketView({
                         .format = kFormat,
                         .start_frame = Fixed(0),
                         .frame_count = 1,
                         .payload = nullptr,  // ignored
                     })));

  // Second command should push a packet with frame timestamp 1, since it is continuous.
  auto cmd1 = queue->pop();
  ASSERT_TRUE(cmd1);
  EXPECT_THAT(*cmd1, PushPacketCommandEq(PacketView({
                         .format = kFormat,
                         .start_frame = Fixed(1),
                         .frame_count = 1,
                         .payload = nullptr,  // ignored
                     })));

  // Check that the fences work.
  cmd0 = std::nullopt;
  ASSERT_TRUE(packet0_fence.Wait(zx::sec(5)));

  cmd1 = std::nullopt;
  ASSERT_TRUE(packet1_fence.Wait(zx::sec(5)));
}

TEST(StreamSinkServerTest, PayloadZeroOffset) {
  auto h = MakeTestHarness();
  auto queue = h.stream_sink->server().command_queue();

  TestFence fence;
  {
    SCOPED_TRACE("send a packet with zero offset");
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        Timestamp::WithUnspecifiedContinuous({}), fence.Take()));
    h.RunLoopUntilIdle();
  }

  // Validate the payload address.
  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  ASSERT_TRUE(std::holds_alternative<PushPacketCommand>(*cmd0));
  ASSERT_EQ(std::get<PushPacketCommand>(*cmd0).packet.payload(),
            h.stream_sink->PayloadBufferOffset(0));
}

TEST(StreamSinkServerTest, PayloadNonzeroOffset) {
  auto h = MakeTestHarness();
  auto queue = h.stream_sink->server().command_queue();

  // Send a packet with a non-zero offset.
  const uint32_t kOffset = 42;
  TestFence fence;
  {
    SCOPED_TRACE("send a packet with non-zero offset");
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = kOffset,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        Timestamp::WithUnspecifiedContinuous({}), fence.Take()));
    h.RunLoopUntilIdle();
  }

  // Validate the payload address.
  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  ASSERT_TRUE(std::holds_alternative<PushPacketCommand>(*cmd0));
  ASSERT_EQ(std::get<PushPacketCommand>(*cmd0).packet.payload(),
            h.stream_sink->PayloadBufferOffset(kOffset));
}

TEST(StreamSinkServerTest, SegementIds) {
  auto h = MakeTestHarness();
  auto queue = h.stream_sink->server().command_queue();

  TestFence packet0_fence;
  TestFence packet1_fence;

  {
    SCOPED_TRACE("first packet, segment 0");
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        Timestamp::WithUnspecifiedContinuous({}), packet0_fence.Take()));
    h.RunLoopUntilIdle();
  }

  {
    SCOPED_TRACE("second packet, segment 1");
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->StartSegment(1));
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        Timestamp::WithUnspecifiedContinuous({}), packet1_fence.Take()));
    h.RunLoopUntilIdle();
  }

  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  ASSERT_TRUE(std::holds_alternative<PushPacketCommand>(*cmd0));
  EXPECT_EQ(std::get<PushPacketCommand>(*cmd0).segment_id, 0);

  auto cmd1 = queue->pop();
  ASSERT_TRUE(cmd1);
  ASSERT_TRUE(std::holds_alternative<PushPacketCommand>(*cmd1));
  EXPECT_EQ(std::get<PushPacketCommand>(*cmd1).segment_id, 0);
}

TEST(StreamSinkServerTest, ReleasePackets) {
  auto h = MakeTestHarness();
  auto queue = h.stream_sink->server().command_queue();

  h.stream_sink->server().ReleasePackets(99);

  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  ASSERT_TRUE(std::holds_alternative<ReleasePacketsCommand>(*cmd0));
  EXPECT_EQ(std::get<ReleasePacketsCommand>(*cmd0).before_segment_id, 99);
}

TEST(StreamSinkServerTest, PutPacketFailsMissingPacket) {
  auto h = MakeTestHarness();
  auto queue = h.stream_sink->server().command_queue();

  TestFence fence;
  auto result = h.stream_sink->client()->PutPacket(
      fuchsia_audio::wire::StreamSinkPutPacketRequest::Builder(*h.arena)
          // no .packet()
          .release_fence(fence.Take())
          .Build());
  ASSERT_TRUE(result.ok()) << result;
  h.RunLoopUntilIdle();

  EXPECT_EQ(queue->pop(), std::nullopt);
  EXPECT_EQ(h.stream_sink->on_will_close_reason(), ConsumerClosedReason::kInvalidPacket);
}

fidl::WireTableBuilder<fuchsia_audio::wire::Packet> MakeDefaultPacket(fidl::AnyArena& arena) {
  return fuchsia_audio::wire::Packet::Builder(arena)
      .payload({
          .buffer_id = kBufferId,
          .offset = 0,
          .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
      })
      .timestamp(Timestamp::WithUnspecifiedContinuous({}));
}

TEST(StreamSinkServerTest, PutPacketFailsInvalidPacket) {
  struct TestCase {
    std::string name;
    std::function<void(fidl::WireTableBuilder<fuchsia_audio::wire::Packet>&)> edit;
  };
  std::vector<TestCase> test_cases = {
      {
          .name = "MissingPayload",
          .edit = [](auto& packet) { packet.clear_payload(); },
      },
      {
          .name = "UnsupportedFieldFlags",
          .edit = [](auto& packet) { packet.flags(fuchsia_audio::PacketFlags::kDropAfterDecode); },
      },
      {
          .name = "UnsupportedFieldFrontFramesToDrop",
          .edit = [](auto& packet) { packet.front_frames_to_drop(1); },
      },
      {
          .name = "UnsupportedFieldBackFramesToDrop",
          .edit = [](auto& packet) { packet.back_frames_to_drop(1); },
      },
      {
          .name = "UnknownPayloadBufferId",
          .edit = [](auto& packet) { packet.payload().buffer_id = kBufferId + 1; },
      },
      {
          .name = "PayloadBelowRange",
          .edit = [](auto& packet) { packet.payload().offset = static_cast<uint64_t>(-1); },
      },
      {
          .name = "PayloadAboveRange",
          .edit =
              [](auto& packet) {
                packet.payload().offset = kBufferSize - kFormat.bytes_per_frame() + 1;
              },
      },
      {
          .name = "PayloadNonIntegralFrames",
          .edit =
              [](auto& packet) {
                packet.payload().size = static_cast<uint64_t>(kFormat.bytes_per_frame()) - 1;
              },
      },
  };

  for (auto& tc : test_cases) {
    SCOPED_TRACE(tc.name);

    auto h = MakeTestHarness();
    auto packet = MakeDefaultPacket(*h.arena);
    tc.edit(packet);

    TestFence fence;
    auto result = h.stream_sink->client()->PutPacket(
        fuchsia_audio::wire::StreamSinkPutPacketRequest::Builder(*h.arena)
            .packet(packet.Build())
            .release_fence(fence.Take())
            .Build());
    ASSERT_TRUE(result.ok()) << result;
    h.RunLoopUntilIdle();

    auto queue = h.stream_sink->server().command_queue();
    EXPECT_EQ(queue->pop(), std::nullopt);
    EXPECT_EQ(h.stream_sink->on_will_close_reason(), ConsumerClosedReason::kInvalidPacket);
  }
}

}  // namespace
}  // namespace media_audio
