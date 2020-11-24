// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_context_impl.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/testing/fake_launcher.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "lib/sys/cpp/testing/test_with_environment.h"
#include "src/lib/files/path.h"
#include "src/modular/bin/basemgr/sessions.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_accessor.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular_testing {
namespace {

class SessionContextImplTest : public sys::testing::TestWithEnvironment {
 public:
  // Returns a ModuleConfigAccessor that returns true for |use_random_session_id|.
  static modular::ModularConfigAccessor ConfigWithRandomId() {
    auto config_accessor = modular::ModularConfigAccessor(modular::DefaultConfig());
    config_accessor.set_use_random_session_id(true);
    return config_accessor;
  }

  // Deletes all session directories that are used to determine which sessions have been created.
  // This does not erase any existing isolated storage.
  static void DeleteAllSessionDirectories() {
    std::vector<std::string> session_ids = modular::sessions::GetExistingSessionIds();
    for (const auto& session_id : session_ids) {
      auto session_dir = modular::sessions::GetSessionDirectory(session_id);
      EXPECT_TRUE(files::DeletePath(session_dir, /*recursive=*/true));
    }
  }

  modular::SessionContextImpl CreateSessionContextImpl(
      modular::ModularConfigAccessor* modular_config,
      modular::SessionContextImpl::OnSessionShutdownCallback on_session_shutdown) {
    static constexpr auto kTestSessionmgrUrl = "test_sessionmgr_url";

    sys::testing::FakeLauncher launcher;

    // Register a fake sessionmgr component that never launches.
    fuchsia::modular::session::AppConfig sessionmgr_app_config;
    sessionmgr_app_config.set_url(kTestSessionmgrUrl);

    launcher.RegisterComponent(
        kTestSessionmgrUrl,
        [](fuchsia::sys::LaunchInfo /* unused */,
           fidl::InterfaceRequest<fuchsia::sys::ComponentController> /* unused */) {});

    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

    return modular::SessionContextImpl{
        &launcher, real_env().get(), std::move(sessionmgr_app_config), modular_config,
        std::move(view_token),
        /*additional_services_for_sessionmgr=*/nullptr,
        /*additional_services_for_agents=*/fuchsia::sys::ServiceList(),
        /*get_presentation=*/
        [](fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> /* unused */) {},
        std::move(on_session_shutdown)};
  }
};

TEST_F(SessionContextImplTest, StartSessionmgr) {
  sys::testing::FakeLauncher launcher;

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
      &launcher, real_env().get(), std::move(sessionmgr_app_config), &modular_config_accessor,
      std::move(view_token),
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
  sys::testing::FakeLauncher launcher;

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
      &launcher, real_env().get(), std::move(sessionmgr_app_config), &modular_config_accessor,
      std::move(view_token),
      /*additional_services_for_sessionmgr=*/nullptr,
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

TEST_F(SessionContextImplTest, DeleteEphemeralSessions) {
  auto random_id_config = SessionContextImplTest::ConfigWithRandomId();
  auto default_config = modular::ModularConfigAccessor(modular::DefaultConfig());

  std::vector<std::string> existing_sessions;

  // Start with zero sessions.
  DeleteAllSessionDirectories();

  // Create a session with a random ID.
  auto is_random_id_session_shutdown{false};
  auto random_id_session = CreateSessionContextImpl(
      &random_id_config, [&](auto /*unused*/) { is_random_id_session_shutdown = true; });
  RunLoopUntil([&]() { return is_random_id_session_shutdown; });

  existing_sessions = modular::sessions::GetExistingSessionIds();
  ASSERT_EQ(1u, existing_sessions.size());
  EXPECT_NE(modular::sessions::GetStableSessionId(), existing_sessions.at(0));

  // Create a session with the default config that uses the stable session ID.
  auto is_stable_id_session_shutdown{false};
  auto stable_id_session = CreateSessionContextImpl(
      &default_config, [&](auto /*unused*/) { is_stable_id_session_shutdown = true; });
  RunLoopUntil([&]() { return is_stable_id_session_shutdown; });

  // The session with the random ID should have been deleted and replaced with the stable session.
  existing_sessions = modular::sessions::GetExistingSessionIds();
  ASSERT_EQ(1u, existing_sessions.size());
  EXPECT_EQ(modular::sessions::GetStableSessionId(), existing_sessions.at(0));
}

}  // namespace
}  // namespace modular_testing
