// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular/testing/cpp/test_harness_builder.h>

#include "fuchsia/modular/session/cpp/fidl.h"
#include "gtest/gtest.h"
#include "src/modular/lib/app_driver/cpp/module_driver.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

constexpr char kModuleName[] = "mod_name";
constexpr char kStoryName[] = "story";
constexpr char kIntentAction[] = "action";

// Minimal module that connects to `fuchsia.intl.PropertyProvider` and retrieves a `Profile`.
class IntlClientModule : public modular_testing::FakeModule {
 public:
  IntlClientModule()
      : modular_testing::FakeModule(
            {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl("FakeIntlClientModule"),
             .sandbox_services = {"fuchsia.intl.PropertyProvider",
                                  "fuchsia.modular.ComponentContext",
                                  "fuchsia.modular.ModuleContext"}},
            [](fuchsia::modular::Intent) {}) {}

  zx_status_t ConnectToIntlPropertyProvider() {
    auto status =
        component_context()->svc()->Connect<fuchsia::intl::PropertyProvider>(client_.NewRequest());
    client_.set_error_handler([this](zx_status_t status) {
      FXL_LOG(ERROR) << "fuchsia::intl::PropertyProvider connection status: " << status;
      has_error_ = true;
    });
    return status;
  }

  void LoadProfile() {
    client_->GetProfile(
        [this](fuchsia::intl::Profile new_profile) { profile_ = std::move(new_profile); });
  }

  fuchsia::intl::Profile* Profile() { return HasProfile() ? &(profile_.value()) : nullptr; }

  bool HasProfile() { return !!profile_; }
  bool HasError() { return has_error_; }

 private:
  fuchsia::intl::PropertyProviderPtr client_;
  std::optional<fuchsia::intl::Profile> profile_;
  bool has_error_ = false;
};

// Smoke test for Modular's provision of fuchsia.intl.PropertyProvider.
class IntlPropertyProviderTest : public modular_testing::TestHarnessFixture {
 public:
  IntlPropertyProviderTest() {
    test_module_ = std::make_unique<IntlClientModule>();
    builder_.InterceptComponent(test_module_->BuildInterceptOptions());
    builder_.BuildAndRun(test_harness());
  }

  std::unique_ptr<IntlClientModule> test_module_;
  modular_testing::TestHarnessBuilder builder_;
};

TEST_F(IntlPropertyProviderTest, GetsProfileFromProvider) {
  fuchsia::modular::Intent intent;
  intent.handler = test_module_->url();
  intent.action = kIntentAction;

  modular_testing::AddModToStory(test_harness(), kStoryName, kModuleName, std::move(intent));
  RunLoopUntil([&] { return test_module_->is_running(); });
  ASSERT_EQ(ZX_OK, test_module_->ConnectToIntlPropertyProvider());

  test_module_->LoadProfile();
  RunLoopUntil([&] { return test_module_->HasProfile() || test_module_->HasError(); });
  ASSERT_TRUE(test_module_->HasProfile());

  fuchsia::intl::Profile* profile = test_module_->Profile();
  ASSERT_TRUE(profile->has_locales());
  ASSERT_TRUE(profile->has_calendars());
  ASSERT_TRUE(profile->has_time_zones());
  ASSERT_TRUE(profile->has_temperature_unit());
}

}  // namespace
