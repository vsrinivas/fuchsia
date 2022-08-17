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
  void CheckPipelineStagesAfterCreate(GlobalTaskQueue& q, FakePipelineStagePtr src,
                                      FakePipelineStagePtr dest) {
    // The PipelineStages are updated asynchronously by fake_thread_.
    // Initially, they are not connected.
    EXPECT_THAT(src->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_EQ(src->thread(), detached_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);

    // Still not connected because fake_thread_ hasn't run yet.
    q.RunForThread(detached_thread_->id());
    EXPECT_THAT(src->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_EQ(src->thread(), detached_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);

    // Finally connected.
    q.RunForThread(fake_thread_->id());
    EXPECT_THAT(src->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre(src));
    EXPECT_EQ(src->thread(), fake_thread_);
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
// - (error) src already connected to a different node (if !src->is_meta)
// - (error) src has too many outputs (if src->is_meta)
// - (error) dest has too many inputs (if dest->is_meta)
// - (error) dest doesn't accept src
// - (error) would create a cycle
// - success
//
// In the "success" scenarios, we verify that the nodes are properly connected and that the source
// PipelineStage is assigned to the same thread as destination PipelineStage (which is assigned to
// fake_thread_).

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinarySourceAlreadyConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 2}},
      .unconnected_ordinary_nodes = {3},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(1), /*dest=*/graph.node(3));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryDestRejectsSource) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .unconnected_ordinary_nodes = {1, 2},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCreateCanAcceptSource([](auto n) { return false; });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToOrdinaryCycle) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 2}, {2, 3}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(3), /*dest=*/graph.node(1));
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

  auto src = graph.node(1);
  auto dest = graph.node(2);

  ASSERT_EQ(src->pipeline_stage_thread(), detached_thread_);
  ASSERT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  auto result = Node::CreateEdge(q, src, dest);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(src->dest(), dest);
  EXPECT_THAT(dest->sources(), ElementsAre(src));

  EXPECT_EQ(src->pipeline_stage_thread(), fake_thread_);
  EXPECT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterCreate(q, src->fake_pipeline_stage(), dest->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaSourceAlreadyConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {}, .dest_children = {}}}},
      .edges = {{1, 2}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(1), /*dest=*/graph.node(3));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kAlreadyConnected);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaDestRejectsSource) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([&graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    child->SetOnCreateCanAcceptSource([](auto n) { return false; });
    return child;
  });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaDestTooManyInputs) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCreateNewChildSource([]() { return nullptr; });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestHasTooManyInputs);
}

TEST_F(NodeCreateEdgeTest, OrdinaryToMetaCycle) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {2}}}},
      .edges = {{2, 3}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(3), /*dest=*/graph.node(1));
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

  auto src = graph.node(1);
  auto dest = graph.node(2);

  dest->SetOnCreateNewChildSource([this, &graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    child->set_pipeline_stage_thread(fake_thread_);
    child->fake_pipeline_stage()->set_thread(fake_thread_);
    return child;
  });

  auto result = Node::CreateEdge(q, src, dest);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(dest->child_sources().size(), 1u);
  ASSERT_EQ(dest->child_dests().size(), 0u);

  auto dest_child = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);
  EXPECT_EQ(src->dest(), dest_child);
  EXPECT_THAT(dest_child->sources(), ElementsAre(src));

  EXPECT_EQ(src->pipeline_stage_thread(), fake_thread_);
  EXPECT_EQ(dest_child->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterCreate(q, src->fake_pipeline_stage(), dest_child->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinarySourceTooManyOutputs) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
      .default_thread = detached_thread_,
  });

  auto src = graph.node(1);
  src->SetOnCreateNewChildDest([]() { return nullptr; });

  auto result = Node::CreateEdge(q, src, /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kSourceHasTooManyOutputs);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryDestRejectsSource) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
      .default_thread = detached_thread_,
  });

  auto dest = graph.node(2);
  dest->SetOnCreateCanAcceptSource([](auto n) { return false; });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
}

