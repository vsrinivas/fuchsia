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
using ::fuchsia_audio_effects::wire::InputConfiguration;
using ::fuchsia_audio_effects::wire::OutputConfiguration;
using ::fuchsia_audio_effects::wire::ProcessorConfiguration;
using ::fuchsia_mediastreams::wire::AudioFormat;
using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::fuchsia_mem::wire::Range;
using ::testing::ElementsAre;

constexpr uint64_t kDefaultBufferSize = 100;
constexpr uint32_t kFrameRate = 10;
const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 1, kFrameRate});

Range MakeBuffer(uint64_t size = kDefaultBufferSize, uint64_t offset = 0) {
  zx::vmo vmo;
  FX_CHECK(zx::vmo::create(size, 0, &vmo) == ZX_OK);
  return {.vmo = std::move(vmo), .offset = offset, .size = size};
}

class CustomNodeTest : public ::testing::Test {
 protected:
  auto MakeInputConfigBuilder() { return InputConfiguration::Builder(arena_); }

  auto MakeOutputConfigBuilder() { return OutputConfiguration::Builder(arena_); }

  auto MakeInputs(InputConfiguration config, int count = 1) {
    fidl::VectorView<InputConfiguration> inputs(arena_, count);
    for (int i = 0; i < count; ++i) {
      inputs.at(i) = config;
    }
    return fidl::ObjectView{arena_, inputs};
  }

  auto MakeOutputs(OutputConfiguration config, int count = 1) {
    fidl::VectorView<OutputConfiguration> outputs(arena_, count);
    for (int i = 0; i < count; ++i) {
      outputs.at(i) = config;
    }
    return fidl::ObjectView{arena_, outputs};
  }

  auto MakeProcessorConfigBuilder(uint64_t block_size_frames = 1, uint64_t latency_frames = 0) {
    auto builder = ProcessorConfiguration::Builder(arena_);

    builder.block_size_frames(block_size_frames);
    builder.max_frames_per_call(block_size_frames);

    builder.inputs(MakeInputs(
        MakeInputConfigBuilder().buffer(MakeBuffer()).format(kFormat.ToLegacyFidl()).Build()));
    builder.outputs(MakeOutputs(MakeOutputConfigBuilder()
                                    .buffer(MakeBuffer())
                                    .format(kFormat.ToLegacyFidl())
                                    .latency_frames(latency_frames)
                                    .ring_out_frames(0)
                                    .Build()));

    auto endpoints = fidl::CreateEndpoints<Processor>();
    FX_CHECK(endpoints.is_ok());
    builder.processor(std::move(endpoints->client));

    return builder;
  }

 private:
  fidl::Arena<100> arena_;
};

