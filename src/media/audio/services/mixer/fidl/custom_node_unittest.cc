// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/custom_node.h"

#include <lib/fidl/cpp/wire/arena.h>
#include <lib/zx/vmo.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.audio.effects/cpp/markers.h"
#include "fidl/fuchsia.audio.effects/cpp/wire_types.h"
#include "fidl/fuchsia.audio/cpp/common_types.h"
#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "fidl/fuchsia.mem/cpp/wire_types.h"
#include "lib/fidl/cpp/wire/object_view.h"
#include "lib/fidl/cpp/wire/vector_view.h"
#include "src/media/audio/services/mixer/common/global_task_queue.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using ::fuchsia_audio_effects::Processor;
using ::fuchsia_audio_effects::wire::ProcessorConfiguration;
using ::fuchsia_mediastreams::wire::AudioFormat;
using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::ElementsAre;

constexpr uint32_t kFrameRate = 10;
const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 1, kFrameRate});

class CustomNodeTest : public ::testing::Test {
 protected:
  ProcessorConfiguration MakeTestConfig(uint64_t block_size_frames, uint64_t latency_frames) {
    auto builder = ProcessorConfiguration::Builder(arena_);

    builder.block_size_frames(block_size_frames);
    builder.max_frames_per_call(block_size_frames);

    const AudioFormat format = {AudioSampleFormat::kFloat, 1, kFrameRate};

    zx::vmo input_vmo;
    FX_CHECK(zx::vmo::create(100, 0, &input_vmo) == ZX_OK);
    fuchsia_mem::wire::Range input_buffer = {.vmo = std::move(input_vmo), .offset = 0, .size = 100};

    fidl::VectorView<fuchsia_audio_effects::wire::InputConfiguration> inputs(arena_, 1);
    inputs.at(0) = fuchsia_audio_effects::wire::InputConfiguration::Builder(arena_)
                       .buffer(std::move(input_buffer))
                       .format(format)
                       .Build();
    builder.inputs(fidl::ObjectView{arena_, inputs});

    zx::vmo output_vmo;
    FX_CHECK(zx::vmo::create(100, 0, &output_vmo) == ZX_OK);
    fuchsia_mem::wire::Range output_buffer = {
        .vmo = std::move(output_vmo), .offset = 0, .size = 100};

    fidl::VectorView<fuchsia_audio_effects::wire::OutputConfiguration> outputs(arena_, 1);
    outputs.at(0) = fuchsia_audio_effects::wire::OutputConfiguration::Builder(arena_)
                        .buffer(std::move(output_buffer))
                        .format(format)
                        .latency_frames(latency_frames)
                        .ring_out_frames(0)
                        .Build();
    builder.outputs(fidl::ObjectView{arena_, outputs});

    auto endpoints = fidl::CreateEndpoints<Processor>();
    FX_CHECK(endpoints.is_ok());
    builder.processor(std::move(endpoints->client));

    return builder.Build();
  }

  const DetachedThreadPtr& detached_thread() const { return detached_thread_; }

 private:
  const DetachedThreadPtr detached_thread_ = DetachedThread::Create();
  fidl::Arena<100> arena_;
};

TEST_F(CustomNodeTest, CreateDeleteEdge) {
  const auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeTestConfig(/*block_size_frames=*/6, /*latency_frames=*/10),
      .detached_thread = detached_thread(),
  });
  EXPECT_EQ(custom_node->reference_clock(), DefaultClock());
  ASSERT_EQ(custom_node->child_sources().size(), 1ul);
  ASSERT_EQ(custom_node->child_dests().size(), 1ul);

  const auto& child_source_node = custom_node->child_sources().front();
  // Presentation delay of child source should be set to `10 + 6 - 1 = 15` frames at `kFrameRate`.
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(/*source=*/nullptr),
            zx::nsec(1'500'000'000));
  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);
  EXPECT_EQ(child_source_node->reference_clock(), DefaultClock());
  EXPECT_EQ(child_source_node->pipeline_stage_thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);

  const auto& child_dest_node = custom_node->child_dests().front();
  // Presentation delay of child destination should be set to zero.
  EXPECT_EQ(child_dest_node->GetSelfPresentationDelayForSource(/*source=*/nullptr), zx::nsec(0));
  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), nullptr);
  EXPECT_EQ(child_dest_node->reference_clock(), DefaultClock());
  EXPECT_EQ(child_dest_node->pipeline_stage_thread(), detached_thread());
  EXPECT_EQ(child_dest_node->pipeline_stage()->thread(), detached_thread());
  EXPECT_EQ(child_dest_node->pipeline_stage()->format(), kFormat);

  GlobalTaskQueue q;
  const FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2, 3},
      .formats = {{&kFormat, {1, 2, 3}}},
      .default_thread = detached_thread(),
  });

  // Connect graph node `1` to `child_source_node`.
  ASSERT_TRUE(Node::CreateEdge(q, graph.node(1), child_source_node).is_ok());
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(graph.node(1)),
            zx::nsec(1'500'000'000));
  EXPECT_THAT(child_source_node->sources(), ElementsAre(graph.node(1)));
  EXPECT_EQ(child_source_node->dest(), nullptr);
  EXPECT_EQ(child_source_node->pipeline_stage_thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_source_node->pipeline_stage()->reference_clock(), DefaultClock());

  // Attempt to connect graph node `2` to `child_source_node`, which should get rejected since
  // `child_source_node` can only have a single source.
  auto result = Node::CreateEdge(q, graph.node(2), child_source_node);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
  EXPECT_THAT(child_source_node->sources(), ElementsAre(graph.node(1)));
  EXPECT_EQ(child_source_node->dest(), nullptr);

  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), nullptr);

  // Connect `child_dest_node` to graph node `3`.
  ASSERT_TRUE(Node::CreateEdge(q, child_dest_node, graph.node(3)).is_ok());
  EXPECT_EQ(child_dest_node->GetSelfPresentationDelayForSource(/*source=*/nullptr), zx::nsec(0));
  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), graph.node(3));
  EXPECT_EQ(child_dest_node->pipeline_stage_thread(), detached_thread());
  EXPECT_EQ(child_dest_node->pipeline_stage()->thread(), detached_thread());
  EXPECT_EQ(child_dest_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_dest_node->pipeline_stage()->reference_clock(), DefaultClock());

  // Attempt to connect `child_dest_node` to graph node `2`, which should get rejected since
  // `child_dest_node` can only have a single destination.
  result = Node::CreateEdge(q, child_dest_node, graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), graph.node(3));

  EXPECT_THAT(child_source_node->sources(), ElementsAre(graph.node(1)));
  EXPECT_EQ(child_source_node->dest(), nullptr);

  // Disconnect graph node `1` from `child_source_node`.
  ASSERT_TRUE(Node::DeleteEdge(q, graph.node(1), child_source_node, detached_thread()).is_ok());
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(/*source=*/nullptr),
            zx::nsec(1'500'000'000));
  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);
  EXPECT_EQ(child_source_node->pipeline_stage_thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_source_node->pipeline_stage()->reference_clock(), DefaultClock());

  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), graph.node(3));

  // Disconnect `child_dest_node` from graph node `3`.
  ASSERT_TRUE(Node::DeleteEdge(q, child_dest_node, graph.node(3), detached_thread()).is_ok());
  EXPECT_EQ(child_dest_node->GetSelfPresentationDelayForSource(/*source=*/nullptr), zx::nsec(0));
  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), nullptr);
  EXPECT_EQ(child_dest_node->pipeline_stage_thread(), detached_thread());
  EXPECT_EQ(child_dest_node->pipeline_stage()->thread(), detached_thread());
  EXPECT_EQ(child_dest_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_dest_node->pipeline_stage()->reference_clock(), DefaultClock());

  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);

  // Clear all child nodes referring to `custom_node` to ensure that the parent will be destroyed.
  custom_node->PrepareToDestroy();
  EXPECT_TRUE(custom_node->child_sources().empty());
  EXPECT_TRUE(custom_node->child_dests().empty());
}

