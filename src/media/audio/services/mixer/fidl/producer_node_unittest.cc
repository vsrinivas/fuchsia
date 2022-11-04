// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/producer_node.h"

#include <lib/async-testing/test_loop.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/real_clock.h"
#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/fidl/testing/test_stream_sink_server_and_client.h"
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

constexpr uint32_t kStreamSinkBufferId = 0;
constexpr uint64_t kStreamSinkBufferSize = 4096;
constexpr int64_t kRingBufferFrames = 10;

std::unique_ptr<TestStreamSinkServerAndClient> MakeStreamSink() {
  return std::make_unique<TestStreamSinkServerAndClient>(
      FidlThread::CreateFromNewThread("test_fidl_thread"), kStreamSinkBufferId,
      kStreamSinkBufferSize, kFormat, kMediaTicksPerNs);
}

enum class TestDataSource {
  kStreamSink,
  kRingBuffer,
};

struct TestHarness {
  TestHarness(TestHarness&& rhs) = default;
  ~TestHarness();

  FakeGraph& graph;
  std::shared_ptr<Clock> clock;
  ClockSnapshots clock_snapshots;
  std::unique_ptr<TestStreamSinkServerAndClient> stream_sink;
  std::shared_ptr<MemoryMappedBuffer> buffer;
  std::shared_ptr<RingBuffer> ring_buffer;
  std::shared_ptr<ProducerNode> producer;
};

TestHarness::~TestHarness() {
  // Destroy to cleanup references.
  const auto& ctx = graph.ctx();
  Node::Destroy(ctx, producer);
  ctx.global_task_queue.RunForThread(ctx.detached_thread->id());
}

TestHarness MakeTestHarness(FakeGraph& graph, TestDataSource source) {
  TestHarness h{.graph = graph};
  h.clock = RealClock::CreateFromMonotonic("ReferenceClock", Clock::kExternalDomain, true);
  h.clock_snapshots.AddClock(h.clock);
  h.clock_snapshots.Update(zx::clock::get_monotonic());

  if (source == TestDataSource::kStreamSink) {
    h.stream_sink = MakeStreamSink();
  } else {
    h.buffer = MemoryMappedBuffer::CreateOrDie(kRingBufferFrames * kFormat.bytes_per_frame(), true);
    h.ring_buffer = std::make_shared<RingBuffer>(kFormat, UnreadableClock(h.clock), h.buffer);
  }

  h.producer = ProducerNode::Create({
      .pipeline_direction = PipelineDirection::kInput,
      .format = kFormat,
      .reference_clock = h.clock,
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .data_source = (source == TestDataSource::kStreamSink)
                         ? ProducerNode::DataSource(h.stream_sink->server_ptr())
                         : ProducerNode::DataSource(h.ring_buffer),
      .delay_watcher = DelayWatcherClient::Create({
          .initial_delay = zx::nsec(100),
      }),
      .detached_thread = graph.ctx().detached_thread,
      .global_task_queue = graph.global_task_queue(),
  });

  return h;
}

