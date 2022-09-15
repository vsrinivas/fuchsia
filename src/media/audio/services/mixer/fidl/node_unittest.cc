// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/node.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/mix/testing/fake_thread.h"

namespace media_audio {
namespace {

using ::testing::ElementsAre;

class NodeCreateEdgeTest : public ::testing::Test {
 protected:
  void CheckPipelineStagesAfterCreate(GlobalTaskQueue& q, FakePipelineStagePtr source,
                                      FakePipelineStagePtr dest) {
    // The PipelineStages are updated asynchronously by fake_thread_.
    // Initially, they are not connected.
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_EQ(source->thread(), detached_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);

    // Still not connected because fake_thread_ hasn't run yet.
    q.RunForThread(detached_thread_->id());
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_EQ(source->thread(), detached_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);

    // Finally connected.
    q.RunForThread(fake_thread_->id());
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre(source));
    EXPECT_EQ(source->thread(), fake_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);
  }

  const DetachedThreadPtr detached_thread_ = DetachedThread::Create();
  const FakeThreadPtr fake_thread_ = FakeThread::Create(1);
};

// For CreateEdge, we test the following kinds of edges:
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

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryAlreadyConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 2}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinarySourceDisallowsOutgoingEdges) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .default_thread = detached_thread_,
  });

  auto source = graph.node(1);
  source->SetOnAllowsDest([]() { return false; });

  auto result = Node::CreateEdge(q, source, /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinarySourceAlreadyHasOutgoingEdge) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 2}},
      .unconnected_ordinary_nodes = {3},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(3));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryDestNodeTooManyIncomingEdges) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 3}},
      .unconnected_ordinary_nodes = {2},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(3);
  dest->SetOnMaxSources([]() { return 1; });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(2), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryIncompatibleFormats) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCanAcceptSourceFormat([](auto n) { return false; });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryPipelineMismatch) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .pipeline_directions =
          {
              {PipelineDirection::kInput, {1}},
              {PipelineDirection::kOutput, {2}},
          },
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryCycle) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 2}, {2, 3}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(3), /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinarySuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .threads = {{fake_thread_, {2}}},
      .default_thread = detached_thread_,
  });

  auto source = graph.node(1);
  auto dest = graph.node(2);

  ASSERT_EQ(source->pipeline_stage_thread(), detached_thread_);
  ASSERT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  auto result = Node::CreateEdge(q, source, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->dest(), dest);
  EXPECT_THAT(dest->sources(), ElementsAre(source));

  EXPECT_EQ(source->pipeline_stage_thread(), fake_thread_);
  EXPECT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterCreate(q, source->fake_pipeline_stage(), dest->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaAlreadyConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {2}, .dest_children = {}}}},
      .edges = {{1, 2}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(3));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaSourceDisallowsOutgoingEdges) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .default_thread = detached_thread_,
  });

  auto source = graph.node(1);
  source->SetOnAllowsDest([]() { return false; });

  auto result = Node::CreateEdge(q, source, /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaSourceAlreadyHasOutgoingEdge) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {}, .dest_children = {}}}},
      .edges = {{1, 2}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(3));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaIncompatibleFormats) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([&graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    child->SetOnCanAcceptSourceFormat([](auto n) { return false; });
    return child;
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaPipelineMismatch) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .pipeline_directions =
          {
              {PipelineDirection::kInput, {1}},
              {PipelineDirection::kOutput, {2}},
          },
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaDestNodeTooManyIncomingEdges) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([]() { return nullptr; });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaCycle) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {2}}}},
      .edges = {{2, 3}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(3), /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaSuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .default_thread = detached_thread_,
  });

  auto source = graph.node(1);
  auto dest = graph.node(2);

  dest->SetOnCreateNewChildSource([this, &graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    child->set_pipeline_stage_thread(fake_thread_);
    child->fake_pipeline_stage()->set_thread(fake_thread_);
    return child;
  });

  auto result = Node::CreateEdge(q, source, dest);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(dest->child_sources().size(), 1u);
  ASSERT_EQ(dest->child_dests().size(), 0u);

  auto dest_child = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);
  EXPECT_EQ(source->dest(), dest_child);
  EXPECT_THAT(dest_child->sources(), ElementsAre(source));

  EXPECT_EQ(source->pipeline_stage_thread(), fake_thread_);
  EXPECT_EQ(dest_child->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterCreate(q, source->fake_pipeline_stage(),
                                 dest_child->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinarySourceNodeTooManyOutgoingEdges) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
      .default_thread = detached_thread_,
  });

  auto source = graph.node(1);
  source->SetOnCreateNewChildDest([]() { return nullptr; });

  auto result = Node::CreateEdge(q, source, /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryDestNodeTooManyIncomingEdges) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {2}}},
              {3, {.source_children = {}, .dest_children = {}}},
          },
      .edges = {{2, 4}},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(4);
  dest->SetOnMaxSources([]() { return 1; });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(3), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryIncompatibleFormats) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCanAcceptSourceFormat([](auto n) { return false; });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryPipelineMismatch) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
      .pipeline_directions =
          {
              {PipelineDirection::kInput, {1}},
              {PipelineDirection::kOutput, {2}},
          },
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryCycle) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {2}, .dest_children = {}}}},
      .edges = {{1, 2}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(3), /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinarySuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
      .threads = {{fake_thread_, {2}}},
      .default_thread = detached_thread_,
  });

  auto source = graph.node(1);
  auto dest = graph.node(2);

  auto result = Node::CreateEdge(q, source, dest);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(source->child_sources().size(), 0u);
  ASSERT_EQ(source->child_dests().size(), 1u);

  auto source_child = std::static_pointer_cast<FakeNode>(source->child_dests()[0]);
  EXPECT_EQ(source_child->dest(), dest);
  EXPECT_THAT(dest->sources(), ElementsAre(source_child));

  EXPECT_EQ(source_child->pipeline_stage_thread(), fake_thread_);
  EXPECT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterCreate(q, source_child->fake_pipeline_stage(),
                                 dest->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, MetaToMetaSourceNodeTooManyOutgoingEdges) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
      .default_thread = detached_thread_,
  });

  auto source = graph.node(1);
  source->SetOnCreateNewChildDest([]() { return nullptr; });

  auto result = Node::CreateEdge(q, source, /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kSourceNodeHasTooManyOutgoingEdges);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaDestNodeTooManyIncomingEdges) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([]() { return nullptr; });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestNodeHasTooManyIncomingEdges);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaIncompatibleFormats) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([&graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    child->SetOnCanAcceptSourceFormat([](auto n) { return false; });
    return child;
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaPipelineMismatch) {
  GlobalTaskQueue q;
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
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(),
            fuchsia_audio_mixer::CreateEdgeError::kOutputPipelineCannotReadFromInputPipeline);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaCycle) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {4, {.source_children = {3}, .dest_children = {}}},
              {1, {.source_children = {}, .dest_children = {2}}},
          },
      .edges = {{2, 3}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*source=*/graph.node(4), /*dest=*/graph.node(1));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kCycle);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaSuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
      .default_thread = detached_thread_,
  });

  auto source = graph.node(1);
  auto dest = graph.node(2);

  dest->SetOnCreateNewChildSource([this, &graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    child->set_pipeline_stage_thread(fake_thread_);
    child->fake_pipeline_stage()->set_thread(fake_thread_);
    return child;
  });

  auto result = Node::CreateEdge(q, source, dest);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(source->child_sources().size(), 0u);
  ASSERT_EQ(source->child_dests().size(), 1u);
  ASSERT_EQ(dest->child_sources().size(), 1u);
  ASSERT_EQ(dest->child_dests().size(), 0u);

  auto source_child = std::static_pointer_cast<FakeNode>(source->child_dests()[0]);
  auto dest_child = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);

  EXPECT_EQ(source_child->dest(), dest_child);
  EXPECT_THAT(dest_child->sources(), ElementsAre(source_child));

  EXPECT_EQ(source_child->pipeline_stage_thread(), fake_thread_);
  EXPECT_EQ(dest_child->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterCreate(q, source_child->fake_pipeline_stage(),
                                 dest_child->fake_pipeline_stage());
}