TEST_F(CustomNodeTest, CreateDeleteEdge) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2, 3},
      .formats = {{&kFormat, {1, 2, 3}}},
  });

  const auto& ctx = graph.ctx();
  auto q = graph.global_task_queue();

  const auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder(/*block_size_frames=*/6, /*latency_frames=*/10).Build(),
      .detached_thread = ctx.detached_thread,
  });
  ASSERT_NE(custom_node, nullptr);
  EXPECT_EQ(custom_node->type(), Node::Type::kMeta);
  EXPECT_EQ(custom_node->reference_clock(), DefaultClock());
  ASSERT_EQ(custom_node->child_sources().size(), 1ul);
  ASSERT_EQ(custom_node->child_dests().size(), 1ul);

  const auto& child_source_node = custom_node->child_sources().front();
  EXPECT_EQ(child_source_node->type(), Node::Type::kCustom);
  // Presentation delay of child source should be set to `10 + 6 - 1 = 15` frames at `kFrameRate`.
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(/*source=*/nullptr),
            zx::nsec(1'500'000'000));
  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);
  EXPECT_EQ(child_source_node->reference_clock(), DefaultClock());
  EXPECT_EQ(child_source_node->thread(), ctx.detached_thread);
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);

  const auto& child_dest_node = custom_node->child_dests().front();
  EXPECT_EQ(child_dest_node->type(), Node::Type::kCustom);
  // Presentation delay of child destination should be set to zero.
  EXPECT_EQ(child_dest_node->GetSelfPresentationDelayForSource(/*source=*/nullptr), zx::nsec(0));
  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), nullptr);
  EXPECT_EQ(child_dest_node->reference_clock(), DefaultClock());
  EXPECT_EQ(child_dest_node->thread(), ctx.detached_thread);
  EXPECT_EQ(child_dest_node->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(child_dest_node->pipeline_stage()->format(), kFormat);

  // Connect graph node `1` to `child_source_node`.
  ASSERT_TRUE(Node::CreateEdge(ctx, graph.node(1), child_source_node, /*options=*/{}).is_ok());
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(graph.node(1).get()),
            zx::nsec(1'500'000'000));
  EXPECT_THAT(child_source_node->sources(), ElementsAre(graph.node(1)));
  EXPECT_EQ(child_source_node->dest(), nullptr);
  EXPECT_EQ(child_source_node->thread(), ctx.detached_thread);
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_source_node->pipeline_stage()->reference_clock(), DefaultClock());

  // Attempt to connect graph node `2` to `child_source_node`, which should get rejected since
  // `child_source_node` can only have a single source.
  auto result = Node::CreateEdge(ctx, graph.node(2), child_source_node, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
  EXPECT_THAT(child_source_node->sources(), ElementsAre(graph.node(1)));
  EXPECT_EQ(child_source_node->dest(), nullptr);

  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), nullptr);

  // Connect `child_dest_node` to graph node `3`.
  ASSERT_TRUE(Node::CreateEdge(ctx, child_dest_node, graph.node(3), /*options=*/{}).is_ok());
  EXPECT_EQ(child_dest_node->GetSelfPresentationDelayForSource(/*source=*/nullptr), zx::nsec(0));
  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), graph.node(3));
  EXPECT_EQ(child_dest_node->thread(), ctx.detached_thread);
  EXPECT_EQ(child_dest_node->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(child_dest_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_dest_node->pipeline_stage()->reference_clock(), DefaultClock());

  // Attempt to connect `child_dest_node` to graph node `2`, which should get rejected since
  // `child_dest_node` can only have a single destination.
  result = Node::CreateEdge(ctx, child_dest_node, graph.node(2), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), graph.node(3));

  EXPECT_THAT(child_source_node->sources(), ElementsAre(graph.node(1)));
  EXPECT_EQ(child_source_node->dest(), nullptr);

  // Disconnect graph node `1` from `child_source_node`.
  ASSERT_TRUE(Node::DeleteEdge(ctx, graph.node(1), child_source_node).is_ok());
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(/*source=*/nullptr),
            zx::nsec(1'500'000'000));
  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);
  EXPECT_EQ(child_source_node->thread(), ctx.detached_thread);
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_source_node->pipeline_stage()->reference_clock(), DefaultClock());

  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), graph.node(3));

  // Disconnect `child_dest_node` from graph node `3`.
  ASSERT_TRUE(Node::DeleteEdge(ctx, child_dest_node, graph.node(3)).is_ok());
  EXPECT_EQ(child_dest_node->GetSelfPresentationDelayForSource(/*source=*/nullptr), zx::nsec(0));
  EXPECT_THAT(child_dest_node->sources(), ElementsAre());
  EXPECT_EQ(child_dest_node->dest(), nullptr);
  EXPECT_EQ(child_dest_node->thread(), ctx.detached_thread);
  EXPECT_EQ(child_dest_node->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(child_dest_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_dest_node->pipeline_stage()->reference_clock(), DefaultClock());

  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);

  // Clear all child nodes referring to `custom_node` to ensure that the parent will be destroyed.
  Node::Destroy(ctx, custom_node);
  EXPECT_TRUE(custom_node->child_sources().empty());
  EXPECT_TRUE(custom_node->child_dests().empty());
}

