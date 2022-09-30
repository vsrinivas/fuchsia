// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/meta_producer_node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/real_clock.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/fidl_realtime/testing/test_stream_sink_server_and_client.h"
#include "src/media/audio/services/mixer/mix/ring_buffer.h"
#include "src/media/audio/services/mixer/mix/ring_buffer_consumer_writer.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using ::testing::ElementsAre;

const auto kFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
const auto kMediaTicksPerNs = TimelineRate(1, 10'000'000);  // 1 tick per 10ms

class MetaProducerNodeTestStreamSink : public ::testing::Test {
 public:
  static constexpr uint32_t kBufferId = 0;
  static constexpr uint64_t kBufferSize = 4096;

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

TEST_F(MetaProducerNodeTestStreamSink, CreateEdgeCannotAcceptSource) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();

  auto producer = MetaProducerNode::Create({
      .format = kFormat,
      .reference_clock = DefaultClock(),
      .data_source = stream_sink().server_ptr(),
      .detached_thread = graph.detached_thread(),
  });

  // Cannot create an edge where a Producer node is the destination.
  auto result = Node::CreateEdge(*q, graph.detached_thread(), graph.node(1), producer);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(MetaProducerNodeTestStreamSink, CreateEdgeSuccess) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();

  const auto clock = RealClock::CreateFromMonotonic("ReferenceClock", Clock::kExternalDomain, true);
  auto producer = MetaProducerNode::Create({
      .format = kFormat,
      .reference_clock = UnreadableClock(clock),
      .data_source = stream_sink().server_ptr(),
      .detached_thread = graph.detached_thread(),
  });

  // Connect producer -> dest.
  auto dest = graph.node(1);
  {
    auto result = Node::CreateEdge(*q, graph.detached_thread(), producer, dest);
    ASSERT_TRUE(result.is_ok());
  }

  ASSERT_EQ(producer->child_sources().size(), 0u);
  ASSERT_EQ(producer->child_dests().size(), 1u);

  auto producer_child = std::static_pointer_cast<FakeNode>(producer->child_dests()[0]);
  EXPECT_EQ(producer_child->thread(), graph.detached_thread());
  EXPECT_EQ(producer_child->dest(), dest);
  EXPECT_EQ(producer_child->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());
  EXPECT_EQ(producer_child->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(producer_child->pipeline_stage()->reference_clock(), clock);
  EXPECT_THAT(dest->sources(), ElementsAre(producer_child));

  q->RunForThread(graph.detached_thread()->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(),
              ElementsAre(producer_child->pipeline_stage()));

  // Wait for until the new CommandQueue has been added to the StreamSinkServer. This happens
  // asynchronously.
  ASSERT_TRUE(stream_sink().WaitUntilNumQueuesIs(1));

  // Start the producer's internal frame timeline.
  producer->Start({
      .start_presentation_time = zx::time(0),
      .start_frame = Fixed(0),
  });

  // Also start the producer's downstream frame timeline. This is normally updated by the Consumer.
  producer_child->pipeline_stage()->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(kFormat));

  // Send a packet with 10 frames.
  {
    TestFence fence;
    ASSERT_NO_FATAL_FAILURE(stream_sink().PutPacket(
        {
            .buffer_id = kBufferId,
            .offset = 0,
            .size = static_cast<uint64_t>(10 * kFormat.bytes_per_frame()),
        },
        fuchsia_media2::wire::PacketTimestamp::WithSpecified(arena_, 0), fence.Take()));
  }

  // Verify that packet was received by the producer stage.
  {
    const auto packet = producer_child->pipeline_stage()->Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_TRUE(packet);
    EXPECT_EQ(0, packet->start());
    EXPECT_EQ(10, packet->length());
    EXPECT_EQ(10, packet->end());
  }

  // Disconnect producer -> dest.
  {
    auto result = Node::DeleteEdge(*q, graph.detached_thread(), producer, dest);
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(producer->child_sources().size(), 0u);
  EXPECT_EQ(producer->child_dests().size(), 0u);
  EXPECT_THAT(dest->sources(), ElementsAre());

  q->RunForThread(graph.detached_thread()->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(), ElementsAre());
}

TEST(MetaProducerNodeTestRingBuffer, CreateEdgeSuccess) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();

  const auto clock = RealClock::CreateFromMonotonic("ReferenceClock", Clock::kExternalDomain, true);
  constexpr int64_t kRingBufferFrames = 10;

  auto buffer =
      MemoryMappedBuffer::CreateOrDie(kRingBufferFrames * kFormat.bytes_per_frame(), true);
  auto ring_buffer = RingBuffer::Create({
      .format = kFormat,
      .reference_clock = UnreadableClock(clock),
      .buffer = buffer,
      .producer_frames = kRingBufferFrames / 2,
      .consumer_frames = kRingBufferFrames / 2,
  });
  auto producer = MetaProducerNode::Create({
      .pipeline_direction = PipelineDirection::kInput,
      .format = kFormat,
      .reference_clock = UnreadableClock(clock),
      .data_source = ring_buffer,
      .detached_thread = graph.detached_thread(),
  });

  // Connect producer -> dest.
  auto dest = graph.node(1);
  {
    auto result = Node::CreateEdge(*q, graph.detached_thread(), producer, dest);
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(producer->pipeline_direction(), PipelineDirection::kInput);
  ASSERT_EQ(producer->child_sources().size(), 0u);
  ASSERT_EQ(producer->child_dests().size(), 1u);

  auto producer_child = std::static_pointer_cast<FakeNode>(producer->child_dests()[0]);
  EXPECT_EQ(producer_child->dest(), dest);
  EXPECT_EQ(producer_child->pipeline_direction(), PipelineDirection::kInput);
  EXPECT_EQ(producer_child->thread(), graph.detached_thread());
  EXPECT_EQ(producer_child->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());
  EXPECT_EQ(producer_child->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(producer_child->pipeline_stage()->reference_clock(), clock);
  EXPECT_THAT(dest->sources(), ElementsAre(producer_child));

  q->RunForThread(graph.detached_thread()->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(),
              ElementsAre(producer_child->pipeline_stage()));

  // Start the producer's internal frame timeline.
  producer->Start({
      .start_presentation_time = zx::time(0),
      .start_frame = Fixed(0),
  });

  // Also start the producer's downstream frame timeline. This is normally updated by the Consumer.
  producer_child->pipeline_stage()->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(kFormat));

  // Write to the ring buffer.
  std::vector<float> payload(kFormat.channels() * kRingBufferFrames, 0.25f);
  RingBufferConsumerWriter writer(ring_buffer);
  writer.WriteData(0, 5, payload.data());

  // Verify that packet was received by the producer stage.
  {
    const auto packet = producer_child->pipeline_stage()->Read(DefaultCtx(), Fixed(0), 5);
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

  EXPECT_EQ(producer->child_sources().size(), 0u);
  EXPECT_EQ(producer->child_dests().size(), 0u);
  EXPECT_THAT(dest->sources(), ElementsAre());

  q->RunForThread(graph.detached_thread()->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(), ElementsAre());
}

}  // namespace
}  // namespace media_audio
