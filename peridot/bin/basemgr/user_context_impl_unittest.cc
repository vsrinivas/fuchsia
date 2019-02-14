// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/user_context_impl.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/testing/fake_launcher.h>
#include <lib/gtest/test_loop_fixture.h>

#include "peridot/lib/fidl/clone.h"

namespace modular {
namespace testing {
namespace {

using ::component::testing::FakeLauncher;
using UserContextImplTest = gtest::TestLoopFixture;

TEST_F(UserContextImplTest, StartSessionmgrWithTokenManagers) {
  FakeLauncher launcher;
  std::string url = "test_url_string";
  fuchsia::modular::AppConfig app_config;
  app_config.url = url;

  bool callback_called = false;
  launcher.RegisterComponent(
      url, [&callback_called](
               fuchsia::sys::LaunchInfo launch_info,
               fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        callback_called = true;
      });

  fuchsia::auth::TokenManagerPtr ledger_token_manager;
  fuchsia::auth::TokenManagerPtr agent_token_manager;

  UserContextImpl impl(
      &launcher, CloneStruct(app_config), CloneStruct(app_config),
      CloneStruct(app_config), std::move(ledger_token_manager),
      std::move(agent_token_manager), nullptr /* account */,
      nullptr /* view_owner_request */, nullptr /* base_shell_services */,
      nullptr /* done_callback */);

  EXPECT_TRUE(callback_called);
}

TEST_F(UserContextImplTest, SessionmgrCrashInvokesDoneCallback) {
  // Program the fake launcher to drop the CreateComponent request such that
  // the error handler of the sessionmgr_app is invoked. This should invoke the
  // done_callback.
  FakeLauncher launcher;
  std::string url = "test_url_string";
  fuchsia::modular::AppConfig app_config;
  app_config.url = url;

  launcher.RegisterComponent(
      url, [](fuchsia::sys::LaunchInfo launch_info,
              fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        return;
      });

  fuchsia::auth::TokenManagerPtr ledger_token_manager;
  fuchsia::auth::TokenManagerPtr agent_token_manager;

  bool done_callback_called = false;
  UserContextImpl impl(
      &launcher, CloneStruct(app_config), CloneStruct(app_config),
      CloneStruct(app_config), std::move(ledger_token_manager),
      std::move(agent_token_manager), nullptr /* account */,
      nullptr /* view_owner_request */, nullptr /* base_shell_services */,
      /* done_callback = */ [&done_callback_called](UserContextImpl*) {
        done_callback_called = true;
      });

  RunLoopUntilIdle();
  EXPECT_TRUE(done_callback_called);
}
}  // namespace
}  // namespace testing
}  // namespace modular