TEST_F(CustomNodeTest, CreateEdgeCannotAcceptSourceFormat) {
  const Format mismatching_format = Format::CreateOrDie({SampleType::kFloat32, 1, kFrameRate * 2});
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&mismatching_format, {1}}},
  });

  const auto& ctx = graph.ctx();
  auto q = graph.global_task_queue();

  const auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder(/*block_size_frames=*/1, /*latency_frames=*/5).Build(),
      .detached_thread = ctx.detached_thread,
  });
  ASSERT_NE(custom_node, nullptr);
  EXPECT_EQ(custom_node->reference_clock(), DefaultClock());
  ASSERT_EQ(custom_node->child_sources().size(), 1ul);
  ASSERT_EQ(custom_node->child_dests().size(), 1ul);

  const auto& child_source_node = custom_node->child_sources().front();
  // Presentation delay of child source should be set to `5 + 1 - 1 = 5` frames at `kFrameRate`.
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(/*source=*/nullptr),
            zx::nsec(500'000'000));
  EXPECT_EQ(child_source_node->thread(), ctx.detached_thread);
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_source_node->pipeline_stage()->reference_clock(), DefaultClock());
  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);

  // Attempt to connect graph node `1` to `child_source_node`, which should get rejected due to the
  // mismatching source format of graph node `1`.
  auto result = Node::CreateEdge(ctx, graph.node(1), child_source_node,
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
  EXPECT_EQ(child_source_node->GetSelfPresentationDelayForSource(/*source=*/nullptr),
            zx::nsec(500'000'000));
  EXPECT_EQ(child_source_node->thread(), ctx.detached_thread);
  EXPECT_EQ(child_source_node->pipeline_stage()->thread(), ctx.detached_thread->pipeline_thread());
  EXPECT_EQ(child_source_node->pipeline_stage()->format(), kFormat);
  EXPECT_EQ(child_source_node->pipeline_stage()->reference_clock(), DefaultClock());
  EXPECT_THAT(child_source_node->sources(), ElementsAre());
  EXPECT_EQ(child_source_node->dest(), nullptr);

  // Clear all child nodes referring to `custom_node` to ensure that the parent will be destroyed.
  Node::Destroy(ctx, custom_node);
  EXPECT_TRUE(custom_node->child_sources().empty());
  EXPECT_TRUE(custom_node->child_dests().empty());
}

TEST_F(CustomNodeTest, CreateEdgeDisallowed) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&kFormat, {1}}},
  });

  const auto& ctx = graph.ctx();
  auto q = graph.global_task_queue();

  const auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder().Build(),
      .detached_thread = ctx.detached_thread,
  });
  ASSERT_NE(custom_node, nullptr);
  EXPECT_EQ(custom_node->reference_clock(), DefaultClock());
  ASSERT_EQ(custom_node->child_sources().size(), 1ul);
  ASSERT_EQ(custom_node->child_dests().size(), 1ul);

  // Adding a source to `custom_node` is not allowed.
  auto result = Node::CreateEdge(ctx, graph.node(1), custom_node, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);

  // Adding a source to child destination node is not allowed.
  result = Node::CreateEdge(ctx, graph.node(1), custom_node->child_dests().front(), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);

  // Adding a destination to `custom_node` is not allowed.
  result = Node::CreateEdge(ctx, custom_node, graph.node(1), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);

  // Adding a destination to child source node is not allowed.
  result =
      Node::CreateEdge(ctx, custom_node->child_sources().front(), graph.node(1), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);

  // Clear all child nodes referring to `custom_node` to ensure that the parent will be destroyed.
  Node::Destroy(ctx, custom_node);
  EXPECT_TRUE(custom_node->child_sources().empty());
  EXPECT_TRUE(custom_node->child_dests().empty());
}

TEST_F(CustomNodeTest, CreateFailsMissingConfig) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      // no .config
  });
  EXPECT_EQ(custom_node, nullptr);
}

TEST_F(CustomNodeTest, CreateFailsMissingProcessor) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder().processor({}).Build(),
  });
  EXPECT_EQ(custom_node, nullptr);
}

