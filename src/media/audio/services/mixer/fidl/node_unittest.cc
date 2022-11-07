// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

//
// CreateEdge
//
// We test the following kinds of edges:
// - (ordinary -> ordinary)
// - (ordinary -> meta)
// - (meta -> ordinary)
// - (meta -> meta)
//
// In these scenarios:
// - (error) source already connected to the same node node (if `source->type() != Type::kMeta`)
// - (error) source already connected to a different node (if `source->type() != Type::kMeta`)
// - (error) source has too many dest edges (if `source->type() == Type::kMeta`)
// - (error) dest has too many source edges
// - (error) dest doesn't accept source's format
// - (error) dest is an output pipeline, source is an input pipeline
// - (error) would create a cycle
// - success
//
// In the "success" scenarios, we verify that the nodes are properly connected and that the source
// PipelineStage is assigned to the same thread as destination PipelineStage (which is assigned to
// fake_thread_).
//

class NodeCreateEdgeTest : public ::testing::Test {
 protected:
  static inline ThreadId kThreadId = 1;

  void CheckPipelineStagesAfterCreate(FakeGraph& graph, FakePipelineStagePtr source,
                                      FakePipelineStagePtr dest) {
    auto q = graph.global_task_queue();

    auto detached_thread = graph.ctx().detached_thread->pipeline_thread();
    auto mix_thread = graph.thread(kThreadId)->pipeline_thread();

    // The PipelineStages are updated asynchronously by kThreadId.
    // Initially, they are not connected.
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_EQ(source->thread(), detached_thread);
    EXPECT_EQ(dest->thread(), mix_thread);

    // Still not connected because kThreadId hasn't run yet.
    q->RunForThread(detached_thread->id());
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_EQ(source->thread(), detached_thread);
    EXPECT_EQ(dest->thread(), mix_thread);

    // Finally connected.
    q->RunForThread(mix_thread->id());
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre(source));
    EXPECT_EQ(source->thread(), mix_thread);
    EXPECT_EQ(dest->thread(), mix_thread);
  }
};

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryAlreadyConnected) {
  FakeGraph graph({
      .edges = {{1, 2}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(2),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinarySourceDisallowsOutgoingEdges) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
  });

  auto source = graph.node(1);
  source->SetAllowsDest(false);

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), source, /*dest=*/graph.node(2), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinarySourceAlreadyHasOutgoingEdge) {
  FakeGraph graph({
      .edges = {{1, 2}},
      .unconnected_ordinary_nodes = {3},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(3),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryDestNodeTooManyIncomingEdges) {
  FakeGraph graph({
      .edges = {{1, 3}},
      .unconnected_ordinary_nodes = {2},
  });

  auto dest = graph.node(3);
  dest->SetMaxSources(1);

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(2), dest, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryIncompatibleFormats) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
  });

  auto dest = graph.node(2);
  dest->SetOnCanAcceptSourceFormat([](auto n) { return false; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), dest, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryIncompatibleFormatsMixerDest) {
  const auto source_format = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
  const auto dest_format = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 9, 48000});
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .types = {{Node::Type::kMixer, {2}}},
      .formats = {{&source_format, {1}}, {&dest_format, {2}}},
  });

  auto q = graph.global_task_queue();
  auto result =
      Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), graph.node(2), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryPipelineMismatch) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .pipeline_directions =
          {
              {PipelineDirection::kInput, {1}},
              {PipelineDirection::kOutput, {2}},
          },
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(2),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryCycle) {
  FakeGraph graph({
      .edges = {{1, 2}, {2, 3}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(3), /*dest=*/graph.node(1),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinarySuccess) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .threads = {{kThreadId, {2}}},
  });

  auto source = graph.node(1);
  auto dest = graph.node(2);

  ASSERT_EQ(source->thread(), graph.ctx().detached_thread);
  ASSERT_EQ(dest->thread(), graph.thread(kThreadId));

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 1)));

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), source, dest, /*options=*/{});
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->dest(), dest);
  EXPECT_THAT(dest->sources(), ElementsAre(source));

  EXPECT_EQ(source->thread(), graph.thread(kThreadId));
  EXPECT_EQ(dest->thread(), graph.thread(kThreadId));

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  CheckPipelineStagesAfterCreate(graph, source->fake_pipeline_stage(), dest->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaAlreadyConnected) {
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {2}, .dest_children = {}}}},
      .edges = {{1, 2}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(3),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaSourceDisallowsOutgoingEdges) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
  });

  auto source = graph.node(1);
  source->SetAllowsDest(false);

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), source, /*dest=*/graph.node(2), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaSourceAlreadyHasOutgoingEdge) {
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {}, .dest_children = {}}}},
      .edges = {{1, 2}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(3),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaIncompatibleFormats) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([&graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    child->SetOnCanAcceptSourceFormat([](auto n) { return false; });
    return child;
  });

  bool child_destroyed = false;
  dest->SetOnDestroyChildSource(
      [&child_destroyed](NodePtr child_source) { child_destroyed = true; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), dest, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
  EXPECT_TRUE(child_destroyed);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaPipelineMismatch) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .pipeline_directions =
          {
              {PipelineDirection::kInput, {1}},
              {PipelineDirection::kOutput, {2}},
          },
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(2),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaDestNodeTooManyIncomingEdges) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([]() { return nullptr; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), dest, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaCycle) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {2}}}},
      .edges = {{2, 3}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(3), /*dest=*/graph.node(1),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaSuccess) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .threads = {{kThreadId, {}}},
  });

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(), UnorderedElementsAre());

  auto source = graph.node(1);
  auto dest = graph.node(2);

  dest->SetOnCreateNewChildSource([&graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    auto thread = graph.thread(kThreadId);
    child->set_thread(thread);
    child->fake_pipeline_stage()->set_thread(thread->pipeline_thread());
    return child;
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), source, dest, /*options=*/{});
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(dest->child_sources().size(), 1u);
  ASSERT_EQ(dest->child_dests().size(), 0u);

  auto dest_child = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);
  EXPECT_EQ(source->dest(), dest_child);
  EXPECT_THAT(dest_child->sources(), ElementsAre(source));

  EXPECT_EQ(source->thread(), graph.thread(kThreadId));
  EXPECT_EQ(dest_child->thread(), graph.thread(kThreadId));

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  CheckPipelineStagesAfterCreate(graph, source->fake_pipeline_stage(),
                                 dest_child->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinarySourceNodeTooManyOutgoingEdges) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
  });

  auto source = graph.node(1);
  source->SetOnCreateNewChildDest([]() { return nullptr; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), source, /*dest=*/graph.node(2), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryDestNodeTooManyIncomingEdges) {
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {2}}},
              {3, {.source_children = {}, .dest_children = {}}},
          },
      .edges = {{2, 4}},
  });

  auto dest = graph.node(4);
  dest->SetMaxSources(1);

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(3), dest, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryIncompatibleFormats) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
  });

  auto dest = graph.node(2);
  dest->SetOnCanAcceptSourceFormat([](auto n) { return false; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), dest, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryIncompatibleFormatsMixerDest) {
  const auto source_format = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
  const auto dest_format = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 9, 48000});
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {2}}},
          },
      .unconnected_ordinary_nodes = {3},
      .types = {{Node::Type::kMixer, {3}}},
      .formats = {{&source_format, {2}}, {&dest_format, {3}}},
  });

  auto q = graph.global_task_queue();
  auto result =
      Node::CreateEdge(graph.ctx(), /*source=*/graph.node(2), graph.node(3), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryPipelineMismatch) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
      .pipeline_directions =
          {
              {PipelineDirection::kInput, {1}},
              {PipelineDirection::kOutput, {2}},
          },
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(2),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryCycle) {
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {2}, .dest_children = {}}}},
      .edges = {{1, 2}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(3), /*dest=*/graph.node(1),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinarySuccess) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
      .threads = {{kThreadId, {2}}},
  });

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 1)));

  auto source = graph.node(1);
  auto dest = graph.node(2);

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), source, dest, /*options=*/{});
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(source->child_sources().size(), 0u);
  ASSERT_EQ(source->child_dests().size(), 1u);

  auto source_child = std::static_pointer_cast<FakeNode>(source->child_dests()[0]);
  EXPECT_EQ(source_child->dest(), dest);
  EXPECT_THAT(dest->sources(), ElementsAre(source_child));

  EXPECT_EQ(source_child->thread(), graph.thread(kThreadId));
  EXPECT_EQ(dest->thread(), graph.thread(kThreadId));

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  CheckPipelineStagesAfterCreate(graph, source_child->fake_pipeline_stage(),
                                 dest->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, MetaToMetaSourceNodeTooManyOutgoingEdges) {
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
  });

  auto source = graph.node(1);
  source->SetOnCreateNewChildDest([]() { return nullptr; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), source, /*dest=*/graph.node(2), /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaDestNodeTooManyIncomingEdges) {
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([]() { return nullptr; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), dest, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaIncompatibleFormats) {
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([&graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    child->SetOnCanAcceptSourceFormat([](auto n) { return false; });
    return child;
  });

  bool child_destroyed = false;
  dest->SetOnDestroyChildSource(
      [&child_destroyed](NodePtr child_source) { child_destroyed = true; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), dest, /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
  EXPECT_TRUE(child_destroyed);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaPipelineMismatch) {
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
      .pipeline_directions =
          {
              {PipelineDirection::kInput, {1}},
              {PipelineDirection::kOutput, {2}},
          },
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(2),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaCycle) {
  FakeGraph graph({
      .meta_nodes =
          {
              {4, {.source_children = {3}, .dest_children = {}}},
              {1, {.source_children = {}, .dest_children = {2}}},
          },
      .edges = {{2, 3}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), /*source=*/graph.node(4), /*dest=*/graph.node(1),
                                 /*options=*/{});
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaSuccess) {
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
      .threads = {{kThreadId, {}}},
  });

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(), UnorderedElementsAre());

  auto source = graph.node(1);
  auto dest = graph.node(2);

  dest->SetOnCreateNewChildSource([&graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    auto thread = graph.thread(kThreadId);
    child->set_thread(thread);
    child->fake_pipeline_stage()->set_thread(thread->pipeline_thread());
    return child;
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(graph.ctx(), source, dest, /*options=*/{});
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(source->child_sources().size(), 0u);
  ASSERT_EQ(source->child_dests().size(), 1u);
  ASSERT_EQ(dest->child_sources().size(), 1u);
  ASSERT_EQ(dest->child_dests().size(), 0u);

  auto source_child = std::static_pointer_cast<FakeNode>(source->child_dests()[0]);
  auto dest_child = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);

  EXPECT_EQ(source_child->dest(), dest_child);
  EXPECT_THAT(dest_child->sources(), ElementsAre(source_child));

  EXPECT_EQ(source_child->thread(), graph.thread(kThreadId));
  EXPECT_EQ(dest_child->thread(), graph.thread(kThreadId));

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  CheckPipelineStagesAfterCreate(graph, source_child->fake_pipeline_stage(),
                                 dest_child->fake_pipeline_stage());
}

//
// DeleteEdge
//
// We test the following kinds of edges:
// - (ordinary -> ordinary)
// - (ordinary -> meta)
// - (meta -> ordinary)
// - (meta -> meta)
//
// In these scenarios:
// - (error) not connected
// - (error) connected backwards
// - success
//
// In the "success" scenarios, the source PipelineStage is initially assigned to fake_thread_, but
// must be assigned to detached_thread_ after the edge is deleted.
//

class NodeDeleteEdgeTest : public ::testing::Test {
 protected:
  static inline ThreadId kThreadId = 1;

  void CheckPipelineStagesAfterDelete(FakeGraph& graph, FakePipelineStagePtr source,
                                      FakePipelineStagePtr dest) {
    auto q = graph.global_task_queue();

    auto detached_thread = graph.ctx().detached_thread->pipeline_thread();
    auto mix_thread = graph.thread(kThreadId)->pipeline_thread();

    // The PipelineStages are updated asynchronously, by kThreadId.
    // Initially, they are connected.
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre(source));
    EXPECT_EQ(source->thread(), mix_thread);
    EXPECT_EQ(dest->thread(), mix_thread);

    // Still connected because kThreadId hasn't run yet.
    q->RunForThread(detached_thread->id());
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre(source));
    EXPECT_EQ(source->thread(), mix_thread);
    EXPECT_EQ(dest->thread(), mix_thread);

    // Finally, not connected.
    q->RunForThread(mix_thread->id());
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_EQ(source->thread(), detached_thread);
    EXPECT_EQ(dest->thread(), mix_thread);
  }
};

