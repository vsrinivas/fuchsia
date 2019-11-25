// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_context_impl.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/fake_launcher.h>

#include "src/modular/lib/fidl/clone.h"

namespace modular_testing {
namespace {

using ::sys::testing::FakeLauncher;
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
      url, [&callback_called](fuchsia::sys::LaunchInfo launch_info,
                              fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        callback_called = true;
      });

  fuchsia::auth::TokenManagerPtr agent_token_manager;

  modular::SessionContextImpl impl(
      &launcher, kSessionId, modular::CloneStruct(app_config) /* sessionmgr_config */,
      modular::CloneStruct(app_config) /* session_shell_config */,
      modular::CloneStruct(app_config) /* story_shell_config */,
      false /* use_session_shell_for_story_shell_factory */, std::move(agent_token_manager),
      nullptr /* account */, fuchsia::ui::views::ViewToken(), nullptr /* additional_services */,
      zx::channel() /* overridden_config_handle */,
      [](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>) {} /* get_presentation */,
      [](modular::SessionContextImpl::ShutDownReason, bool) {} /* done_callback */);

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
              fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) { return; });

  fuchsia::auth::TokenManagerPtr agent_token_manager;

  bool done_callback_called = false;
  modular::SessionContextImpl impl(
      &launcher, kSessionId, /* sessionmgr_config= */ modular::CloneStruct(app_config),
      /* session_shell_config= */ modular::CloneStruct(app_config),
      /* story_shell_config= */ modular::CloneStruct(app_config),
      /* use_session_shell_for_story_shell_factory= */ false, std::move(agent_token_manager),
      /* account= */ nullptr, fuchsia::ui::views::ViewToken(),
      /* additional_services */ nullptr, zx::channel() /* overridden_config_handle */,
      /* get_presentation= */
      [](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>) {},
      /* done_callback= */
      [&done_callback_called](modular::SessionContextImpl::ShutDownReason, bool) {
        done_callback_called = true;
      });

  RunLoopUntilIdle();
  EXPECT_TRUE(done_callback_called);
}

}  // namespace
}  // namespace modular_testing
