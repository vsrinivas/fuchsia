// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_context_impl.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/fake_launcher.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/modular_config/modular_config.h"

namespace modular_testing {
namespace {

using ::sys::testing::FakeLauncher;
using SessionContextImplTest = gtest::TestLoopFixture;

TEST_F(SessionContextImplTest, StartSessionmgr) {
  FakeLauncher launcher;
  std::string url = "test_url_string";
  fuchsia::modular::session::AppConfig app_config;
  app_config.set_url(url);

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  bool callback_called = false;
  launcher.RegisterComponent(
      url, [&callback_called](fuchsia::sys::LaunchInfo,
                              fidl::InterfaceRequest<fuchsia::sys::ComponentController>) {
        callback_called = true;
      });

  modular::SessionContextImpl impl(
      &launcher, false /* use_random_id */,
      modular::CloneStruct(app_config) /* sessionmgr_config */,
      modular::CloneStruct(app_config) /* session_shell_config */,
      modular::CloneStruct(app_config) /* story_shell_config */,
      false /* use_session_shell_for_story_shell_factory */, std::move(view_token),
      nullptr /* additional_services */, zx::channel() /* overridden_config_handle */,
      [](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>) {} /* get_presentation */,
      [](modular::SessionContextImpl::ShutDownReason) {} /* done_callback */);

  EXPECT_TRUE(callback_called);
}

TEST_F(SessionContextImplTest, SessionmgrCrashInvokesDoneCallback) {
  // Program the fake launcher to drop the CreateComponent request such that
  // the error handler of the sessionmgr_app is invoked. This should invoke the
  // done_callback.
  FakeLauncher launcher;
  std::string url = "test_url_string";
  fuchsia::modular::session::AppConfig app_config;
  app_config.set_url(url);

  launcher.RegisterComponent(url, [](fuchsia::sys::LaunchInfo,
                                     fidl::InterfaceRequest<fuchsia::sys::ComponentController>) {});

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  bool done_callback_called = false;
  modular::SessionContextImpl impl(
      &launcher,
      /* use_random_id */ false,
      /* sessionmgr_config= */ modular::CloneStruct(app_config),
      /* session_shell_config= */ modular::CloneStruct(app_config),
      /* story_shell_config= */ modular::CloneStruct(app_config),
      /* use_session_shell_for_story_shell_factory= */ false, std::move(view_token),
      /* additional_services */ nullptr, zx::channel() /* overridden_config_handle */,
      /* get_presentation= */
      [](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>) {},
      /* done_callback= */
      [&done_callback_called](modular::SessionContextImpl::ShutDownReason) {
        done_callback_called = true;
      });

  RunLoopUntilIdle();
  EXPECT_TRUE(done_callback_called);
}

}  // namespace
}  // namespace modular_testing