TEST_F(NodeDeleteEdgeTest, OrdinaryToOrdinaryNotConnected) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToOrdinaryConnectedBackwards) {
  FakeGraph graph({
      .edges = {{1, 2}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), /*source=*/graph.node(2),
                                 /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToOrdinarySuccess) {
  FakeGraph graph({
      .edges = {{1, 2}},
      .threads = {{kThreadId, {1, 2}}},
  });

  auto source = graph.node(1);
  auto dest = graph.node(2);

  ASSERT_EQ(source->thread(), graph.thread(kThreadId));
  ASSERT_EQ(dest->thread(), graph.thread(kThreadId));

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->dest(), nullptr);
  EXPECT_THAT(dest->sources(), ElementsAre());

  EXPECT_EQ(source->thread(), graph.ctx().detached_thread);
  EXPECT_EQ(dest->thread(), graph.thread(kThreadId));

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 1)));

  CheckPipelineStagesAfterDelete(graph, source->fake_pipeline_stage(), dest->fake_pipeline_stage());
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaNotConnected) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaConnectedBackwards) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {3}, .dest_children = {}}}},
      .edges = {{1, 3}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), /*source=*/graph.node(2), /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaSuccess) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {3}, .dest_children = {}}}},
      .edges = {{1, 3}},
      .threads = {{kThreadId, {1, 3}}},
  });

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  auto source = graph.node(1);
  auto dest = graph.node(2);

  auto source_stage = source->fake_pipeline_stage();
  auto dest_child_source = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);
  auto dest_stage = dest_child_source->fake_pipeline_stage();

  bool dest_destroyed = false;
  dest->SetOnDestroyChildSource(
      [&dest_destroyed, expected = dest_child_source](NodePtr child_source) {
        EXPECT_EQ(child_source, expected);
        dest_destroyed = true;
      });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->dest(), nullptr);
  EXPECT_EQ(source->thread(), graph.ctx().detached_thread);
  EXPECT_EQ(dest->child_sources().size(), 0u);
  EXPECT_EQ(dest->child_dests().size(), 0u);
  EXPECT_TRUE(dest_destroyed);

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 1)));

  CheckPipelineStagesAfterDelete(graph, source_stage, dest_stage);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinaryNotConnected) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinaryConnectedBackwards) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {3}}}},
      .edges = {{3, 2}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), /*source=*/graph.node(2), /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinarySuccess) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {3}}}},
      .edges = {{3, 2}},
      .threads = {{kThreadId, {2, 3}}},
  });

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  auto source = graph.node(1);
  auto dest = graph.node(2);

  auto source_child_dest = std::static_pointer_cast<FakeNode>(source->child_dests()[0]);
  auto source_stage = source_child_dest->fake_pipeline_stage();
  auto dest_stage = dest->fake_pipeline_stage();

  bool source_destroyed = false;
  source->SetOnDestroyChildDest(
      [&source_destroyed, expected = source_child_dest](NodePtr child_dest) {
        EXPECT_EQ(child_dest, expected);
        source_destroyed = true;
      });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->child_sources().size(), 0u);
  EXPECT_EQ(source->child_dests().size(), 0u);
  EXPECT_EQ(dest->sources().size(), 0u);
  EXPECT_EQ(dest->thread(), graph.thread(kThreadId));
  EXPECT_TRUE(source_destroyed);

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 1)));

  CheckPipelineStagesAfterDelete(graph, source_stage, dest_stage);
}

