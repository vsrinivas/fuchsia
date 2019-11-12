// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/devicesettings/cpp/fidl.h>
#include <fuchsia/identity/account/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_component.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/component_interceptor.h>

#include "src/lib/files/glob.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"
#include "src/ui/scenic/lib/scenic/scenic.h"

constexpr char kModularTestHarnessGlobPath[] =
    "/hub/r/mth_*_test/*/c/sessionmgr.cmx/*/out/debug/sessionctl";

constexpr char kScenicGlobPath[] = "/hub/r/mth_*_test/*/c/scenic.cmx";

class SessionctlTest : public modular_testing::TestHarnessFixture {
 public:
  SessionctlTest()
      : interceptor_(sys::testing::ComponentInterceptor::CreateWithEnvironmentLoader(real_env())) {}

  void SetUp() override {
    auto enclosing_env_services = interceptor_.MakeEnvironmentServices(real_env());
    enclosing_env_services->AddServiceWithLaunchInfo(
        fuchsia::sys::LaunchInfo{
            .url = "fuchsia-pkg://fuchsia.com/account_manager#meta/account_manager.cmx"},
        fuchsia::identity::account::AccountManager::Name_);
    enclosing_env_services->AddServiceWithLaunchInfo(
        fuchsia::sys::LaunchInfo{.url = "fuchsia-pkg://fuchsia.com/device_settings_manager#meta/"
                                        "device_settings_manager.cmx"},
        fuchsia::devicesettings::DeviceSettingsManager::Name_);

    env_ = sys::testing::EnclosingEnvironment::Create("env", real_env(),
                                                      std::move(enclosing_env_services));
  }

  modular::FuturePtr<> RunSessionCtl(std::vector<std::string> args) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "fuchsia-pkg://fuchsia.com/sessionctl#meta/sessionctl.cmx";
    launch_info.arguments = args;

    // Launch sessionctl in enclosing environment
    auto fut = modular::Future<>::Create("StopSessionCtl");
    env_->CreateComponent(std::move(launch_info), sessionctl_controller_.NewRequest());
    sessionctl_controller_.set_error_handler([fut](zx_status_t) { fut->Complete(); });
    return fut;
  }

  std::unique_ptr<sys::testing::EnclosingEnvironment> env_;
  sys::testing::ComponentInterceptor interceptor_;
  fuchsia::sys::ComponentControllerPtr sessionctl_controller_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
};

TEST_F(SessionctlTest, FindSessionCtlService) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.set_environment_suffix("test");
  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return files::Glob(kModularTestHarnessGlobPath).size() == 1; });
}

TEST_F(SessionctlTest, ConnectAndKillScenicService) {
  fuchsia::modular::testing::TestHarnessSpec spec;

  // Add scenic service to the modular test harness.
  fuchsia::modular::testing::ComponentService svc;
  svc.name = fuchsia::ui::scenic::Scenic::Name_;
  svc.url = "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx";
  spec.mutable_env_services()->mutable_services_from_components()->push_back(std::move(svc));

  spec.mutable_basemgr_config()->set_test(true);

  spec.set_environment_suffix("test");
  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.BuildAndRun(test_harness());

  // Ask the test_harness for scenic to make sure it's actively present.
  test_harness()->ConnectToEnvironmentService(fuchsia::ui::scenic::Scenic::Name_,
                                              scenic_.NewRequest().TakeChannel());
  RunLoopUntil([&] { return files::Glob(kScenicGlobPath).size() == 1; });

  RunSessionCtl({"shutdown_basemgr"})->Then([&] {});

  RunLoopUntil([&] { return (files::Glob(kScenicGlobPath).size() == 0); });
}