TEST_F(NodeCreateEdgeTest, MetaToOrdinaryCycle) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{3, {.source_children = {2}, .dest_children = {}}}},
      .edges = {{1, 2}},
      .default_thread = detached_thread_,
  });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(3), /*dest=*/graph.node(1));
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

  auto src = graph.node(1);
  auto dest = graph.node(2);

  auto result = Node::CreateEdge(q, src, dest);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(src->child_sources().size(), 0u);
  ASSERT_EQ(src->child_dests().size(), 1u);

  auto src_child = std::static_pointer_cast<FakeNode>(src->child_dests()[0]);
  EXPECT_EQ(src_child->dest(), dest);
  EXPECT_THAT(dest->sources(), ElementsAre(src_child));

  EXPECT_EQ(src_child->pipeline_stage_thread(), fake_thread_);
  EXPECT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterCreate(q, src_child->fake_pipeline_stage(), dest->fake_pipeline_stage());
}

TEST_F(NodeCreateEdgeTest, MetaToMetaSourceTooManyOutputs) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes =
          {
              {1, {.source_children = {}, .dest_children = {}}},
              {2, {.source_children = {}, .dest_children = {}}},
          },
      .default_thread = detached_thread_,
  });

  auto src = graph.node(1);
  src->SetOnCreateNewChildDest([]() { return nullptr; });

  auto result = Node::CreateEdge(q, src, /*dest=*/graph.node(2));
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kSourceHasTooManyOutputs);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaDestTooManyInputs) {
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

  auto result = Node::CreateEdge(q, /*src=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kDestHasTooManyInputs);
}

TEST_F(NodeCreateEdgeTest, MetaToMetaDestRejectsSource) {
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
    child->SetOnCreateCanAcceptSource([](auto n) { return false; });
    return child;
  });

  auto result = Node::CreateEdge(q, /*src=*/graph.node(1), dest);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::CreateEdgeError::kIncompatibleFormats);
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

  auto result = Node::CreateEdge(q, /*src=*/graph.node(4), /*dest=*/graph.node(1));
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

  auto src = graph.node(1);
  auto dest = graph.node(2);

  dest->SetOnCreateNewChildSource([this, &graph, dest]() {
    auto child = graph.CreateOrdinaryNode(std::nullopt, dest);
    child->set_pipeline_stage_thread(fake_thread_);
    child->fake_pipeline_stage()->set_thread(fake_thread_);
    return child;
  });

  auto result = Node::CreateEdge(q, src, dest);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(src->child_sources().size(), 0u);
  ASSERT_EQ(src->child_dests().size(), 1u);
  ASSERT_EQ(dest->child_sources().size(), 1u);
  ASSERT_EQ(dest->child_dests().size(), 0u);

  auto src_child = std::static_pointer_cast<FakeNode>(src->child_dests()[0]);
  auto dest_child = std::static_pointer_cast<FakeNode>(dest->child_sources()[0]);

  EXPECT_EQ(src_child->dest(), dest_child);
  EXPECT_THAT(dest_child->sources(), ElementsAre(src_child));

  EXPECT_EQ(src_child->pipeline_stage_thread(), fake_thread_);
  EXPECT_EQ(dest_child->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterCreate(q, src_child->fake_pipeline_stage(),
                                 dest_child->fake_pipeline_stage());
}

class NodeDeleteEdgeTest : public ::testing::Test {
 protected:
  void CheckPipelineStagesAfterDelete(GlobalTaskQueue& q, FakePipelineStagePtr src,
                                      FakePipelineStagePtr dest) {
    // The PipelineStages are updated asynchronously, by fake_thread_.
    // Initially, they are connected.
    EXPECT_THAT(src->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre(src));
    EXPECT_EQ(src->thread(), fake_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);

    // Still connected because fake_thread_ hasn't run yet.
    q.RunForThread(detached_thread_->id());
    EXPECT_THAT(src->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre(src));
    EXPECT_EQ(src->thread(), fake_thread_);
    EXPECT_EQ(dest->thread(), fake_thread_);

    // Finally, not connected.
    q.RunForThread(fake_thread_->id());
    EXPECT_THAT(src->sources(), ElementsAre());
    EXPECT_THAT(dest->sources(), ElementsAre());
    EXPECT_EQ(src->thread(), detached_thread_);
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
      Node::DeleteEdge(q, /*src=*/graph.node(1), /*dest=*/graph.node(2), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToOrdinaryConnectedBackwards) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 2}},
  });

  auto result =
      Node::DeleteEdge(q, /*src=*/graph.node(2), /*dest=*/graph.node(1), detached_thread_);
  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.error(), fuchsia_audio_mixer::DeleteEdgeError::kEdgeNotFound);
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToOrdinarySuccess) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .edges = {{1, 2}},
      .threads = {{fake_thread_, {1, 2}}},
  });

  auto src = graph.node(1);
  auto dest = graph.node(2);

  ASSERT_EQ(src->pipeline_stage_thread(), fake_thread_);
  ASSERT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  auto result = Node::DeleteEdge(q, src, dest, detached_thread_);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(src->dest(), nullptr);
  EXPECT_THAT(dest->sources(), ElementsAre());

  EXPECT_EQ(src->pipeline_stage_thread(), detached_thread_);
  EXPECT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterDelete(q, src->fake_pipeline_stage(), dest->fake_pipeline_stage());
}

