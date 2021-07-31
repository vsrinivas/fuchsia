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
#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

using flatland::LinkSystem;
using ChildLink = flatland::LinkSystem::ChildLink;
using ParentLink = flatland::LinkSystem::ParentLink;
using flatland::TransformGraph;
using flatland::UberStructSystem;
using TopologyEntry = flatland::TransformGraph::TopologyEntry;
using flatland::TransformHandle;
using fuchsia::math::SizeU;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::ViewCreationToken;
using fuchsia::ui::composition::ViewportCreationToken;
using fuchsia::ui::composition::ViewportProperties;

namespace flatland {
namespace test {

class LinkSystemTest : public gtest::TestLoopFixture {
 public:
  LinkSystemTest()
      : uber_struct_system_(std::make_shared<UberStructSystem>()),
        root_instance_id_(uber_struct_system_->GetNextInstanceId()),
        root_graph_(root_instance_id_),
        root_handle_(root_graph_.CreateTransform()) {}

  std::shared_ptr<LinkSystem> CreateViewportSystem() {
    return std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId());
  }

  TransformGraph CreateTransformGraph() {
    return TransformGraph(uber_struct_system_->GetNextInstanceId());
  }

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();
    // UnownedDispatcherHolder is safe to use because the dispatcher will be valid until TearDown().
    dispatcher_holder_ = std::make_shared<utils::UnownedDispatcherHolder>(dispatcher());
  }

  void TearDown() override {
    dispatcher_holder_.reset();
    gtest::TestLoopFixture::TearDown();
  }

  const std::shared_ptr<UberStructSystem> uber_struct_system_;
  const TransformHandle::InstanceId root_instance_id_;
  TransformGraph root_graph_;
  TransformHandle root_handle_;
  std::shared_ptr<utils::DispatcherHolder> dispatcher_holder_;
};

TEST_F(LinkSystemTest, UnresolvedParentViewportWatcherDiesOnContentTokenDeath) {
  auto link_system = CreateViewportSystem();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  TransformHandle handle;

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size(SizeU{1, 2});
  ChildLink parent_link =
      link_system->CreateChildLink(dispatcher_holder_, std::move(parent_token),
                                   std::move(properties), child_view_watcher.NewRequest(), handle,
                                   [](const std::string& error_log) { GTEST_FAIL() << error_log; });
  EXPECT_TRUE(parent_link.importer.valid());
  EXPECT_TRUE(child_view_watcher.is_bound());

  child_token.value.reset();
  RunLoopUntilIdle();

  EXPECT_FALSE(parent_link.importer.valid());
  EXPECT_FALSE(child_view_watcher.is_bound());
}

TEST_F(LinkSystemTest, UnresolvedChildViewWatcherDiesOnGraphTokenDeath) {
  auto link_system = CreateViewportSystem();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  TransformHandle handle;

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  ParentLink child_link = link_system->CreateParentLink(
      dispatcher_holder_, std::move(child_token), parent_viewport_watcher.NewRequest(), handle,
      [](const std::string& error_log) { GTEST_FAIL() << error_log; });
  EXPECT_TRUE(child_link.exporter.valid());
  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  parent_token.value.reset();
  RunLoopUntilIdle();

  EXPECT_FALSE(child_link.exporter.valid());
  EXPECT_FALSE(parent_viewport_watcher.is_bound());
}

TEST_F(LinkSystemTest, ResolvedLinkCreatesLinkTopology) {
  auto link_system = CreateViewportSystem();
  auto child_graph = CreateTransformGraph();
  auto parent_graph = CreateTransformGraph();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  ParentLink parent_link = link_system->CreateParentLink(
      dispatcher_holder_, std::move(child_token), parent_viewport_watcher.NewRequest(),
      child_graph.CreateTransform(),
      [](const std::string& error_log) { GTEST_FAIL() << error_log; });
  EXPECT_TRUE(parent_link.exporter.valid());
  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size(SizeU{1, 2});
  ChildLink child_link = link_system->CreateChildLink(
      dispatcher_holder_, std::move(parent_token), std::move(properties),
      child_view_watcher.NewRequest(), parent_graph.CreateTransform(),
      [](const std::string& error_log) { GTEST_FAIL() << error_log; });

  EXPECT_TRUE(child_link.importer.valid());
  EXPECT_TRUE(child_view_watcher.is_bound());

  auto links = link_system->GetResolvedTopologyLinks();
  EXPECT_FALSE(links.empty());
  EXPECT_EQ(links.count(child_link.link_handle), 1u);
  EXPECT_EQ(links[child_link.link_handle], parent_link.child_view_watcher_handle);

  bool layout_updated = false;
  parent_viewport_watcher->GetLayout([&](LayoutInfo info) {
    EXPECT_EQ(1u, info.logical_size().width);
    EXPECT_EQ(2u, info.logical_size().height);
    layout_updated = true;
  });
  EXPECT_FALSE(layout_updated);
  RunLoopUntilIdle();
  ASSERT_TRUE(layout_updated);
}

