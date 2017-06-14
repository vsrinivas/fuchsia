// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/src/scene/resources/material.h"
#include "apps/mozart/src/scene/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene/resources/shapes/circle_shape.h"
#include "apps/mozart/src/scene/tests/session_test.h"

#include "gtest/gtest.h"

namespace mozart {
namespace scene {
namespace test {

TEST_F(SessionTest, ResourceIdAlreadyUsed) {
  EXPECT_TRUE(Apply(NewCreateEntityNodeOp(1)));
  EXPECT_TRUE(Apply(NewCreateShapeNodeOp(2)));
  ExpectLastReportedError(nullptr);
  EXPECT_FALSE(Apply(NewCreateShapeNodeOp(2)));
  ExpectLastReportedError(
      "scene::ResourceMap::AddResource(): resource with ID 2 already "
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

// TODO:
// - test that FindResource() cannot return resources that have the wrong type.

}  // namespace test
}  // namespace scene
}  // namespace mozart
