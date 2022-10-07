// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl_realtime/stream_sink_server.h"

#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>

#include <condition_variable>
#include <mutex>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl_realtime/testing/test_stream_sink_server_and_client.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using CommandQueue = SimplePacketQueueProducerStage::CommandQueue;
using ClearCommand = SimplePacketQueueProducerStage::ClearCommand;
using PushPacketCommand = SimplePacketQueueProducerStage::PushPacketCommand;

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
  if (got_packet.start() != want_packet.start()) {
    *result_listener << ffl::String::DecRational << ""
                     << "expected start: " << want_packet.start() << " "
                     << "actual start: " << got_packet.start();
    return false;
  }
  if (got_packet.length() != want_packet.length()) {
    *result_listener << "expected length: " << want_packet.length() << " "
                     << "actual length: " << got_packet.length();
    return false;
  }

  return true;
}

}  // namespace

class StreamSinkServerTest : public ::testing::Test {
 public:
  void SetUp() {
    stream_sink_ = std::make_unique<TestStreamSinkServerAndClient>(thread_, kBufferId, kBufferSize,
                                                                   kFormat, kMediaTicksPerNs);
  }

  TestStreamSinkServerAndClient& stream_sink() { return *stream_sink_; }

 protected:
  fidl::Arena<> arena_;

 private:
  std::shared_ptr<FidlThread> thread_ = FidlThread::CreateFromNewThread("test_fidl_thread");
  std::unique_ptr<TestStreamSinkServerAndClient> stream_sink_;
};

TEST_F(StreamSinkServerTest, ExplicitTimestamp) {
  auto queue = stream_sink().server().command_queue();

  // This timestamp is equivalent to 1s, since there is 1 media tick per 10ms reference time.
  // See kMediaTicksPerNs.
  const int64_t packet0_ts = 100;
  TestFence packet0_fence;
  TestFence packet1_fence;

  {
    SCOPED_TRACE("send a 10ms packet with an explicit timestamp");
    ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(480 * kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithSpecified(arena_, packet0_ts),
        packet0_fence.Take()));
  }

  {
    SCOPED_TRACE("send a 1-frame packet with a 'continuous' timestamp");
    ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}),
        packet1_fence.Take()));
  }

  // First command should push a packet with frame timestamp 48000, since packet0_ts = 1s.
  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  EXPECT_THAT(*cmd0, PushPacketCommandEq(PacketView({
                         .format = kFormat,
                         .start = Fixed(48000),
                         .length = 480,
                         .payload = nullptr,  // ignored
                     })));

  // Second command should push a packet with frame timestamp 48480, since the second packet
  // is continuous with the first packet.
  auto cmd1 = queue->pop();
  ASSERT_TRUE(cmd1);
  EXPECT_THAT(*cmd1, PushPacketCommandEq(PacketView({
                         .format = kFormat,
                         .start = Fixed(48480),
                         .length = 1,
                         .payload = nullptr,  // ignored
                     })));

  // Check that the fences work.
  cmd0 = std::nullopt;
  ASSERT_TRUE(packet0_fence.Wait(zx::sec(5)));

  cmd1 = std::nullopt;
  ASSERT_TRUE(packet1_fence.Wait(zx::sec(5)));
}

TEST_F(StreamSinkServerTest, ContinuousTimestamps) {
  auto queue = stream_sink().server().command_queue();

  TestFence packet0_fence;
  TestFence packet1_fence;

  {
    SCOPED_TRACE("send first 'continuous' packet");
    ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}),
        packet0_fence.Take()));
  }

  {
    SCOPED_TRACE("send second 'continuous' packet");
    ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}),
        packet1_fence.Take()));
  }

  // First command should push a packet with frame timestamp 0.
  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  EXPECT_THAT(*cmd0, PushPacketCommandEq(PacketView({
                         .format = kFormat,
                         .start = Fixed(0),
                         .length = 1,
                         .payload = nullptr,  // ignored
                     })));

  // Second command should push a packet with frame timestamp 1, since it is continuous.
  auto cmd1 = queue->pop();
  ASSERT_TRUE(cmd1);
  EXPECT_THAT(*cmd1, PushPacketCommandEq(PacketView({
                         .format = kFormat,
                         .start = Fixed(1),
                         .length = 1,
                         .payload = nullptr,  // ignored
                     })));

  // Check that the fences work.
  cmd0 = std::nullopt;
  ASSERT_TRUE(packet0_fence.Wait(zx::sec(5)));

  cmd1 = std::nullopt;
  ASSERT_TRUE(packet1_fence.Wait(zx::sec(5)));
}