TEST_F(LinkSystemTest, ChildLinkDeathDestroysTopology) {
  auto link_system = CreateViewportSystem();
  auto child_graph = CreateTransformGraph();
  auto parent_graph = CreateTransformGraph();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  ParentLink parent_link = link_system->CreateParentLink(
      dispatcher_holder_, std::move(child_token), parent_viewport_watcher.NewRequest(),
      child_graph.CreateTransform(),
      [](const std::string& error_log) { GTEST_FAIL() << error_log; });

  {
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    ViewportProperties properties;
    properties.set_logical_size(SizeU{1, 2});
    ChildLink child_link = link_system->CreateChildLink(
        dispatcher_holder_, std::move(parent_token), std::move(properties),
        child_view_watcher.NewRequest(), parent_graph.CreateTransform(),
        [](const std::string& error_log) { GTEST_FAIL() << error_log; });

    auto links = link_system->GetResolvedTopologyLinks();
    EXPECT_FALSE(links.empty());
    EXPECT_EQ(links.count(child_link.link_handle), 1u);
    EXPECT_EQ(links[child_link.link_handle], parent_link.child_view_watcher_handle);

    // |child_link| dies here, which destroys the link topology.
  }

  auto links = link_system->GetResolvedTopologyLinks();
  EXPECT_TRUE(links.empty());
}

TEST_F(LinkSystemTest, ParentLinkDeathDestroysTopology) {
  auto link_system = CreateViewportSystem();
  auto child_graph = CreateTransformGraph();
  auto parent_graph = CreateTransformGraph();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size(SizeU{1, 2});
  ChildLink child_link = link_system->CreateChildLink(
      dispatcher_holder_, std::move(parent_token), std::move(properties),
      child_view_watcher.NewRequest(), parent_graph.CreateTransform(),
      [](const std::string& error_log) { GTEST_FAIL() << error_log; });

  {
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    ParentLink parent_link = link_system->CreateParentLink(
        dispatcher_holder_, std::move(child_token), parent_viewport_watcher.NewRequest(),
        child_graph.CreateTransform(),
        [](const std::string& error_log) { GTEST_FAIL() << error_log; });

    auto links = link_system->GetResolvedTopologyLinks();
    EXPECT_FALSE(links.empty());
    EXPECT_EQ(links.count(child_link.link_handle), 1u);
    EXPECT_EQ(links[child_link.link_handle], parent_link.child_view_watcher_handle);

    // |parent_link| dies here, which destroys the link topology.
  }

  auto links = link_system->GetResolvedTopologyLinks();
  EXPECT_TRUE(links.empty());
}

TEST_F(LinkSystemTest, OverwrittenHangingGetsReturnError) {
  auto link_system = CreateViewportSystem();
  auto child_graph = CreateTransformGraph();
  auto parent_graph = CreateTransformGraph();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  bool parent_link_returned_error = false;
  ParentLink parent_link = link_system->CreateParentLink(
      dispatcher_holder_, std::move(child_token), parent_viewport_watcher.NewRequest(),
      child_graph.CreateTransform(),
      [&](const std::string& error_log) { parent_link_returned_error = true; });

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  bool child_link_returned_error = false;
  ViewportProperties properties;
  properties.set_logical_size(SizeU{1, 2});
  ChildLink child_link = link_system->CreateChildLink(
      dispatcher_holder_, std::move(parent_token), std::move(properties),
      child_view_watcher.NewRequest(), parent_graph.CreateTransform(),
      [&](const std::string& error_log) { child_link_returned_error = true; });

  {
    bool status_updated = false;
    child_view_watcher->GetStatus([&](auto) { status_updated = true; });
    EXPECT_FALSE(child_link_returned_error);
    EXPECT_FALSE(status_updated);

    child_view_watcher->GetStatus([&](auto) {});
    RunLoopUntilIdle();
    EXPECT_TRUE(child_link_returned_error);
    EXPECT_FALSE(status_updated);
  }

  {
    bool layout_updated = false;
    parent_viewport_watcher->GetLayout([&](auto) { layout_updated = true; });
    RunLoopUntilIdle();
    EXPECT_FALSE(parent_link_returned_error);
    EXPECT_TRUE(layout_updated);
  }

  {
    parent_link_returned_error = false;
    bool layout_updated = false;
    parent_viewport_watcher->GetLayout([&](auto) { layout_updated = true; });
    EXPECT_FALSE(parent_link_returned_error);
    EXPECT_FALSE(layout_updated);

    parent_viewport_watcher->GetLayout([&](auto) {});
    RunLoopUntilIdle();
    EXPECT_TRUE(parent_link_returned_error);
    EXPECT_FALSE(layout_updated);
  }
}

// LinkSystem::UpdateLinks() requires substantial setup to unit test:
// ParentViewportWatcher/ChildViewWatcher protocols attached to the correct TransformHandles in a
// correctly constructed global topology. As a result, LinkSystem::UpdateLinks() is effectively
// tested in the Flatland unit tests in flatland_unittest.cc, since those tests simplify performing
// the correct setup.

}  // namespace test
}  // namespace flatland
