// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/tests/common.h"

using ::testing::_;
using ::testing::Return;

using flatland::ImageMetadata;
using flatland::LinkSystem;
using flatland::Renderer;
using flatland::TransformGraph;
using flatland::TransformHandle;
using flatland::UberStruct;
using flatland::UberStructSystem;
using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkStatus;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::LayoutInfo;
using fuchsia::ui::scenic::internal::LinkProperties;

namespace flatland {

EngineTestBase::FakeFlatlandSession::ChildLink EngineTestBase::FakeFlatlandSession::LinkToParent(
    FakeFlatlandSession& parent_session) {
  // Create the tokens.
  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  EXPECT_EQ(zx::eventpair::create(0, &parent_token.value, &child_token.value), ZX_OK);

  // Create the parent link.
  fidl::InterfacePtr<GraphLink> graph_link;
  LinkSystem::ParentLink parent_link = link_system_->CreateParentLink(
      std::move(child_token), graph_link.NewRequest(), graph_.CreateTransform());

  // Create the child link.
  fidl::InterfacePtr<ContentLink> content_link;
  LinkSystem::ChildLink child_link = link_system_->CreateChildLink(
      std::move(parent_token), LinkProperties(), content_link.NewRequest(),
      parent_session.graph_.CreateTransform());

  // Run the loop to establish the link.
  harness_->RunLoopUntilIdle();

  parent_link_ = ParentLink({
      .graph_link = std::move(graph_link),
      .parent_link = std::move(parent_link),
  });

  return ChildLink({
      .content_link = std::move(content_link),
      .child_link = std::move(child_link),
  });
}

std::unique_ptr<UberStruct>
EngineTestBase::FakeFlatlandSession::CreateUberStructWithCurrentTopology(
    TransformHandle local_root) {
  auto uber_struct = std::make_unique<UberStruct>();

  // Only use the supplied |local_root| if no there is no ParentLink, otherwise use the
  // |link_origin| from the ParentLink.
  const TransformHandle root =
      parent_link_.has_value() ? parent_link_.value().parent_link.link_origin : local_root;

  // Compute the local topology and place it in the UberStruct.
  auto local_topology_data = graph_.ComputeAndCleanup(root, std::numeric_limits<uint64_t>::max());
  EXPECT_NE(local_topology_data.iterations, std::numeric_limits<uint64_t>::max());
  EXPECT_TRUE(local_topology_data.cyclical_edges.empty());

  uber_struct->local_topology = local_topology_data.sorted_transforms;

  return uber_struct;
}

// Pushes |uber_struct| to the UberStructSystem and updates the system so that it represents
// this session in the InstanceMap.
void EngineTestBase::FakeFlatlandSession::PushUberStruct(std::unique_ptr<UberStruct> uber_struct) {
  EXPECT_FALSE(uber_struct->local_topology.empty());
  EXPECT_EQ(uber_struct->local_topology[0].handle.GetInstanceId(), id_);

  queue_->Push(/*present_id=*/0, std::move(uber_struct));
  uber_struct_system_->UpdateSessions({{id_, 0}});
}

}  // namespace flatland
