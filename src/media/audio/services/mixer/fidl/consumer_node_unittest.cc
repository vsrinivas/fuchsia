// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/consumer_node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/fidl/testing/graph_mix_thread_without_loop.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/fake_consumer_stage_writer.h"

namespace media_audio {
namespace {

using SampleType = fuchsia_audio::SampleType;
using ::testing::ElementsAre;

const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 10000});
const Format kWrongFormat = Format::CreateOrDie({SampleType::kFloat32, 1, 10000});
const auto kPipelineDirection = PipelineDirection::kOutput;

// At 10kHz fps, 1ms is 10 frames.
constexpr auto kMixJobFrames = 10;
constexpr auto kMixJobPeriod = zx::msec(1);

class ConsumerNodeTest : public ::testing::Test {
 protected:
  struct TestHarness {
    TestHarness() = default;
    TestHarness(TestHarness&&) = default;
    ~TestHarness();

    std::unique_ptr<FakeGraph> graph;
    std::shared_ptr<SyntheticClockRealm> clock_realm;
    std::shared_ptr<SyntheticClock> clock;
    std::shared_ptr<GraphMixThread> mix_thread;
    std::shared_ptr<FakeConsumerStageWriter> consumer_writer;
    std::shared_ptr<ConsumerNode> consumer_node;
  };

  TestHarness MakeTestHarness(FakeGraph::Args graph_args);
};

ConsumerNodeTest::TestHarness ConsumerNodeTest::MakeTestHarness(FakeGraph::Args graph_args) {
  TestHarness h;
  h.graph = std::make_unique<FakeGraph>(std::move(graph_args));
  h.clock_realm = SyntheticClockRealm::Create();
  h.clock = h.clock_realm->CreateClock("clock", Clock::kMonotonicDomain, false);

  // Since we don't run the mix loop, immediately stop the timer so the realm never waits for it.
  auto timer = h.clock_realm->CreateTimer();
  timer->Stop();
  h.mix_thread = CreateGraphMixThreadWithoutLoop({
      .id = 1,
      .name = "TestThread",
      .mix_period = kMixJobPeriod,
      .cpu_per_period = kMixJobPeriod / 2,
      .global_task_queue = h.graph->global_task_queue(),
      .timer = std::move(timer),
      .mono_clock = h.clock_realm->CreateClock("mono_clock", Clock::kMonotonicDomain, false),
  });

  h.consumer_writer = std::make_shared<FakeConsumerStageWriter>();
  h.consumer_node = ConsumerNode::Create({
      .pipeline_direction = kPipelineDirection,
      .format = kFormat,
      .reference_clock = h.clock,
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .writer = h.consumer_writer,
      .thread = h.mix_thread,
  });

  return h;
}

// This removes a circular references between the consumer and thread objects.
ConsumerNodeTest::TestHarness::~TestHarness() {
  Node::Destroy(graph->ctx(), consumer_node);
  EXPECT_EQ(mix_thread->num_consumers(), 0);
  graph->global_task_queue()->RunForThread(mix_thread->id());
}

