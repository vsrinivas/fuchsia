// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/auth/account/cpp/fidl.h>
#include <fuchsia/devicesettings/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/testing/component_interceptor.h>

#include <src/lib/fxl/logging.h>

#include "gtest/gtest.h"
#include "lib/sys/cpp/testing/test_with_environment.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

class BasemgrLauncherTest : public sys::testing::TestWithEnvironment {
 public:
  BasemgrLauncherTest()
      : interceptor_(sys::testing::ComponentInterceptor::CreateWithEnvironmentLoader(real_env())) {}

 protected:
  void SetUp() override {
    // Setup an enclosing environment with AccountManager and DeviceSettings services for basemgr.
    auto enclosing_env_services = interceptor_.MakeEnvironmentServices(real_env());
    enclosing_env_services->AddServiceWithLaunchInfo(
        fuchsia::sys::LaunchInfo{
            .url = "fuchsia-pkg://fuchsia.com/account_manager#meta/account_manager.cmx"},
        fuchsia::auth::account::AccountManager::Name_);
    enclosing_env_services->AddServiceWithLaunchInfo(
        fuchsia::sys::LaunchInfo{.url = "fuchsia-pkg://fuchsia.com/device_settings_manager#meta/"
                                        "device_settings_manager.cmx"},
        fuchsia::devicesettings::DeviceSettingsManager::Name_);

    env_ = sys::testing::EnclosingEnvironment::Create("env", real_env(),
                                                      std::move(enclosing_env_services));
  }

  void RunBasemgrLauncher(std::vector<std::string> args) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "fuchsia-pkg://fuchsia.com/basemgr_launcher#meta/basemgr_launcher.cmx";
    for (auto arg : args) {
      launch_info.arguments.push_back(arg);
    }

    // Launch basemgr_launcher in enclosing environment
    env_->CreateComponent(std::move(launch_info), basemgr_launcher_controller_.NewRequest());
  }

  sys::testing::ComponentInterceptor interceptor_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> env_;
  fuchsia::sys::ComponentControllerPtr basemgr_launcher_controller_;
};

// Sets up interception of a base shell and passes if the specified base shell is launched
// through the base_shell basemgr_launcher arg.
TEST_F(BasemgrLauncherTest, BaseShellArg) {
  constexpr char kInterceptUrl[] =
      "fuchsia-pkg://fuchsia.com/test_base_shell#meta/test_base_shell.cmx";

  // Setup intercepting base shell
  bool intercepted = false;
  ASSERT_TRUE(interceptor_.InterceptURL(
      kInterceptUrl, "",
      [&intercepted](fuchsia::sys::StartupInfo startup_info,
                     std::unique_ptr<sys::testing::InterceptedComponent> component) {
        intercepted = true;
      }));

  // Create args for basemgr_launcher
  std::vector<std::string> args;
  args.push_back(std::string("--base_shell=") + std::string(kInterceptUrl));
  RunBasemgrLauncher(std::move(args));

  // Intercepting the component means the right base shell was launched
  RunLoopUntil([&] { return intercepted; });
}

// Sets up interception of a session shell and passes if the specified session shell is laucnhed
// through the session_shell basemgr_launcher arg.
TEST_F(BasemgrLauncherTest, SessionShellArg) {
  constexpr char kInterceptUrl[] =
      "fuchsia-pkg://fuchsia.com/test_session_shell#meta/test_session_shell.cmx";

  // Setup intercepting base shell
  bool intercepted = false;
  ASSERT_TRUE(interceptor_.InterceptURL(
      kInterceptUrl, "",
      [&intercepted](fuchsia::sys::StartupInfo startup_info,
                     std::unique_ptr<sys::testing::InterceptedComponent> component) {
        intercepted = true;
      }));

  // Create args for basemgr_launcher
  std::vector<std::string> args;
  args.push_back(std::string("--session_shell=") + std::string(kInterceptUrl));
  RunBasemgrLauncher(std::move(args));

  // Intercepting the component means the right session shell was launched
  RunLoopUntil([&] { return intercepted; });
}

// Sets up interception of a session agent and passes if the specified agent is launched through
// the session_agent basemgr_launcher arg. Starting a session is a prerequisite for starting an
// agent, so this test requires a session shell to be launched.
TEST_F(BasemgrLauncherTest, SessionAgentArg) {
  constexpr char kInterceptUrl[] =
      "fuchsia-pkg://fuchsia.com/test_session_agent#meta/test_session_agent.cmx";
  constexpr char kSessionShellDefaultUrl[] =
      "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
      "test_session_shell.cmx";

  // Setup intercepting base shell
  bool intercepted = false;
  ASSERT_TRUE(interceptor_.InterceptURL(
      kInterceptUrl, "",
      [&intercepted](fuchsia::sys::StartupInfo startup_info,
                     std::unique_ptr<sys::testing::InterceptedComponent> component) {
        intercepted = true;
      }));

  // Create args for basemgr_launcher. Use a test session shell so sessionmgr completes
  // initialization.
  std::vector<std::string> args;
  args.push_back(std::string("--session_shell=") + std::string(kSessionShellDefaultUrl));
  args.push_back(std::string("--session_agent=") + std::string(kInterceptUrl));
  RunBasemgrLauncher(std::move(args));

  // Intercepting the component means the right session agent was launched
  RunLoopUntil([&] { return intercepted; });
}
