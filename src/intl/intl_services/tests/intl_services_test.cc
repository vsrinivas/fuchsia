// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

using fuchsia::intl::LocaleId;
using fuchsia::intl::Profile;
using fuchsia::intl::TemperatureUnit;
using fuchsia::intl::TimeZoneId;
using fuchsia::settings::Error;
using fuchsia::settings::HourCycle;
using fuchsia::settings::IntlSettings;

static const int32_t kTimeoutSec = 10;

// See README.md for more detail about this test.
class IntlServicesTest : public sys::testing::TestWithEnvironment {
 public:
  IntlServicesTest()
      : deadline_(zx::clock::get_monotonic() + zx::sec(kTimeoutSec)),
        ctx_(sys::ComponentContext::Create()),
        settings_intl_(ctx_->svc()->Connect<fuchsia::settings::Intl>()),
        intl_property_provider_(ctx_->svc()->Connect<fuchsia::intl::PropertyProvider>()),
        settings_intl_status_(ZX_OK),
        intl_property_provider_status_(ZX_OK) {
    settings_intl_.set_error_handler(
        [this](zx_status_t status) { settings_intl_status_ = status; });
    intl_property_provider_.set_error_handler(
        [this](zx_status_t status) { intl_property_provider_status_ = status; });
  }

  // Returns true if any error occurred in the FIDL roundtrip.
  bool FIDLError() const {
    return intl_property_provider_status_ != ZX_OK || settings_intl_status_ != ZX_OK;
  }

  // Returns true if timeout occurred.  Used so that the tests do not block.
  bool Timeout() const {
    zx::time now = zx::clock::get_monotonic();
    return now > deadline_;
  }

 protected:
  zx::time deadline_;

  std::unique_ptr<sys::ComponentContext> ctx_;
  fuchsia::settings::IntlPtr settings_intl_;
  fuchsia::intl::PropertyProviderPtr intl_property_provider_;

  zx_status_t settings_intl_status_;
  zx_status_t intl_property_provider_status_;
};

TEST_F(IntlServicesTest, AsyncSetThenGet) {
  IntlSettings settings;
  settings.set_locales({LocaleId{.id = "ru-RU"}});
  settings.set_time_zone_id(TimeZoneId{.id = "Europe/Moscow"});
  settings.set_temperature_unit(TemperatureUnit::CELSIUS);
  settings.set_hour_cycle(HourCycle::H23);

  Profile get_result;
  bool get_completed{};
  auto get_callback = [&](fuchsia::intl::Profile res) {
    get_result = std::move(res);
    get_completed = true;
  };

  bool on_change_completed{};
  intl_property_provider_.events().OnChange = [&] {
    // Reading the profile before OnChange arrives will cause a data race and
    // make result comparison flaky in the test.
    intl_property_provider_->GetProfile(get_callback);
    on_change_completed = true;
  };

  fit::result<void, Error> set_result;
  bool set_completed{};
  auto set_callback = [&](fit::result<void, Error> res) {
    set_result = std::move(res);
    set_completed = true;
  };
  settings_intl_->Set(std::move(settings), set_callback);
  RunLoopUntil([&] {
    return (set_completed && get_completed && on_change_completed) || FIDLError() || Timeout();
  });

  ASSERT_EQ(ZX_OK, settings_intl_status_) << zx_status_get_string(settings_intl_status_);
  ASSERT_EQ(ZX_OK, intl_property_provider_status_)
      << zx_status_get_string(intl_property_provider_status_);

  // The test should normally run for a fraction of a second, so even though this measures time
  // *after* the test events completed, it should not matter for timeout checks.
  ASSERT_FALSE(Timeout()) << "Test took too long to complete";

  ASSERT_TRUE(set_result.is_ok());

  EXPECT_EQ(TemperatureUnit::CELSIUS, get_result.temperature_unit());
  EXPECT_EQ("Europe/Moscow", get_result.time_zones()[0].id);
  EXPECT_EQ("ru-RU-u-ca-gregory-fw-mon-hc-h23-ms-metric-nu-latn-tz-rumow",
            get_result.locales()[0].id)
      << "Expected BCP-47 locale";
}
