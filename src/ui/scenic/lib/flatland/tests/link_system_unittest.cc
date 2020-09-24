// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/link_system.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

using flatland::LinkSystem;
using ChildLink = flatland::LinkSystem::ChildLink;
using ParentLink = flatland::LinkSystem::ParentLink;
using flatland::TransformGraph;
using flatland::UberStructSystem;
using TopologyEntry = flatland::TransformGraph::TopologyEntry;
using flatland::TransformHandle;
using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::LayoutInfo;
using fuchsia::ui::scenic::internal::LinkProperties;
using fuchsia::ui::scenic::internal::Vec2;

namespace flatland {
namespace test {

class LinkSystemTest : public gtest::TestLoopFixture {
 public:
  LinkSystemTest()
      : uber_struct_system_(std::make_shared<UberStructSystem>()),
        root_instance_id_(uber_struct_system_->GetNextInstanceId()),
        root_graph_(root_instance_id_),
        root_handle_(root_graph_.CreateTransform()) {}

  std::shared_ptr<LinkSystem> CreateLinkSystem() {
    return std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId());
  }

  TransformGraph CreateTransformGraph() {
    return TransformGraph(uber_struct_system_->GetNextInstanceId());
  }

  const std::shared_ptr<UberStructSystem> uber_struct_system_;
  const TransformHandle::InstanceId root_instance_id_;
  TransformGraph root_graph_;
  TransformHandle root_handle_;
};

TEST_F(LinkSystemTest, UnresolvedGraphLinkDiesOnContentTokenDeath) {
  auto link_system = CreateLinkSystem();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  TransformHandle handle;

  fidl::InterfacePtr<ContentLink> content_link;
  ChildLink parent_link = link_system->CreateChildLink(std::move(parent_token), LinkProperties(),
                                                       content_link.NewRequest(), handle);
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

  TransformHandle handle;

  fidl::InterfacePtr<GraphLink> graph_link;
  ParentLink child_link =
      link_system->CreateParentLink(std::move(child_token), graph_link.NewRequest(), handle);
  EXPECT_TRUE(child_link.exporter.valid());
  EXPECT_TRUE(graph_link.is_bound());

  parent_token.value.reset();
  RunLoopUntilIdle();

  EXPECT_FALSE(child_link.exporter.valid());
  EXPECT_FALSE(graph_link.is_bound());
}

TEST_F(LinkSystemTest, ResolvedLinkCreatesLinkTopology) {
  auto link_system = CreateLinkSystem();
  auto child_graph = CreateTransformGraph();
  auto parent_graph = CreateTransformGraph();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<GraphLink> graph_link;
  ParentLink parent_link = link_system->CreateParentLink(
      std::move(child_token), graph_link.NewRequest(), child_graph.CreateTransform());
  EXPECT_TRUE(parent_link.exporter.valid());
  EXPECT_TRUE(graph_link.is_bound());

  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  properties.set_logical_size(Vec2{1.0f, 2.0f});
  ChildLink child_link =
      link_system->CreateChildLink(std::move(parent_token), std::move(properties),
                                   content_link.NewRequest(), parent_graph.CreateTransform());

  EXPECT_TRUE(child_link.importer.valid());
  EXPECT_TRUE(content_link.is_bound());

  auto links = link_system->GetResolvedTopologyLinks();
  EXPECT_FALSE(links.empty());
  EXPECT_EQ(links.count(child_link.link_handle), 1u);
  EXPECT_EQ(links[child_link.link_handle], parent_link.link_origin);

  bool layout_updated = false;
  graph_link->GetLayout([&](LayoutInfo info) {
    EXPECT_EQ(1.0f, info.logical_size().x);
    EXPECT_EQ(2.0f, info.logical_size().y);
    layout_updated = true;
  });
  EXPECT_FALSE(layout_updated);
  RunLoopUntilIdle();
  ASSERT_TRUE(layout_updated);
}

TEST_F(LinkSystemTest, ChildLinkDeathDestroysTopology) {
  auto link_system = CreateLinkSystem();
  auto child_graph = CreateTransformGraph();
  auto parent_graph = CreateTransformGraph();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<GraphLink> graph_link;
  ParentLink parent_link = link_system->CreateParentLink(
      std::move(child_token), graph_link.NewRequest(), child_graph.CreateTransform());

  {
    fidl::InterfacePtr<ContentLink> content_link;
    ChildLink child_link =
        link_system->CreateChildLink(std::move(parent_token), LinkProperties(),
                                     content_link.NewRequest(), parent_graph.CreateTransform());

    auto links = link_system->GetResolvedTopologyLinks();
    EXPECT_FALSE(links.empty());
    EXPECT_EQ(links.count(child_link.link_handle), 1u);
    EXPECT_EQ(links[child_link.link_handle], parent_link.link_origin);

    // |child_link| dies here, which destroys the link topology.
  }

  auto links = link_system->GetResolvedTopologyLinks();
  EXPECT_TRUE(links.empty());
}

TEST_F(LinkSystemTest, ParentLinkDeathDestroysTopology) {
  auto link_system = CreateLinkSystem();
  auto child_graph = CreateTransformGraph();
  auto parent_graph = CreateTransformGraph();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ContentLink> content_link;
  ChildLink child_link =
      link_system->CreateChildLink(std::move(parent_token), LinkProperties(),
                                   content_link.NewRequest(), parent_graph.CreateTransform());

  {
    fidl::InterfacePtr<GraphLink> graph_link;
    ParentLink parent_link = link_system->CreateParentLink(
        std::move(child_token), graph_link.NewRequest(), child_graph.CreateTransform());

    auto links = link_system->GetResolvedTopologyLinks();
    EXPECT_FALSE(links.empty());
    EXPECT_EQ(links.count(child_link.link_handle), 1u);
    EXPECT_EQ(links[child_link.link_handle], parent_link.link_origin);

    // |parent_link| dies here, which destroys the link topology.
  }

  auto links = link_system->GetResolvedTopologyLinks();
  EXPECT_TRUE(links.empty());
}

// LinkSystem::UpdateLinks() requires substantial setup to unit test: GraphLink/ContentLink
// protocols attached to the correct TransformHandles in a correctly constructed global topology.
// As a result, LinkSystem::UpdateLinks() is effectively tested in the Flatland unit tests in
// flatland_unittest.cc, since those tests simplify performing the correct setup.

}  // namespace test
}  // namespace flatland