TEST(ProducerNodeTest, CreateEdgeCannotAcceptSource) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .default_pipeline_direction = PipelineDirection::kInput,
  });

  const auto& ctx = graph.ctx();
  auto q = graph.global_task_queue();
  auto h = MakeTestHarness(graph, TestDataSource::kStreamSink);

  EXPECT_EQ(h.producer->thread(), ctx.detached_thread);
  EXPECT_EQ(h.producer->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());

  auto result = Node::CreateEdge(ctx, graph.node(1), h.producer, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST(ProducerNodeTest, CreateEdgeSuccessWithStreamSink) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .default_pipeline_direction = PipelineDirection::kInput,
  });

  const auto& ctx = graph.ctx();
  auto q = graph.global_task_queue();
  auto h = MakeTestHarness(graph, TestDataSource::kStreamSink);

  EXPECT_EQ(h.producer->type(), Node::Type::kProducer);
  EXPECT_EQ(h.producer->pipeline_direction(), PipelineDirection::kInput);
  EXPECT_EQ(h.producer->reference_clock(), h.clock);
  EXPECT_EQ(h.producer->thread(), ctx.detached_thread);
  EXPECT_EQ(h.producer->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(h.producer->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(h.producer->pipeline_stage()->reference_clock(), h.clock);

  // Connect producer -> dest.
  auto dest = graph.node(1);
  {
    auto result = Node::CreateEdge(ctx, h.producer, dest, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(h.producer->dest(), dest);
  EXPECT_THAT(dest->sources(), ::testing::ElementsAre(h.producer));

  q->RunForThread(ctx.detached_thread->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(),
              ::testing::ElementsAre(h.producer->pipeline_stage()));

  // Start the producer's internal frame timeline.
  h.producer->Start(ProducerStage::StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = zx::time(0)},
      .start_position = Fixed(0),
  });

  // Also start the producer's downstream frame timeline.
  h.producer->pipeline_stage()->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(kFormat));

  // Send a packet with 10 frames.
  {
    fidl::Arena arena;
    TestFence fence;
    ASSERT_NO_FATAL_FAILURE(h.stream_sink->PutPacket(
        {
            .buffer_id = kStreamSinkBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(10 * kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithSpecified(arena, 0), fence.Take()));
  }

  // Verify those commands were received by the ProducerStage.
  {
    MixJobContext ctx(h.clock_snapshots, zx::time(0), zx::time(10));
    const auto packet = h.producer->pipeline_stage()->Read(ctx, Fixed(0), 20);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start_frame(), 0);
    EXPECT_EQ(packet->frame_count(), 10);
    EXPECT_EQ(packet->end_frame(), 10);
  }

  // Disconnect producer -> dest.
  {
    auto result = Node::DeleteEdge(ctx, h.producer, dest);
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(h.producer->dest(), nullptr);
  EXPECT_THAT(dest->sources(), ::testing::ElementsAre());

  q->RunForThread(ctx.detached_thread->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(), ::testing::ElementsAre());
}

TEST(ProducerNodeTest, CreateEdgeSuccessWithRingBuffer) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .default_pipeline_direction = PipelineDirection::kInput,
  });

  const auto& ctx = graph.ctx();
  auto q = graph.global_task_queue();
  auto h = MakeTestHarness(graph, TestDataSource::kRingBuffer);

  // Connect producer -> dest.
  auto dest = graph.node(1);
  {
    auto result = Node::CreateEdge(ctx, h.producer, dest, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(h.producer->dest(), dest);
  EXPECT_EQ(h.producer->pipeline_direction(), PipelineDirection::kInput);
  EXPECT_EQ(h.producer->thread(), ctx.detached_thread);
  EXPECT_EQ(h.producer->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(h.producer->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(h.producer->pipeline_stage()->reference_clock(), h.clock);
  EXPECT_THAT(dest->sources(), ElementsAre(h.producer));

  q->RunForThread(ctx.detached_thread->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(), ElementsAre(h.producer->pipeline_stage()));

  // Start the producer's internal frame timeline.
  h.producer->Start({
      .start_time = RealTime{.clock = WhichClock::kReference, .time = zx::time(0)},
      .start_position = Fixed(0),
  });

  // Also start the producer's downstream frame timeline. This is normally updated by the Consumer.
  h.producer->pipeline_stage()->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(kFormat));

  // Write to the ring buffer.
  std::vector<float> payload(kFormat.channels() * kRingBufferFrames, 0.25f);
  RingBufferConsumerWriter writer(h.ring_buffer);
  writer.WriteData(0, 5, payload.data());

  // Verify that packet was received by the producer stage.
  {
    MixJobContext ctx(h.clock_snapshots, zx::time(0), zx::time(10));
    const auto packet = h.producer->pipeline_stage()->Read(ctx, Fixed(0), 5);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start_frame(), 0);
    EXPECT_EQ(packet->frame_count(), 5);
    EXPECT_EQ(packet->end_frame(), 5);
    EXPECT_EQ(packet->payload(), h.buffer->start());

    auto* data = static_cast<const float*>(packet->payload());
    for (size_t k = 0; k < 5; k++) {
      EXPECT_EQ(data[k], 0.25f) << "data[" << k << "]";
    }
  }

  // Disconnect producer -> dest.
  {
    auto result = Node::DeleteEdge(ctx, h.producer, dest);
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(h.producer->dest(), nullptr);
  EXPECT_THAT(dest->sources(), ElementsAre());

  q->RunForThread(ctx.detached_thread->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(), ElementsAre());

  // Destroy to cleanup references.
  Node::Destroy(ctx, h.producer);
  q->RunForThread(ctx.detached_thread->id());
}

TEST(ProducerNodeTest, StopCancelsStart) {
  FakeGraph graph({});

  auto h = MakeTestHarness(graph, TestDataSource::kStreamSink);

  // Start then stop immediately -- the stop should cancel the start.
  bool canceled = false;
  h.producer->Start(ProducerStage::StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = zx::time(0)},
      .start_position = Fixed(0),
      .callback =
          [&canceled](auto result) {
            ASSERT_TRUE(result.is_error());
            EXPECT_EQ(result.error(), StartStopControl::StartError::kCanceled);
            canceled = true;
          },
  });
  h.producer->Stop(ProducerStage::StopCommand{
      .when = Fixed(1),
  });

  EXPECT_TRUE(canceled);
}

TEST(ProducerNodeTest, StartCancelsStop) {
  FakeGraph graph({});

  auto h = MakeTestHarness(graph, TestDataSource::kStreamSink);

  // Start the producer's internal frame timeline.
  h.producer->Start(ProducerStage::StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = zx::time(0)},
      .start_position = Fixed(0),
  });

  // Also start the producer's downstream frame timeline.
  h.producer->pipeline_stage()->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(kFormat));

  // Read from the producer to ensure the Start command is applied.
  {
    MixJobContext ctx(h.clock_snapshots, zx::time(0), zx::time(10));
    [[maybe_unused]] const auto packet = h.producer->pipeline_stage()->Read(ctx, Fixed(0), 20);
  }

  // Stop then start immediately -- the start should cancel the stop.
  bool canceled = false;
  h.producer->Stop(ProducerStage::StopCommand{
      .when = Fixed(1),
      .callback =
          [&canceled](auto result) {
            ASSERT_TRUE(result.is_error());
            EXPECT_EQ(result.error(), StartStopControl::StopError::kCanceled);
            canceled = true;
          },
  });
  h.producer->Start(ProducerStage::StartCommand{
      .start_time = RealTime{.clock = WhichClock::kReference, .time = zx::time(0) + zx::msec(100)},
      .start_position = Fixed(1000),
  });

  EXPECT_TRUE(canceled);
}

