// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/user_controller_impl.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/testing/fake_launcher.h>
#include <lib/gtest/test_loop_fixture.h>

#include "peridot/lib/fidl/clone.h"

namespace modular {
namespace testing {
namespace {

using ::component::testing::FakeLauncher;
using UserControllerImplTest = gtest::TestLoopFixture;

TEST_F(UserControllerImplTest, StartUserRunnerWithTokenProviderFactory) {
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

  fuchsia::modular::auth::TokenProviderFactoryPtr token_provider_factory_ptr;
  auto token_provider_factory_request = token_provider_factory_ptr.NewRequest();

  fuchsia::auth::TokenManagerPtr ledger_token_manager;
  fuchsia::auth::TokenManagerPtr agent_token_manager;
  fuchsia::modular::UserControllerPtr user_controller_ptr;
  UserControllerImpl impl(
      &launcher, CloneStruct(app_config), CloneStruct(app_config),
      CloneStruct(app_config), std::move(token_provider_factory_ptr),
      std::move(ledger_token_manager), std::move(agent_token_manager),
      nullptr /* account */, nullptr /* view_owner_request */,
      nullptr /* base_shell_services */, user_controller_ptr.NewRequest(),
      nullptr /* done_callback */);

  EXPECT_TRUE(callback_called);
}

TEST_F(UserControllerImplTest, StartUserRunnerWithTokenManagers) {
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

  fuchsia::modular::auth::TokenProviderFactoryPtr token_provider_factory_ptr;
  fuchsia::auth::TokenManagerPtr ledger_token_manager_ptr;
  auto ledger_token_manager = ledger_token_manager_ptr.NewRequest();
  fuchsia::auth::TokenManagerPtr agent_token_manager_ptr;
  auto agent_token_manager = agent_token_manager_ptr.NewRequest();

  fuchsia::modular::UserControllerPtr user_controller_ptr;
  UserControllerImpl impl(
      &launcher, CloneStruct(app_config), CloneStruct(app_config),
      CloneStruct(app_config), std::move(token_provider_factory_ptr),
      std::move(ledger_token_manager_ptr), std::move(agent_token_manager_ptr),
      nullptr /* account */, nullptr /* view_owner_request */,
      nullptr /* base_shell_services */, user_controller_ptr.NewRequest(),
      nullptr /* done_callback */);

  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace testing
}  // namespace modular
