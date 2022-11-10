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

// At 10kHz fps, 1ms is 10 frames.
constexpr auto kMixJobFrames = 10;
constexpr auto kMixJobPeriod = zx::msec(1);

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

TestHarness MakeTestHarness(FakeGraph::Args graph_args,
                            zx::duration consumer_downstream_delay = zx::nsec(0),
                            std::optional<SampleType> source_sample_type = std::nullopt) {
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

  std::shared_ptr<DelayWatcherClient> delay_watcher;
  if (graph_args.default_pipeline_direction == PipelineDirection::kOutput) {
    delay_watcher = DelayWatcherClient::Create({.initial_delay = consumer_downstream_delay});
  }

  h.consumer_writer = std::make_shared<FakeConsumerStageWriter>();
  h.consumer_node = ConsumerNode::Create({
      .pipeline_direction = graph_args.default_pipeline_direction,
      .format = kFormat,
      .source_sample_type = source_sample_type ? *source_sample_type : kFormat.sample_type(),
      .reference_clock = h.clock,
      .media_ticks_per_ns = kFormat.frames_per_ns(),
      .writer = h.consumer_writer,
      .thread = h.mix_thread,
      .delay_watcher = std::move(delay_watcher),
      .global_task_queue = h.graph->global_task_queue(),
  });

  return h;
}

// This removes a circular references between the consumer and thread objects.
TestHarness::~TestHarness() {
  Node::PrepareToDelete(graph->ctx(), consumer_node);
  EXPECT_EQ(mix_thread->num_consumers(), 0);
  graph->global_task_queue()->RunForThread(mix_thread->id());
}

TEST(ConsumerNodeTest, CreateEdgeSourceOtherFormats) {
  using ::fuchsia_audio_mixer::CreateEdgeError;

  struct TestCase {
    std::string name;
    Format source_format;
    std::optional<CreateEdgeError> expected_error;
  };
  const std::vector<TestCase> test_cases = {
      {
          .name = "DifferentChannels",
          .source_format = Format::CreateOrDie({SampleType::kFloat32, 1, 10000}),
          .expected_error = CreateEdgeError::kIncompatibleFormats,
      },
      {
          .name = "DifferentRate",
          .source_format = Format::CreateOrDie({SampleType::kFloat32, 2, 20000}),
          .expected_error = CreateEdgeError::kIncompatibleFormats,
      },
      {
          .name = "DifferentSampleType",
          .source_format = Format::CreateOrDie({SampleType::kInt32, 2, 10000}),
          .expected_error = std::nullopt,  // success
      },
  };

  for (auto& tc : test_cases) {
    SCOPED_TRACE(tc.name);

    auto h = MakeTestHarness(
        {
            .unconnected_ordinary_nodes = {1},
            .formats = {{&tc.source_format, {1}}},
        },
        /*consumer_downstream_delay=*/zx::nsec(0),
        /*source_sample_type=*/tc.source_format.sample_type());

    auto& graph = *h.graph;
    auto result = Node::CreateEdge(graph.ctx(), graph.node(1), h.consumer_node, /*options=*/{});

    if (tc.expected_error) {
      ASSERT_FALSE(result.is_ok());
      EXPECT_EQ(result.error(), *tc.expected_error);
    } else {
      ASSERT_TRUE(result.is_ok());
    }
  }
}

