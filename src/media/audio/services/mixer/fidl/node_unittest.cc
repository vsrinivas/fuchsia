// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"

namespace media_audio {
namespace {

using ::testing::ElementsAre;

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
// - (error) source already connected to the same node node (if !source->is_meta)
// - (error) source already connected to a different node (if !source->is_meta)
// - (error) source has too many dest edges (if source->is_meta)
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

    auto detached_thread = graph.detached_thread()->pipeline_thread();
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinarySourceDisallowsOutgoingEdges) {
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
  });

  auto source = graph.node(1);
  source->SetOnAllowsDest([]() { return false; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, source, /*dest=*/graph.node(2));
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), /*dest=*/graph.node(3));
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
  dest->SetOnMaxSources([]() { return 1; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(2), dest);
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), dest);
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryCycle) {
  FakeGraph graph({
      .edges = {{1, 2}, {2, 3}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(3), /*dest=*/graph.node(1));
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

  ASSERT_EQ(source->thread(), graph.detached_thread());
  ASSERT_EQ(dest->thread(), graph.thread(kThreadId));

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->dest(), dest);
  EXPECT_THAT(dest->sources(), ElementsAre(source));

  EXPECT_EQ(source->thread(), graph.thread(kThreadId));
  EXPECT_EQ(dest->thread(), graph.thread(kThreadId));

  CheckPipelineStagesAfterCreate(graph, source->fake_pipeline_stage(), dest->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaAlreadyConnected) {
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {2}, .dest_children = {}}}},
      .edges = {{1, 2}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), /*dest=*/graph.node(3));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaSourceDisallowsOutgoingEdges) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
  });

  auto source = graph.node(1);
  source->SetOnAllowsDest([]() { return false; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, source, /*dest=*/graph.node(2));
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), /*dest=*/graph.node(3));
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

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaCycle) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {2}}}},
      .edges = {{2, 3}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(3), /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaSuccess) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .threads = {{kThreadId, {}}},
  });

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
  auto result = Node::CreateEdge(*q, source, dest);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(dest->child_sources().size(), 1u);
  ASSERT_EQ(dest->child_dests().size(), 0u);

  auto dest_child = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);
  EXPECT_EQ(source->dest(), dest_child);
  EXPECT_THAT(dest_child->sources(), ElementsAre(source));

  EXPECT_EQ(source->thread(), graph.thread(kThreadId));
  EXPECT_EQ(dest_child->thread(), graph.thread(kThreadId));

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
  auto result = Node::CreateEdge(*q, source, /*dest=*/graph.node(2));
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
  dest->SetOnMaxSources([]() { return 1; });

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(3), dest);
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), dest);
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(3), /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinarySuccess) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
      .threads = {{kThreadId, {2}}},
  });

  auto source = graph.node(1);
  auto dest = graph.node(2);

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, source, dest);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(source->child_sources().size(), 0u);
  ASSERT_EQ(source->child_dests().size(), 1u);

  auto source_child = std::static_pointer_cast<FakeNode>(source->child_dests()[0]);
  EXPECT_EQ(source_child->dest(), dest);
  EXPECT_THAT(dest->sources(), ElementsAre(source_child));

  EXPECT_EQ(source_child->thread(), graph.thread(kThreadId));
  EXPECT_EQ(dest->thread(), graph.thread(kThreadId));

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
  auto result = Node::CreateEdge(*q, source, /*dest=*/graph.node(2));
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), dest);
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

  auto q = graph.global_task_queue();
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
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
  auto result = Node::CreateEdge(*q, /*source=*/graph.node(4), /*dest=*/graph.node(1));
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
  auto result = Node::CreateEdge(*q, source, dest);
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

    auto detached_thread = graph.detached_thread()->pipeline_thread();
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
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), /*source=*/graph.node(1),
                                 /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToOrdinaryConnectedBackwards) {
  FakeGraph graph({
      .edges = {{1, 2}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), /*source=*/graph.node(2),
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

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->dest(), nullptr);
  EXPECT_THAT(dest->sources(), ElementsAre());

  EXPECT_EQ(source->thread(), graph.detached_thread());
  EXPECT_EQ(dest->thread(), graph.thread(kThreadId));

  CheckPipelineStagesAfterDelete(graph, source->fake_pipeline_stage(), dest->fake_pipeline_stage());
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaNotConnected) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), /*source=*/graph.node(1),
                                 /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaConnectedBackwards) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {3}, .dest_children = {}}}},
      .edges = {{1, 3}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), /*source=*/graph.node(2),
                                 /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaSuccess) {
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {3}, .dest_children = {}}}},
      .edges = {{1, 3}},
      .threads = {{kThreadId, {1, 3}}},
  });

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
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->dest(), nullptr);
  EXPECT_EQ(source->thread(), graph.detached_thread());
  EXPECT_EQ(dest->child_sources().size(), 0u);
  EXPECT_EQ(dest->child_dests().size(), 0u);
  EXPECT_TRUE(dest_destroyed);

  CheckPipelineStagesAfterDelete(graph, source_stage, dest_stage);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinaryNotConnected) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), /*source=*/graph.node(1),
                                 /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinaryConnectedBackwards) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {3}}}},
      .edges = {{3, 2}},
  });

  auto q = graph.global_task_queue();
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), /*source=*/graph.node(2),
                                 /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinarySuccess) {
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {3}}}},
      .edges = {{3, 2}},
      .threads = {{kThreadId, {2, 3}}},
  });

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
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->child_sources().size(), 0u);
  EXPECT_EQ(source->child_dests().size(), 0u);
  EXPECT_EQ(dest->sources().size(), 0u);
  EXPECT_EQ(dest->thread(), graph.thread(kThreadId));
  EXPECT_TRUE(source_destroyed);

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
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), /*source=*/graph.node(1),
                                 /*dest=*/graph.node(2));
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
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), /*source=*/graph.node(2),
                                 /*dest=*/graph.node(1));
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
  auto result = Node::DeleteEdge(*q, graph.detached_thread(), source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->child_sources().size(), 0u);
  EXPECT_EQ(source->child_dests().size(), 0u);
  EXPECT_EQ(dest->child_sources().size(), 0u);
  EXPECT_EQ(dest->child_dests().size(), 0u);
  EXPECT_TRUE(source_destroyed);
  EXPECT_TRUE(dest_destroyed);

  CheckPipelineStagesAfterDelete(graph, source_stage, dest_stage);
}

