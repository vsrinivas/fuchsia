// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/consumer_node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/fake_consumer_stage_writer.h"
#include "src/media/audio/services/mixer/mix/testing/fake_thread.h"

namespace media_audio {
namespace {

using AudioSampleFormat = fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::ElementsAre;

const Format kFormat = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 10000});
const Format kWrongFormat = Format::CreateOrDie({AudioSampleFormat::kFloat, 1, 10000});
const auto kPipelineDirection = PipelineDirection::kOutput;

// At 10kHz fps, 1ms is 10 frames.
constexpr auto kMixJobFrames = 10;
constexpr auto kMixJobPeriod = zx::msec(1);

class ConsumerNodeTest : public ::testing::Test {
 protected:
  const ThreadPtr mix_thread_ = FakeThread::Create(1);
  const DetachedThreadPtr detached_thread_ = DetachedThread::Create();

  const std::shared_ptr<SyntheticClockRealm> clock_realm_ = SyntheticClockRealm::Create();
  const std::shared_ptr<SyntheticClock> clock_ =
      clock_realm_->CreateClock("clock", Clock::kMonotonicDomain, false);

  const std::shared_ptr<FakeConsumerStageWriter> consumer_writer_ =
      std::make_shared<FakeConsumerStageWriter>();
  const std::shared_ptr<ConsumerNode> consumer_node_ = ConsumerNode::Create({
      .pipeline_direction = kPipelineDirection,
      .format = kFormat,
      .reference_clock = UnreadableClock(clock_),
      .writer = consumer_writer_,
      .thread = mix_thread_,
  });
};

TEST_F(ConsumerNodeTest, CreateEdgeSourceBadFormat) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&kWrongFormat, {1}}},
      .default_thread = detached_thread_,
  });

  // Cannot create an edge where a the source has a different format than the consumer.
  auto result = Node::CreateEdge(q, graph.node(1), consumer_node_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(ConsumerNodeTest, CreateEdgeTooManySources) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .formats = {{&kFormat, {1, 2}}},
      .default_thread = detached_thread_,
  });

  // First edge is OK.
  {
    auto result = Node::CreateEdge(q, graph.node(1), consumer_node_);
    ASSERT_TRUE(result.is_ok());
  }

  // Cannot create a second incoming edge.
  {
    auto result = Node::CreateEdge(q, graph.node(2), consumer_node_);
    ASSERT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(),
              fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
  }
}

TEST_F(ConsumerNodeTest, CreateEdgeDestNotAllowed) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&kFormat, {1}}},
      .default_thread = detached_thread_,
  });

  // Cannot use consumers as a source.
  auto result = Node::CreateEdge(q, consumer_node_, graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(ConsumerNodeTest, CreateEdgeSuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&kFormat, {1}}},
      .default_thread = detached_thread_,
  });

  // Connect source -> consumer.
  auto source = graph.node(1);
  {
    auto result = Node::CreateEdge(q, source, consumer_node_);
    ASSERT_TRUE(result.is_ok());
  }

  auto consumer_stage = static_cast<ConsumerStage*>(consumer_node_->pipeline_stage().get());
  EXPECT_EQ(consumer_node_->pipeline_direction(), kPipelineDirection);
  EXPECT_EQ(consumer_node_->pipeline_stage_thread(), mix_thread_);
  EXPECT_EQ(consumer_stage->thread(), mix_thread_);
  EXPECT_EQ(consumer_stage->format(), kFormat);
  EXPECT_EQ(consumer_stage->reference_clock(), clock_);

  q.RunForThread(mix_thread_->id());

  // Start the consumer.
  consumer_node_->Start({
      .start_presentation_time = zx::time(0),
      .start_frame = 0,
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
    ClockSnapshots clock_snapshots;
    clock_snapshots.AddClock(clock_);
    clock_snapshots.Update(clock_realm_->now());

    MixJobContext ctx(clock_snapshots);
    auto status = consumer_stage->RunMixJob(ctx, clock_realm_->now(), kMixJobPeriod);
    ASSERT_TRUE(std::holds_alternative<ConsumerStage::StartedStatus>(status));

    ASSERT_EQ(consumer_writer_->packets().size(), 1u);
    EXPECT_FALSE(consumer_writer_->packets()[0].is_silence);
    EXPECT_EQ(consumer_writer_->packets()[0].start_frame, kMixJobFrames);  // first mix job
    EXPECT_EQ(consumer_writer_->packets()[0].length, kMixJobFrames);
    EXPECT_EQ(consumer_writer_->packets()[0].data, source_payload.data());
    consumer_writer_->packets().clear();
  }

  // Disconnect source -> consumer.
  {
    auto result = Node::DeleteEdge(q, source, consumer_node_, detached_thread_);
    ASSERT_TRUE(result.is_ok());
    EXPECT_THAT(consumer_node_->sources(), ElementsAre());
  }

  q.RunForThread(mix_thread_->id());

  // Run a mix job, which should write silence now that the source is disconnected.
  {
    clock_realm_->AdvanceBy(kMixJobPeriod);

    ClockSnapshots clock_snapshots;
    clock_snapshots.AddClock(clock_);
    clock_snapshots.Update(clock_realm_->now());

    MixJobContext ctx(clock_snapshots);
    auto status = consumer_stage->RunMixJob(ctx, clock_realm_->now(), kMixJobPeriod);
    ASSERT_TRUE(std::holds_alternative<ConsumerStage::StartedStatus>(status));

    ASSERT_EQ(consumer_writer_->packets().size(), 1u);
    EXPECT_TRUE(consumer_writer_->packets()[0].is_silence);
    EXPECT_EQ(consumer_writer_->packets()[0].start_frame, 2 * kMixJobFrames);  // second mix job
    EXPECT_EQ(consumer_writer_->packets()[0].length, kMixJobFrames);
  }
}

}  // namespace
}  // namespace media_audio
