// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "gtest/gtest.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using NodeTest = SessionTest;

TEST_F(NodeTest, Tagging) {
  const ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kNodeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_EQ(0u, shape_node->tag_value());
  EXPECT_TRUE(Apply(scenic::NewSetTagCmd(kNodeId, 42u)));
  EXPECT_EQ(42u, shape_node->tag_value());
  EXPECT_TRUE(Apply(scenic::NewSetTagCmd(kNodeId, 0u)));
  EXPECT_EQ(0u, shape_node->tag_value());
}

TEST_F(NodeTest, ShapeNodeMaterialAndShape) {
  const ResourceId kNodeId = 1;
  const ResourceId kMaterialId = 2;
  const ResourceId kShapeId = 3;

  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kNodeId)));
  EXPECT_TRUE(Apply(scenic::NewCreateMaterialCmd(kMaterialId)));
  EXPECT_TRUE(Apply(scenic::NewSetTextureCmd(kMaterialId, 0)));
  EXPECT_TRUE(Apply(scenic::NewSetColorCmd(kMaterialId, 255, 100, 100, 255)));
  EXPECT_TRUE(Apply(scenic::NewCreateCircleCmd(kShapeId, 50.f)));
  EXPECT_TRUE(Apply(scenic::NewSetMaterialCmd(kNodeId, kMaterialId)));
  EXPECT_TRUE(Apply(scenic::NewSetShapeCmd(kNodeId, kShapeId)));
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
  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kChildNodeId)));
  auto child_node = FindResource<Node>(kChildNodeId);

  // OK to detach a child that hasn't been attached.
  EXPECT_TRUE(Apply(scenic::NewDetachCmd(kChildNodeId)));

  const ResourceId kEntityNodeId = 10;
  const ResourceId kShapeNodeId = 11;
  // TODO: const ResourceId kClipNodeId = 12;
  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(kEntityNodeId)));
  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kShapeNodeId)));
  // TODO:
  // EXPECT_TRUE(Apply(scenic::NewCreateClipNodeCmd(kClipNodeId)));
  auto entity_node = FindResource<EntityNode>(kEntityNodeId);
  auto shape_node = FindResource<ShapeNode>(kShapeNodeId);
  // auto clip_node = FindResource<ClipNode>(kClipNodeId);

  // We expect to be able to add children to these types.
  EXPECT_TRUE(Apply(scenic::NewAddChildCmd(kEntityNodeId, kChildNodeId)));
  EXPECT_EQ(entity_node.get(), child_node->parent());
  EXPECT_TRUE(Apply(scenic::NewDetachCmd(kChildNodeId)));
  // EXPECT_TRUE(Apply(scenic::NewDetachCmd(kChildNodeId)));

  // We do not expect to be able to add children to these types.
  // TODO:
  // EXPECT_FALSE(Apply(scenic::NewAddChildCmd(kClipNodeId,
  // kChildNodeId))); EXPECT_EQ(nullptr, child_node->parent());
  // EXPECT_EQ(nullptr, child_node->parent());
  EXPECT_FALSE(Apply(scenic::NewAddChildCmd(kShapeNodeId, kChildNodeId)));
  EXPECT_EQ(nullptr, child_node->parent());
}

TEST_F(NodeTest, SettingHitTestBehavior) {
  const ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic::NewCreateShapeNodeCmd(kNodeId)));

  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_EQ(::fuchsia::ui::gfx::HitTestBehavior::kDefault, shape_node->hit_test_behavior());

  EXPECT_TRUE(Apply(
      scenic::NewSetHitTestBehaviorCmd(kNodeId, ::fuchsia::ui::gfx::HitTestBehavior::kSuppress)));
  EXPECT_EQ(::fuchsia::ui::gfx::HitTestBehavior::kSuppress, shape_node->hit_test_behavior());
}

TEST_F(NodeTest, SettingClipPlanes) {
  const ResourceId kNodeId = 1;

  EXPECT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(kNodeId)));

  auto node = FindResource<EntityNode>(kNodeId);
  EXPECT_EQ(0U, node->clip_planes().size());

  std::vector<fuchsia::ui::gfx::Plane3> planes;
  planes.push_back({.dir = {.x = 1.f, .y = 0.f, .z = 0.f}, .dist = -1.f});
  planes.push_back({.dir = {.x = 0.f, .y = 1.f, .z = 0.f}, .dist = -2.f});
  EXPECT_TRUE(Apply(scenic::NewSetClipPlanesCmd(kNodeId, planes)));
  EXPECT_EQ(2U, node->clip_planes().size());

  // Setting clip planes replaces the previous ones.
  planes.push_back({.dir = {.x = 0.f, .y = 0.f, .z = 1.f}, .dist = -3.f});
  EXPECT_TRUE(Apply(scenic::NewSetClipPlanesCmd(kNodeId, planes)));
  EXPECT_EQ(3U, node->clip_planes().size());

  // Verify the the planes have the values set by the Cmd.
  for (size_t i = 0; i < planes.size(); ++i) {
    EXPECT_EQ(planes[i].dir.x, node->clip_planes()[i].dir().x);
    EXPECT_EQ(planes[i].dir.y, node->clip_planes()[i].dir().y);
    EXPECT_EQ(planes[i].dir.z, node->clip_planes()[i].dir().z);
    EXPECT_EQ(planes[i].dist, node->clip_planes()[i].dist());
  }

  // Clear clip planes by setting empty vector of planes.
  EXPECT_TRUE(Apply(scenic::NewSetClipPlanesCmd(kNodeId, {})));
  EXPECT_EQ(0U, node->clip_planes().size());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