TEST(ProducerNodeTest, InputPipelineUsesExternalDelay) {
  FakeGraph graph({});

  const auto& ctx = graph.ctx();
  auto q = graph.global_task_queue();

  async::TestLoop loop;
  auto thread = FidlThread::CreateFromCurrentThread("TestFidlThread", loop.dispatcher());
  auto endpoints = fidl::CreateEndpoints<fuchsia_audio::DelayWatcher>();
  auto external_delay_server = DelayWatcherServer::Create(thread, std::move(endpoints->server), {});

  auto stream_sink = MakeStreamSink();
  auto producer = ProducerNode::Create({
      .pipeline_direction = PipelineDirection::kInput,
      .format = kFormat,
      .reference_clock = DefaultClock(),
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .data_source = stream_sink->server_ptr(),
      .delay_watcher = DelayWatcherClient::Create({
          .client_end = std::move(endpoints->client),
          .thread = thread,
      }),
      .detached_thread = graph.ctx().detached_thread,
      .global_task_queue = graph.global_task_queue(),
  });

  loop.RunUntilIdle();
  EXPECT_EQ(producer->max_upstream_input_pipeline_delay(), zx::nsec(0));

  external_delay_server->set_delay(zx::nsec(10));
  loop.RunUntilIdle();
  EXPECT_EQ(producer->max_upstream_input_pipeline_delay(), zx::nsec(10));

  external_delay_server->set_delay(zx::nsec(20));
  loop.RunUntilIdle();
  EXPECT_EQ(producer->max_upstream_input_pipeline_delay(), zx::nsec(20));

  // Destroy to cleanup references.
  Node::Destroy(ctx, producer);
  q->RunForThread(ctx.detached_thread->id());
  external_delay_server->Shutdown();
}

TEST(ProducerNodeTest, OutputPipelineReportsLeadTime) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .default_pipeline_direction = PipelineDirection::kOutput,
  });

  const auto& ctx = graph.ctx();
  auto q = graph.global_task_queue();

  async::TestLoop loop;
  auto thread = FidlThread::CreateFromCurrentThread("TestFidlThread", loop.dispatcher());

  auto stream_sink = MakeStreamSink();
  auto producer = ProducerNode::Create({
      .pipeline_direction = PipelineDirection::kOutput,
      .format = kFormat,
      .reference_clock = DefaultClock(),
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .data_source = stream_sink->server_ptr(),
      .thread_for_lead_time_servers = thread,
      .detached_thread = graph.ctx().detached_thread,
      .global_task_queue = graph.global_task_queue(),
  });

  struct LeadTimeClient {
    static std::shared_ptr<LeadTimeClient> Create(std::shared_ptr<const FidlThread> thread,
                                                  std::shared_ptr<ProducerNode> producer) {
      auto c = std::make_shared<LeadTimeClient>();

      auto endpoints = fidl::CreateEndpoints<fuchsia_audio::DelayWatcher>();
      c->client = DelayWatcherClient::Create({
          .client_end = std::move(endpoints->client),
          .thread = thread,
      });
      c->client->SetCallback([c](auto d) {
        c->value = d;
        c->have_response = true;
      });
      producer->BindLeadTimeWatcher(std::move(endpoints->server));
      return c;
    }

    void Expect(std::optional<zx::duration> expected) {
      EXPECT_TRUE(have_response);
      EXPECT_EQ(value, expected);
      have_response = false;
    }

    std::shared_ptr<DelayWatcherClient> client;
    std::optional<zx::duration> value;
    bool have_response = false;
  };

  // A client to watch for lead time updates.
  auto client1 = LeadTimeClient::Create(thread, producer);

  // Create an edge to `dest` with a non-zero delay.
  auto dest = graph.node(1);
  dest->SetOnPresentationDelayForSourceEdge([](auto source) { return zx::nsec(10); });

  {
    auto result = Node::CreateEdge(ctx, producer, dest, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  {
    SCOPED_TRACE("edge updates lead time");
    loop.RunUntilIdle();
    EXPECT_EQ(producer->max_downstream_output_pipeline_delay(), zx::nsec(10));
    client1->Expect(zx::nsec(10));
  }

  auto client2 = LeadTimeClient::Create(thread, producer);
  {
    SCOPED_TRACE("second client sees value instantly");
    loop.RunUntilIdle();
    client2->Expect(zx::nsec(10));
  }

  // Destroy to cleanup references.
  Node::Destroy(ctx, producer);
  q->RunForThread(ctx.detached_thread->id());
  client1->client->Shutdown();
  client2->client->Shutdown();
  loop.RunUntilIdle();
}

}  // namespace
}  // namespace media_audio
