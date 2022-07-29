// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl_realtime/fidl_stream_sink.h"

#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>

#include <condition_variable>
#include <mutex>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/services/common/testing/test_server_and_client.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using CommandQueue = PacketQueueProducerStage::CommandQueue;
using ClearCommand = PacketQueueProducerStage::ClearCommand;
using PushPacketCommand = PacketQueueProducerStage::PushPacketCommand;

// These tests work best if we use a format with >= 2 bytes per frame to ensure we compute frame
// counts correctly. Other than that constraint, the specific choice of format does not matter.
const auto kFormat = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 48000});
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

class FidlStreamSinkTest : public ::testing::Test {
 public:
  void SetUp() {
    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create(kBufferSize, 0, &vmo), ZX_OK);

    auto buffer_result = MemoryMappedBuffer::Create(vmo, false);
    ASSERT_TRUE(buffer_result.is_ok()) << buffer_result.status_string();

    buffer_ = *buffer_result;
    thread_ = FidlThread::CreateFromNewThread("test_fidl_thread");
    stream_sink_wrapper_ = std::make_unique<TestServerAndClient<FidlStreamSink>>(
        thread_, FidlStreamSink::Args{
                     .format = kFormat,
                     .media_ticks_per_ns = kMediaTicksPerNs,
                     .payload_buffers = {{{kBufferId, buffer_}}},
                 });

    stream_sink_server().on_method_complete_ = [this]() {
      std::unique_lock<std::mutex> lock(mutex_);
      calls_completed_++;
      cvar_.notify_all();
    };
  }

  void TearDown() {
    // Close the client and wait until the server shuts down.
    stream_sink_wrapper_.reset();
  }

  FidlStreamSink& stream_sink_server() { return stream_sink_wrapper_->server(); }
  fidl::WireSyncClient<fuchsia_media2::StreamSink>& stream_sink_client() {
    return stream_sink_wrapper_->client();
  }

  void* BufferOffset(int64_t offset) { return static_cast<char*>(buffer_->start()) + offset; }

  void AddProducerQueue(std::shared_ptr<CommandQueue> q) {
    libsync::Completion done;
    thread_->PostTask([this, q, &done]() {
      ScopedThreadChecker checker(stream_sink_server().thread().checker());
      stream_sink_server().AddProducerQueue(q);
      done.Signal();
    });
    ASSERT_EQ(done.Wait(zx::sec(5)), ZX_OK);
  }

  void RemoveProducerQueue(std::shared_ptr<CommandQueue> q) {
    libsync::Completion done;
    thread_->PostTask([this, q, &done]() {
      ScopedThreadChecker checker(stream_sink_server().thread().checker());
      stream_sink_server().RemoveProducerQueue(q);
      done.Signal();
    });
    ASSERT_EQ(done.Wait(zx::sec(5)), ZX_OK);
  }

  void PutPacket(fuchsia_media2::wire::PayloadRange payload,
                 fuchsia_media2::wire::PacketTimestamp timestamp, zx::eventpair fence) {
    std::vector<fuchsia_media2::wire::PayloadRange> payloads{payload};
    auto result = stream_sink_client()->PutPacket(
        {
            .payload = fidl::VectorView<fuchsia_media2::wire::PayloadRange>(arena_, payloads),
            .timestamp = timestamp,
        },
        std::move(fence));

    ASSERT_TRUE(result.ok()) << result.status_string();
    ASSERT_TRUE(WaitForNextCall());
  }

  // Blocks until the next FIDL call is completed.
  bool WaitForNextCall() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (cvar_.wait_for(lock, std::chrono::seconds(5),
                       [this] { return calls_completed_ > calls_delivered_; })) {
      calls_delivered_++;
      return true;
    }
    return false;
  }

 protected:
  fidl::Arena<> arena_;

 private:
  std::shared_ptr<MemoryMappedBuffer> buffer_;
  std::shared_ptr<FidlThread> thread_;
  std::unique_ptr<TestServerAndClient<FidlStreamSink>> stream_sink_wrapper_;

  // The following fields are guarded by mutex_.
  // We can't check this statically because thread-safety analysis doesn't support std::unique_lock.
  std::mutex mutex_;
  std::condition_variable cvar_;
  int64_t calls_completed_{0};
  int64_t calls_delivered_{0};
};

