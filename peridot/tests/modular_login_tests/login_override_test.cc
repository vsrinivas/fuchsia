// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/setui/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <sdk/lib/sys/cpp/service_directory.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>

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

  fuchsia::modular::testing::TestHarnessSpec spec;
  std::vector<std::string> env_services;
  env_services.push_back("fuchsia.setui.SetUiService");
  spec.set_env_services_to_inherit(env_services);

  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kSingleUserBaseShellUrl);
  spec.mutable_base_shell()->set_intercept_spec(
      std::move(shell_intercept_spec));

  // Listen for session shell interception.
  bool intercepted = false;
  test_harness().events().OnNewBaseShell =
      [&intercepted](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) { intercepted = true; };

  SetLoginOverride(fuchsia::setui::LoginOverride::AUTH_PROVIDER,
                   [this, spec = std::move(spec)]() mutable {
                     test_harness()->Run(std::move(spec));
                   });

  ASSERT_TRUE(RunLoopUntil([&] { return intercepted; }));
}

// Setting LoginOverride to AUTOLOGIN_GUEST should launch the session shell.
TEST_F(LoginOverrideTest, AutoLoginGuestOverrideLaunchesSessionShell) {
  constexpr char kFakeSessionShellUrl[] =
      "fuchsia-pkg://example.com/FAKE_SESSION_SHELL_PKG/fake_session_shell.cmx";

  fuchsia::modular::testing::TestHarnessSpec spec;
  std::vector<std::string> env_services;
  env_services.push_back("fuchsia.setui.SetUiService");
  spec.set_env_services_to_inherit(env_services);

  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kFakeSessionShellUrl);
  spec.mutable_session_shell()->set_intercept_spec(
      std::move(shell_intercept_spec));

  // Listen for session shell interception.
  bool intercepted = false;
  test_harness().events().OnNewSessionShell =
      [&intercepted](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) { intercepted = true; };

  SetLoginOverride(fuchsia::setui::LoginOverride::AUTOLOGIN_GUEST,
                   [this, spec = std::move(spec)]() mutable {
                     test_harness()->Run(std::move(spec));
                   });

  ASSERT_TRUE(RunLoopUntil([&] { return intercepted; }));
}