TEST_F(NodeDeleteEdgeTest, OrdinaryToMetaNotConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{2, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {1},
  });

  auto result =
      Node::DeleteEdge(q, /*src=*/graph.node(1), /*dest=*/graph.node(2), detached_thread_);
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
      Node::DeleteEdge(q, /*src=*/graph.node(2), /*dest=*/graph.node(1), detached_thread_);
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

  auto src = graph.node(1);
  auto dest = graph.node(2);

  auto src_stage = src->fake_pipeline_stage();
  auto dest_stage =
      std::static_pointer_cast<FakeNode>(dest->child_sources()[0])->fake_pipeline_stage();

  auto result = Node::DeleteEdge(q, src, dest, detached_thread_);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(src->dest(), nullptr);
  EXPECT_EQ(src->pipeline_stage_thread(), detached_thread_);
  EXPECT_EQ(dest->child_sources().size(), 0u);
  EXPECT_EQ(dest->child_dests().size(), 0u);

  CheckPipelineStagesAfterDelete(q, src_stage, dest_stage);
}

TEST_F(NodeDeleteEdgeTest, MetaToOrdinaryNotConnected) {
  GlobalTaskQueue q;
  FakeGraph graph({
      .meta_nodes = {{1, {.source_children = {}, .dest_children = {}}}},
      .unconnected_ordinary_nodes = {2},
  });

  auto result =
      Node::DeleteEdge(q, /*src=*/graph.node(1), /*dest=*/graph.node(2), detached_thread_);
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
      Node::DeleteEdge(q, /*src=*/graph.node(2), /*dest=*/graph.node(1), detached_thread_);
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

  auto src = graph.node(1);
  auto dest = graph.node(2);

  auto src_stage = std::static_pointer_cast<FakeNode>(src->child_dests()[0])->fake_pipeline_stage();
  auto dest_stage = dest->fake_pipeline_stage();

  auto result = Node::DeleteEdge(q, src, dest, detached_thread_);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(src->child_sources().size(), 0u);
  EXPECT_EQ(src->child_dests().size(), 0u);
  EXPECT_EQ(dest->sources().size(), 0u);
  EXPECT_EQ(dest->pipeline_stage_thread(), fake_thread_);

  CheckPipelineStagesAfterDelete(q, src_stage, dest_stage);
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
      Node::DeleteEdge(q, /*src=*/graph.node(1), /*dest=*/graph.node(2), detached_thread_);
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
      Node::DeleteEdge(q, /*src=*/graph.node(2), /*dest=*/graph.node(1), detached_thread_);
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

  auto src = graph.node(1);
  auto dest = graph.node(2);

  auto src_stage = std::static_pointer_cast<FakeNode>(src->child_dests()[0])->fake_pipeline_stage();
  auto dest_stage =
      std::static_pointer_cast<FakeNode>(dest->child_sources()[0])->fake_pipeline_stage();

  auto result = Node::DeleteEdge(q, src, dest, detached_thread_);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(src->child_sources().size(), 0u);
  EXPECT_EQ(src->child_dests().size(), 0u);
  EXPECT_EQ(dest->child_sources().size(), 0u);
  EXPECT_EQ(dest->child_dests().size(), 0u);

  CheckPipelineStagesAfterDelete(q, src_stage, dest_stage);
}

}  // namespace
}  // namespace media_audio
