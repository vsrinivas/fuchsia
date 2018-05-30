// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "lib/ui/scenic/fidl_helpers.h"

#include "gtest/gtest.h"

namespace scenic {
namespace gfx {
namespace test {

using NodeTest = SessionTest;

TEST_F(NodeTest, Tagging) {
  const scenic::ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(kNodeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_EQ(0u, shape_node->tag_value());
  EXPECT_TRUE(Apply(scenic_lib::NewSetTagCommand(kNodeId, 42u)));
  EXPECT_EQ(42u, shape_node->tag_value());
  EXPECT_TRUE(Apply(scenic_lib::NewSetTagCommand(kNodeId, 0u)));
  EXPECT_EQ(0u, shape_node->tag_value());
}

TEST_F(NodeTest, ShapeNodeMaterialAndShape) {
  const scenic::ResourceId kNodeId = 1;
  const scenic::ResourceId kMaterialId = 2;
  const scenic::ResourceId kShapeId = 3;

  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(kNodeId)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateMaterialCommand(kMaterialId)));
  EXPECT_TRUE(Apply(scenic_lib::NewSetTextureCommand(kMaterialId, 0)));
  EXPECT_TRUE(
      Apply(scenic_lib::NewSetColorCommand(kMaterialId, 255, 100, 100, 255)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateCircleCommand(kShapeId, 50.f)));
  EXPECT_TRUE(Apply(scenic_lib::NewSetMaterialCommand(kNodeId, kMaterialId)));
  EXPECT_TRUE(Apply(scenic_lib::NewSetShapeCommand(kNodeId, kShapeId)));
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
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(kChildNodeId)));
  auto child_node = FindResource<Node>(kChildNodeId);

  // OK to detach a child that hasn't been attached.
  EXPECT_TRUE(Apply(scenic_lib::NewDetachCommand(kChildNodeId)));

  const scenic::ResourceId kEntityNodeId = 10;
  const scenic::ResourceId kShapeNodeId = 11;
  // TODO: const scenic::ResourceId kClipNodeId = 12;
  EXPECT_TRUE(Apply(scenic_lib::NewCreateEntityNodeCommand(kEntityNodeId)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(kShapeNodeId)));
  // TODO:
  // EXPECT_TRUE(Apply(scenic_lib::NewCreateClipNodeCommand(kClipNodeId)));
  auto entity_node = FindResource<EntityNode>(kEntityNodeId);
  auto shape_node = FindResource<ShapeNode>(kShapeNodeId);
  // auto clip_node = FindResource<ClipNode>(kClipNodeId);

  // We expect to be able to add children to these types.
  EXPECT_TRUE(
      Apply(scenic_lib::NewAddChildCommand(kEntityNodeId, kChildNodeId)));
  EXPECT_EQ(entity_node.get(), child_node->parent());
  EXPECT_TRUE(Apply(scenic_lib::NewDetachCommand(kChildNodeId)));
  // EXPECT_TRUE(Apply(scenic_lib::NewDetachCommand(kChildNodeId)));

  // We do not expect to be able to add children to these types.
  // TODO:
  // EXPECT_FALSE(Apply(scenic_lib::NewAddChildCommand(kClipNodeId,
  // kChildNodeId))); EXPECT_EQ(nullptr, child_node->parent());
  // EXPECT_EQ(nullptr, child_node->parent());
  EXPECT_FALSE(
      Apply(scenic_lib::NewAddChildCommand(kShapeNodeId, kChildNodeId)));
  EXPECT_EQ(nullptr, child_node->parent());
}

TEST_F(NodeTest, SettingHitTestBehavior) {
  const scenic::ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(kNodeId)));

  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_EQ(::fuchsia::ui::gfx::HitTestBehavior::kDefault,
            shape_node->hit_test_behavior());

  EXPECT_TRUE(Apply(scenic_lib::NewSetHitTestBehaviorCommand(
      kNodeId, ::fuchsia::ui::gfx::HitTestBehavior::kSuppress)));
  EXPECT_EQ(::fuchsia::ui::gfx::HitTestBehavior::kSuppress,
            shape_node->hit_test_behavior());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
