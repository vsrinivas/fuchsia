// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/devicesettings/cpp/fidl.h>
#include <fuchsia/identity/account/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/testing/component_interceptor.h>

#include <src/lib/files/glob.h>
#include <src/lib/fxl/logging.h>

#include "gtest/gtest.h"
#include "lib/sys/cpp/testing/test_with_environment.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/ui/scenic/lib/scenic/scenic.h"

constexpr char kBasemgrHubPathForTests[] = "/hub/r/env/*/c/basemgr.cmx/*/out/debug/basemgr";
constexpr char kScenicGlobPath[] = "/hub/r/env/*/c/scenic.cmx";

class BasemgrLauncherTest : public sys::testing::TestWithEnvironment {
 public:
  BasemgrLauncherTest()
      : interceptor_(sys::testing::ComponentInterceptor::CreateWithEnvironmentLoader(real_env())) {}

 protected:
  void SetUp() override {
    // Setup an enclosing environment with AccountManager and DeviceSettings services for basemgr.
    // Add Scenic to ensure that it shuts down nicely when basemgr shuts down.
    // Add Presenter to ensure no false negatives for Scenic being launched.
    auto enclosing_env_services = interceptor_.MakeEnvironmentServices(real_env());
    enclosing_env_services->AddServiceWithLaunchInfo(
        fuchsia::sys::LaunchInfo{
            .url = "fuchsia-pkg://fuchsia.com/account_manager#meta/account_manager.cmx"},
        fuchsia::identity::account::AccountManager::Name_);
    enclosing_env_services->AddServiceWithLaunchInfo(
        fuchsia::sys::LaunchInfo{.url = "fuchsia-pkg://fuchsia.com/device_settings_manager#meta/"
                                        "device_settings_manager.cmx"},
        fuchsia::devicesettings::DeviceSettingsManager::Name_);

    enclosing_env_services->AddServiceWithLaunchInfo(
        fuchsia::sys::LaunchInfo{.url = "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx"},
        fuchsia::ui::scenic::Scenic::Name_);

    env_ = sys::testing::EnclosingEnvironment::Create("env", real_env(),
                                                      std::move(enclosing_env_services));
  }

  void RunBasemgrLauncher(std::vector<std::string> args) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "fuchsia-pkg://fuchsia.com/basemgr_launcher#meta/basemgr_launcher.cmx";
    launch_info.arguments = args;

    // Launch basemgr_launcher in enclosing environment
    env_->CreateComponent(std::move(launch_info), basemgr_launcher_controller_.NewRequest());
  }

  std::unique_ptr<sys::testing::EnclosingEnvironment> env_;
  sys::testing::ComponentInterceptor interceptor_;
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
  std::vector<std::string> args({std::string("--base_shell=") + std::string(kInterceptUrl)});
  RunBasemgrLauncher(std::move(args));

  // Intercepting the component means the right base shell was launched
  RunLoopUntil([&] { return intercepted; });
}

TEST_F(BasemgrLauncherTest, BasemgrLauncherDestroysRunningBasemgr) {
  // Launch and intercept basemgr.
  RunBasemgrLauncher({});

  // Get the exact service path, which includes a unique id, of the basemgr instance.
  std::string service_path;
  RunLoopUntil([&] {
    files::Glob glob(kBasemgrHubPathForTests);
    if (glob.size() == 1) {
      service_path = *glob.begin();
      return true;
    }
    return false;
  });

  RunBasemgrLauncher({});

  // Check that the first instance of basemgr no longer exists in the hub and that it has been
  // replaced with another instance.
  RunLoopUntil([&] { return files::Glob(service_path).size() == 0; });
  RunLoopUntil([&] { return files::Glob(kBasemgrHubPathForTests).size() == 1; });
}

// Ensures basemgr isn't launched when bad arguments are provided to basemgr_launcher.
TEST_F(BasemgrLauncherTest, BadArgs) {
  constexpr char kInterceptUrl[] =
      "fuchsia-pkg://fuchsia.com/not_base_shell#meta/not_base_shell.cmx";

  // Setup intercepting a component. This component should never be launched because the argument
  // is not supported by basemgr_launcher.
  bool intercepted = false;
  ASSERT_TRUE(interceptor_.InterceptURL(
      kInterceptUrl, "",
      [&intercepted](fuchsia::sys::StartupInfo startup_info,
                     std::unique_ptr<sys::testing::InterceptedComponent> component) {
        intercepted = true;
      }));

  // Create invalid argument for basemgr_launcher and run it
  std::vector<std::string> args({std::string("--not_supported=") + std::string(kInterceptUrl)});

  bool basemgr_launcher_terminated = false;
  basemgr_launcher_controller_.events().OnTerminated = [&](int64_t err,
                                                           fuchsia::sys::TerminationReason reason) {
    basemgr_launcher_terminated = true;
  };

  RunBasemgrLauncher(std::move(args));
  RunLoopUntil([&] { return basemgr_launcher_terminated; });
  EXPECT_FALSE(intercepted);
}

TEST_F(BasemgrLauncherTest, ShutdownBasemgrCommand) {
  // Launch and intercept basemgr.
  RunBasemgrLauncher({});

  // Get the exact service path, which includes a unique id, of the basemgr instance.
  std::string service_path;
  RunLoopUntil([&] {
    files::Glob glob(kBasemgrHubPathForTests);
    if (glob.size() == 1) {
      service_path = *glob.begin();
      return true;
    }
    return false;
  });

  RunBasemgrLauncher({"shutdown"});

  // Check that the instance of basemgr no longer exists in the hub and it did not restart.
  RunLoopUntil([&] { return files::Glob(service_path).size() == 0; });
  RunLoopUntil([&] { return files::Glob(kBasemgrHubPathForTests).size() == 0; });
}

TEST_F(BasemgrLauncherTest, ShutdownBasemgrShutsdownScenic) {
  // Launch and intercept basemgr.
  RunBasemgrLauncher({});

  // Get the exact service path, which includes a unique id, of the basemgr instance.
  std::string service_path;
  RunLoopUntil([&] {
    files::Glob glob(kBasemgrHubPathForTests);
    if (glob.size() == 1) {
      service_path = *glob.begin();
      return true;
    }
    return false;
  });

  RunBasemgrLauncher({"shutdown"});

  // Check that the instance of basemgr no longer exists in the hub and it did not restart.
  RunLoopUntil([&] { return files::Glob(service_path).size() == 0; });
  RunLoopUntil([&] { return files::Glob(kBasemgrHubPathForTests).size() == 0; });

  // Ensure that scenic also shutdown properly.
  RunLoopUntil([&] { return files::Glob(kScenicGlobPath).size() == 0; });
}