TEST_F(NodeDeleteEdgeTest, MetaToMetaNotConnected) {
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToMetaConnectedBackwards) {
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {3}}},
              {2, {.source_children = {4}, .dest_children = {}}},
          },
      .edges = {{3, 4}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), /*source=*/graph.node(2), /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToMetaSuccess) {
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {3}}},
              {2, {.source_children = {4}, .dest_children = {}}},
          },
      .edges = {{3, 4}},
      .threads = {{kThreadId, {3, 4}}},
  });

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  auto source = graph.node(1);
  auto dest = graph.node(2);

  auto source_child_dest = std::static_pointer_cast<FakeNode>(source->child_dests()[0]);
  auto dest_child_source = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);

  auto source_stage = source_child_dest->fake_pipeline_stage();
  auto dest_stage = dest_child_source->fake_pipeline_stage();

  bool source_destroyed = false;
  source->SetOnDestroyChildDest(
      [&source_destroyed, expected = source_child_dest](NodePtr child_dest) {
        EXPECT_EQ(child_dest, expected);
        source_destroyed = true;
      });

  bool dest_destroyed = false;
  dest->SetOnDestroyChildSource(
      [&dest_destroyed, expected = dest_child_source](NodePtr child_source) {
        EXPECT_EQ(child_source, expected);
        dest_destroyed = true;
      });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(graph.ctx(), source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->child_sources().size(), 0u);
  EXPECT_EQ(source->child_dests().size(), 0u);
  EXPECT_EQ(dest->child_sources().size(), 0u);
  EXPECT_EQ(dest->child_dests().size(), 0u);
  EXPECT_TRUE(source_destroyed);
  EXPECT_TRUE(dest_destroyed);

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 1)));

  CheckPipelineStagesAfterDelete(graph, source_stage, dest_stage);
}

