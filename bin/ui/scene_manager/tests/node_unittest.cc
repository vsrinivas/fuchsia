// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scenic/fidl_helpers.h"
#include "apps/mozart/src/scene_manager/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene_manager/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene_manager/tests/session_test.h"

#include "gtest/gtest.h"

namespace scene_manager {
namespace test {

using NodeTest = SessionTest;

TEST_F(NodeTest, Tagging) {
  const scenic::ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(kNodeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_EQ(0u, shape_node->tag_value());
  EXPECT_TRUE(Apply(scenic_lib::NewSetTagOp(kNodeId, 42u)));
  EXPECT_EQ(42u, shape_node->tag_value());
  EXPECT_TRUE(Apply(scenic_lib::NewSetTagOp(kNodeId, 0u)));
  EXPECT_EQ(0u, shape_node->tag_value());
}

TEST_F(NodeTest, ShapeNodeMaterialAndShape) {
  const scenic::ResourceId kNodeId = 1;
  const scenic::ResourceId kMaterialId = 2;
  const scenic::ResourceId kShapeId = 3;

  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(kNodeId)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateMaterialOp(kMaterialId)));
  EXPECT_TRUE(Apply(scenic_lib::NewSetTextureOp(kMaterialId, 0)));
  EXPECT_TRUE(
      Apply(scenic_lib::NewSetColorOp(kMaterialId, 255, 100, 100, 255)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateCircleOp(kShapeId, 50.f)));
  EXPECT_TRUE(Apply(scenic_lib::NewSetMaterialOp(kNodeId, kMaterialId)));
  EXPECT_TRUE(Apply(scenic_lib::NewSetShapeOp(kNodeId, kShapeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  auto material = FindResource<Material>(kMaterialId);
  auto circle = FindResource<Shape>(kShapeId);
  ASSERT_NE(nullptr, shape_node.get());
  ASSERT_NE(nullptr, material.get());
  ASSERT_NE(nullptr, circle.get());
  EXPECT_EQ(shape_node->material(), material);
  EXPECT_EQ(shape_node->shape(), circle);
}

TEST_F(NodeTest, NodesWithChildren) {
  // Child node that we will attach to various types of nodes.
  const scenic::ResourceId kChildNodeId = 1;
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(kChildNodeId)));
  auto child_node = FindResource<Node>(kChildNodeId);

  // OK to detach a child that hasn't been attached.
  EXPECT_TRUE(Apply(scenic_lib::NewDetachOp(kChildNodeId)));

  const scenic::ResourceId kEntityNodeId = 10;
  const scenic::ResourceId kShapeNodeId = 11;
  // TODO: const scenic::ResourceId kClipNodeId = 12;
  EXPECT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(kEntityNodeId)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(kShapeNodeId)));
  // TODO: EXPECT_TRUE(Apply(scenic_lib::NewCreateClipNodeOp(kClipNodeId)));
  auto entity_node = FindResource<EntityNode>(kEntityNodeId);
  auto shape_node = FindResource<ShapeNode>(kShapeNodeId);
  // auto clip_node = FindResource<ClipNode>(kClipNodeId);

  // We expect to be able to add children to these types.
  EXPECT_TRUE(Apply(scenic_lib::NewAddChildOp(kEntityNodeId, kChildNodeId)));
  EXPECT_EQ(entity_node.get(), child_node->parent());
  EXPECT_TRUE(Apply(scenic_lib::NewDetachOp(kChildNodeId)));
  // EXPECT_TRUE(Apply(scenic_lib::NewDetachOp(kChildNodeId)));

  // We do not expect to be able to add children to these types.
  // TODO:
  // EXPECT_FALSE(Apply(scenic_lib::NewAddChildOp(kClipNodeId, kChildNodeId)));
  // EXPECT_EQ(nullptr, child_node->parent());
  // EXPECT_EQ(nullptr, child_node->parent());
  EXPECT_FALSE(Apply(scenic_lib::NewAddChildOp(kShapeNodeId, kChildNodeId)));
  EXPECT_EQ(nullptr, child_node->parent());
}

TEST_F(NodeTest, SettingHitTestBehavior) {
  const scenic::ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(kNodeId)));

  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_EQ(scenic::HitTestBehavior::kDefault, shape_node->hit_test_behavior());

  EXPECT_TRUE(Apply(scenic_lib::NewSetHitTestBehaviorOp(
      kNodeId, scenic::HitTestBehavior::kSuppress)));
  EXPECT_EQ(scenic::HitTestBehavior::kSuppress,
            shape_node->hit_test_behavior());
}

}  // namespace test
}  // namespace scene_manager
