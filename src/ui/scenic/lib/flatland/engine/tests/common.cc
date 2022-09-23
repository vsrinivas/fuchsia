// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/tests/common.h"

#include <lib/ui/scenic/cpp/view_identity.h>

using ::testing::_;
using ::testing::Return;

using allocation::ImageMetadata;
using flatland::LinkSystem;
using flatland::Renderer;
using flatland::TransformGraph;
using flatland::TransformHandle;
using flatland::UberStruct;
using flatland::UberStructSystem;
using fuchsia::ui::composition::ChildViewStatus;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;

namespace flatland {

DisplayCompositorTestBase::FakeFlatlandSession::LinkToChild
DisplayCompositorTestBase::FakeFlatlandSession::CreateView(FakeFlatlandSession& parent_session) {
  // Create the tokens.
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  EXPECT_EQ(zx::channel::create(0, &parent_token.value, &child_token.value), ZX_OK);

  // Create the parent link.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  LinkSystem::LinkToParent link_to_parent = link_system_->CreateLinkToParent(
      dispatcher_holder_, std::move(child_token), scenic::NewViewIdentityOnCreation(),
      parent_viewport_watcher.NewRequest(), graph_.CreateTransform(),
      [](const std::string& error_log) { GTEST_FAIL() << error_log; });

  // Create the child link.
  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size(fuchsia::math::SizeU{1, 2});
  properties.set_inset({0, 0, 0, 0});
  LinkSystem::LinkToChild link_to_child = link_system_->CreateLinkToChild(
      dispatcher_holder_, std::move(parent_token), std::move(properties),
      child_view_watcher.NewRequest(), parent_session.graph_.CreateTransform(),
      [](const std::string& error_log) { GTEST_FAIL() << error_log; });

  // Run the loop to establish the link.
  harness_->RunLoopUntilIdle();

  link_to_parent_ = LinkToParent({
      .parent_viewport_watcher = std::move(parent_viewport_watcher),
      .link_to_parent = std::move(link_to_parent),
  });

  return LinkToChild{
      .child_view_watcher = std::move(child_view_watcher),
      .link_to_child = std::move(link_to_child),
  };
}

std::unique_ptr<UberStruct>
DisplayCompositorTestBase::FakeFlatlandSession::CreateUberStructWithCurrentTopology(
    TransformHandle local_root) {
  auto uber_struct = std::make_unique<UberStruct>();

  // Only use the supplied |local_root| if no there is no LinkToParent, otherwise use the
  // |child_transform_handle| from the LinkToParent.
  const TransformHandle root = link_to_parent_.has_value()
                                   ? link_to_parent_.value().link_to_parent.child_transform_handle
                                   : local_root;

  // Compute the local topology and place it in the UberStruct.
  auto local_topology_data = graph_.ComputeAndCleanup(root, std::numeric_limits<uint64_t>::max());
  EXPECT_NE(local_topology_data.iterations, std::numeric_limits<uint64_t>::max());
  EXPECT_TRUE(local_topology_data.cyclical_edges.empty());

  uber_struct->local_topology = local_topology_data.sorted_transforms;

  return uber_struct;
}

// Pushes |uber_struct| to the UberStructSystem and updates the system so that it represents
// this session in the InstanceMap.
void DisplayCompositorTestBase::FakeFlatlandSession::PushUberStruct(
    std::unique_ptr<UberStruct> uber_struct) {
  EXPECT_FALSE(uber_struct->local_topology.empty());
  EXPECT_EQ(uber_struct->local_topology[0].handle.GetInstanceId(), id_);

  queue_->Push(/*present_id=*/0, std::move(uber_struct));
  uber_struct_system_->UpdateSessions({{id_, 0}});
}

}  // namespace flatland