//
// DestroyAllEdges
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

    Node::Destroy(*q, graph.detached_thread(), to_destroy);

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

    Node::Destroy(*q, graph.detached_thread(), to_destroy);

    EXPECT_EQ(source->dest(), nullptr);
    EXPECT_EQ(dest->child_sources().size(), 0u);
    EXPECT_TRUE(dest_destroyed);
    EXPECT_TRUE(destroyed);

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

  auto q = graph.global_task_queue();
  auto source = graph.node(1);
  auto dest = graph.node(2);
  auto dest_child_source = graph.node(3);

  NodePtr destroyed;
  dest->SetOnDestroyChildSource(
      [&destroyed](NodePtr child_source) mutable { destroyed = child_source; });

  // When destroying node 1, we disconnect from child node 3, but don't delete child node 3 because
  // it's a builtin child of meta node 2.
  Node::Destroy(*q, graph.detached_thread(), source);

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

    Node::Destroy(*q, graph.detached_thread(), to_destroy);

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

  auto q = graph.global_task_queue();
  auto source = graph.node(1);
  auto dest = graph.node(2);
  auto source_child_dest = graph.node(3);

  NodePtr destroyed;
  source->SetOnDestroyChildDest(
      [&destroyed](NodePtr child_source) mutable { destroyed = child_source; });

  // When destroying node 2, we disconnect from child node 3, but don't delete child node 3 because
  // it's a builtin child of meta node 1.
  Node::Destroy(*q, graph.detached_thread(), dest);

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

    Node::Destroy(*q, graph.detached_thread(), to_destroy);

    EXPECT_EQ(source->child_dests().size(), 0u);
    EXPECT_EQ(dest->child_sources().size(), 0u);
    EXPECT_TRUE(source_destroyed);
    EXPECT_TRUE(dest_destroyed);
    EXPECT_TRUE(destroyed);

    CheckPipelineStagesAfterDelete(graph, source_child_dest->fake_pipeline_stage(),
                                   dest_child_source->fake_pipeline_stage());
  }
}

}  // namespace
}  // namespace media_audio
