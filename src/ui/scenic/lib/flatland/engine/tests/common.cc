// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/tests/common.h"

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
using fuchsia::ui::composition::ViewCreationToken;
using fuchsia::ui::composition::ViewportCreationToken;
using fuchsia::ui::composition::ViewportProperties;

namespace flatland {

DisplayCompositorTestBase::FakeFlatlandSession::ChildLink
DisplayCompositorTestBase::FakeFlatlandSession::CreateView(FakeFlatlandSession& parent_session) {
  // Create the tokens.
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  EXPECT_EQ(zx::channel::create(0, &parent_token.value, &child_token.value), ZX_OK);

  // Create the parent link.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  LinkSystem::ParentLink parent_link = link_system_->CreateParentLink(
      dispatcher_holder_, std::move(child_token), parent_viewport_watcher.NewRequest(),
      graph_.CreateTransform(), [](const std::string& error_log) { GTEST_FAIL() << error_log; });

  // Create the child link.
  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size(fuchsia::math::SizeU{1, 2});
  LinkSystem::ChildLink child_link = link_system_->CreateChildLink(
      dispatcher_holder_, std::move(parent_token), std::move(properties),
      child_view_watcher.NewRequest(), parent_session.graph_.CreateTransform(),
      [](const std::string& error_log) { GTEST_FAIL() << error_log; });

  // Run the loop to establish the link.
  harness_->RunLoopUntilIdle();

  parent_link_ = ParentLink({
      .parent_viewport_watcher = std::move(parent_viewport_watcher),
      .parent_link = std::move(parent_link),
  });

  return ChildLink({
      .child_view_watcher = std::move(child_view_watcher),
      .child_link = std::move(child_link),
  });
}

std::unique_ptr<UberStruct>
DisplayCompositorTestBase::FakeFlatlandSession::CreateUberStructWithCurrentTopology(
    TransformHandle local_root) {
  auto uber_struct = std::make_unique<UberStruct>();

  // Only use the supplied |local_root| if no there is no ParentLink, otherwise use the
  // |link_origin| from the ParentLink.
  const TransformHandle root = parent_link_.has_value()
                                   ? parent_link_.value().parent_link.child_view_watcher_handle
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