//
// Tests that combine CreateEdge and DeleteEdge
//

TEST(NodeCreateDeleteEdgeTest, ThreadMoves) {
  FakeGraph graph({
      // This is the example from the comments at MoveNodeToThread in reachability.h, but
      // with an extra "dest" node below N.
      .meta_nodes =
          {
              {
                  3,
                  {.source_children = {2}, .dest_children = {4, 5, 6}},
              },
          },
      .edges =
          {
              {1, 2},    // A -> C
              {4, 7},    // P1 -> D
              {5, 8},    // P2 -> E
              {6, 9},    // P3 -> F
              {9, 12},   // F -> N
              {10, 11},  // H -> G
              {11, 12},  // G -> N
          },
      // The "dest" node starts on thread 1, while all other nodes start on the detached thread.
      .unconnected_ordinary_nodes = {13},
      .types = {{Node::Type::kConsumer, {2}}},
      .threads = {{1, {13}}},
  });

  auto q = graph.global_task_queue();
  auto source = graph.node(12);
  auto dest = graph.node(13);

  auto new_thread = graph.thread(1);
  auto detached_thread = graph.ctx().detached_thread;

  ASSERT_EQ(source->thread(), detached_thread);
  ASSERT_EQ(dest->thread(), new_thread);

  EXPECT_THAT(new_thread->clock_usages(), UnorderedElementsAre(Pair(DefaultClock(), 1)));

  // Create source -> dest.
  {
    auto result = Node::CreateEdge(graph.ctx(), source, dest, /*options=*/{});
    ASSERT_TRUE(result.is_ok());
  }

  // These nodes should move.
  EXPECT_EQ(graph.node(6)->thread(), new_thread);
  EXPECT_EQ(graph.node(9)->thread(), new_thread);
  EXPECT_EQ(graph.node(10)->thread(), new_thread);
  EXPECT_EQ(graph.node(11)->thread(), new_thread);
  EXPECT_EQ(graph.node(12)->thread(), new_thread);
  // These nodes should not move.
  EXPECT_EQ(graph.node(1)->thread(), detached_thread);
  EXPECT_EQ(graph.node(2)->thread(), detached_thread);
  EXPECT_EQ(graph.node(4)->thread(), detached_thread);
  EXPECT_EQ(graph.node(5)->thread(), detached_thread);
  EXPECT_EQ(graph.node(7)->thread(), detached_thread);
  EXPECT_EQ(graph.node(8)->thread(), detached_thread);
  // The dest doesn't change.
  EXPECT_EQ(graph.node(13)->thread(), new_thread);

  EXPECT_THAT(new_thread->clock_usages(), UnorderedElementsAre(Pair(DefaultClock(), 6)));

  q->RunForThread(new_thread->id());

  // Same as above, but check the PipelineStages.
  EXPECT_EQ(graph.node(6)->pipeline_stage()->thread(), new_thread->pipeline_thread());
  EXPECT_EQ(graph.node(9)->pipeline_stage()->thread(), new_thread->pipeline_thread());
  EXPECT_EQ(graph.node(10)->pipeline_stage()->thread(), new_thread->pipeline_thread());
  EXPECT_EQ(graph.node(11)->pipeline_stage()->thread(), new_thread->pipeline_thread());
  EXPECT_EQ(graph.node(12)->pipeline_stage()->thread(), new_thread->pipeline_thread());
  EXPECT_EQ(graph.node(1)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(2)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(4)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(5)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(7)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(8)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(13)->pipeline_stage()->thread(), new_thread->pipeline_thread());

  // Delete source -> dest.
  {
    auto result = Node::DeleteEdge(graph.ctx(), source, dest);
    ASSERT_TRUE(result.is_ok());
  }

  // Everything should be detached except the dest.
  EXPECT_EQ(graph.node(6)->thread(), detached_thread);
  EXPECT_EQ(graph.node(9)->thread(), detached_thread);
  EXPECT_EQ(graph.node(10)->thread(), detached_thread);
  EXPECT_EQ(graph.node(11)->thread(), detached_thread);
  EXPECT_EQ(graph.node(12)->thread(), detached_thread);
  EXPECT_EQ(graph.node(1)->thread(), detached_thread);
  EXPECT_EQ(graph.node(2)->thread(), detached_thread);
  EXPECT_EQ(graph.node(4)->thread(), detached_thread);
  EXPECT_EQ(graph.node(5)->thread(), detached_thread);
  EXPECT_EQ(graph.node(7)->thread(), detached_thread);
  EXPECT_EQ(graph.node(8)->thread(), detached_thread);
  EXPECT_EQ(graph.node(13)->thread(), new_thread);

  EXPECT_THAT(new_thread->clock_usages(), UnorderedElementsAre(Pair(DefaultClock(), 1)));

  q->RunForThread(new_thread->id());

  // Same as above, but check the PipelineStages.
  EXPECT_EQ(graph.node(6)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(9)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(10)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(11)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(12)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(1)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(2)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(4)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(5)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(7)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(8)->pipeline_stage()->thread(), detached_thread->pipeline_thread());
  EXPECT_EQ(graph.node(13)->pipeline_stage()->thread(), new_thread->pipeline_thread());
}

