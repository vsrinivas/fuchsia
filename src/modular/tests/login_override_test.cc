// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/devicesettings/cpp/fidl.h>
#include <fuchsia/identity/account/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/setui/cpp/fidl.h>
#include <fuchsia/stash/cpp/fidl.h>
#include <lib/modular/testing/cpp/test_harness_builder.h>

#include "src/lib/fxl/logging.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

class LoginOverrideTest : public modular_testing::TestHarnessFixture {
 public:
  LoginOverrideTest() {}

  // Setting LoginOverride to AUTH_PROVIDER should launch the single user
  // base shell.
  void SetLoginOverride(fuchsia::setui::LoginOverride login_override,
                        modular_testing::TestHarnessBuilder* builder) {
    fuchsia::setui::AccountMutation account_mutation;
    account_mutation.set_operation(fuchsia::setui::AccountOperation::SET_LOGIN_OVERRIDE);
    account_mutation.set_login_override(login_override);

    fuchsia::setui::Mutation mutation;
    mutation.set_account_mutation_value(std::move(account_mutation));

    builder->BuildAndRun(test_harness());

    test_harness()->ConnectToEnvironmentService(fuchsia::setui::SetUiService::Name_,
                                                setui_.NewRequest().TakeChannel());
    setui_->Mutate(fuchsia::setui::SettingType::ACCOUNT, std::move(mutation),
                   [](fuchsia::setui::MutationResponse response) {});
  }

 private:
  fuchsia::setui::SetUiServicePtr setui_;
};

TEST_F(LoginOverrideTest, AuthProviderOverrideLaunchesBaseShell) {
  constexpr char kBaseShellUrl[] =
      "fuchsia-pkg://fuchsia.com/dev_base_shell#meta/"
      "dev_base_shell.cmx";

  modular_testing::TestHarnessBuilder builder;
  builder.AddServiceFromComponent<fuchsia::setui::SetUiService>(
      "fuchsia-pkg://fuchsia.com/setui_service#meta/setui_service.cmx");
  builder.AddServiceFromComponent<fuchsia::identity::account::AccountManager>(
      "fuchsia-pkg://fuchsia.com/account_manager#meta/account_manager.cmx");
  builder.AddServiceFromComponent<fuchsia::devicesettings::DeviceSettingsManager>(
      "fuchsia-pkg://fuchsia.com/device_settings_manager#meta/"
      "device_settings_manager.cmx");
  builder.AddServiceFromComponent<fuchsia::stash::Store>(
      "fuchsia-pkg://fuchsia.com/stash#meta/"
      "stash.cmx");

  bool intercepted = false;
  builder.InterceptBaseShell(modular_testing::TestHarnessBuilder::InterceptOptions{
      .url = kBaseShellUrl,
      .launch_handler =
          [&](fuchsia::sys::StartupInfo startup_info,
              fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
            intercepted = true;
          }});

  // Setting AUTH_PROVIDER should launch the configured base shell.
  SetLoginOverride(fuchsia::setui::LoginOverride::AUTH_PROVIDER, &builder);

  RunLoopUntil([&] { return intercepted; });
}

// Setting LoginOverride to AUTOLOGIN_GUEST should skip the base shell and
// launch the session shell.
// Test is DISABLED. see fxb/37780
TEST_F(LoginOverrideTest, DISABLED_AutoLoginGuestOverrideSkipsBaseShell) {
  modular_testing::TestHarnessBuilder builder;
  builder.AddServiceFromComponent<fuchsia::setui::SetUiService>(
      "fuchsia-pkg://fuchsia.com/setui_service#meta/setui_service.cmx");
  builder.AddServiceFromComponent<fuchsia::identity::account::AccountManager>(
      "fuchsia-pkg://fuchsia.com/account_manager#meta/account_manager.cmx");
  builder.AddServiceFromComponent<fuchsia::devicesettings::DeviceSettingsManager>(
      "fuchsia-pkg://fuchsia.com/device_settings_manager#meta/"
      "device_settings_manager.cmx");
  builder.AddServiceFromComponent<fuchsia::stash::Store>(
      "fuchsia-pkg://fuchsia.com/stash#meta/"
      "stash.cmx");

  // Base shell should never be launched, so |intercepted_base_shell| should
  // remain false when the session shell launches.
  bool intercepted_base_shell = false;
  builder.InterceptBaseShell(
      {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
       .launch_handler =
           [&](fuchsia::sys::StartupInfo startup_info,
               fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
             intercepted_base_shell = true;
           }});

  bool intercepted_session_shell = false;
  builder.InterceptSessionShell(
      {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
       .launch_handler =
           [&](fuchsia::sys::StartupInfo startup_info,
               fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
             intercepted_session_shell = true;
           }});

  SetLoginOverride(fuchsia::setui::LoginOverride::AUTOLOGIN_GUEST, &builder);

  RunLoopUntil([&] { return intercepted_session_shell; });
  EXPECT_FALSE(intercepted_base_shell);
}
