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
#include "src/modular/lib/modular_config/modular_config_accessor.h"

namespace modular_testing {
namespace {

using ::sys::testing::FakeLauncher;
using SessionContextImplTest = gtest::TestLoopFixture;

TEST_F(SessionContextImplTest, StartSessionmgr) {
  FakeLauncher launcher;

  std::string url = "test_url_string";
  fuchsia::modular::session::AppConfig sessionmgr_app_config;
  sessionmgr_app_config.set_url(url);

  bool callback_called = false;
  launcher.RegisterComponent(
      url,
      [&callback_called](fuchsia::sys::LaunchInfo /* unused */,
                         fidl::InterfaceRequest<fuchsia::sys::ComponentController> /* unused */) {
        callback_called = true;
      });

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto modular_config_accessor = modular::ModularConfigAccessor(modular::DefaultConfig());

  modular::SessionContextImpl impl(
      &launcher, std::move(sessionmgr_app_config), &modular_config_accessor, std::move(view_token),
      /*additional_services_for_sessionmgr=*/nullptr,
      /*additional_services_for_agents=*/fuchsia::sys::ServiceList(),
      /*get_presentation=*/
      [](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> /* unused */) {},
      /*on_session_shutdown=*/[](modular::SessionContextImpl::ShutDownReason /* unused */) {});

  EXPECT_TRUE(callback_called);
}

TEST_F(SessionContextImplTest, SessionmgrCrashInvokesDoneCallback) {
  // Program the fake launcher to drop the CreateComponent request such that
  // the error handler of the sessionmgr_app is invoked. This should invoke the
  // done_callback.
  FakeLauncher launcher;

  std::string url = "test_url_string";
  fuchsia::modular::session::AppConfig sessionmgr_app_config;
  sessionmgr_app_config.set_url(url);

  launcher.RegisterComponent(
      url, [](fuchsia::sys::LaunchInfo /* unused */,
              fidl::InterfaceRequest<fuchsia::sys::ComponentController> /* unused */) {});

  auto modular_config_accessor = modular::ModularConfigAccessor(modular::DefaultConfig());
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  bool done_callback_called = false;
  modular::SessionContextImpl impl(
      &launcher, std::move(sessionmgr_app_config), &modular_config_accessor, std::move(view_token),
      /*additional_services=*/nullptr,
      /*additional_services_for_agents=*/fuchsia::sys::ServiceList(),
      /*get_presentation=*/
      [](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> /* unused */) {},
      /*on_session_shutdown=*/
      [&done_callback_called](modular::SessionContextImpl::ShutDownReason /* unused */) {
        done_callback_called = true;
      });

  RunLoopUntilIdle();
  EXPECT_TRUE(done_callback_called);
}

}  // namespace
}  // namespace modular_testing