class NodeDeleteEdgeTest : public ::testing::Test {
 protected:
  void CheckPipelineStagesAfterDelete(GlobalTaskQueue& q, FakePipelineStagePtr source,
                                      FakePipelineStagePtr dest) {
    // The PipelineStages are updated asynchronously, by fake_thread_.
    // Initially, they are connected.
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre(source));
    EXPECT_EQ(source->thread(), fake_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);

    // Still connected because fake_thread_ hasn't run yet.
    q.RunForThread(detached_thread_->id());
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre(source));
    EXPECT_EQ(source->thread(), fake_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);

    // Finally, not connected.
    q.RunForThread(fake_thread_->id());
    EXPECT_THAT(source->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_EQ(source->thread(), detached_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);
  }

  const DetachedThreadPtr detached_thread_ = DetachedThread::Create();
  const FakeThreadPtr fake_thread_ = FakeThread::Create(1);
};

// For DeleteEdge, we test the following kinds of edges:
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

TEST_F(NodeDeleteEdgeTest, OrdinaryToOrdinaryNotConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
  });

  auto result =
      Node::DeleteEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(2), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToOrdinaryConnectedBackwards) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 2}},
  });

  auto result =
      Node::DeleteEdge(q, /*source=*/graph.node(2), /*dest=*/graph.node(1), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToOrdinarySuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 2}},
      .threads = {{fake_thread_, {1, 2}}},
  });

  auto source = graph.node(1);
  auto dest = graph.node(2);

  ASSERT_EQ(source->pipeline_stage_thread(), fake_thread_);
  ASSERT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  auto result = Node::DeleteEdge(q, source, dest, detached_thread_);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->dest(), nullptr);
  EXPECT_THAT(dest->sources(), ElementsAre());

  EXPECT_EQ(source->pipeline_stage_thread(), detached_thread_);
  EXPECT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterDelete(q, source->fake_pipeline_stage(), dest->fake_pipeline_stage());
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaNotConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
  });

  auto result =
      Node::DeleteEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(2), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaConnectedBackwards) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {3}, .dest_children = {}}}},
      .edges = {{1, 3}},
  });

  auto result =
      Node::DeleteEdge(q, /*source=*/graph.node(2), /*dest=*/graph.node(1), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaSuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {3}, .dest_children = {}}}},
      .edges = {{1, 3}},
      .threads = {{fake_thread_, {1, 3}}},
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

  auto result = Node::DeleteEdge(q, source, dest, detached_thread_);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->dest(), nullptr);
  EXPECT_EQ(source->pipeline_stage_thread(), detached_thread_);
  EXPECT_EQ(dest->child_sources().size(), 0u);
  EXPECT_EQ(dest->child_dests().size(), 0u);
  EXPECT_TRUE(dest_destroyed);

  CheckPipelineStagesAfterDelete(q, source_stage, dest_stage);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinaryNotConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
  });

  auto result =
      Node::DeleteEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(2), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinaryConnectedBackwards) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {3}}}},
      .edges = {{3, 2}},
  });

  auto result =
      Node::DeleteEdge(q, /*source=*/graph.node(2), /*dest=*/graph.node(1), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinarySuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {3}}}},
      .edges = {{3, 2}},
      .threads = {{fake_thread_, {2, 3}}},
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

  auto result = Node::DeleteEdge(q, source, dest, detached_thread_);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->child_sources().size(), 0u);
  EXPECT_EQ(source->child_dests().size(), 0u);
  EXPECT_EQ(dest->sources().size(), 0u);
  EXPECT_EQ(dest->pipeline_stage_thread(), fake_thread_);
  EXPECT_TRUE(source_destroyed);

  CheckPipelineStagesAfterDelete(q, source_stage, dest_stage);
}

TEST_F(NodeDeleteEdgeTest, MetaToMetaNotConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
  });

  auto result =
      Node::DeleteEdge(q, /*source=*/graph.node(1), /*dest=*/graph.node(2), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToMetaConnectedBackwards) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {3}}},
              {2, {.source_children = {4}, .dest_children = {}}},
          },
      .edges = {{3, 4}},
  });

  auto result =
      Node::DeleteEdge(q, /*source=*/graph.node(2), /*dest=*/graph.node(1), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, MetaToMetaSuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {3}}},
              {2, {.source_children = {4}, .dest_children = {}}},
          },
      .edges = {{3, 4}},
      .threads = {{fake_thread_, {3, 4}}},
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

  auto result = Node::DeleteEdge(q, source, dest, detached_thread_);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(source->child_sources().size(), 0u);
  EXPECT_EQ(source->child_dests().size(), 0u);
  EXPECT_EQ(dest->child_sources().size(), 0u);
  EXPECT_EQ(dest->child_dests().size(), 0u);
  EXPECT_TRUE(source_destroyed);
  EXPECT_TRUE(dest_destroyed);

  CheckPipelineStagesAfterDelete(q, source_stage, dest_stage);
}

}  // namespace
}  // namespace media_audio
