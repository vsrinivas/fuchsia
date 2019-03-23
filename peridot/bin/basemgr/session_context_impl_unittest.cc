// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/session_context_impl.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/testing/fake_launcher.h>
#include <lib/gtest/test_loop_fixture.h>

#include "peridot/lib/fidl/clone.h"

namespace modular {
namespace testing {
namespace {

using ::component::testing::FakeLauncher;
using SessionContextImplTest = gtest::TestLoopFixture;

// Unique identifier for a test session.
constexpr char kSessionId[] = "session_id";

TEST_F(SessionContextImplTest, StartSessionmgrWithTokenManagers) {
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

  SessionContextImpl impl(
      &launcher, kSessionId, CloneStruct(app_config) /* sessionmgr_config */,
      CloneStruct(app_config) /* session_shell_config */,
      CloneStruct(app_config) /* story_shell_config */,
      false, /* use_session_shell_for_story_shell_factory */
      std::move(ledger_token_manager), std::move(agent_token_manager),
      nullptr /* account */, nullptr /* view_owner_request */,
      [](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>) {
      } /* get_presentation */,
      [](bool) {} /* done_callback */);

  EXPECT_TRUE(callback_called);
}

TEST_F(SessionContextImplTest, SessionmgrCrashInvokesDoneCallback) {
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
  SessionContextImpl impl(
      &launcher, kSessionId, /* sessionmgr_config= */ CloneStruct(app_config),
      /* session_shell_config= */ CloneStruct(app_config),
      /* story_shell_config= */ CloneStruct(app_config),
      /* use_session_shell_for_story_shell_factory= */ false,
      std::move(ledger_token_manager), std::move(agent_token_manager),
      /* account= */ nullptr, /* view_owner_request= */ nullptr,
      /* get_presentation= */
      [](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>) {},
      /* done_callback= */
      [&done_callback_called](bool) { done_callback_called = true; });

  RunLoopUntilIdle();
  EXPECT_TRUE(done_callback_called);
}

}  // namespace
}  // namespace testing
}  // namespace modular
