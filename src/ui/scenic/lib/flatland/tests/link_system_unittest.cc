// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/link_system.h"

#include <memory>

#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/flatland/topology_system.h"

using flatland::LinkSystem;
using ChildLink = flatland::LinkSystem::ChildLink;
using ParentLink = flatland::LinkSystem::ParentLink;
using flatland::TopologySystem;
using flatland::TransformGraph;
using TopologyEntry = flatland::TransformGraph::TopologyEntry;
using flatland::TransformHandle;
using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;

// This is a macro so that, if the various test macros fail, we get a line number associated with a
// particular Present() call in a unit test.
#define EXPECT_TOPOLOGY_VECTOR(topology_system, handle, expected_topology_vector) \
  {                                                                               \
    auto topology_vector = topology_system->ComputeGlobalTopologyVector(handle);  \
    EXPECT_EQ(topology_vector.size(), expected_topology_vector.size());           \
    for (size_t i = 0; i < topology_vector.size(); ++i) {                         \
      EXPECT_EQ(topology_vector[i], expected_topology_vector[i]);                 \
    }                                                                             \
  }

namespace flatland {
namespace test {

class LinkSystemTest : public gtest::TestLoopFixture {
 public:
  LinkSystemTest()
      : topology_system_(std::make_shared<TopologySystem>()),
        root_graph_(topology_system_->CreateGraph()),
        root_handle_(root_graph_.CreateTransform()) {}

  std::shared_ptr<LinkSystem> CreateLinkSystem() {
    return std::make_shared<LinkSystem>(topology_system_);
  }

  void ConnectToRootGraph(TransformHandle handle) {
    topology_system_->SetLocalTopology({{root_handle_, 0}, {handle, 0}});
  }

  void TearDown() override {
    topology_system_->ClearLocalTopology(root_handle_);
    EXPECT_EQ(topology_system_->GetSize(), 0u);
  }

  const std::shared_ptr<TopologySystem> topology_system_;
  TransformGraph root_graph_;
  TransformHandle root_handle_;
};

TEST_F(LinkSystemTest, UnresolvedGraphLinkDiesOnContentTokenDeath) {
  auto link_system = CreateLinkSystem();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ContentLink> content_link;
  ChildLink parent_link =
      link_system->CreateChildLink(std::move(parent_token), content_link.NewRequest());
  EXPECT_TRUE(parent_link.importer.valid());
  EXPECT_TRUE(content_link.is_bound());

  child_token.value.reset();
  RunLoopUntilIdle();

  EXPECT_FALSE(parent_link.importer.valid());
  EXPECT_FALSE(content_link.is_bound());
}

TEST_F(LinkSystemTest, UnresolvedContentLinkDiesOnGraphTokenDeath) {
  auto link_system = CreateLinkSystem();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<GraphLink> graph_link;
  ParentLink child_link =
      link_system->CreateParentLink(std::move(child_token), graph_link.NewRequest());
  EXPECT_TRUE(child_link.exporter.valid());
  EXPECT_TRUE(graph_link.is_bound());

  parent_token.value.reset();
  RunLoopUntilIdle();

  EXPECT_FALSE(child_link.exporter.valid());
  EXPECT_FALSE(graph_link.is_bound());
}

TEST_F(LinkSystemTest, ResolvedLinkCreatesLinkTopology) {
  auto link_system = CreateLinkSystem();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<GraphLink> graph_link;
  ParentLink parent_link =
      link_system->CreateParentLink(std::move(child_token), graph_link.NewRequest());
  EXPECT_TRUE(parent_link.exporter.valid());
  EXPECT_TRUE(graph_link.is_bound());

  fidl::InterfacePtr<ContentLink> content_link;
  ChildLink child_link =
      link_system->CreateChildLink(std::move(parent_token), content_link.NewRequest());
  EXPECT_TRUE(child_link.importer.valid());
  EXPECT_TRUE(content_link.is_bound());

  EXPECT_TOPOLOGY_VECTOR(topology_system_, child_link.link_handle,
                         std::vector<TopologyEntry>({
                             {.handle = child_link.link_handle, .parent_index = 0u},
                             {.handle = parent_link.link_handle, .parent_index = 0u},
                         }));
}

TEST_F(LinkSystemTest, ChildLinkDeathDestroysTopology) {
  auto link_system = CreateLinkSystem();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<GraphLink> graph_link;
  ParentLink parent_link =
      link_system->CreateParentLink(std::move(child_token), graph_link.NewRequest());

  TransformHandle child_link_handle;

  {
    fidl::InterfacePtr<ContentLink> content_link;
    ChildLink child_link =
        link_system->CreateChildLink(std::move(parent_token), content_link.NewRequest());
    child_link_handle = child_link.link_handle;

    ConnectToRootGraph(child_link_handle);

    EXPECT_TOPOLOGY_VECTOR(topology_system_, root_handle_,
                           std::vector<TopologyEntry>({
                               {.handle = root_handle_, .parent_index = 0u},
                               {.handle = child_link.link_handle, .parent_index = 0u},
                               {.handle = parent_link.link_handle, .parent_index = 1u},
                           }));

    // |child_link| dies here, which destroys the link topology.
  }

  // child_link_handle was added to the root graph, but the parent_link.link_handle should not be
  // reachable from that graph.
  EXPECT_TOPOLOGY_VECTOR(topology_system_, root_handle_,
                         std::vector<TopologyEntry>({
                             {.handle = root_handle_, .parent_index = 0u},
                             {.handle = child_link_handle, .parent_index = 0u},
                         }));
}

TEST_F(LinkSystemTest, ParentLinkDeathDestroysTopology) {
  auto link_system = CreateLinkSystem();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ContentLink> content_link;
  ChildLink child_link =
      link_system->CreateChildLink(std::move(parent_token), content_link.NewRequest());

  ConnectToRootGraph(child_link.link_handle);

  {
    fidl::InterfacePtr<GraphLink> graph_link;
    ParentLink parent_link =
        link_system->CreateParentLink(std::move(child_token), graph_link.NewRequest());

    EXPECT_TOPOLOGY_VECTOR(topology_system_, root_handle_,
                           std::vector<TopologyEntry>({
                               {.handle = root_handle_, .parent_index = 0u},
                               {.handle = child_link.link_handle, .parent_index = 0u},
                               {.handle = parent_link.link_handle, .parent_index = 1u},
                           }));

    // |parent_link| dies here, which destroys the link topology.
  }

  // child_link.link_handle was added to the root graph, but the parent_link.link_handle should not
  // be reachable from that graph.
  EXPECT_TOPOLOGY_VECTOR(topology_system_, root_handle_,
                         std::vector<TopologyEntry>({
                             {.handle = root_handle_, .parent_index = 0u},
                             {.handle = child_link.link_handle, .parent_index = 0u},
                         }));
}

#undef EXPECT_TOPOLOGY_VECTOR

}  // namespace test
}  // namespace flatland