TEST_F(ConsumerNodeTest, CreateEdgeSourceBadFormat) {
  auto h = MakeTestHarness({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&kWrongFormat, {1}}},
  });

  auto& graph = *h.graph;

  // Cannot create an edge where a the source has a different format than the consumer.
  auto result = Node::CreateEdge(graph.ctx(), graph.node(1), h.consumer_node, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(ConsumerNodeTest, CreateEdgeTooManySources) {
  auto h = MakeTestHarness({
      .unconnected_ordinary_nodes = {1, 2},
      .formats = {{&kFormat, {1, 2}}},
  });

  auto& graph = *h.graph;

  // First edge is OK.
  {
    auto result = Node::CreateEdge(graph.ctx(), graph.node(1), h.consumer_node,
                                   /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  // Cannot create a second incoming edge.
  {
    auto result = Node::CreateEdge(graph.ctx(), graph.node(2), h.consumer_node,
                                   /*options=*/{});
    ASSERT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(),
              fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
  }
}

TEST_F(ConsumerNodeTest, CreateEdgeDestNotAllowed) {
  auto h = MakeTestHarness({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&kFormat, {1}}},
  });

  auto& graph = *h.graph;

  // Cannot use consumers as a source.
  auto result = Node::CreateEdge(graph.ctx(), h.consumer_node, graph.node(1), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(ConsumerNodeTest, CreateEdgeSuccess) {
  auto h = MakeTestHarness({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&kFormat, {1}}},
  });

  auto& graph = *h.graph;
  auto& q = *graph.global_task_queue();

  // Connect source -> consumer.
  auto source = graph.node(1);
  {
    auto result = Node::CreateEdge(graph.ctx(), source, h.consumer_node, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  auto consumer_stage = static_cast<ConsumerStage*>(h.consumer_node->pipeline_stage().get());
  EXPECT_EQ(h.consumer_node->pipeline_direction(), kPipelineDirection);
  EXPECT_EQ(h.consumer_node->thread(), h.mix_thread);
  EXPECT_EQ(consumer_stage->thread(), h.mix_thread->pipeline_thread());
  EXPECT_EQ(consumer_stage->format(), kFormat);
  EXPECT_EQ(consumer_stage->reference_clock(), h.clock);
  EXPECT_EQ(h.mix_thread->num_consumers(), 1);

  q.RunForThread(h.mix_thread->id());

  // Start the consumer.
  h.consumer_node->Start({
      .start_time =
          StartStopControl::RealTime{
              .clock = StartStopControl::WhichClock::Reference,
              .time = zx::time(0),
          },
      .start_position = Fixed(0),
  });

  // Feed data into the source, including data for the second mix job -- later, we'll verify we
  // successfully disconnected from this source by checking that the second mix job doesn't read
  // this data.
  std::vector<float> source_payload(2 * kMixJobFrames);
  source->fake_pipeline_stage()->SetPacketForRead(PacketView({
      .format = kFormat,
      .start = Fixed(kMixJobFrames),  // first mix job happens one period in the future
      .length = 2 * kMixJobFrames,
      .payload = source_payload.data(),
  }));

  // Run a mix job, which should write the source data to the destination buffer.
  {
    const auto now = h.clock_realm->now();

    ClockSnapshots clock_snapshots;
    clock_snapshots.AddClock(h.clock);
    clock_snapshots.Update(now);

    MixJobContext ctx(clock_snapshots, now, now + kMixJobPeriod);
    auto status = consumer_stage->RunMixJob(ctx, now, kMixJobPeriod);
    ASSERT_TRUE(std::holds_alternative<ConsumerStage::StartedStatus>(status));

    ASSERT_EQ(h.consumer_writer->packets().size(), 1u);
    EXPECT_FALSE(h.consumer_writer->packets()[0].is_silence);
    EXPECT_EQ(h.consumer_writer->packets()[0].start_frame, kMixJobFrames);  // first mix job
    EXPECT_EQ(h.consumer_writer->packets()[0].length, kMixJobFrames);
    EXPECT_EQ(h.consumer_writer->packets()[0].data, source_payload.data());
    h.consumer_writer->packets().clear();
  }

  // Disconnect source -> consumer.
  {
    auto result = Node::DeleteEdge(graph.ctx(), source, h.consumer_node);
    ASSERT_TRUE(result.is_ok());
    EXPECT_THAT(h.consumer_node->sources(), ElementsAre());
  }

  q.RunForThread(h.mix_thread->id());

  // Run a mix job, which should write silence now that the source is disconnected.
  {
    h.clock_realm->AdvanceBy(kMixJobPeriod);
    const auto now = h.clock_realm->now();

    ClockSnapshots clock_snapshots;
    clock_snapshots.AddClock(h.clock);
    clock_snapshots.Update(now);

    MixJobContext ctx(clock_snapshots, now, now + kMixJobPeriod);
    auto status = consumer_stage->RunMixJob(ctx, now, kMixJobPeriod);
    ASSERT_TRUE(std::holds_alternative<ConsumerStage::StartedStatus>(status));

    ASSERT_EQ(h.consumer_writer->packets().size(), 1u);
    EXPECT_TRUE(h.consumer_writer->packets()[0].is_silence);
    EXPECT_EQ(h.consumer_writer->packets()[0].start_frame, 2 * kMixJobFrames);  // second mix job
    EXPECT_EQ(h.consumer_writer->packets()[0].length, kMixJobFrames);
  }
}

}  // namespace
}  // namespace media_audio