TEST(NodeCreateDeleteEdgeTest, RecomputeDelays) {
  // The graph is:
  //
  // ```
  //        1     producer (renderer)
  //    +---|---+
  //    |   2   |
  //    |       | meta 10 (splitter; 3=output, 4=loopback)
  //    | 3   4 |
  //    +-|---|-+
  //      |    \
  //      5     \   6  producer (input device)
  // consumer    \ /
  // (device)     7
  //              |
  //              8    consumer (capturer)
  // ```
  //
  // None of these edges exist at the beginning. They are created 1-by-1.
  FakeGraph graph({
      .meta_nodes = {{10, {.source_children = {2}, .dest_children = {3, 4}}}},
      .unconnected_ordinary_nodes = {1, 5, 6, 7, 8},
      .types =
          {
              {Node::Type::kProducer, {1, 6}},
              {Node::Type::kConsumer, {2, 5, 8}},
          },
      .pipeline_directions =
          {
              {PipelineDirection::kOutput, {1, 2, 3, 4, 5, 10}},
              {PipelineDirection::kInput, {6, 7, 8}},
          },
      .threads =
          {
              {ThreadId(1), {2, 5}},
              {ThreadId(2), {8}},
          },
  });

  // Set external values.
  graph.node(5)->SetMaxDelays({.downstream_output_pipeline_delay = zx::nsec(5000)});
  graph.node(6)->SetMaxDelays({.upstream_input_pipeline_delay = zx::nsec(6000)});

  // Set internal values.
  graph.node(2)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(1).get()) {
      return zx::nsec(12);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(5)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(3).get()) {
      return zx::nsec(35);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(7)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(4).get()) {
      return zx::nsec(0);
    } else if (source == graph.node(6).get()) {
      return zx::nsec(67);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(8)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(7).get()) {
      return zx::nsec(78);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });

  // Setup callbacks.
  struct Updated {
    std::unordered_set<int> downstream_output;
    std::unordered_set<int> downstream_input;
    std::unordered_set<int> upstream_input;
  };
  Updated updated;
  for (int k = 1; k <= 8; k++) {
    auto node = graph.node(k);
    node->SetOnSetMaxDelays([&updated, k, node](auto delays) {
      return std::make_pair(node->thread()->id(), [&updated, delays, k]() {
        if (delays.downstream_output_pipeline_delay) {
          updated.downstream_output.insert(k);
        }
        if (delays.downstream_input_pipeline_delay) {
          updated.downstream_input.insert(k);
        }
        if (delays.upstream_input_pipeline_delay) {
          updated.upstream_input.insert(k);
        }
      });
    });
  }

  auto q = graph.global_task_queue();

  // These test cases are run forward then backwards.
  //
  // When run forwards, we create edge `source -> dest`, then validate the two closures, then move
  // to the next test case.
  //
  // When run backwards, we delete edge `source -> dest`, then calidate `changes_on_edge`, then
  // validate `stage_after_edge` in the prior test case.
  struct TestCase {
    NodePtr source;
    NodePtr dest;
    std::function<void()> state_after_edge;
    std::function<void()> changes_on_edge;
  };
  std::vector<TestCase> test_cases = {
      {
          // This is the expected initial state.
          .source = nullptr,
          .dest = nullptr,
          .state_after_edge =
              [&graph]() {
                for (int k = 1; k <= 4; k++) {
                  auto node = graph.node(k);
                  SCOPED_TRACE(node->name());
                  EXPECT_EQ(node->max_downstream_output_pipeline_delay(), zx::nsec(0));
                  EXPECT_EQ(node->max_downstream_input_pipeline_delay(), zx::nsec(0));
                }
                for (int k = 7; k <= 8; k++) {
                  auto node = graph.node(k);
                  SCOPED_TRACE(node->name());
                  EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(0));
                }
                EXPECT_EQ(graph.node(5)->max_downstream_output_pipeline_delay(), zx::nsec(5000));
                EXPECT_EQ(graph.node(5)->max_downstream_input_pipeline_delay(), zx::nsec(0));
                EXPECT_EQ(graph.node(6)->max_upstream_input_pipeline_delay(), zx::nsec(6000));
              },
          .changes_on_edge = []() {},
      },
      {
          .source = graph.node(1),
          .dest = graph.node(2),
          .state_after_edge =
              [&graph]() {
                EXPECT_EQ(graph.node(1)->max_downstream_output_pipeline_delay(), zx::nsec(12));
                EXPECT_EQ(graph.node(2)->max_downstream_output_pipeline_delay(), zx::nsec(0));
                EXPECT_EQ(graph.node(3)->max_downstream_output_pipeline_delay(), zx::nsec(0));
                EXPECT_EQ(graph.node(5)->max_downstream_output_pipeline_delay(), zx::nsec(5000));
              },
          .changes_on_edge =
              [q, &updated]() {
                q->RunForThread(1);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre(1));
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre());
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre());

                updated.downstream_output.clear();
                updated.downstream_input.clear();
                updated.upstream_input.clear();

                q->RunForThread(2);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre());
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre());
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre());
              },
      },
      {
          .source = graph.node(3),
          .dest = graph.node(5),
          .state_after_edge =
              [&graph]() {
                EXPECT_EQ(graph.node(1)->max_downstream_output_pipeline_delay(),
                          zx::nsec(12 + 35 + 5000));
                EXPECT_EQ(graph.node(2)->max_downstream_output_pipeline_delay(),
                          zx::nsec(35 + 5000));
                EXPECT_EQ(graph.node(3)->max_downstream_output_pipeline_delay(),
                          zx::nsec(35 + 5000));
                EXPECT_EQ(graph.node(5)->max_downstream_output_pipeline_delay(), zx::nsec(5000));
              },
          .changes_on_edge =
              [q, &updated]() {
                q->RunForThread(1);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre(1, 2, 3));
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre());
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre());

                updated.downstream_output.clear();
                updated.downstream_input.clear();
                updated.upstream_input.clear();

                q->RunForThread(2);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre());
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre());
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre());
              },
      },
      {
          .source = graph.node(6),
          .dest = graph.node(7),
          .state_after_edge =
              [&graph]() {
                EXPECT_EQ(graph.node(4)->max_downstream_input_pipeline_delay(), zx::nsec(0));
                EXPECT_EQ(graph.node(6)->max_downstream_input_pipeline_delay(), zx::nsec(67));
                EXPECT_EQ(graph.node(7)->max_downstream_input_pipeline_delay(), zx::nsec(0));
                EXPECT_EQ(graph.node(8)->max_downstream_input_pipeline_delay(), zx::nsec(0));

                EXPECT_EQ(graph.node(6)->max_upstream_input_pipeline_delay(), zx::nsec(6000));
                EXPECT_EQ(graph.node(7)->max_upstream_input_pipeline_delay(), zx::nsec(6000 + 67));
                EXPECT_EQ(graph.node(8)->max_upstream_input_pipeline_delay(), zx::nsec(0));
              },
          .changes_on_edge =
              [q, &updated]() {
                // Since 6 and 7 aren't connected to a consumer before this edges is created,
                // they're detached, hence these run on the first available thread.
                q->RunForThread(1);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre());
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre(6));
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre(7));

                updated.downstream_output.clear();
                updated.downstream_input.clear();
                updated.upstream_input.clear();

                q->RunForThread(2);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre());
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre());
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre());
              },
      },
      {
          .source = graph.node(7),
          .dest = graph.node(8),
          .state_after_edge =
              [&graph]() {
                EXPECT_EQ(graph.node(4)->max_downstream_input_pipeline_delay(), zx::nsec(0));
                EXPECT_EQ(graph.node(6)->max_downstream_input_pipeline_delay(), zx::nsec(67 + 78));
                EXPECT_EQ(graph.node(7)->max_downstream_input_pipeline_delay(), zx::nsec(78));
                EXPECT_EQ(graph.node(8)->max_downstream_input_pipeline_delay(), zx::nsec(0));

                EXPECT_EQ(graph.node(6)->max_upstream_input_pipeline_delay(), zx::nsec(6000));
                EXPECT_EQ(graph.node(7)->max_upstream_input_pipeline_delay(), zx::nsec(6000 + 67));
                EXPECT_EQ(graph.node(8)->max_upstream_input_pipeline_delay(),
                          zx::nsec(6000 + 67 + 78));
              },
          .changes_on_edge =
              [q, &updated]() {
                // Tasks are pushed on node 8's thread first.
                q->RunForThread(2);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre());
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre(6, 7));
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre(8));

                updated.downstream_output.clear();
                updated.downstream_input.clear();
                updated.upstream_input.clear();

                q->RunForThread(1);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre());
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre());
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre());
              },
      },
      {
          .source = graph.node(4),
          .dest = graph.node(7),
          .state_after_edge =
              [&graph]() {
                EXPECT_EQ(graph.node(1)->max_downstream_input_pipeline_delay(), zx::nsec(78));
                EXPECT_EQ(graph.node(2)->max_downstream_input_pipeline_delay(), zx::nsec(78));
                EXPECT_EQ(graph.node(4)->max_downstream_input_pipeline_delay(), zx::nsec(78));
                EXPECT_EQ(graph.node(6)->max_downstream_input_pipeline_delay(), zx::nsec(67 + 78));
                EXPECT_EQ(graph.node(7)->max_downstream_input_pipeline_delay(), zx::nsec(78));
                EXPECT_EQ(graph.node(8)->max_downstream_input_pipeline_delay(), zx::nsec(0));

                EXPECT_EQ(graph.node(6)->max_upstream_input_pipeline_delay(), zx::nsec(6000));
                EXPECT_EQ(graph.node(7)->max_upstream_input_pipeline_delay(), zx::nsec(6000 + 67));
                EXPECT_EQ(graph.node(8)->max_upstream_input_pipeline_delay(),
                          zx::nsec(6000 + 67 + 78));
              },
          .changes_on_edge =
              [q, &updated]() {
                // Tasks are pushed on node 7's thread first.
                q->RunForThread(2);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre());
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre(4));
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre());

                updated.downstream_output.clear();
                updated.downstream_input.clear();
                updated.upstream_input.clear();

                q->RunForThread(1);
                EXPECT_THAT(updated.downstream_output, UnorderedElementsAre());
                EXPECT_THAT(updated.downstream_input, UnorderedElementsAre(1, 2));
                EXPECT_THAT(updated.upstream_input, UnorderedElementsAre());

                updated.downstream_output.clear();
                updated.downstream_input.clear();
                updated.upstream_input.clear();
              },
      },
  };

  // Run forwards.
  for (auto& tc : test_cases) {
    if (!tc.source) {
      SCOPED_TRACE("InitialState");
      tc.state_after_edge();
      continue;
    }
    SCOPED_TRACE("Create " + std::string(tc.source->name()) + "->" + std::string(tc.dest->name()));
    ASSERT_TRUE(Node::CreateEdge(graph.ctx(), tc.source, tc.dest, /*options=*/{}).is_ok());
    tc.state_after_edge();
    tc.changes_on_edge();
  }

  // Run backwards.
  for (size_t k = test_cases.size() - 1; k > 0; k--) {
    auto& tc = test_cases[k];
    SCOPED_TRACE("Delete " + std::string(tc.source->name()) + "->" + std::string(tc.dest->name()));
    ASSERT_TRUE(Node::DeleteEdge(graph.ctx(), tc.source, tc.dest).is_ok());
    tc.changes_on_edge();

    auto& prior = test_cases[k - 1];
    prior.state_after_edge();
  }
}

