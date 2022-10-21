// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/mixer_node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.audio/cpp/common_types.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/fidl/graph_detached_thread.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/mix/gain_control.h"
#include "src/media/audio/services/mixer/mix/mixer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using ::testing::ElementsAre;

const auto kDestFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});

TEST(MixerNodeTest, Create) {
  FakeGraph graph({});

  const auto mixer_node = MixerNode::Create({
      .format = kDestFormat,
      .reference_clock = DefaultClock(),
      .dest_buffer_frame_count = 10,
      .detached_thread = graph.detached_thread(),
  });
  ASSERT_NE(mixer_node, nullptr);
  EXPECT_EQ(mixer_node->type(), Node::Type::kMixer);
  EXPECT_EQ(mixer_node->reference_clock(), DefaultClock());
  EXPECT_EQ(mixer_node->pipeline_stage()->format(), kDestFormat);
  EXPECT_EQ(mixer_node->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());
  EXPECT_EQ(mixer_node->thread(), graph.detached_thread());
  EXPECT_THAT(mixer_node->sources(), ElementsAre());
  EXPECT_EQ(mixer_node->dest(), nullptr);
}

TEST(MixerNodeTest, CreateDeleteEdge) {
  const Format source_format_1 =
      Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 1, 48000});
  const Format source_format_2 = Format::CreateOrDie({fuchsia_audio::SampleType::kInt16, 2, 24000});

  FakeGraph graph({
      .gain_controls =
          {
              GainControlId{10},
              GainControlId{20},
              GainControlId{30},
          },
      .unconnected_ordinary_nodes = {1, 2, 3, 4},
      .formats =
          {
              {&source_format_1, {1}},
              {&source_format_2, {2}},
              {&kDestFormat, {3}},
          },
  });

  auto q = graph.global_task_queue();

  const auto mixer_node = MixerNode::Create({
      .format = kDestFormat,
      .reference_clock = DefaultClock(),
      .dest_buffer_frame_count = 10,
      .detached_thread = graph.detached_thread(),
  });
  ASSERT_NE(mixer_node, nullptr);
  EXPECT_EQ(mixer_node->type(), Node::Type::kMixer);
  EXPECT_EQ(mixer_node->reference_clock(), DefaultClock());
  EXPECT_EQ(mixer_node->pipeline_stage()->format(), kDestFormat);
  EXPECT_EQ(mixer_node->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());
  EXPECT_EQ(mixer_node->thread(), graph.detached_thread());
  EXPECT_THAT(mixer_node->sources(), ElementsAre());
  EXPECT_EQ(mixer_node->dest(), nullptr);

  // Connect graph node `1` to `mixer_node` with gain control `10`.
  ASSERT_TRUE(Node::CreateEdge(graph.gain_controls(), *q, graph.detached_thread(), graph.node(1),
                               mixer_node, {.gain_ids = {GainControlId{10}}})
                  .is_ok());
  EXPECT_THAT(mixer_node->sources(), ElementsAre(graph.node(1)));
  EXPECT_EQ(mixer_node->dest(), nullptr);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{10})->num_mixers(), 1ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{20})->num_mixers(), 0ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{30})->num_mixers(), 0ul);

  // Gain control `10` should be passed to the underlying mixer, i.e., this should not fail.
  q->RunForThread(graph.detached_thread()->id());
  static_cast<MixerStage*>(mixer_node->pipeline_stage().get())
      ->gain_controls()
      .Get(GainControlId{10});

  // Connect graph node `2` to `mixer_node` with gain controls `10` and `20`.
  ASSERT_TRUE(Node::CreateEdge(graph.gain_controls(), *q, graph.detached_thread(), graph.node(2),
                               mixer_node, {.gain_ids = {GainControlId{10}, GainControlId{20}}})
                  .is_ok());
  EXPECT_THAT(mixer_node->sources(), ElementsAre(graph.node(1), graph.node(2)));
  EXPECT_EQ(mixer_node->dest(), nullptr);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{10})->num_mixers(), 1ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{20})->num_mixers(), 1ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{30})->num_mixers(), 0ul);

  // Gain control `20` should be passed to the underlying mixer, i.e., this should not fail.
  q->RunForThread(graph.detached_thread()->id());
  static_cast<MixerStage*>(mixer_node->pipeline_stage().get())
      ->gain_controls()
      .Get(GainControlId{20});

  // Connect `mixer_node` to graph node `3` with gain control `30`.
  ASSERT_TRUE(Node::CreateEdge(graph.gain_controls(), *q, graph.detached_thread(), mixer_node,
                               graph.node(3), {.gain_ids = {GainControlId{30}}})
                  .is_ok());
  EXPECT_THAT(mixer_node->sources(), ElementsAre(graph.node(1), graph.node(2)));
  EXPECT_EQ(mixer_node->dest(), graph.node(3));
  EXPECT_EQ(graph.gain_controls().at(GainControlId{10})->num_mixers(), 1ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{20})->num_mixers(), 1ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{30})->num_mixers(), 1ul);

  // Gain control `30` should be passed to the underlying mixer, i.e., this should not fail.
  q->RunForThread(graph.detached_thread()->id());
  static_cast<MixerStage*>(mixer_node->pipeline_stage().get())
      ->gain_controls()
      .Get(GainControlId{30});

  // Disconnect graph node `1` from `mixer_node`.
  ASSERT_TRUE(Node::DeleteEdge(graph.gain_controls(), *q, graph.detached_thread(), graph.node(1),
                               mixer_node)
                  .is_ok());
  EXPECT_THAT(mixer_node->sources(), ElementsAre(graph.node(2)));
  EXPECT_EQ(mixer_node->dest(), graph.node(3));
  EXPECT_EQ(graph.gain_controls().at(GainControlId{10})->num_mixers(), 1ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{20})->num_mixers(), 1ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{30})->num_mixers(), 1ul);

  q->RunForThread(graph.detached_thread()->id());

  // Disconnect `mixer_node` from graph node `3`.
  ASSERT_TRUE(Node::DeleteEdge(graph.gain_controls(), *q, graph.detached_thread(), mixer_node,
                               graph.node(3))
                  .is_ok());
  EXPECT_THAT(mixer_node->sources(), ElementsAre(graph.node(2)));
  EXPECT_EQ(mixer_node->dest(), nullptr);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{10})->num_mixers(), 1ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{20})->num_mixers(), 1ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{30})->num_mixers(), 0ul);

  q->RunForThread(graph.detached_thread()->id());

  // Finally disconnect graph node `2` from `mixer_node`.
  ASSERT_TRUE(Node::DeleteEdge(graph.gain_controls(), *q, graph.detached_thread(), graph.node(2),
                               mixer_node)
                  .is_ok());
  EXPECT_THAT(mixer_node->sources(), ElementsAre());
  EXPECT_EQ(mixer_node->dest(), nullptr);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{10})->num_mixers(), 0ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{20})->num_mixers(), 0ul);
  EXPECT_EQ(graph.gain_controls().at(GainControlId{30})->num_mixers(), 0ul);
}