TEST_F(CustomNodeTest, CreateEdgeCannotAcceptSourceFormat) {
  const auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeTestConfig(/*block_size_frames=*/1, /*latency_frames=*/5),
      .detached_thread = detached_thread(),
  });
  EXPECT_EQ(custom_node->reference_clock(), DefaultClock());
  ASSERT_EQ(custom_node->child_sources().size(), 1ul);
  ASSERT_EQ(custom_node->child_dests().size(), 1ul);

  const auto& child_source_node = custom_node->child_sources().front();
  // Presentation delay of child source should be set to `5 + 1 - 1 = 5` frames at `kFrameRate`.
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(/*source=*/nullptr),
            zx::nsec(500'000'000));
  EXPECT_EQ(child_source_node->pipeline_stage_thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_source_node->pipeline_stage()->reference_clock(), DefaultClock());
  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);

  const Format mismatching_format = Format::CreateOrDie({SampleType::kFloat32, 1, kFrameRate * 2});
  GlobalTaskQueue q;
  const FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&mismatching_format, {1}}},
      .default_thread = detached_thread(),
  });

  // Attempt to connect graph node `1` to `child_source_node`, which should get rejected due to the
  // mismatching source format of graph node `1`.
  auto result = Node::CreateEdge(q, graph.node(1), child_source_node);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(/*source=*/nullptr),
            zx::nsec(500'000'000));
  EXPECT_EQ(child_source_node->pipeline_stage_thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), detached_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_source_node->pipeline_stage()->reference_clock(), DefaultClock());
  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);

  // Clear all child nodes referring to `custom_node` to ensure that the parent will be destroyed.
  custom_node->PrepareToDestroy();
  EXPECT_TRUE(custom_node->child_sources().empty());
  EXPECT_TRUE(custom_node->child_dests().empty());
}

TEST_F(CustomNodeTest, CreateEdgeDisallowed) {
  const auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeTestConfig(/*block_size_frames=*/10, /*latency_frames=*/10),
      .detached_thread = detached_thread(),
  });
  EXPECT_EQ(custom_node->reference_clock(), DefaultClock());
  ASSERT_EQ(custom_node->child_sources().size(), 1ul);
  ASSERT_EQ(custom_node->child_dests().size(), 1ul);

  GlobalTaskQueue q;
  const FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&kFormat, {1}}},
      .default_thread = detached_thread(),
  });

  // Adding a source to `custom_node` is not allowed.
  auto result = Node::CreateEdge(q, graph.node(1), custom_node);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);

  // Adding a source to child destination node is not allowed.
  result = Node::CreateEdge(q, graph.node(1), custom_node->child_dests().front());
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);

  // Adding a destination to `custom_node` is not allowed.
  result = Node::CreateEdge(q, custom_node, graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);

  // Adding a destination to child source node is not allowed.
  result = Node::CreateEdge(q, custom_node->child_sources().front(), graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);

  // Clear all child nodes referring to `custom_node` to ensure that the parent will be destroyed.
  custom_node->PrepareToDestroy();
  EXPECT_TRUE(custom_node->child_sources().empty());
  EXPECT_TRUE(custom_node->child_dests().empty());
}

}  // namespace
}  // namespace media_audio