//
// Destroy
//
// We create the following pairs of edges:
// - (ordinary -> ordinary)
// - (ordinary -> meta)
// - (meta -> ordinary)
// - (meta -> meta)
//
// Then for each pair, we run these cases
// - delete the source
// - delete the dest
//
// Plus two additional cases:
// - delete A, where there exists an edge (A->B), where B is a built-in child of a meta node
// - delete A, where there exists an edge (B->A), where B is a built-in child of a meta node
//

class NodeDestroyTest : public NodeDeleteEdgeTest {};

TEST_F(NodeDestroyTest, OrdinaryToOrdinary) {
  for (int k = 0; k < 2; k++) {
    SCOPED_TRACE(std::string("Delete ") + (k == 0 ? "source" : "dest"));

    FakeGraph graph({
        .edges = {{1, 2}},
        .threads = {{kThreadId, {1, 2}}},
    });

    auto q = graph.global_task_queue();
    auto source = graph.node(1);
    auto dest = graph.node(2);

    auto to_destroy = (k == 0) ? source : dest;
    bool destroyed = false;
    to_destroy->SetOnDestroySelf([&destroyed]() { destroyed = true; });

    Node::Destroy(graph.ctx(), to_destroy);

    EXPECT_EQ(source->dest(), nullptr);
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_TRUE(destroyed);

    CheckPipelineStagesAfterDelete(graph, source->fake_pipeline_stage(),
                                   dest->fake_pipeline_stage());
  }
}

