// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/intl_property_provider_impl/intl_property_provider_impl.h"

#include <fuchsia/deprecatedtimezone/cpp/fidl.h>
#include <fuchsia/deprecatedtimezone/cpp/fidl_test_base.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <src/lib/fxl/log_settings.h>

#include "lib/fostr/fidl/fuchsia/intl/formatting.h"
#include "src/lib/fidl_fuchsia_intl_ext/cpp/fidl_ext.h"

namespace modular {
namespace testing {
namespace {

using fuchsia::deprecatedtimezone::Timezone;
using fuchsia::deprecatedtimezone::TimezonePtr;
using fuchsia::deprecatedtimezone::TimezoneWatcher;
using fuchsia::deprecatedtimezone::TimezoneWatcherPtr;
using fuchsia::intl::CalendarId;
using fuchsia::intl::LocaleId;
using fuchsia::intl::Profile;
using fuchsia::intl::PropertyProvider;
using fuchsia::intl::TemperatureUnit;
using fuchsia::intl::TimeZoneId;
using fuchsia::sys::LaunchInfo;
using modular::IntlPropertyProviderImpl;
using sys::testing::ComponentContextProvider;

class FakeTimezone : public fuchsia::deprecatedtimezone::testing::Timezone_TestBase {
 public:
  FakeTimezone() = default;

  void NotImplemented_(const std::string& name) override {}

  fidl::InterfaceRequestHandler<Timezone> GetHandler(async_dispatcher_t* dispatcher = nullptr) {
    return bindings_.GetHandler(static_cast<Timezone*>(this), dispatcher);
  }

  void SetTimeZone(const std::string& iana_tz_id) {
    if (iana_tz_id_ != iana_tz_id) {
      iana_tz_id_ = iana_tz_id;
      NotifyWatchers();
    }
  }

  void GetTimezoneId(GetTimezoneIdCallback callback) override { callback(iana_tz_id_); }

  void Watch(fidl::InterfaceHandle<TimezoneWatcher> watcher) override {
    auto watcher_ptr = watcher.Bind();
    watcher_ptr->OnTimezoneOffsetChange(iana_tz_id_);
    watchers_.push_back(std::move(watcher_ptr));
  }

 private:
  void NotifyWatchers() {
    for (auto& watcher : watchers_) {
      watcher->OnTimezoneOffsetChange(iana_tz_id_);
    }
  }

  fidl::BindingSet<fuchsia::deprecatedtimezone::Timezone> bindings_;
  std::vector<TimezoneWatcherPtr> watchers_;
  std::string iana_tz_id_;
};

// Tests for `IntlPropertyProviderImpl`.
class IntlPropertyProviderImplTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    SetUpInstanceWithIncomingServices();
    PublishOutgoingService();
  }

  void SetUpInstanceWithIncomingServices() {
    time_zone_service_ = std::make_unique<FakeTimezone>();
    ASSERT_EQ(ZX_OK, provider_.service_directory_provider()->AddService(
                         time_zone_service_->GetHandler(dispatcher())));
    TimezonePtr time_zone_client = provider_.context()->svc()->Connect<Timezone>();
    instance_ = std::make_unique<IntlPropertyProviderImpl>(std::move(time_zone_client));
  }

  void PublishOutgoingService() {
    ASSERT_EQ(ZX_OK, provider_.context()->outgoing()->AddPublicService(
                         instance_->GetHandler(dispatcher())));
  }

  fuchsia::intl::PropertyProviderPtr GetClient() {
    return provider_.ConnectToPublicService<fuchsia::intl::PropertyProvider>();
  }

  ComponentContextProvider provider_;
  std::unique_ptr<FakeTimezone> time_zone_service_;
  std::unique_ptr<IntlPropertyProviderImpl> instance_;
};

TEST_F(IntlPropertyProviderImplTest, GeneratesValidProfileFromDefaults) {
  time_zone_service_->SetTimeZone("America/New_York");

  Profile expected{};
  expected.set_locales({LocaleId{.id = "en-US-u"
                                       "-ca-gregory"
                                       "-fw-sun"
                                       "-hc-h12"
                                       "-ms-ussystem"
                                       "-nu-latn"
                                       "-tz-usnyc"}});
  expected.set_calendars({{CalendarId{.id = "und-u-ca-gregory"}}});
  expected.set_time_zones({TimeZoneId{.id = "America/New_York"}});
  expected.set_temperature_unit(TemperatureUnit::FAHRENHEIT);

  auto client = GetClient();

  Profile actual;
  client->GetProfile([&](Profile profile) { actual = std::move(profile); });
  RunLoopUntilIdle();
  EXPECT_EQ(expected, actual);
}

TEST_F(IntlPropertyProviderImplTest, NotifiesOnTimeZoneChange) {
  time_zone_service_->SetTimeZone("America/New_York");

  Profile expected_a{};
  expected_a.set_locales({LocaleId{.id = "en-US-u"
                                         "-ca-gregory"
                                         "-fw-sun"
                                         "-hc-h12"
                                         "-ms-ussystem"
                                         "-nu-latn"
                                         "-tz-usnyc"}});
  expected_a.set_calendars({{CalendarId{.id = "und-u-ca-gregory"}}});
  expected_a.set_time_zones({TimeZoneId{.id = "America/New_York"}});
  expected_a.set_temperature_unit(TemperatureUnit::FAHRENHEIT);

  auto client = GetClient();

  Profile actual;
  client->GetProfile([&](Profile profile) { actual = std::move(profile); });
  RunLoopUntilIdle();
  EXPECT_EQ(expected_a, actual);

  bool changed = false;
  client.events().OnChange = [&]() { changed = true; };
  RunLoopUntilIdle();
  EXPECT_FALSE(changed);

  time_zone_service_->SetTimeZone("Asia/Shanghai");
  RunLoopUntilIdle();
  EXPECT_TRUE(changed);

  Profile expected_b{};
  expected_b.set_locales({LocaleId{.id = "en-US-u"
                                         "-ca-gregory"
                                         "-fw-sun"
                                         "-hc-h12"
                                         "-ms-ussystem"
                                         "-nu-latn"
                                         "-tz-cnsha"}});
  expected_b.set_calendars({{CalendarId{.id = "und-u-ca-gregory"}}});
  expected_b.set_time_zones({TimeZoneId{.id = "Asia/Shanghai"}});
  expected_b.set_temperature_unit(TemperatureUnit::FAHRENHEIT);

  client->GetProfile([&](Profile profile) { actual = std::move(profile); });
  RunLoopUntilIdle();
  EXPECT_EQ(expected_b, actual);
}

}  // namespace
}  // namespace testing
}  // namespace modular
