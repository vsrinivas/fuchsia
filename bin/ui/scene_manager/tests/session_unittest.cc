// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/material.h"
#include "garnet/bin/ui/scene_manager/resources/nodes/shape_node.h"
#include "garnet/bin/ui/scene_manager/resources/shapes/circle_shape.h"
#include "garnet/bin/ui/scene_manager/tests/session_test.h"
#include "lib/ui/scenic/fidl_helpers.h"

#include "gtest/gtest.h"

namespace scene_manager {
namespace test {

TEST_F(SessionTest, ScheduleUpdateOutOfOrder) {
  scenic::Session::PresentCallback callback = [](auto) {};
  EXPECT_TRUE(session_->ScheduleUpdate(1, ::fidl::Array<scenic::OpPtr>(),
                                       ::fidl::Array<zx::event>(),
                                       ::fidl::Array<zx::event>(), callback));
  EXPECT_FALSE(session_->ScheduleUpdate(0, ::fidl::Array<scenic::OpPtr>(),
                                        ::fidl::Array<zx::event>(),
                                        ::fidl::Array<zx::event>(), callback));
  ExpectLastReportedError(
      "scene_manager::Session: Present called with out-of-order presentation "
      "time. presentation_time=0, last scheduled presentation time=1.");
}

TEST_F(SessionTest, ScheduleUpdateInOrder) {
  scenic::Session::PresentCallback callback = [](auto) {};
  EXPECT_TRUE(session_->ScheduleUpdate(1, ::fidl::Array<scenic::OpPtr>(),
                                       ::fidl::Array<zx::event>(),
                                       ::fidl::Array<zx::event>(), callback));
  EXPECT_TRUE(session_->ScheduleUpdate(1, ::fidl::Array<scenic::OpPtr>(),
                                       ::fidl::Array<zx::event>(),
                                       ::fidl::Array<zx::event>(), callback));
  ExpectLastReportedError(nullptr);
}

TEST_F(SessionTest, ResourceIdAlreadyUsed) {
  EXPECT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(1)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(2)));
  ExpectLastReportedError(nullptr);
  EXPECT_FALSE(Apply(scenic_lib::NewCreateShapeNodeOp(2)));
  ExpectLastReportedError(
      "scene_manager::ResourceMap::AddResource(): resource with ID 2 already "
      "exists.");
}

TEST_F(SessionTest, AddAndRemoveResource) {
  EXPECT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(1)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(2)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(3)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(4)));
  EXPECT_TRUE(Apply(scenic_lib::NewAddChildOp(1, 2)));
  EXPECT_TRUE(Apply(scenic_lib::NewAddPartOp(1, 3)));
  EXPECT_EQ(4U, session_->GetTotalResourceCount());
  EXPECT_EQ(4U, session_->GetMappedResourceCount());

  // Even though we release node 2 and 3, they continue to exist because they
  // referenced by node 1.  Only node 4 is destroyed.
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceOp(2)));
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceOp(3)));
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceOp(4)));
  EXPECT_EQ(3U, session_->GetTotalResourceCount());
  EXPECT_EQ(1U, session_->GetMappedResourceCount());

  // Releasing node 1 causes nodes 1-3 to be destroyed.
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceOp(1)));
  EXPECT_EQ(0U, session_->GetTotalResourceCount());
  EXPECT_EQ(0U, session_->GetMappedResourceCount());
}

TEST_F(SessionTest, Labeling) {
  const scenic::ResourceId kNodeId = 1;
  const std::string kShortLabel = "test!";
  const std::string kLongLabel = std::string(scenic::kLabelMaxLength, 'x');
  const std::string kTooLongLabel =
      std::string(scenic::kLabelMaxLength + 1, '?');

  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeOp(kNodeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_TRUE(shape_node->label().empty());
  EXPECT_TRUE(Apply(scenic_lib::NewSetLabelOp(kNodeId, kShortLabel)));
  EXPECT_EQ(kShortLabel, shape_node->label());
  EXPECT_TRUE(Apply(scenic_lib::NewSetLabelOp(kNodeId, kLongLabel)));
  EXPECT_EQ(kLongLabel, shape_node->label());
  EXPECT_TRUE(Apply(scenic_lib::NewSetLabelOp(kNodeId, kTooLongLabel)));
  EXPECT_EQ(kTooLongLabel.substr(0, scenic::kLabelMaxLength),
            shape_node->label());
  EXPECT_TRUE(Apply(scenic_lib::NewSetLabelOp(kNodeId, "")));
  EXPECT_TRUE(shape_node->label().empty());

  // Bypass the truncation performed by session helpers.
  shape_node->SetLabel(kTooLongLabel);
  EXPECT_EQ(kTooLongLabel.substr(0, scenic::kLabelMaxLength),
            shape_node->label());
}

// TODO:
// - test that FindResource() cannot return resources that have the wrong type.

}  // namespace test
}  // namespace scene_manager
