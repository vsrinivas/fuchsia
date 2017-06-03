// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/composer/session_helpers.h"
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene/tests/session_test.h"

#include "gtest/gtest.h"

namespace mozart {
namespace composer {
namespace test {

typedef SessionTest NodeTest;

TEST_F(NodeTest, ShapeNodeMaterialAndShape) {
  const ResourceId kNodeId = 1;
  const ResourceId kMaterialId = 2;
  const ResourceId kShapeId = 3;

  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(kNodeId)));
  EXPECT_TRUE(Apply(NewCreateMaterialOp(kMaterialId, 0, 255, 100, 100, 255)));
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

  // TODO: const ResourceId kClipNodeId = 10;
  const ResourceId kEntityNodeId = 11;
  // TODO: const ResourceId kLinkNodeId = 12;
  const ResourceId kShapeNodeId = 13;
  // TODO: const ResourceId kTagNodeId = 14;

  // TODO: EXPECT_TRUE(Apply(NewCreateClipNodeOp(kClipNodeId)));
  EXPECT_TRUE(Apply(NewCreateEntityNodeOp(kEntityNodeId)));
  // TODO: EXPECT_TRUE(Apply(NewCreateLinkNodeOp(kLinkNodeId)));
  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(kShapeNodeId)));
  // TODO: EXPECT_TRUE(Apply(NewCreateTagNodeOp(kTagNodeId)));
  // auto clip_node = FindResource<ClipNode>(kClipNodeId);
  auto entity_node = FindResource<EntityNode>(kEntityNodeId);
  // TODO: auto link_node = FindResource<LinkNode>(kLinkNodeId);
  auto shape_node = FindResource<ShapeNode>(kShapeNodeId);
  // TODO: auto tag_node = FindResource<TagNode>(kTagNodeId);

  // We expect to be able to add children to these types.
  EXPECT_TRUE(Apply(NewAddChildOp(kEntityNodeId, kChildNodeId)));
  EXPECT_EQ(entity_node.get(), child_node->parent());
  EXPECT_TRUE(Apply(NewDetachOp(kChildNodeId)));
  // TODO:
  // EXPECT_TRUE(Apply(NewAddChildOp(kTagNodeId, kChildNodeId)));
  // EXPECT_EQ(tag_node.get(), child_node->parent());
  // EXPECT_TRUE(Apply(NewDetachOp(kChildNodeId)));

  // We do not expect to be able to add children to these types.
  // TODO:
  // EXPECT_FALSE(Apply(NewAddChildOp(kClipNodeId, kChildNodeId)));
  // EXPECT_EQ(nullptr, child_node->parent());
  // EXPECT_FALSE(Apply(NewAddChildOp(kLinkNodeId, kChildNodeId)));
  // EXPECT_EQ(nullptr, child_node->parent());
  EXPECT_FALSE(Apply(NewAddChildOp(kShapeNodeId, kChildNodeId)));
  EXPECT_EQ(nullptr, child_node->parent());
}

}  // namespace test
}  // namespace composer
}  // namespace mozart
