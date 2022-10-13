// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/producer_node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/real_clock.h"
#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/fidl_realtime/testing/test_stream_sink_server_and_client.h"
#include "src/media/audio/services/mixer/mix/ring_buffer.h"
#include "src/media/audio/services/mixer/mix/ring_buffer_consumer_writer.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using RealTime = StartStopControl::RealTime;
using WhichClock = StartStopControl::WhichClock;
using ::testing::ElementsAre;

const Format kFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
const auto kMediaTicksPerNs = TimelineRate(1, 10'000'000);  // 1 tick per 10ms

constexpr uint32_t kBufferId = 0;
constexpr uint64_t kBufferSize = 4096;

std::unique_ptr<TestStreamSinkServerAndClient> MakeStreamSink() {
  return std::make_unique<TestStreamSinkServerAndClient>(
      FidlThread::CreateFromNewThread("test_fidl_thread"), kBufferId, kBufferSize, kFormat,
      kMediaTicksPerNs);
}

TEST(ProducerNodeTest, CreateEdgeCannotAcceptSource) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();

  auto stream_sink = MakeStreamSink();
  auto producer = ProducerNode::Create({
      .pipeline_direction = PipelineDirection::kInput,
      .format = kFormat,
      .reference_clock = DefaultClock(),
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .data_source = stream_sink->server_ptr(),
      .detached_thread = graph.detached_thread(),
  });

  EXPECT_EQ(producer->thread(), graph.detached_thread());
  EXPECT_EQ(producer->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());

  auto result =
      Node::CreateEdge(*q, graph.detached_thread(), graph.node(1), producer, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST(ProducerNodeTest, CreateEdgeSuccessWithStreamSink) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();

  const auto clock = RealClock::CreateFromMonotonic("ReferenceClock", Clock::kExternalDomain, true);
  ClockSnapshots clock_snapshots;
  clock_snapshots.AddClock(clock);
  clock_snapshots.Update(zx::clock::get_monotonic());
  MixJobContext ctx(clock_snapshots);

  auto stream_sink = MakeStreamSink();
  auto producer = ProducerNode::Create({
      .pipeline_direction = PipelineDirection::kInput,
      .format = kFormat,
      .reference_clock = clock,
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .data_source = stream_sink->server_ptr(),
      .detached_thread = graph.detached_thread(),
  });

  ASSERT_NE(producer, nullptr);
  EXPECT_EQ(producer->type(), Node::Type::kProducer);
  EXPECT_EQ(producer->pipeline_direction(), PipelineDirection::kInput);
  EXPECT_EQ(producer->reference_clock(), clock);
  EXPECT_EQ(producer->thread(), graph.detached_thread());
  EXPECT_EQ(producer->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());
  EXPECT_EQ(producer->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(producer->pipeline_stage()->reference_clock(), clock);

  // Connect producer -> dest.
  auto dest = graph.node(1);
  {
    auto result = Node::CreateEdge(*q, graph.detached_thread(), producer, dest, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(producer->dest(), dest);
  EXPECT_THAT(dest->sources(), ::testing::ElementsAre(producer));

  q->RunForThread(graph.detached_thread()->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(),
              ::testing::ElementsAre(producer->pipeline_stage()));

  // Start the producer's internal frame timeline.
  producer->Start(ProducerStage::StartCommand{
      .start_time = RealTime{.clock = WhichClock::Reference, .time = zx::time(0)},
      .start_position = Fixed(0),
  });

  // Also start the producer's downstream frame timeline.
  producer->pipeline_stage()->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(kFormat));

  // Send a packet with 10 frames.
  {
    fidl::Arena arena;
    TestFence fence;
    ASSERT_NO_FATAL_FAILURE(stream_sink->PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(10 * kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithSpecified(arena, 0), fence.Take()));
  }

  // Verify those commands were received by the ProducerStage.
  {
    const auto packet = producer->pipeline_stage()->Read(ctx, Fixed(0), 20);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start(), 0);
    EXPECT_EQ(packet->length(), 10);
    EXPECT_EQ(packet->end(), 10);
  }

  // Disconnect producer -> dest.
  {
    auto result = Node::DeleteEdge(*q, graph.detached_thread(), producer, dest);
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(producer->dest(), nullptr);
  EXPECT_THAT(dest->sources(), ::testing::ElementsAre());

  q->RunForThread(graph.detached_thread()->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(), ::testing::ElementsAre());
}

TEST(ProducerNodeTest, CreateEdgeSuccessWithRingBuffer) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();

  const auto clock = RealClock::CreateFromMonotonic("ReferenceClock", Clock::kExternalDomain, true);
  ClockSnapshots clock_snapshots;
  clock_snapshots.AddClock(clock);
  clock_snapshots.Update(zx::clock::get_monotonic());
  MixJobContext ctx(clock_snapshots);

  constexpr int64_t kRingBufferFrames = 10;
  auto buffer =
      MemoryMappedBuffer::CreateOrDie(kRingBufferFrames * kFormat.bytes_per_frame(), true);

  auto ring_buffer = std::make_shared<RingBuffer>(
      kFormat, UnreadableClock(clock),
      std::make_shared<RingBuffer::Buffer>(buffer,
                                           /*producer_frames=*/kRingBufferFrames / 2,
                                           /*consumer_frames=*/kRingBufferFrames / 2));
  auto producer = ProducerNode::Create({
      .pipeline_direction = PipelineDirection::kInput,
      .format = kFormat,
      .reference_clock = clock,
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .data_source = ring_buffer,
      .detached_thread = graph.detached_thread(),
  });

  // Connect producer -> dest.
  auto dest = graph.node(1);
  {
    auto result = Node::CreateEdge(*q, graph.detached_thread(), producer, dest, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(producer->dest(), dest);
  EXPECT_EQ(producer->pipeline_direction(), PipelineDirection::kInput);
  EXPECT_EQ(producer->thread(), graph.detached_thread());
  EXPECT_EQ(producer->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());
  EXPECT_EQ(producer->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(producer->pipeline_stage()->reference_clock(), clock);
  EXPECT_THAT(dest->sources(), ElementsAre(producer));

  q->RunForThread(graph.detached_thread()->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(), ElementsAre(producer->pipeline_stage()));

  // Start the producer's internal frame timeline.
  producer->Start({
      .start_time = RealTime{.clock = WhichClock::Reference, .time = zx::time(0)},
      .start_position = Fixed(0),
  });

  // Also start the producer's downstream frame timeline. This is normally updated by the Consumer.
  producer->pipeline_stage()->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(kFormat));

  // Write to the ring buffer.
  std::vector<float> payload(kFormat.channels() * kRingBufferFrames, 0.25f);
  RingBufferConsumerWriter writer(ring_buffer);
  writer.WriteData(0, 5, payload.data());

  // Verify that packet was received by the producer stage.
  {
    const auto packet = producer->pipeline_stage()->Read(ctx, Fixed(0), 5);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start(), 0);
    EXPECT_EQ(packet->length(), 5);
    EXPECT_EQ(packet->end(), 5);
    EXPECT_EQ(packet->payload(), buffer->start());

    auto* data = static_cast<const float*>(packet->payload());
    for (size_t k = 0; k < 5; k++) {
      EXPECT_EQ(data[k], 0.25f) << "data[" << k << "]";
    }
  }

  // Disconnect producer -> dest.
  {
    auto result = Node::DeleteEdge(*q, graph.detached_thread(), producer, dest);
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(producer->dest(), nullptr);
  EXPECT_THAT(dest->sources(), ElementsAre());

  q->RunForThread(graph.detached_thread()->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(), ElementsAre());
}

TEST(ProducerNodeTest, StopCancelsStart) {
  FakeGraph graph({});

  auto stream_sink = MakeStreamSink();
  auto producer = ProducerNode::Create({
      .pipeline_direction = PipelineDirection::kInput,
      .format = kFormat,
      .reference_clock =
          RealClock::CreateFromMonotonic("ReferenceClock", Clock::kExternalDomain, true),
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .data_source = stream_sink->server_ptr(),
      .detached_thread = graph.detached_thread(),
  });

  // Start then stop immediately -- the stop should cancel the start.
  bool canceled = false;
  producer->Start(ProducerStage::StartCommand{
      .start_time = RealTime{.clock = WhichClock::Reference, .time = zx::time(0)},
      .start_position = Fixed(0),
      .callback =
          [&canceled](auto result) {
            ASSERT_TRUE(result.is_error());
            EXPECT_EQ(result.error(), StartStopControl::StartError::Canceled);
            canceled = true;
          },
  });
  producer->Stop(ProducerStage::StopCommand{
      .when = Fixed(1),
  });

  EXPECT_TRUE(canceled);
}

TEST(ProducerNodeTest, StartCancelsStop) {
  FakeGraph graph({});

  const auto clock = RealClock::CreateFromMonotonic("ReferenceClock", Clock::kExternalDomain, true);
  ClockSnapshots clock_snapshots;
  clock_snapshots.AddClock(clock);
  clock_snapshots.Update(zx::clock::get_monotonic());
  MixJobContext ctx(clock_snapshots);

  auto stream_sink = MakeStreamSink();
  auto producer = ProducerNode::Create({
      .pipeline_direction = PipelineDirection::kInput,
      .format = kFormat,
      .reference_clock = clock,
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .data_source = stream_sink->server_ptr(),
      .detached_thread = graph.detached_thread(),
  });

  // Start the producer's internal frame timeline.
  producer->Start(ProducerStage::StartCommand{
      .start_time = RealTime{.clock = WhichClock::Reference, .time = zx::time(0)},
      .start_position = Fixed(0),
  });

  // Also start the producer's downstream frame timeline.
  producer->pipeline_stage()->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(kFormat));

  // Read from the producer to ensure the Start command is applied.
  [[maybe_unused]] const auto packet = producer->pipeline_stage()->Read(ctx, Fixed(0), 20);

  // Stop then start immediately -- the start should cancel the stop.
  bool canceled = false;
  producer->Stop(ProducerStage::StopCommand{
      .when = Fixed(1),
      .callback =
          [&canceled](auto result) {
            ASSERT_TRUE(result.is_error());
            EXPECT_EQ(result.error(), StartStopControl::StopError::Canceled);
            canceled = true;
          },
  });
  producer->Start(ProducerStage::StartCommand{
      .start_time = RealTime{.clock = WhichClock::Reference, .time = zx::time(0) + zx::msec(100)},
      .start_position = Fixed(1000),
  });

  EXPECT_TRUE(canceled);
}

}  // namespace
}  // namespace media_audio