TEST_F(NodeDestroyTest, OrdinaryToMeta) {
  for (int k = 0; k < 2; k++) {
    SCOPED_TRACE(std::string("Delete ") + (k == 0 ? "source" : "dest"));

    FakeGraph graph({
        .meta_nodes = {{2, {.source_children = {3}, .dest_children = {}}}},
        .edges = {{1, 3}},
        .threads = {{kThreadId, {1, 3}}},
    });

    EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
                UnorderedElementsAre(Pair(DefaultClock(), 2)));

    auto q = graph.global_task_queue();
    auto source = graph.node(1);
    auto dest = graph.node(2);
    auto dest_child_source = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);

    bool dest_destroyed = false;
    dest->SetOnDestroyChildSource(
        [&dest_destroyed, expected = dest_child_source](NodePtr child_source) {
          EXPECT_EQ(child_source, expected);
          dest_destroyed = true;
        });

    auto to_destroy = (k == 0) ? source : dest;
    bool destroyed = false;
    to_destroy->SetOnDestroySelf([&destroyed]() { destroyed = true; });

    Node::Destroy(graph.ctx(), to_destroy);

    EXPECT_EQ(source->dest(), nullptr);
    EXPECT_EQ(dest->child_sources().size(), 0u);
    EXPECT_TRUE(dest_destroyed);
    EXPECT_TRUE(destroyed);

    if (to_destroy == dest) {
      // `source` is moved to detached thread after `dest` edge is deleted.
      EXPECT_THAT(graph.thread(kThreadId)->clock_usages(), UnorderedElementsAre());
    } else {
      EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
                  UnorderedElementsAre(Pair(DefaultClock(), 1)));
    }

    CheckPipelineStagesAfterDelete(graph, source->fake_pipeline_stage(),
                                   dest_child_source->fake_pipeline_stage());
  }
}

TEST_F(NodeDestroyTest, OrdinaryToMetaWithBuiltinChild) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {3}, .built_in_children = true}}},
      .edges = {{1, 3}},
      .threads = {{kThreadId, {1, 3}}},
  });

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  auto q = graph.global_task_queue();
  auto source = graph.node(1);
  auto dest = graph.node(2);
  auto dest_child_source = graph.node(3);

  NodePtr destroyed;
  dest->SetOnDestroyChildSource(
      [&destroyed](NodePtr child_source) mutable { destroyed = child_source; });

  // When destroying node 1, we disconnect from child node 3, but don't delete child node 3 because
  // it's a builtin child of meta node 2.
  Node::Destroy(graph.ctx(), source);

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 1)));

  EXPECT_EQ(source->dest(), nullptr);
  EXPECT_EQ(dest->child_sources().size(), 1u);
  EXPECT_EQ(dest_child_source->sources().size(), 0u);
  EXPECT_FALSE(destroyed) << "should not have destroyed " << destroyed->name();
}

TEST_F(NodeDestroyTest, MetaToOrdinary) {
  for (int k = 0; k < 2; k++) {
    SCOPED_TRACE(std::string("Delete ") + (k == 0 ? "source" : "dest"));

    FakeGraph graph({
        .meta_nodes = {{1, {.source_children = {}, .dest_children = {3}}}},
        .edges = {{3, 2}},
        .threads = {{kThreadId, {2, 3}}},
    });

    EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
                UnorderedElementsAre(Pair(DefaultClock(), 2)));

    auto q = graph.global_task_queue();
    auto source = graph.node(1);
    auto dest = graph.node(2);
    auto source_child_dest = std::static_pointer_cast<FakeNode>(source->child_dests()[0]);

    bool source_destroyed = false;
    source->SetOnDestroyChildDest(
        [&source_destroyed, expected = source_child_dest](NodePtr child_dest) {
          EXPECT_EQ(child_dest, expected);
          source_destroyed = true;
        });

    auto to_destroy = (k == 0) ? source : dest;
    bool destroyed = false;
    to_destroy->SetOnDestroySelf([&destroyed]() { destroyed = true; });

    Node::Destroy(graph.ctx(), to_destroy);

    if (to_destroy == dest) {
      // `source` is moved to detached thread after `dest` edge is deleted.
      EXPECT_THAT(graph.thread(kThreadId)->clock_usages(), UnorderedElementsAre());
    } else {
      EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
                  UnorderedElementsAre(Pair(DefaultClock(), 1)));
    }

    EXPECT_EQ(source->child_dests().size(), 0u);
    EXPECT_EQ(dest->sources().size(), 0u);
    EXPECT_TRUE(source_destroyed);
    EXPECT_TRUE(destroyed);

    CheckPipelineStagesAfterDelete(graph, source_child_dest->fake_pipeline_stage(),
                                   dest->fake_pipeline_stage());
  }
}

