// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/stream_sink_producer_node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/fidl_realtime/testing/test_stream_sink_server_and_client.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using ::testing::ElementsAre;

const auto kFormat =
    Format::CreateOrDie({fuchsia_mediastreams::wire::AudioSampleFormat::kFloat, 2, 48000});
const auto kMediaTicksPerNs = TimelineRate(1, 10'000'000);  // 1 tick per 10ms
constexpr uint32_t kBufferId = 0;
constexpr uint64_t kBufferSize = 4096;

class StreamSinkProducerNodeTest : public ::testing::Test {
 public:
  void SetUp() {
    stream_sink_ = std::make_unique<TestStreamSinkServerAndClient>(thread_, kBufferId, kBufferSize,
                                                                   kFormat, kMediaTicksPerNs);
  }

  TestStreamSinkServerAndClient& stream_sink() { return *stream_sink_; }

 protected:
  fidl::Arena<> arena_;
  const DetachedThreadPtr detached_thread_ = DetachedThread::Create();

 private:
  std::shared_ptr<FidlThread> thread_ = FidlThread::CreateFromNewThread("test_fidl_thread");
  std::unique_ptr<TestStreamSinkServerAndClient> stream_sink_;
};

TEST_F(StreamSinkProducerNodeTest, CreateEdgeCannotAcceptSource) {
  auto producer = StreamSinkProducerNode::Create({
      .reference_clock_koid = 0,
      .stream_sink_server = stream_sink().server_ptr(),
      .detached_thread = detached_thread_,
  });

  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .default_thread = detached_thread_,
  });

  // Cannot create an edge where a Producer node is the destination.
  auto result = Node::CreateEdge(q, graph.node(1), producer);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(StreamSinkProducerNodeTest, CreateEdgeSuccess) {
  constexpr zx_koid_t kReferenceClockKoid = 42;
  auto producer = StreamSinkProducerNode::Create({
      .reference_clock_koid = kReferenceClockKoid,
      .stream_sink_server = stream_sink().server_ptr(),
      .detached_thread = detached_thread_,
  });

  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .default_thread = detached_thread_,
  });

  // Connect producer -> dest.
  auto dest = graph.node(1);
  {
    auto result = Node::CreateEdge(q, producer, dest);
    ASSERT_TRUE(result.is_ok());
  }

  ASSERT_EQ(producer->child_sources().size(), 0u);
  ASSERT_EQ(producer->child_dests().size(), 1u);

  auto producer_child = std::static_pointer_cast<FakeNode>(producer->child_dests()[0]);
  EXPECT_EQ(producer_child->pipeline_stage_thread(), detached_thread_);
  EXPECT_EQ(producer_child->dest(), dest);
  EXPECT_EQ(producer_child->pipeline_stage()->thread(), detached_thread_);
  EXPECT_EQ(producer_child->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(producer_child->pipeline_stage()->reference_clock_koid(), kReferenceClockKoid);
  EXPECT_THAT(dest->sources(), ElementsAre(producer_child));

  q.RunForThread(detached_thread_->id());
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
    auto result = Node::DeleteEdge(q, producer, dest, detached_thread_);
    ASSERT_TRUE(result.is_ok());
  }

  EXPECT_EQ(producer->child_sources().size(), 0u);
  EXPECT_EQ(producer->child_dests().size(), 0u);
  EXPECT_THAT(dest->sources(), ElementsAre());

  q.RunForThread(detached_thread_->id());
  EXPECT_THAT(dest->fake_pipeline_stage()->sources(), ElementsAre());
}

}  // namespace
}  // namespace media_audio
