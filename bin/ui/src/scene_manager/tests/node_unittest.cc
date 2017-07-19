// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/src/scene_manager/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene_manager/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene_manager/tests/session_test.h"

#include "gtest/gtest.h"

namespace mozart {
namespace scene {
namespace test {

using NodeTest = SessionTest;

TEST_F(NodeTest, Tagging) {
  const ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(kNodeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_EQ(0u, shape_node->tag_value());
  EXPECT_TRUE(Apply(NewSetTagOp(kNodeId, 42u)));
  EXPECT_EQ(42u, shape_node->tag_value());
  EXPECT_TRUE(Apply(NewSetTagOp(kNodeId, 0u)));
  EXPECT_EQ(0u, shape_node->tag_value());
}

TEST_F(NodeTest, ShapeNodeMaterialAndShape) {
  const ResourceId kNodeId = 1;
  const ResourceId kMaterialId = 2;
  const ResourceId kShapeId = 3;

  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(kNodeId)));
  EXPECT_TRUE(Apply(NewCreateMaterialOp(kMaterialId)));
  EXPECT_TRUE(Apply(NewSetTextureOp(kMaterialId, 0)));
  EXPECT_TRUE(Apply(NewSetColorOp(kMaterialId, 255, 100, 100, 255)));
  EXPECT_TRUE(Apply(NewCreateCircleOp(kShapeId, 50.f)));
  EXPECT_TRUE(Apply(NewSetMaterialOp(kNodeId, kMaterialId)));
  EXPECT_TRUE(Apply(NewSetShapeOp(kNodeId, kShapeId)));
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
  const ResourceId kChildNodeId = 1;
  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(kChildNodeId)));
  auto child_node = FindResource<Node>(kChildNodeId);

  // OK to detach a child that hasn't been attached.
  EXPECT_TRUE(Apply(NewDetachOp(kChildNodeId)));

  const ResourceId kEntityNodeId = 10;
  const ResourceId kShapeNodeId = 11;
  // TODO: const ResourceId kClipNodeId = 12;
  EXPECT_TRUE(Apply(NewCreateEntityNodeOp(kEntityNodeId)));
  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(kShapeNodeId)));
  // TODO: EXPECT_TRUE(Apply(NewCreateClipNodeOp(kClipNodeId)));
  auto entity_node = FindResource<EntityNode>(kEntityNodeId);
  auto shape_node = FindResource<ShapeNode>(kShapeNodeId);
  // auto clip_node = FindResource<ClipNode>(kClipNodeId);

  // We expect to be able to add children to these types.
  EXPECT_TRUE(Apply(NewAddChildOp(kEntityNodeId, kChildNodeId)));
  EXPECT_EQ(entity_node.get(), child_node->parent());
  EXPECT_TRUE(Apply(NewDetachOp(kChildNodeId)));
  // EXPECT_TRUE(Apply(NewDetachOp(kChildNodeId)));

  // We do not expect to be able to add children to these types.
  // TODO:
  // EXPECT_FALSE(Apply(NewAddChildOp(kClipNodeId, kChildNodeId)));
  // EXPECT_EQ(nullptr, child_node->parent());
  // EXPECT_EQ(nullptr, child_node->parent());
  EXPECT_FALSE(Apply(NewAddChildOp(kShapeNodeId, kChildNodeId)));
  EXPECT_EQ(nullptr, child_node->parent());
}

TEST_F(NodeTest, SettingHitTestBehavior) {
  const ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(kNodeId)));

  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_EQ(mozart2::HitTestBehavior::kDefault,
            shape_node->hit_test_behavior());

  EXPECT_TRUE(Apply(
      NewSetHitTestBehaviorOp(kNodeId, mozart2::HitTestBehavior::kSuppress)));
  EXPECT_EQ(mozart2::HitTestBehavior::kSuppress,
            shape_node->hit_test_behavior());
}

}  // namespace test
}  // namespace scene
}  // namespace mozart
