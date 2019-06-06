// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/auth/account/cpp/fidl.h>
#include <fuchsia/devicesettings/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/setui/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <sdk/lib/sys/cpp/service_directory.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>
#include <src/lib/fxl/logging.h>

class LoginOverrideTest : public modular::testing::TestHarnessFixture {
 public:
  LoginOverrideTest() { real_services()->Connect(setui_.NewRequest()); }

  // Setting LoginOverride to AUTH_PROVIDER should launch the single user
  // base shell.
  void SetLoginOverride(fuchsia::setui::LoginOverride login_override,
                        fit::function<void()> callback) {
    fuchsia::setui::AccountMutation account_mutation;
    account_mutation.set_operation(
        fuchsia::setui::AccountOperation::SET_LOGIN_OVERRIDE);
    account_mutation.set_login_override(login_override);

    fuchsia::setui::Mutation mutation;
    mutation.set_account_mutation_value(std::move(account_mutation));

    setui_->Mutate(
        fuchsia::setui::SettingType::ACCOUNT, std::move(mutation),
        [callback = std::move(callback)](
            fuchsia::setui::MutationResponse response) { callback(); });
  }

 private:
  fuchsia::setui::SetUiServicePtr setui_;
};

TEST_F(LoginOverrideTest, AuthProviderOverrideLaunchesBaseShell) {
  constexpr char kSingleUserBaseShellUrl[] =
      "fuchsia-pkg://fuchsia.com/single_user_base_shell#meta/"
      "single_user_base_shell.cmx";

  modular::testing::TestHarnessBuilder builder;
  builder.AddServiceFromServiceDirectory<fuchsia::setui::SetUiService>(
      real_services());
  builder
      .AddServiceFromServiceDirectory<fuchsia::auth::account::AccountManager>(
          real_services());
  builder.AddServiceFromServiceDirectory<
      fuchsia::devicesettings::DeviceSettingsManager>(real_services());

  bool intercepted = false;
  builder.InterceptBaseShell(
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) { intercepted = true; },
      modular::testing::TestHarnessBuilder::InterceptOptions{
          .url = kSingleUserBaseShellUrl});

  // Setting AUTH_PROVIDER should launch the configured base shell.
  SetLoginOverride(fuchsia::setui::LoginOverride::AUTH_PROVIDER, [&] {
    test_harness().events().OnNewComponent =
        builder.BuildOnNewComponentHandler();
    test_harness()->Run(builder.BuildSpec());
  });

  RunLoopUntil([&] { return intercepted; });
}

// Setting LoginOverride to AUTOLOGIN_GUEST should skip the base shell and
// launch the session shell.
TEST_F(LoginOverrideTest, AutoLoginGuestOverrideSkipsBaseShell) {
  modular::testing::TestHarnessBuilder builder;
  builder.AddServiceFromServiceDirectory<fuchsia::setui::SetUiService>(
      real_services());
  builder
      .AddServiceFromServiceDirectory<fuchsia::auth::account::AccountManager>(
          real_services());
  builder.AddServiceFromServiceDirectory<
      fuchsia::devicesettings::DeviceSettingsManager>(real_services());

  // Base shell should never be launched, so |intercepted_base_shell| should
  // remain false when the session shell launches.
  bool intercepted_base_shell = false;
  builder.InterceptBaseShell(
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) { intercepted_base_shell = true; });

  bool intercepted_session_shell = false;
  builder.InterceptSessionShell(
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) { intercepted_session_shell = true; });

  SetLoginOverride(fuchsia::setui::LoginOverride::AUTOLOGIN_GUEST, [&] {
    test_harness().events().OnNewComponent =
        builder.BuildOnNewComponentHandler();
    test_harness()->Run(builder.BuildSpec());
  });

  RunLoopUntil([&] { return intercepted_session_shell; });
  EXPECT_FALSE(intercepted_base_shell);
}