TEST(ConsumerNodeTest, CreateEdgeTooManySources) {
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

TEST(ConsumerNodeTest, CreateEdgeDestNotAllowed) {
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

struct CreateEdgeSuccessArgs {
  PipelineDirection direction;
  zx::duration consumer_downstream_delay;  // only if direction == output
  zx::duration source_upstream_delay;      // only if direction == input
  zx::time consumer_start_time;            // when the consumer starts
  zx::time mix_job_start_time;             // when the first mix job starts
  Fixed packet_start;                      // first non-silent frame in the source
  // Expected delays after connecting source -> dest.
  zx::duration expected_source_downstream_delay;  // only if direction == output
  zx::duration expected_consumer_upstream_delay;  // only if direction == input
  // Normally we run two mix jobs, one with a source and another after the source has been
  // disconnected. If this is true, that second mix job is skipped.
  bool skip_mix_job_after_disconnect = false;
};

void TestCreateEdgeSuccess(CreateEdgeSuccessArgs args) {
  auto h = MakeTestHarness(
      {
          .unconnected_ordinary_nodes = {1},
          .formats = {{&kFormat, {1}}},
          .default_pipeline_direction = args.direction,
      },
      args.consumer_downstream_delay);

  auto& graph = *h.graph;
  auto& q = *graph.global_task_queue();

  // Connect source -> consumer.
  auto source = graph.node(1);
  if (args.direction == PipelineDirection::kInput) {
    source->SetMaxDelays({.upstream_input_pipeline_delay = args.source_upstream_delay});
  }
  {
    auto result = Node::CreateEdge(graph.ctx(), source, h.consumer_node, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  auto consumer_stage = static_cast<ConsumerStage*>(h.consumer_node->pipeline_stage().get());
  EXPECT_EQ(h.consumer_node->pipeline_direction(), args.direction);
  EXPECT_EQ(h.consumer_node->thread(), h.mix_thread);
  EXPECT_EQ(consumer_stage->thread(), h.mix_thread->pipeline_thread());
  EXPECT_EQ(consumer_stage->format(), kFormat);
  EXPECT_EQ(consumer_stage->reference_clock(), h.clock);
  EXPECT_EQ(h.mix_thread->num_consumers(), 1);

  q.RunForThread(h.mix_thread->id());

  if (args.direction == PipelineDirection::kOutput) {
    EXPECT_EQ(source->max_downstream_output_pipeline_delay(),
              args.expected_source_downstream_delay);
  } else {
    EXPECT_EQ(h.consumer_node->max_upstream_input_pipeline_delay(),
              args.expected_consumer_upstream_delay);
  }

  // Start the consumer.
  h.consumer_node->Start({
      .start_time =
          StartStopControl::RealTime{
              .clock = StartStopControl::WhichClock::kReference,
              .time = args.consumer_start_time,
          },
      .stream_time = Fixed(0),
  });

  q.RunForThread(h.mix_thread->id());

  // Feed data into the source, including data for the second mix job -- later, we'll verify we
  // successfully disconnected from this source by checking that the second mix job doesn't read
  // this data.
  std::vector<float> source_payload(2 * kMixJobFrames);
  source->fake_pipeline_stage()->SetPacketForRead(PacketView({
      .format = kFormat,
      .start_frame = args.packet_start,
      .frame_count = 2 * kMixJobFrames,
      .payload = source_payload.data(),
  }));

  // Run a mix job, which should write the source data to the destination buffer.
  {
    h.clock_realm->AdvanceTo(args.mix_job_start_time);
    const auto now = h.clock_realm->now();

    ClockSnapshots clock_snapshots;
    clock_snapshots.AddClock(h.clock);
    clock_snapshots.Update(now);

    MixJobContext ctx(clock_snapshots, now, now + kMixJobPeriod);
    auto status = consumer_stage->RunMixJob(ctx, now, kMixJobPeriod);
    ASSERT_TRUE(std::holds_alternative<ConsumerStage::StartedStatus>(status));

    ASSERT_EQ(h.consumer_writer->packets().size(), 1u);
    EXPECT_FALSE(h.consumer_writer->packets()[0].is_silence);
    EXPECT_EQ(h.consumer_writer->packets()[0].start_frame, args.packet_start.Floor());
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

  // Run a second mix job, which should write silence now that the source is disconnected.
  if (!args.skip_mix_job_after_disconnect) {
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
    EXPECT_EQ(h.consumer_writer->packets()[0].start_frame,
              args.packet_start.Floor() + kMixJobFrames);
    EXPECT_EQ(h.consumer_writer->packets()[0].length, kMixJobFrames);
  }
}

TEST(ConsumerNodeTest, CreateEdgeSuccessOutputPipelineZeroExternalDelay) {
  // The first frame of the source's packet must align with the start of the first mix job. In this
  // case, the first mix job happens one period in the future.
  TestCreateEdgeSuccess({
      .direction = PipelineDirection::kOutput,
      .consumer_downstream_delay = zx::msec(0),
      .consumer_start_time = zx::time(0) + zx::msec(1),
      .mix_job_start_time = zx::time(0) + zx::msec(1),
      .packet_start = Fixed(kMixJobFrames),
      // Delay introduced by this consumer.
      .expected_source_downstream_delay = 2 * kMixJobPeriod,
  });
}

TEST(ConsumerNodeTest, CreateEdgeSuccessOutputPipelineNonZeroExternalDelay) {
  // As above, except there is an additional 1ms of delay, so the mix job must start 1ms earlier to
  // produce the same output.
  TestCreateEdgeSuccess({
      .direction = PipelineDirection::kOutput,
      .consumer_downstream_delay = zx::msec(1),
      .consumer_start_time = zx::time(0) + zx::msec(1),
      .mix_job_start_time = zx::time(0) + zx::msec(0),
      .packet_start = Fixed(kMixJobFrames),
      // Delay introduced when the consumer reads from its source, plus external delay.
      .expected_source_downstream_delay = 2 * kMixJobPeriod + zx::msec(1),
  });
}

TEST(ConsumerNodeTest, CreateEdgeSuccessInputPipelineZeroUpstreamDelay) {
  // The first frame of the source's packet must align with the start of the first mix job. In this
  // case, the first mix job happens one period in the past, so to read the frame presented at time
  // 0 we must run the mix job at time 0+period.
  TestCreateEdgeSuccess({
      .direction = PipelineDirection::kInput,
      .source_upstream_delay = zx::msec(0),
      .consumer_start_time = zx::time(0),
      .mix_job_start_time = zx::time(0) + kMixJobPeriod,
      .packet_start = Fixed(0),
      // Delay introduced when the consumer reads from its source.
      .expected_consumer_upstream_delay = 2 * kMixJobPeriod,
  });
}

TEST(ConsumerNodeTest, CreateEdgeSuccessInputPipelineNonZeroUpstreamDelay) {
  // As above, except there is an additional 1ms of delay, so the mix job must start 1ms later to
  // produce the same output.
  TestCreateEdgeSuccess({
      .direction = PipelineDirection::kInput,
      .source_upstream_delay = zx::msec(1),
      .consumer_start_time = zx::time(0),
      .mix_job_start_time = zx::time(0) + kMixJobPeriod + zx::msec(1),
      .packet_start = Fixed(0),
      // Delay introduced when the consumer reads from its source, plus source external delay.
      .expected_consumer_upstream_delay = 2 * kMixJobPeriod + zx::msec(1),
      // After disconnect, the upstream delay changes, meaning the second mix job won't be
      // continuous with the first mix job, so just skip it.
      .skip_mix_job_after_disconnect = true,
  });
}

}  // namespace
}  // namespace media_audio
