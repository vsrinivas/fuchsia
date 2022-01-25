// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/virtualization/scenic_wayland_dispatcher.h"

#include <fuchsia/wayland/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_component.h>
#include <lib/sys/cpp/testing/fake_launcher.h>

namespace guest {

static constexpr const char* kWaylandBridgeUrl =
    "fuchsia-pkg://fuchsia.com/wayland_bridge#meta/wayland_bridge.cmx";

class FakeDispatcher : public fuchsia::wayland::Server {
 public:
  FakeDispatcher() { component_.AddPublicService(bindings_.GetHandler(this)); }

  // Register to be launched with a fake URL
  void Register(const char* fake_url, sys::testing::FakeLauncher& fake_launcher) {
    component_.Register(fake_url, fake_launcher);
  }

  size_t BindingCount() const { return bindings_.size(); }
  size_t ConnectionCount() const { return connections_.size(); }

  // Simulates the bridge dying by clearing all state and closing any bindings.
  void Terminate() {
    bindings_.CloseAll();
    connections_.clear();
  }

 private:
  // |fuchsia::wayland::Server|
  void Connect(zx::channel channel) override { connections_.push_back(std::move(channel)); }

  sys::testing::FakeComponent component_;
  fidl::BindingSet<fuchsia::wayland::Server> bindings_;
  std::vector<zx::channel> connections_;
};

class ScenicWaylandDispatcherTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    dispatcher_.reset(new ScenicWaylandDispatcher(provider_.context(), kWaylandBridgeUrl));
    provider_.service_directory_provider()->AddService(fake_launcher_.GetHandler());

    fake_dispatcher_impl_.reset(new FakeDispatcher());
    fake_dispatcher_impl_->Register(kWaylandBridgeUrl, fake_launcher_);
  }

  void TearDown() override { TestLoopFixture::TearDown(); }

 protected:
  ScenicWaylandDispatcher* dispatcher() const { return dispatcher_.get(); }
  FakeDispatcher* remote_dispatcher() const { return fake_dispatcher_impl_.get(); }

 private:
  sys::testing::FakeLauncher fake_launcher_;
  std::unique_ptr<FakeDispatcher> fake_dispatcher_impl_;
  sys::testing::ComponentContextProvider provider_;
  std::unique_ptr<ScenicWaylandDispatcher> dispatcher_;
};

// The |ScenicWaylandDispatcher| will simply spawn a new bridge process for each
// connection and reuse that process for subsequent connections.
//
// Test that multiple connections are sent to the same bridge component.
TEST_F(ScenicWaylandDispatcherTest, LaunchBridgeOnce) {
  zx::channel c1, c2;
  zx::channel::create(0, &c1, &c2);
  dispatcher()->Connect(std::move(c1));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(1u, remote_dispatcher()->ConnectionCount());

  dispatcher()->Connect(std::move(c2));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(2u, remote_dispatcher()->ConnectionCount());
}

// When the remote wayland_bridge component dies, we restart it on the next
// connection request.
TEST_F(ScenicWaylandDispatcherTest, RelaunchBridgeWhenLost) {
  zx::channel c1, c2;
  zx::channel::create(0, &c1, &c2);
  dispatcher()->Connect(std::move(c1));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(1u, remote_dispatcher()->ConnectionCount());

  remote_dispatcher()->Terminate();
  RunLoopUntilIdle();
  ASSERT_EQ(0u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(0u, remote_dispatcher()->ConnectionCount());

  dispatcher()->Connect(std::move(c2));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, remote_dispatcher()->BindingCount());
  ASSERT_EQ(1u, remote_dispatcher()->ConnectionCount());
}

}  // namespace guest
