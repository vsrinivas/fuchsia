// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/producer_node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/real_clock.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

const Format kFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});

TEST(ProducerNodeTest, CreateEdgeCannotAcceptSource) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();

  auto producer = ProducerNode::Create({
      .start_stop_command_queue = std::make_shared<ProducerStage::CommandQueue>(),
      .internal_source =
          std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
              .format = kFormat,
              .reference_clock = DefaultClock(),
              .command_queue = std::make_shared<SimplePacketQueueProducerStage::CommandQueue>(),
          }),
      .detached_thread = graph.detached_thread(),
  });

  EXPECT_EQ(producer->thread(), graph.detached_thread());
  EXPECT_EQ(producer->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());

  auto result = Node::CreateEdge(*q, graph.detached_thread(), graph.node(1), producer);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST(ProducerNodeTest, CreateEdgeSuccess) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();

  const auto clock = RealClock::CreateFromMonotonic("ReferenceClock", Clock::kExternalDomain, true);
  auto start_stop_command_queue = std::make_shared<ProducerStage::CommandQueue>();
  auto packet_command_queue = std::make_shared<SimplePacketQueueProducerStage::CommandQueue>();

  auto producer = ProducerNode::Create({
      .pipeline_direction = PipelineDirection::kInput,
      .start_stop_command_queue = start_stop_command_queue,
      .internal_source =
          std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
              .format = kFormat,
              .reference_clock = UnreadableClock(clock),
              .command_queue = packet_command_queue,
          }),
      .detached_thread = graph.detached_thread(),
  });

  EXPECT_EQ(producer->pipeline_direction(), PipelineDirection::kInput);
  EXPECT_EQ(producer->reference_clock(), clock);
  EXPECT_EQ(producer->thread(), graph.detached_thread());
  EXPECT_EQ(producer->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());
  EXPECT_EQ(producer->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(producer->pipeline_stage()->reference_clock(), clock);

  // Connect producer -> dest.
  auto dest = graph.node(1);
  {
    auto result = Node::CreateEdge(*q, graph.detached_thread(), producer, dest);
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(producer->dest(), dest);
  EXPECT_THAT(dest->sources(), ::testing::ElementsAre(producer));

  q->RunForThread(graph.detached_thread()->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(),
              ::testing::ElementsAre(producer->pipeline_stage()));

  // Send a Start command.
  // This starts the producer's internal frame timeline.
  start_stop_command_queue->push(ProducerStage::StartCommand{
      .start_presentation_time = zx::time(0),
      .start_frame = Fixed(0),
  });

  // Also start the producer's downstream frame timeline.
  producer->pipeline_stage()->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(kFormat));

  // Send a PushPacket command.
  std::vector<float> payload(10, 0.0f);
  packet_command_queue->push(SimplePacketQueueProducerStage::PushPacketCommand{
      .packet = PacketView({
          .format = kFormat,
          .start = Fixed(0),
          .length = 10,
          .payload = payload.data(),
      }),
  });

  // Verify those commands were received by the ProducerStage.
  {
    const auto packet = producer->pipeline_stage()->Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_TRUE(packet);
    EXPECT_EQ(0, packet->start());
    EXPECT_EQ(10, packet->length());
    EXPECT_EQ(10, packet->end());
    EXPECT_EQ(payload.data(), packet->payload());
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

}  // namespace
}  // namespace media_audio