TEST_F(CustomNodeTest, CreateFailsMissingInputs) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder().inputs({}).Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsMissingOutputs) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder().outputs({}).Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsTooManyInputs) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(MakeInputConfigBuilder().Build(), /*count=*/2))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsTooManyOutputs) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .outputs(MakeOutputs(MakeOutputConfigBuilder().Build(), /*count=*/2))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsMissingInputFormat) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(MakeInputConfigBuilder().buffer(MakeBuffer()).Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsMissingOutputFormat) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .outputs(MakeOutputs(MakeOutputConfigBuilder().buffer(MakeBuffer()).Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsMismatchingFrameRate) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(
                        MakeInputConfigBuilder()
                            .buffer(MakeBuffer())
                            .format(AudioFormat{AudioSampleFormat::kFloat, 1, kFrameRate * 2})
                            .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsMissingInputBuffer) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config =
          MakeProcessorConfigBuilder()
              .inputs(MakeInputs(MakeInputConfigBuilder().format(kFormat.ToLegacyFidl()).Build()))
              .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsMissingOutputBuffer) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .outputs(MakeOutputs(
                        MakeOutputConfigBuilder().format(kFormat.ToLegacyFidl()).Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsEmptyInputBuffer) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(MakeInputConfigBuilder()
                                           .buffer(MakeBuffer(/*size=*/0))
                                           .format(kFormat.ToLegacyFidl())
                                           .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsEmptyOutputBuffer) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .outputs(MakeOutputs(MakeOutputConfigBuilder()
                                             .buffer(MakeBuffer(/*size=*/0))
                                             .format(kFormat.ToLegacyFidl())
                                             .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidInputBuffer) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(MakeInputConfigBuilder()
                                           .buffer(Range{.size = 100})
                                           .format(kFormat.ToLegacyFidl())
                                           .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidInputBufferNotMappable) {
  auto buffer = MakeBuffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_WRITE, &buffer.vmo));
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(MakeInputConfigBuilder()
                                           .buffer(std::move(buffer))
                                           .format(kFormat.ToLegacyFidl())
                                           .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidInputBufferNotWritable) {
  auto buffer = MakeBuffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_MAP, &buffer.vmo));
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(MakeInputConfigBuilder()
                                           .buffer(std::move(buffer))
                                           .format(kFormat.ToLegacyFidl())
                                           .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidInputBufferSizeTooSmall) {
  auto buffer = MakeBuffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.size = vmo_size + 1;
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(MakeInputConfigBuilder()
                                           .buffer(std::move(buffer))
                                           .format(kFormat.ToLegacyFidl())
                                           .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidInputBufferOffsetTooLarge) {
  auto buffer = MakeBuffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.offset = vmo_size - buffer.size + 1;
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(MakeInputConfigBuilder()
                                           .buffer(std::move(buffer))
                                           .format(kFormat.ToLegacyFidl())
                                           .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidOutputBuffer) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .outputs(MakeOutputs(MakeOutputConfigBuilder()
                                             .buffer(Range{.size = 100})
                                             .format(kFormat.ToLegacyFidl())
                                             .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidOutputBufferNotMappable) {
  auto buffer = MakeBuffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_WRITE, &buffer.vmo));
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .outputs(MakeOutputs(MakeOutputConfigBuilder()
                                             .buffer(std::move(buffer))
                                             .format(kFormat.ToLegacyFidl())
                                             .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidOutputBufferNotReadable) {
  auto buffer = MakeBuffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_MAP, &buffer.vmo));
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .outputs(MakeOutputs(MakeOutputConfigBuilder()
                                             .buffer(std::move(buffer))
                                             .format(kFormat.ToLegacyFidl())
                                             .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidOutputBufferSizeTooSmall) {
  auto buffer = MakeBuffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.size = vmo_size + 1;
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .outputs(MakeOutputs(MakeOutputConfigBuilder()
                                             .buffer(std::move(buffer))
                                             .format(kFormat.ToLegacyFidl())
                                             .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsInvalidOutputBufferOffsetTooLarge) {
  auto buffer = MakeBuffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.offset = vmo_size - buffer.size + 1;
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .outputs(MakeOutputs(MakeOutputConfigBuilder()
                                             .buffer(std::move(buffer))
                                             .format(kFormat.ToLegacyFidl())
                                             .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsOutputBufferPartiallyOverlapsInputBuffer) {
  auto input_buffer = MakeBuffer(1024);
  zx::vmo output_vmo;
  ASSERT_EQ(input_buffer.vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &output_vmo), ZX_OK);
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder()
                    .inputs(MakeInputs(MakeInputConfigBuilder()
                                           .buffer(std::move(input_buffer))
                                           .format(kFormat.ToLegacyFidl())
                                           .Build()))
                    .outputs(MakeOutputs(
                        MakeOutputConfigBuilder()
                            .buffer(Range{.vmo = std::move(output_vmo), .offset = 255, .size = 256})
                            .format(kFormat.ToLegacyFidl())
                            .Build()))
                    .Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsBlockSizeTooBig) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder().block_size_frames(kDefaultBufferSize + 1).Build(),
  });
}

TEST_F(CustomNodeTest, CreateFailsMaxFramesPerCallTooBig) {
  auto custom_node = CustomNode::Create({
      .reference_clock = DefaultClock(),
      .config = MakeProcessorConfigBuilder().max_frames_per_call(kDefaultBufferSize + 1).Build(),
  });
}

}  // namespace
}  // namespace media_audio
