// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/app_driver/cpp/module_driver.h>
#include <lib/modular/testing/cpp/test_harness_builder.h>
#include <lib/modular_test_harness/cpp/fake_module.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>

#include "fuchsia/modular/session/cpp/fidl.h"
#include "gtest/gtest.h"

namespace {

constexpr char kModuleName[] = "mod_name";
constexpr char kStoryName[] = "story";
constexpr char kIntentAction[] = "action";

// Minimal module that connects to `fuchsia.intl.PropertyProvider` and retrieves a `Profile`.
class IntlClientModule : public modular::testing::FakeModule {
 public:
  zx_status_t ConnectToIntlPropertyProvider() {
    return component_context()->svc()->Connect<fuchsia::intl::PropertyProvider>(
        client_.NewRequest());
  }

  void LoadProfile() {
    client_->GetProfile(
        [this](fuchsia::intl::Profile new_profile) { profile_ = std::move(new_profile); });
  }

  fuchsia::intl::Profile* Profile() { return HasProfile() ? &(profile_.value()) : nullptr; }

  bool HasProfile() { return !!profile_; }

 private:
  fuchsia::intl::PropertyProviderPtr client_;
  std::optional<fuchsia::intl::Profile> profile_;
};

// Smoke test for Modular's provision of fuchsia.intl.PropertyProvider.
class IntlPropertyProviderTest : public modular::testing::TestHarnessFixture {
 public:
  void SetUp() override {
    test_module_ = std::make_unique<IntlClientModule>();
    test_module_url_ = modular::testing::GenerateFakeUrl();
    builder_.InterceptComponent(
        test_module_->GetOnCreateHandler(),
        {.url = test_module_url_,
         .sandbox_services = {"fuchsia.intl.PropertyProvider", "fuchsia.modular.ModuleContext"}});
    builder_.BuildAndRun(test_harness());
  }

  std::unique_ptr<IntlClientModule> test_module_;
  modular::testing::TestHarnessBuilder builder_;
  std::string test_module_url_;
};

TEST_F(IntlPropertyProviderTest, GetsProfileFromProvider) {
  fuchsia::modular::Intent intent;
  intent.handler = test_module_url_;
  intent.action = kIntentAction;

  modular::testing::AddModToStory(test_harness(), kStoryName, kModuleName, std::move(intent));
  RunLoopUntil([&] { return test_module_->is_running(); });
  ASSERT_EQ(ZX_OK, test_module_->ConnectToIntlPropertyProvider());

  test_module_->LoadProfile();
  RunLoopUntil([&] { return test_module_->HasProfile(); });

  fuchsia::intl::Profile* profile = test_module_->Profile();
  ASSERT_TRUE(profile->has_locales());
  ASSERT_TRUE(profile->has_calendars());
  ASSERT_TRUE(profile->has_time_zones());
  ASSERT_TRUE(profile->has_temperature_unit());
}

}  // namespace