TEST_F(FidlStreamSinkTest, ExplicitTimestamp) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  // This timestamp is equivalent to 1s, since there is 1 media tick per 10ms reference time.
  // See kMediaTicksPerNs.
  const int64_t packet0_ts = 100;
  TestFence packet0_fence;
  TestFence packet1_fence;

  {
    SCOPED_TRACE("send a 10ms packet with an explicit timestamp");
    ASSERT_NO_FATAL_FAILURE(PutPacket(
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
    ASSERT_NO_FATAL_FAILURE(PutPacket(
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

TEST_F(FidlStreamSinkTest, ContinuousTimestamps) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  TestFence packet0_fence;
  TestFence packet1_fence;

  {
    SCOPED_TRACE("send first 'continuous' packet");
    ASSERT_NO_FATAL_FAILURE(PutPacket(
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
    ASSERT_NO_FATAL_FAILURE(PutPacket(
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

TEST_F(FidlStreamSinkTest, PayloadZeroOffset) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  TestFence fence;
  {
    SCOPED_TRACE("send a packet with zero offset");
    ASSERT_NO_FATAL_FAILURE(PutPacket(
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
  ASSERT_EQ(std::get<PushPacketCommand>(*cmd0).packet.payload(), BufferOffset(0));
}

TEST_F(FidlStreamSinkTest, PayloadNonzeroOffset) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  // Send a packet with a non-zero offset.
  const uint32_t kOffset = 42;
  TestFence fence;
  {
    SCOPED_TRACE("send a packet with non-zero offset");
    ASSERT_NO_FATAL_FAILURE(PutPacket(
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
  ASSERT_EQ(std::get<PushPacketCommand>(*cmd0).packet.payload(), BufferOffset(kOffset));
}

TEST_F(FidlStreamSinkTest, MultipleQueues) {
  auto queue0 = std::make_shared<CommandQueue>();
  auto queue1 = std::make_shared<CommandQueue>();
  AddProducerQueue(queue0);
  AddProducerQueue(queue1);

  TestFence packet0_fence;
  TestFence packet1_fence;

  {
    SCOPED_TRACE("send first continuous packet");
    ASSERT_NO_FATAL_FAILURE(PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}),
        packet0_fence.Take()));
  }

  // The command should appear in both queues.
  auto cmd0 = queue0->pop();
  ASSERT_TRUE(cmd0);
  ASSERT_TRUE(std::holds_alternative<PushPacketCommand>(*cmd0));
  ASSERT_EQ(std::get<PushPacketCommand>(*cmd0).packet.start(), Fixed(0));

  auto cmd1 = queue1->pop();
  ASSERT_TRUE(cmd1);
  ASSERT_TRUE(std::holds_alternative<PushPacketCommand>(*cmd1));
  ASSERT_EQ(std::get<PushPacketCommand>(*cmd1).packet.start(), Fixed(0));

  // The fence should not be released until both commands are dropped.
  cmd0 = std::nullopt;
  ASSERT_FALSE(packet0_fence.Done());
  cmd1 = std::nullopt;
  ASSERT_TRUE(packet0_fence.Wait(zx::sec(5)));

  // Now remove queue0.
  RemoveProducerQueue(queue0);

  {
    SCOPED_TRACE("send second continuous packet");
    ASSERT_NO_FATAL_FAILURE(PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}),
        packet1_fence.Take()));
  }

  // The command should appear in queue1 only.
  cmd0 = queue0->pop();
  ASSERT_FALSE(cmd0);

  cmd1 = queue1->pop();
  ASSERT_TRUE(cmd1);
  ASSERT_TRUE(std::holds_alternative<PushPacketCommand>(*cmd1));
  ASSERT_EQ(std::get<PushPacketCommand>(*cmd1).packet.start(), Fixed(1));

  cmd1 = std::nullopt;
  ASSERT_TRUE(packet1_fence.Wait(zx::sec(5)));
}

TEST_F(FidlStreamSinkTest, Clear) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  TestFence fence;

  // Send a clear command.
  {
    auto result = stream_sink_client()->Clear(false, fence.Take());
    ASSERT_TRUE(result.ok()) << result.status_string();
    ASSERT_TRUE(WaitForNextCall());
  }

  auto cmd0 = queue->pop();
  ASSERT_TRUE(cmd0);
  ASSERT_TRUE(std::holds_alternative<ClearCommand>(*cmd0));

  // Check that the fence works.
  cmd0 = std::nullopt;
  ASSERT_TRUE(fence.Wait(zx::sec(5)));
}

TEST_F(FidlStreamSinkTest, InvalidInputNoPayloadBuffer) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  TestFence fence;

  auto result = stream_sink_client()->PutPacket(
      {
          .payload = fidl::VectorView<fuchsia_media2::wire::PayloadRange>(arena_, 0),
          .timestamp = fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}),
      },
      fence.Take());

  ASSERT_TRUE(result.ok()) << result.status_string();
  ASSERT_TRUE(WaitForNextCall());
  ASSERT_EQ(queue->pop(), std::nullopt);
}

TEST_F(FidlStreamSinkTest, InvalidInputUnknownPayloadBufferId) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  TestFence fence;
  ASSERT_NO_FATAL_FAILURE(PutPacket(
      {
          .buffer_id = kBufferId + 1,
          .offset = 0,
          .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
      },
      fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));

  ASSERT_EQ(queue->pop(), std::nullopt);
}

TEST_F(FidlStreamSinkTest, InvalidInputPayloadBelowRange) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  TestFence fence;
  ASSERT_NO_FATAL_FAILURE(PutPacket(
      {
          .buffer_id = kBufferId,
          .offset = static_cast<uint64_t>(-1),
          .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
      },
      fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));

  ASSERT_EQ(queue->pop(), std::nullopt);
}

TEST_F(FidlStreamSinkTest, InvalidInputPayloadAboveRange) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  TestFence fence;
  ASSERT_NO_FATAL_FAILURE(PutPacket(
      {
          .buffer_id = kBufferId,
          .offset = kBufferSize - kFormat.bytes_per_frame() + 1,
          .size = static_cast<uint64_t>(kFormat.bytes_per_frame()),
      },
      fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));

  ASSERT_EQ(queue->pop(), std::nullopt);
}

TEST_F(FidlStreamSinkTest, InvalidInputPayloadNonIntegralFrames) {
  auto queue = std::make_shared<CommandQueue>();
  AddProducerQueue(queue);

  TestFence fence;
  ASSERT_NO_FATAL_FAILURE(PutPacket(
      {
          .buffer_id = kBufferId,
          .offset = 0,
          .size = static_cast<uint64_t>(kFormat.bytes_per_frame()) - 1,
      },
      fuchsia_media2::wire::PacketTimestamp::WithUnspecifiedContinuous({}), fence.Take()));

  ASSERT_EQ(queue->pop(), std::nullopt);
}

}  // namespace media_audio