TEST(MixerNodeTest, CreateEdgeCannotAcceptSourceFormat) {
  const Format mismatching_format = Format::CreateOrDie({SampleType::kFloat32, 10, 48000});
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1},
      .formats = {{&mismatching_format, {1}}},
  });

  auto q = graph.global_task_queue();

  const auto mixer_node = MixerNode::Create({
      .format = kDestFormat,
      .reference_clock = DefaultClock(),
      .dest_buffer_frame_count = 10,
      .detached_thread = graph.detached_thread(),
  });
  ASSERT_NE(mixer_node, nullptr);
  EXPECT_EQ(mixer_node->type(), Node::Type::kMixer);
  EXPECT_EQ(mixer_node->reference_clock(), DefaultClock());
  EXPECT_EQ(mixer_node->pipeline_stage()->format(), kDestFormat);
  EXPECT_EQ(mixer_node->pipeline_stage()->thread(), graph.detached_thread()->pipeline_thread());
  EXPECT_EQ(mixer_node->thread(), graph.detached_thread());
  EXPECT_THAT(mixer_node->sources(), ElementsAre());
  EXPECT_EQ(mixer_node->dest(), nullptr);

  // Attempt to connect graph node `1`, which should fail since the mixer cannot create a sampler
  // that matches the requested channelization.
  auto result = Node::CreateEdge(graph.gain_controls(), *q, graph.detached_thread(), graph.node(1),
                                 mixer_node,
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
  EXPECT_THAT(mixer_node->sources(), ElementsAre());
  EXPECT_EQ(mixer_node->dest(), nullptr);
}

}  // namespace
}  // namespace media_audio
