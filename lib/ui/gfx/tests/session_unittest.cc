// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/material.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "lib/ui/scenic/fidl_helpers.h"

#include "gtest/gtest.h"

namespace scenic {
namespace gfx {
namespace test {

TEST_F(SessionTest, ScheduleUpdateOutOfOrder) {
  fuchsia::ui::scenic::Session::PresentCallback callback = [](auto) {};
  EXPECT_TRUE(
      session_->ScheduleUpdate(1, std::vector<::fuchsia::ui::gfx::Command>(),
                               ::fidl::VectorPtr<zx::event>(),
                               ::fidl::VectorPtr<zx::event>(), callback));
  EXPECT_FALSE(
      session_->ScheduleUpdate(0, std::vector<::fuchsia::ui::gfx::Command>(),
                               ::fidl::VectorPtr<zx::event>(),
                               ::fidl::VectorPtr<zx::event>(), callback));
  ExpectLastReportedError(
      "scenic::gfx::Session: Present called with out-of-order presentation "
      "time. requested presentation time=0, last scheduled presentation "
      "time=1.");
}

TEST_F(SessionTest, ScheduleUpdateInOrder) {
  fuchsia::ui::scenic::Session::PresentCallback callback = [](auto) {};
  EXPECT_TRUE(
      session_->ScheduleUpdate(1, std::vector<::fuchsia::ui::gfx::Command>(),
                               ::fidl::VectorPtr<zx::event>(),
                               ::fidl::VectorPtr<zx::event>(), callback));
  EXPECT_TRUE(
      session_->ScheduleUpdate(1, std::vector<::fuchsia::ui::gfx::Command>(),
                               ::fidl::VectorPtr<zx::event>(),
                               ::fidl::VectorPtr<zx::event>(), callback));
  ExpectLastReportedError(nullptr);
}

TEST_F(SessionTest, ResourceIdAlreadyUsed) {
  EXPECT_TRUE(Apply(scenic_lib::NewCreateEntityNodeCommand(1)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(2)));
  ExpectLastReportedError(nullptr);
  EXPECT_FALSE(Apply(scenic_lib::NewCreateShapeNodeCommand(2)));
  ExpectLastReportedError(
      "scenic::gfx::ResourceMap::AddResource(): resource with ID 2 already "
      "exists.");
}

TEST_F(SessionTest, AddAndRemoveResource) {
  EXPECT_TRUE(Apply(scenic_lib::NewCreateEntityNodeCommand(1)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(2)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(3)));
  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(4)));
  EXPECT_TRUE(Apply(scenic_lib::NewAddChildCommand(1, 2)));
  EXPECT_TRUE(Apply(scenic_lib::NewAddPartCommand(1, 3)));
  EXPECT_EQ(4U, session_->GetTotalResourceCount());
  EXPECT_EQ(4U, session_->GetMappedResourceCount());

  // Even though we release node 2 and 3, they continue to exist because they
  // referenced by node 1.  Only node 4 is destroyed.
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceCommand(2)));
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceCommand(3)));
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceCommand(4)));
  EXPECT_EQ(3U, session_->GetTotalResourceCount());
  EXPECT_EQ(1U, session_->GetMappedResourceCount());

  // Releasing node 1 causes nodes 1-3 to be destroyed.
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceCommand(1)));
  EXPECT_EQ(0U, session_->GetTotalResourceCount());
  EXPECT_EQ(0U, session_->GetMappedResourceCount());
}

TEST_F(SessionTest, Labeling) {
  const scenic::ResourceId kNodeId = 1;
  const std::string kShortLabel = "test!";
  const std::string kLongLabel =
      std::string(::fuchsia::ui::gfx::kLabelMaxLength, 'x');
  const std::string kTooLongLabel =
      std::string(::fuchsia::ui::gfx::kLabelMaxLength + 1, '?');

  EXPECT_TRUE(Apply(scenic_lib::NewCreateShapeNodeCommand(kNodeId)));
  auto shape_node = FindResource<ShapeNode>(kNodeId);
  EXPECT_TRUE(shape_node->label().empty());
  EXPECT_TRUE(Apply(scenic_lib::NewSetLabelCommand(kNodeId, kShortLabel)));
  EXPECT_EQ(kShortLabel, shape_node->label());
  EXPECT_TRUE(Apply(scenic_lib::NewSetLabelCommand(kNodeId, kLongLabel)));
  EXPECT_EQ(kLongLabel, shape_node->label());
  EXPECT_TRUE(Apply(scenic_lib::NewSetLabelCommand(kNodeId, kTooLongLabel)));
  EXPECT_EQ(kTooLongLabel.substr(0, ::fuchsia::ui::gfx::kLabelMaxLength),
            shape_node->label());
  EXPECT_TRUE(Apply(scenic_lib::NewSetLabelCommand(kNodeId, "")));
  EXPECT_TRUE(shape_node->label().empty());

  // Bypass the truncation performed by session helpers.
  shape_node->SetLabel(kTooLongLabel);
  EXPECT_EQ(kTooLongLabel.substr(0, ::fuchsia::ui::gfx::kLabelMaxLength),
            shape_node->label());
}

// TODO:
// - test that FindResource() cannot return resources that have the wrong type.

}  // namespace test
}  // namespace gfx
}  // namespace scenic