TEST_F(StreamSinkServerTest, PayloadZeroOffset) {
  auto queue = stream_sink().server().command_queue();

  TestFence fence;
  {
    SCOPED_TRACE("send a packet with zero offset");
    ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));
  }

  // Validate the payload address.
  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  ASSERT_TRUE(std::holds_alternative<PushPacketCommand>(*cmd0));
  ASSERT_EQ(std::get<PushPacketCommand>(*cmd0).packet.payload(),
            stream_sink().PayloadBufferOffset(0));
}

TEST_F(StreamSinkServerTest, PayloadNonzeroOffset) {
  auto queue = stream_sink().server().command_queue();

  // Send a packet with a non-zero offset.
  const uint32_t kOffset = 42;
  TestFence fence;
  {
    SCOPED_TRACE("send a packet with non-zero offset");
    ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = kOffset,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));
  }

  // Validate the payload address.
  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  ASSERT_TRUE(std::holds_alternative<PushPacketCommand>(*cmd0));
  ASSERT_EQ(std::get<PushPacketCommand>(*cmd0).packet.payload(),
            stream_sink().PayloadBufferOffset(kOffset));
}

TEST_F(StreamSinkServerTest, Clear) {
  auto queue = stream_sink().server().command_queue();

  TestFence fence;

  // Send a clear command.
  {
    auto result = stream_sink().client()->Clear(false, fence.Take());
    ASSERT_TRUE(result.ok()) << result.status_string();
    ASSERT_TRUE(stream_sink().WaitForNextCall());
  }

  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  ASSERT_TRUE(std::holds_alternative<ClearCommand>(*cmd0));

  // Check that the fence works.
  cmd0 = std::nullopt;
  ASSERT_TRUE(fence.Wait(zx::sec(5)));
}

TEST_F(StreamSinkServerTest, InvalidInputNoPayloadBuffer) {
  auto queue = stream_sink().server().command_queue();

  TestFence fence;

  auto result = stream_sink().client()->PutPacket(
      {
          .payload = fidl::VectorView<fuchsia_media2::wire::PayloadRange>(arena_, 0),
          .timestamp = fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}),
      },
      fence.Take());

  ASSERT_TRUE(result.ok()) << result.status_string();
  ASSERT_TRUE(stream_sink().WaitForNextCall());
  ASSERT_EQ(queue->pop(), std::nullopt);
}

TEST_F(StreamSinkServerTest, InvalidInputUnknownPayloadBufferId) {
  auto queue = stream_sink().server().command_queue();

  TestFence fence;
  ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
      {
          .buffer_id = kBufferId + 1,
          .offset = 0,
          .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
      },
      fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));

  ASSERT_EQ(queue->pop(), std::nullopt);
}

TEST_F(StreamSinkServerTest, InvalidInputPayloadBelowRange) {
  auto queue = stream_sink().server().command_queue();

  TestFence fence;
  ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
      {
          .buffer_id = kBufferId,
          .offset = static_cast<uint64_t>(-1),
          .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
      },
      fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));

  ASSERT_EQ(queue->pop(), std::nullopt);
}

TEST_F(StreamSinkServerTest, InvalidInputPayloadAboveRange) {
  auto queue = stream_sink().server().command_queue();

  TestFence fence;
  ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
      {
          .buffer_id = kBufferId,
          .offset = kBufferSize - kFormat.bytes_per_frame() + 1,
          .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
      },
      fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));

  ASSERT_EQ(queue->pop(), std::nullopt);
}

TEST_F(StreamSinkServerTest, InvalidInputPayloadNonIntegralFrames) {
  auto queue = stream_sink().server().command_queue();

  TestFence fence;
  ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
      {
          .buffer_id = kBufferId,
          .offset = 0,
          .size = static_cast<uint64_t>(kFormat.bytes_per_frame()) - 1,
      },
      fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));

  ASSERT_EQ(queue->pop(), std::nullopt);
}

}  // namespace media_audio
