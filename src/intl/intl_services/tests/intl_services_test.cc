// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <zircon/status.h>

using fuchsia::intl::LocaleId;
using fuchsia::intl::Profile;
using fuchsia::intl::TemperatureUnit;
using fuchsia::intl::TimeZoneId;
using fuchsia::settings::Error;
using fuchsia::settings::HourCycle;
using fuchsia::settings::IntlSettings;

// See README.md for more detail about this test.
class IntlServicesTest : public sys::testing::TestWithEnvironment {
 public:
  IntlServicesTest()
      : ctx_(sys::ComponentContext::Create()),
        settings_intl_(ctx_->svc()->Connect<fuchsia::settings::Intl>()),
        intl_property_provider_(ctx_->svc()->Connect<fuchsia::intl::PropertyProvider>()),
        settings_intl_status_(ZX_OK),
        intl_property_provider_status_(ZX_OK) {
    settings_intl_.set_error_handler(
        [this](zx_status_t status) { settings_intl_status_ = status; });
    intl_property_provider_.set_error_handler(
        [this](zx_status_t status) { intl_property_provider_status_ = status; });
  }

 protected:
  std::unique_ptr<sys::ComponentContext> ctx_;
  fuchsia::settings::IntlPtr settings_intl_;
  fuchsia::intl::PropertyProviderPtr intl_property_provider_;

  zx_status_t settings_intl_status_;
  zx_status_t intl_property_provider_status_;
};

TEST_F(IntlServicesTest, Basic) {
  {
    IntlSettings settings;
    settings.set_locales({LocaleId{.id = "ru-RU"}});
    settings.set_time_zone_id(TimeZoneId{.id = "Europe/Moscow"});
    settings.set_temperature_unit(TemperatureUnit::CELSIUS);
    settings.set_hour_cycle(HourCycle::H23);
    bool completed{};
    fit::result<void, Error> result;
    settings_intl_->Set(std::move(settings), [&](fit::result<void, Error> res) {
      result = res;
      completed = true;
    });
    RunLoopUntil([&] { return completed || settings_intl_status_ != ZX_OK; });
    ASSERT_EQ(ZX_OK, settings_intl_status_) << zx_status_get_string(settings_intl_status_);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(completed);
  }
  {
    bool completed{};
    Profile result;
    intl_property_provider_->GetProfile([&](fuchsia::intl::Profile res) {
      result = std::move(res);
      completed = true;
    });
    RunLoopUntil([&] { return completed || intl_property_provider_status_ != ZX_OK; });
    ASSERT_EQ(ZX_OK, intl_property_provider_status_)
        << zx_status_get_string(intl_property_provider_status_);
    ASSERT_TRUE(completed);
    EXPECT_EQ(TemperatureUnit::CELSIUS, result.temperature_unit());
    EXPECT_EQ("Europe/Moscow", result.time_zones()[0].id);
    EXPECT_EQ("ru-RU-u-ca-gregory-fw-mon-hc-h23-ms-metric-nu-latn-tz-rumow", result.locales()[0].id)
        << "Expected BCP-47 locale";
  }
  {
    IntlSettings settings;
    settings.set_locales({LocaleId{.id = "ru-RU"}});
    settings.set_time_zone_id(TimeZoneId{.id = "Europe/Moscow"});
    settings.set_temperature_unit(TemperatureUnit::FAHRENHEIT);
    bool completed{};
    fit::result<void, Error> result;
    settings_intl_->Set(std::move(settings), [&](fit::result<void, Error> res) {
      result = res;
      completed = true;
    });
    RunLoopUntil([&] { return completed || settings_intl_status_ != ZX_OK; });
    ASSERT_EQ(ZX_OK, settings_intl_status_) << zx_status_get_string(settings_intl_status_);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(completed);
  }
  {
    bool completed{};
    Profile result;
    intl_property_provider_->GetProfile([&](fuchsia::intl::Profile res) {
      result = std::move(res);
      completed = true;
    });
    RunLoopUntil([&] { return completed || intl_property_provider_status_ != ZX_OK; });
    ASSERT_EQ(ZX_OK, intl_property_provider_status_)
        << zx_status_get_string(intl_property_provider_status_);
    ASSERT_TRUE(completed);
    EXPECT_EQ(TemperatureUnit::FAHRENHEIT, result.temperature_unit());
  }
}
