// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/virtualization/scenic_wayland_dispatcher.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_component.h>
#include <lib/sys/cpp/testing/fake_launcher.h>

namespace guest {

static constexpr const char* kWaylandDispatcherUrl =
    "fuchsia-pkg://fuchsia.com/wayland_bridge#meta/wayland_bridge.cmx";

class FakeDispatcher : public fuchsia::virtualization::WaylandDispatcher,
                       public fuchsia::wayland::ViewProducer {
 public:
  FakeDispatcher() {
    component_.AddPublicService(bindings_.GetHandler(this));
    component_.AddPublicService(view_producer_bindings_.GetHandler(this));
  }

  // Register to be launched with a fake URL
  void Register(sys::testing::FakeLauncher& fake_launcher) {
    component_.Register(kWaylandDispatcherUrl, fake_launcher);
  }

  size_t BindingCount() const { return bindings_.size(); }
  size_t ConnectionCount() const { return connections_.size(); }

  // Simulates the bridge dying by clearing all state and closing any bindings.
  void Terminate() {
    bindings_.CloseAll();
    view_producer_bindings_.CloseAll();
    connections_.clear();
  }

  void SendOnNewView() {
    for (auto& binding : view_producer_bindings_.bindings()) {
      zx::channel c1, c2;
      zx::channel::create(0, &c1, &c2);
      binding->events().OnNewView(
          fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider>(std::move(c1)));
      new_view_channels_.push_back(std::move(c2));
    }
  }

 private:
  // |fuchsia::virtualization::WaylandDispatcher|
  void OnNewConnection(zx::channel channel) {
    connections_.push_back(std::move(channel));
  }

  sys::testing::FakeComponent component_;
  fidl::BindingSet<fuchsia::virtualization::WaylandDispatcher> bindings_;
  fidl::BindingSet<fuchsia::wayland::ViewProducer> view_producer_bindings_;
  std::vector<zx::channel> connections_;
  std::vector<zx::channel> new_view_channels_;
};

class ScenicWaylandDispatcherTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    dispatcher_.reset(new ScenicWaylandDispatcher(
        provider_.context(),
        fit::bind_member(this, &ScenicWaylandDispatcherTest::OnNewView)));
    provider_.service_directory_provider()->AddService(
        fake_launcher_.GetHandler());

    fake_dispatcher_impl_.reset(new FakeDispatcher());
    fake_dispatcher_impl_->Register(fake_launcher_);
  }

  void TearDown() override { TestLoopFixture::TearDown(); }

 protected:
  ScenicWaylandDispatcher* dispatcher() const { return dispatcher_.get(); }
  FakeDispatcher* remote_dispatcher() const {
    return fake_dispatcher_impl_.get();
  }
  std::vector<fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider>>* views() {
    return &views_;
  }

 private:
  void OnNewView(fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view) {
    views_.push_back(std::move(view));
  }

  sys::testing::FakeLauncher fake_launcher_;
  std::unique_ptr<FakeDispatcher> fake_dispatcher_impl_;
  sys::testing::ComponentContextProvider provider_;
  std::unique_ptr<ScenicWaylandDispatcher> dispatcher_;
  std::vector<fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider>> views_;
};

// The |ScenicWaylandDispatcher| will simply spawn a new bridge process for each
// connection and reuse that process for subsequent connections.
//
// Test that multiple connections are sent to the same bridge component.
TEST_F(ScenicWaylandDispatcherTest, LaunchBridgeOnce) {
  zx::channel c1, c2;
  zx::channel::create(0, &c1, &c2);
  dispatcher()->OnNewConnection(std::move(c1));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(1u, remote_dispatcher()->ConnectionCount());

  dispatcher()->OnNewConnection(std::move(c2));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(2u, remote_dispatcher()->ConnectionCount());
}

// When the remote wayland_bridge component dies, we restart it on the next
// connection request.
TEST_F(ScenicWaylandDispatcherTest, RelaunchBridgeWhenLost) {
  zx::channel c1, c2;
  zx::channel::create(0, &c1, &c2);
  dispatcher()->OnNewConnection(std::move(c1));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(1u, remote_dispatcher()->ConnectionCount());

  remote_dispatcher()->Terminate();
  RunLoopUntilIdle();
  ASSERT_EQ(0u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(0u, remote_dispatcher()->ConnectionCount());

  dispatcher()->OnNewConnection(std::move(c2));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(1u, remote_dispatcher()->ConnectionCount());
}

// Verify we can correctly receive new ViewProviders from the remote bridge
// process.
TEST_F(ScenicWaylandDispatcherTest, ReceiveNewViewEvents) {
  zx::channel c1, c2;
  zx::channel::create(0, &c1, &c2);
  dispatcher()->OnNewConnection(std::move(c1));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(1u, remote_dispatcher()->ConnectionCount());
  ASSERT_EQ(0u, views()->size());

  remote_dispatcher()->SendOnNewView();
  RunLoopUntilIdle();
  ASSERT_EQ(1u, views()->size());
}

};  // namespace guest
