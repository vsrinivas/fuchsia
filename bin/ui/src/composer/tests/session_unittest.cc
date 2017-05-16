// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/composer/session_helpers.h"
#include "apps/mozart/src/composer/resources/material.h"
#include "apps/mozart/src/composer/resources/nodes/shape_node.h"
#include "apps/mozart/src/composer/resources/shapes/circle_shape.h"
#include "apps/mozart/src/composer/tests/session_test.h"

#include "gtest/gtest.h"

namespace mozart {
namespace composer {
namespace test {

TEST_F(SessionTest, ResourceIdAlreadyUsed) {
  EXPECT_TRUE(Apply(NewCreateEntityNodeOp(1)));
  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(2)));
  ExpectLastReportedError(nullptr);
  EXPECT_FALSE(Apply(NewCreateShapeNodeOp(2)));
  ExpectLastReportedError(
      "composer::ResourceMap::AddResource(): resource with ID 2 already "
      "exists.");
}

TEST_F(SessionTest, AddAndRemoveResource) {
  EXPECT_TRUE(Apply(NewCreateEntityNodeOp(1)));
  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(2)));
  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(3)));
  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(4)));
  EXPECT_TRUE(Apply(NewAddChildOp(1, 2)));
  EXPECT_TRUE(Apply(NewAddPartOp(1, 3)));
  EXPECT_EQ(4U, session_->GetTotalResourceCount());
  EXPECT_EQ(4U, session_->GetMappedResourceCount());

  // Even though we release node 2 and 3, they continue to exist because they
  // referenced by node 1.  Only node 4 is destroyed.
  EXPECT_TRUE(Apply(NewReleaseResourceOp(2)));
  EXPECT_TRUE(Apply(NewReleaseResourceOp(3)));
  EXPECT_TRUE(Apply(NewReleaseResourceOp(4)));
  EXPECT_EQ(3U, session_->GetTotalResourceCount());
  EXPECT_EQ(1U, session_->GetMappedResourceCount());

  // Releasing node 1 causes nodes 1-3 to be destroyed.
  EXPECT_TRUE(Apply(NewReleaseResourceOp(1)));
  EXPECT_EQ(0U, session_->GetTotalResourceCount());
  EXPECT_EQ(0U, session_->GetMappedResourceCount());
}

TEST_F(SessionTest, ShapeNodeMaterialAndShape) {
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

TEST_F(SessionTest, CreateLink) {
  // This fails because the eventpair is null.
  EXPECT_FALSE(Apply(NewCreateLinkOp(1, mx::eventpair())));

  mx::eventpair e1a, e1b;
  EXPECT_EQ(NO_ERROR, mx::eventpair::create(0, &e1a, &e1b));
  EXPECT_TRUE(Apply(NewCreateLinkOp(2, std::move(e1a))));
  // TODO: test attaching things to the link.
  // TODO: test that we can only look up a link via a pre-registered eventpair.
}

// TODO:
// - test that FindResource() cannot return resources that have the wrong type.

}  // namespace test
}  // namespace composer
}  // namespace mozart
