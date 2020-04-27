// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular/testing/cpp/test_harness_builder.h>

#include <gtest/gtest.h>

#include "fuchsia/modular/session/cpp/fidl.h"
#include "intl_property_provider_test_client.h"
#include "src/modular/lib/app_driver/cpp/module_driver.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

constexpr char kModuleName[] = "mod_name";
constexpr char kStoryName[] = "story";
constexpr char kIntentAction[] = "action";

// Smoke test for Modular's provision of fuchsia.intl.PropertyProvider.
class IntlPropertyProviderTest : public modular_testing::TestHarnessFixture {
 public:
  IntlPropertyProviderTest() {
    test_module_ =
        std::make_unique<modular_testing::FakeModule>(modular_testing::FakeComponent::Args{
            .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl("FakeIntlClientModule"),
            .sandbox_services =
                {
                    "fuchsia.intl.PropertyProvider",
                    "fuchsia.modular.ComponentContext",
                    "fuchsia.modular.ModuleContext",
                    "fuchsia.settings.Intl",
                },
        });
    builder_.InterceptComponent(test_module_->BuildInterceptOptions());
    builder_.BuildAndRun(test_harness());
  }

  std::unique_ptr<modular_testing::FakeModule> test_module_;
  modular_testing::TestHarnessBuilder builder_;
};

TEST_F(IntlPropertyProviderTest, GetsProfileFromProvider) {
  fuchsia::modular::Intent intent;
  intent.handler = test_module_->url();
  intent.action = kIntentAction;

  modular_testing::AddModToStory(test_harness(), kStoryName, kModuleName, std::move(intent));
  RunLoopUntil([&] { return test_module_->is_running(); });

  modular_tests::IntlPropertyProviderTestClient intl_client{test_module_.get()};

  ASSERT_EQ(ZX_OK, intl_client.Connect());

  intl_client.LoadProfile();
  RunLoopUntil([&] { return intl_client.HasProfile() || intl_client.HasError(); });
  ASSERT_TRUE(intl_client.HasProfile());

  fuchsia::intl::Profile* profile = intl_client.Profile();
  ASSERT_TRUE(profile->has_locales());
  ASSERT_TRUE(profile->has_calendars());
  ASSERT_TRUE(profile->has_time_zones());
  ASSERT_TRUE(profile->has_temperature_unit());
}

}  // namespace