TEST_F(NodeDestroyTest, MetaToOrdinaryWithBuiltinChild) {
  FakeGraph graph({
      .meta_nodes = {{1, {.dest_children = {3}, .built_in_children = true}}},
      .edges = {{3, 2}},
      .threads = {{kThreadId, {2, 3}}},
  });

  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
              UnorderedElementsAre(Pair(DefaultClock(), 2)));

  auto q = graph.global_task_queue();
  auto source = graph.node(1);
  auto dest = graph.node(2);
  auto source_child_dest = graph.node(3);

  NodePtr destroyed;
  source->SetOnDestroyChildDest(
      [&destroyed](NodePtr child_source) mutable { destroyed = std::move(child_source); });

  // When destroying node 2, we disconnect from child node 3, but don't delete child node 3 because
  // it's a builtin child of meta node 1.
  Node::Destroy(graph.ctx(), dest);

  // Child node 3 is still moved to detached thread.
  EXPECT_THAT(graph.thread(kThreadId)->clock_usages(), UnorderedElementsAre());

  EXPECT_EQ(source->child_dests().size(), 1u);
  EXPECT_EQ(source_child_dest->dest(), nullptr);
  EXPECT_EQ(dest->sources().size(), 0u);
  EXPECT_FALSE(destroyed) << "should not have destroyed " << destroyed->name();
}

TEST_F(NodeDestroyTest, MetaToMeta) {
  for (int k = 0; k < 2; k++) {
    SCOPED_TRACE(std::string("Delete ") + (k == 0 ? "source" : "dest"));

    FakeGraph graph({
        .meta_nodes =
            {
                {1, {.source_children = {}, .dest_children = {3}}},
                {2, {.source_children = {4}, .dest_children = {}}},
            },
        .edges = {{3, 4}},
        .threads = {{kThreadId, {3, 4}}},
    });

    EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
                UnorderedElementsAre(Pair(DefaultClock(), 2)));

    auto q = graph.global_task_queue();
    auto source = graph.node(1);
    auto dest = graph.node(2);
    auto source_child_dest = std::static_pointer_cast<FakeNode>(source->child_dests()[0]);
    auto dest_child_source = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);

    bool source_destroyed = false;
    source->SetOnDestroyChildDest(
        [&source_destroyed, expected = source_child_dest](NodePtr child_dest) {
          EXPECT_EQ(child_dest, expected);
          source_destroyed = true;
        });

    bool dest_destroyed = false;
    dest->SetOnDestroyChildSource(
        [&dest_destroyed, expected = dest_child_source](NodePtr child_source) {
          EXPECT_EQ(child_source, expected);
          dest_destroyed = true;
        });

    auto to_destroy = (k == 0) ? source : dest;
    bool destroyed = false;
    to_destroy->SetOnDestroySelf([&destroyed]() { destroyed = true; });

    Node::Destroy(graph.ctx(), to_destroy);

    if (to_destroy == dest) {
      // `source` is moved to detached thread after `dest` edge is deleted.
      EXPECT_THAT(graph.thread(kThreadId)->clock_usages(), UnorderedElementsAre());
    } else {
      EXPECT_THAT(graph.thread(kThreadId)->clock_usages(),
                  UnorderedElementsAre(Pair(DefaultClock(), 1)));
    }

    EXPECT_EQ(source->child_dests().size(), 0u);
    EXPECT_EQ(dest->child_sources().size(), 0u);
    EXPECT_TRUE(source_destroyed);
    EXPECT_TRUE(dest_destroyed);
    EXPECT_TRUE(destroyed);

    CheckPipelineStagesAfterDelete(graph, source_child_dest->fake_pipeline_stage(),
                                   dest_child_source->fake_pipeline_stage());
  }
}

TEST_F(NodeDestroyTest, OrdinaryMultipleSources) {
  FakeGraph graph({
      .edges = {{1, 3}, {2, 3}},
  });

  auto q = graph.global_task_queue();
  auto source1 = graph.node(1);
  auto source2 = graph.node(2);
  auto dest = graph.node(3);

  bool destroyed = false;
  dest->SetOnDestroySelf([&destroyed]() { destroyed = true; });

  Node::Destroy(graph.ctx(), dest);

  EXPECT_EQ(source1->dest(), nullptr);
  EXPECT_EQ(source2->dest(), nullptr);
  EXPECT_THAT(dest->sources(), ElementsAre());
  EXPECT_TRUE(destroyed);
}

TEST_F(NodeDestroyTest, MetaMultipleChildren) {
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {1, 2}, .dest_children = {4, 5}}}},
      .edges =
          {
              {11, 1},
              {12, 2},
              {4, 14},
              {5, 15},
          },
  });

  auto q = graph.global_task_queue();
  auto meta = graph.node(3);
  Node::Destroy(graph.ctx(), meta);

  EXPECT_EQ(graph.node(11)->dest(), nullptr);
  EXPECT_EQ(graph.node(12)->dest(), nullptr);
  EXPECT_EQ(graph.node(14)->sources().size(), 0u);
  EXPECT_EQ(graph.node(15)->sources().size(), 0u);
  EXPECT_EQ(meta->child_sources().size(), 0u);
  EXPECT_EQ(meta->child_dests().size(), 0u);
}

}  // namespace
}  // namespace media_audio
