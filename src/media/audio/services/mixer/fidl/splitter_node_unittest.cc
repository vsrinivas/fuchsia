// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/splitter_node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/real_clock.h"
#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/fidl/consumer_node.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/fidl/testing/graph_mix_thread_without_loop.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/fake_consumer_stage_writer.h"

namespace media_audio {
namespace {

using ::testing::ElementsAre;

const auto kFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kInt32, 1, 1000});
const auto kMixPeriod = zx::msec(10);
const auto kMixPeriodFrames = 10;  // 10ms = 10 frames at 1kHz

std::shared_ptr<GraphMixThread> MakeThread(std::shared_ptr<SyntheticClockRealm> clock_realm,
                                           std::shared_ptr<GlobalTaskQueue> q, ThreadId id) {
  auto timer = clock_realm->CreateTimer();
  timer->Stop();
  return CreateGraphMixThreadWithoutLoop({
      .id = id,
      .name = "consumer_thread",
      .mix_period = kMixPeriod,
      .cpu_per_period = kMixPeriod / 2,
      .global_task_queue = q,
      .timer = std::move(timer),
      .mono_clock = clock_realm->CreateClock("mono_clock", Clock::kMonotonicDomain, false),
  });
}

TEST(SplitterNodeTest, CreateSourceEdge) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .formats = {{&kFormat, {1, 2}}},
  });

  auto q = graph.global_task_queue();
  auto clock_realm = SyntheticClockRealm::Create();
  auto clock = clock_realm->CreateClock("ref_clock", Clock::kMonotonicDomain, false);
  auto thread = MakeThread(clock_realm, q, 1);

  auto splitter = SplitterNode::Create({
      .pipeline_direction = PipelineDirection::kOutput,
      .format = kFormat,
      .reference_clock = clock,
      .consumer_thread = thread,
      .detached_thread = graph.ctx().detached_thread,
  });

  // First incoming edge succeeds.
  {
    auto result = Node::CreateEdge(graph.ctx(), graph.node(1), splitter, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  ASSERT_EQ(splitter->child_sources().size(), 1u);
  ASSERT_EQ(splitter->child_dests().size(), 0u);

  auto consumer = splitter->child_sources()[0];
  EXPECT_EQ(consumer->thread(), thread);
  ASSERT_EQ(consumer->sources().size(), 1u);
  EXPECT_EQ(consumer->sources()[0], graph.node(1));
  EXPECT_EQ(consumer->pipeline_stage()->thread(), thread->pipeline_thread());
  EXPECT_EQ(consumer->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(consumer->pipeline_stage()->reference_clock(), clock);

  // Second incoming edge fails.
  {
    auto result = Node::CreateEdge(graph.ctx(), graph.node(2), splitter, /*options=*/{});
    ASSERT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(),
              fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
  }

  // Disconnect.
  {
    auto result = Node::DeleteEdge(graph.ctx(), graph.node(1), splitter);
    ASSERT_TRUE(result.is_ok());
  }

  ASSERT_EQ(splitter->child_sources().size(), 0u);
  ASSERT_EQ(splitter->child_dests().size(), 0u);

  // Now that we've disconnected, we can create another edge.
  {
    auto result = Node::CreateEdge(graph.ctx(), graph.node(1), splitter, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  ASSERT_EQ(splitter->child_sources().size(), 1u);
  ASSERT_EQ(splitter->child_dests().size(), 0u);

  // Cleanup all references.
  Node::Destroy(graph.ctx(), splitter);
  q->RunForThread(thread->id());
}

TEST(SplitterNodeTest, CreateDestEdge) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
  });

  auto q = graph.global_task_queue();
  auto clock_realm = SyntheticClockRealm::Create();
  auto clock = clock_realm->CreateClock("ref_clock", Clock::kMonotonicDomain, false);
  auto thread = MakeThread(clock_realm, q, 1);

  auto splitter = SplitterNode::Create({
      .pipeline_direction = PipelineDirection::kOutput,
      .format = kFormat,
      .reference_clock = clock,
      .consumer_thread = thread,
      .detached_thread = graph.ctx().detached_thread,
  });

  // Create two outgoing edges.
  for (size_t n = 1; n <= 2; n++) {
    SCOPED_TRACE("create edge to Node" + std::to_string(n));

    auto dest = graph.node(n);
    auto result = Node::CreateEdge(graph.ctx(), splitter, dest, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(splitter->child_sources().size(), 0u);
    ASSERT_EQ(splitter->child_dests().size(), n);

    auto producer = splitter->child_dests()[n - 1];
    EXPECT_EQ(producer->thread(), graph.ctx().detached_thread);
    EXPECT_EQ(producer->dest(), dest);
    EXPECT_EQ(producer->pipeline_stage()->thread(), graph.ctx().detached_thread->pipeline_thread());
    EXPECT_EQ(producer->pipeline_stage()->format(), kFormat);
    EXPECT_EQ(producer->pipeline_stage()->reference_clock(), clock);
    EXPECT_THAT(dest->sources(), ElementsAre(producer));
  }

  // Disconnect those edges.
  for (size_t n = 1; n <= 2; n++) {
    SCOPED_TRACE("delete edge to Node" + std::to_string(n));

    auto dest = graph.node(n);
    auto result = Node::DeleteEdge(graph.ctx(), splitter, dest);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(splitter->child_sources().size(), 0u);
    ASSERT_EQ(splitter->child_dests().size(), 2 - n);
  }

  // Cleanup all references.
  Node::Destroy(graph.ctx(), splitter);
  q->RunForThread(thread->id());
}

TEST(SplitterNodeTest, CopySourceToDests) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&kFormat, {1}}},
  });

  auto q = graph.global_task_queue();
  auto source = graph.node(1);
  auto clock_realm = SyntheticClockRealm::Create();
  auto clock = clock_realm->CreateClock("ref_clock", Clock::kMonotonicDomain, false);
  auto thread1 = MakeThread(clock_realm, q, 1);
  auto thread2 = MakeThread(clock_realm, q, 2);
  auto thread3 = MakeThread(clock_realm, q, 3);

  auto dest1_writer = std::make_shared<FakeConsumerStageWriter>();
  auto dest1 = ConsumerNode::Create({
      .name = "dest1",
      .pipeline_direction = PipelineDirection::kOutput,
      .format = kFormat,
      .reference_clock = clock,
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .writer = dest1_writer,
      .thread = thread1,
      .delay_watcher = DelayWatcherClient::Create({.initial_delay = zx::nsec(100)}),
      .global_task_queue = q,
  });
  q->RunForThread(thread1->id());

  auto dest2_writer = std::make_shared<FakeConsumerStageWriter>();
  auto dest2 = ConsumerNode::Create({
      .name = "dest2",
      .pipeline_direction = PipelineDirection::kOutput,
      .format = kFormat,
      .reference_clock = clock,
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .writer = dest2_writer,
      .thread = thread2,
      .delay_watcher = DelayWatcherClient::Create({.initial_delay = zx::nsec(200)}),
      .global_task_queue = q,
  });
  q->RunForThread(thread2->id());

  auto dest3_writer = std::make_shared<FakeConsumerStageWriter>();
  auto dest3 = ConsumerNode::Create({
      .name = "dest3",
      .pipeline_direction = PipelineDirection::kInput,
      .format = kFormat,
      .reference_clock = clock,
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .writer = dest3_writer,
      .thread = thread3,
      .global_task_queue = q,
  });
  q->RunForThread(thread3->id());

  auto splitter = SplitterNode::Create({
      .name = "splitter",
      .pipeline_direction = PipelineDirection::kOutput,
      .format = kFormat,
      .reference_clock = clock,
      .consumer_thread = thread1,
      .detached_thread = graph.ctx().detached_thread,
  });

  // Connect source -> splitter -> {dest1, dest2, dest3}.
  {
    auto result = Node::CreateEdge(graph.ctx(), source, splitter, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
    q->RunForThread(thread1->id());
  }
  {
    auto result = Node::CreateEdge(graph.ctx(), splitter, dest1, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
    q->RunForThread(thread1->id());
  }
  {
    auto result = Node::CreateEdge(graph.ctx(), splitter, dest2, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
    q->RunForThread(thread2->id());
    q->RunForThread(thread1->id());
  }
  {
    auto result = Node::CreateEdge(graph.ctx(), splitter, dest3, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
    q->RunForThread(thread3->id());
    q->RunForThread(thread1->id());
  }

  ASSERT_EQ(splitter->child_sources().size(), 1u);
  ASSERT_EQ(splitter->child_dests().size(), 3u);

  auto consumer = splitter->child_sources()[0];
  auto producer1 = splitter->child_dests()[0];  // same thread, not loopback
  auto producer2 = splitter->child_dests()[1];  // cross thread, not loopback
  auto producer3 = splitter->child_dests()[2];  // cross thread, loopback

  // Check node delays.
  EXPECT_EQ(producer1->max_downstream_output_pipeline_delay(), 2 * kMixPeriod + zx::nsec(100));
  EXPECT_EQ(producer2->max_downstream_output_pipeline_delay(), 2 * kMixPeriod + zx::nsec(200));
  EXPECT_EQ(producer3->max_downstream_output_pipeline_delay(), zx::nsec(0));
  // This is producer2's downstream delay plus an extra mix period because producer2 runs on a
  // different thread than the consumer.
  EXPECT_EQ(consumer->max_downstream_output_pipeline_delay(), 3 * kMixPeriod + zx::nsec(200));

  EXPECT_EQ(producer1->max_downstream_input_pipeline_delay(), zx::nsec(0));
  EXPECT_EQ(producer2->max_downstream_input_pipeline_delay(), zx::nsec(0));
  EXPECT_EQ(producer3->max_downstream_input_pipeline_delay(), 2 * kMixPeriod);
  EXPECT_EQ(consumer->max_downstream_input_pipeline_delay(), 2 * kMixPeriod);

  EXPECT_EQ(dest3->max_upstream_input_pipeline_delay(), 2 * kMixPeriod);

  EXPECT_EQ(source->max_downstream_output_pipeline_delay(),
            consumer->max_downstream_output_pipeline_delay());
  EXPECT_EQ(source->max_downstream_input_pipeline_delay(),
            consumer->max_downstream_input_pipeline_delay());

  // Check stage delays.
  {
    auto consumer_stage =
        std::static_pointer_cast<SplitterConsumerStage>(consumer->pipeline_stage());

    ScopedThreadChecker checker(consumer_stage->thread()->checker());
    EXPECT_EQ(consumer_stage->max_downstream_output_pipeline_delay(),
              3 * kMixPeriod + zx::nsec(200));
  }

  // The ring buffer should be large enough for this many frames, rounded up to a page.
  const auto expected_ring_buffer_bytes =
      static_cast<uint64_t>(kFormat.bytes_per(consumer->max_downstream_output_pipeline_delay() +
                                              consumer->max_downstream_input_pipeline_delay()));
  EXPECT_GE(splitter->ring_buffer_bytes(), expected_ring_buffer_bytes);
  EXPECT_EQ(splitter->ring_buffer_bytes() % zx_system_get_page_size(), 0u);

  // Start the pipelines with frame 0 presented at t=0.
  dest1->Start({
      .start_time =
          StartStopControl::RealTime{
              .clock = StartStopControl::WhichClock::kReference,
              .time = zx::time(0),
          },
      .start_position = Fixed(0),
  });
  dest2->Start({
      .start_time =
          StartStopControl::RealTime{
              .clock = StartStopControl::WhichClock::kReference,
              .time = zx::time(0),
          },
      .start_position = Fixed(0),
  });
  dest3->Start({
      .start_time =
          StartStopControl::RealTime{
              .clock = StartStopControl::WhichClock::kReference,
              .time = zx::time(0),
          },
      .start_position = Fixed(0),
  });

  q->RunForThread(thread1->id());
  q->RunForThread(thread2->id());
  q->RunForThread(thread3->id());

  // Give the source one packet starting at frame `kMixPeriodFrames`, which is exactly the start of
  // the first mix job for output pipelines (dest1 and dest2).
  std::vector<int32_t> source_payload(kMixPeriodFrames);
  for (int32_t k = 0; k < kMixPeriodFrames; k++) {
    source_payload[k] = k;
  }
  source->fake_pipeline_stage()->SetPacketForRead(PacketView({
      .format = kFormat,
      .start_frame = Fixed(kMixPeriodFrames),
      .frame_count = kMixPeriodFrames,
      .payload = source_payload.data(),
  }));

  // Run a mix job on each thread. The mix job on thread1 should prime the ring buffer. Both mix
  // jobs should consume the above packet.
  for (ThreadId tid = 1; tid <= 3; tid++) {
    SCOPED_TRACE("Mix on thread " + std::to_string(tid));

    auto& dest = (tid == 1) ? dest1 : (tid == 2) ? dest2 : dest3;
    auto& writer = (tid == 1) ? dest1_writer : (tid == 2) ? dest2_writer : dest3_writer;

    // Input pipelines read from the past. To read frame `kMixPeriodFrames` we need to be one mix
    // period in the future, hence for dest3 we start at the third mix period.
    const auto now = (tid == 3) ? zx::time(0) + 2 * kMixPeriod : zx::time(0);

    ClockSnapshots clock_snapshots;
    clock_snapshots.AddClock(clock);
    clock_snapshots.Update(now);

    MixJobContext ctx(clock_snapshots, now, now + kMixPeriod);
    auto consumer_stage = std::static_pointer_cast<ConsumerStage>(dest->pipeline_stage());
    auto status = consumer_stage->RunMixJob(ctx, now, kMixPeriod);
    ASSERT_TRUE(std::holds_alternative<ConsumerStage::StartedStatus>(status));

    // Verify that `dest` consumed `source_payload`.
    ASSERT_EQ(writer->packets().size(), 1u);
    const auto& packet = writer->packets()[0];
    EXPECT_FALSE(packet.is_silence);
    EXPECT_EQ(packet.start_frame, kMixPeriodFrames);  // first mix job
    EXPECT_EQ(packet.length, kMixPeriodFrames);
    static_assert(kMixPeriodFrames == 10);

    std::vector<int32_t> samples(static_cast<const int32_t*>(packet.data),
                                 static_cast<const int32_t*>(packet.data) + packet.length);
    EXPECT_THAT(samples, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  }

  // Cleanup all references.
  Node::Destroy(graph.ctx(), dest1);
  q->RunForThread(thread1->id());
  Node::Destroy(graph.ctx(), dest2);
  q->RunForThread(thread2->id());
  Node::Destroy(graph.ctx(), dest3);
  q->RunForThread(thread3->id());
  Node::Destroy(graph.ctx(), splitter);
  q->RunForThread(thread1->id());
}

}  // namespace
}  // namespace media_audio
