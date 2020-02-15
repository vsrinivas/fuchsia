// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/intl/intl_property_provider_impl/intl_property_provider_impl.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl_test_base.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "garnet/bin/trace/tests/component_context.h"
#include "lib/fostr/fidl/fuchsia/intl/formatting.h"
#include "lib/fostr/fidl/fuchsia/settings/formatting.h"
#include "src/lib/fidl_fuchsia_intl_ext/cpp/fidl_ext.h"
#include "src/lib/fxl/log_settings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular_testing {
namespace {

using fuchsia::intl::CalendarId;
using fuchsia::intl::LocaleId;
using fuchsia::intl::Profile;
using fuchsia::intl::PropertyProvider;
using fuchsia::intl::TemperatureUnit;
using fuchsia::intl::TimeZoneId;
using fuchsia::settings::HourCycle;
using modular::IntlPropertyProviderImpl;
using sys::testing::ComponentContextProvider;

fuchsia::settings::IntlSettings NewSettings(std::vector<std::string> locale_ids,
                                            HourCycle hour_cycle,
                                            TemperatureUnit temperature_unit) {
  EXPECT_FALSE(locale_ids.empty()) << "by settings protocol locale ids must be nonempty";
  fuchsia::settings::IntlSettings settings;
  std::vector<LocaleId> locales;
  for (const auto& locale_id : locale_ids) {
    locales.emplace_back(LocaleId{
        .id = locale_id,
    });
  }
  settings.set_locales(std::move(locales));
  settings.set_temperature_unit(temperature_unit);
  settings.set_hour_cycle(hour_cycle);
  return settings;
}

// A fake implementation of fuchsia.settings.Intl service.  The Watch protocol specifically is not
// implemented correctly for multiple watchers.
class FakeSettingsService : public fuchsia::settings::testing::Intl_TestBase {
 public:
  FakeSettingsService()
      : intl_settings_(NewSettings({"en-US"}, HourCycle::H12, TemperatureUnit::FAHRENHEIT)),
        state_changed_(true) {}

  fidl::InterfaceRequestHandler<fuchsia::settings::Intl> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return bindings_.GetHandler(static_cast<fuchsia::settings::Intl*>(this), dispatcher);
  }

  // Test method, used to modify the timezone identifier served by the fake setui service.
  void SetTimeZone(const std::string& iana_tz_id) {
    fuchsia::settings::IntlSettings new_settings = modular::CloneStruct(intl_settings_);
    new_settings.mutable_time_zone_id()->id = iana_tz_id;
    SetIntl(new_settings);
  }

  // Test method, used to modify the fake intl data that this fake service implementation will
  // serve.
  void SetIntl(const fuchsia::settings::IntlSettings& intl_settings) {
    if (fidl::Equals(intl_settings_, intl_settings)) {
      return;
    }
    intl_settings_ = modular::CloneStruct(intl_settings);
    state_changed_ = true;
    Notify();
  }

  // Implements `fuchsia.settings.Watch`, but only for a single watcher.
  void Watch(WatchCallback callback) override {
    watcher_ = std::move(callback);
    if (state_changed_) {
      Notify();
    }
  }

  // Called on all other methods that this fake does not implement.
  void NotImplemented_(const std::string& name) override {
    FAIL() << "Method not implemented: " << name;
  }

 private:
  void Notify() {
    if (watcher_ == nullptr) {
      FX_LOGS(INFO) << "No watcher, not notifying.";
      return;
    }
    FX_LOGS(INFO) << "telling watcher it's " << intl_settings_;
    watcher_(fit::ok(modular::CloneStruct(intl_settings_)));
    state_changed_ = false;
    watcher_ = nullptr;
  }

  // The server-side connection for Intl service
  fidl::BindingSet<fuchsia::settings::Intl> bindings_;

  // The fake implementation of the watch protocol works for one listener only.
  WatchCallback watcher_;

  // Settings reported on a Watch call.
  fuchsia::settings::IntlSettings intl_settings_;

  // If set, it means that any incoming watch should return immediately.
  bool state_changed_;
};

// Tests for `IntlPropertyProviderImpl`.
class IntlPropertyProviderImplTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    SetUpInstanceWithIncomingServices();
    PublishOutgoingService();
  }

  // Creates a server under test, connecting to the backend FIDL services that
  // are exposed by the test fixture.
  void SetUpInstanceWithIncomingServices() {
    setui_service_ = std::make_unique<FakeSettingsService>();
    ASSERT_EQ(ZX_OK, provider_.service_directory_provider()->AddService(
                         setui_service_->GetHandler(dispatcher())));
    fuchsia::settings::IntlPtr client =
        provider_.context()->svc()->Connect<fuchsia::settings::Intl>();
    instance_ = std::make_unique<IntlPropertyProviderImpl>(std::move(client));
  }

  // Makes the service of the unit under test available in the outgoing testing
  // directory, so that the tests can connect to it.
  void PublishOutgoingService() {
    ASSERT_EQ(ZX_OK, provider_.context()->outgoing()->AddPublicService(
                         instance_->GetHandler(dispatcher())));
  }

  // Creates a client of `fuchsia.intl.PropertyProvider`, which can be instantiated in a test case
  // to connect to the service under test.
  fuchsia::intl::PropertyProviderPtr GetClient() {
    return provider_.ConnectToPublicService<fuchsia::intl::PropertyProvider>();
  }

  // The default component context provider.
  ComponentContextProvider provider_;

  // The fake setui service instance.
  std::unique_ptr<FakeSettingsService> setui_service_;

  // The instance of the server under test.
  std::unique_ptr<IntlPropertyProviderImpl> instance_;
};

TEST_F(IntlPropertyProviderImplTest, GeneratesValidProfileFromDefaults) {
  setui_service_->SetTimeZone("America/New_York");

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
  setui_service_->SetTimeZone("America/New_York");

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
  ASSERT_EQ(expected_a, actual);

  bool changed = false;
  client.events().OnChange = [&]() { changed = true; };
  RunLoopUntilIdle();
  ASSERT_FALSE(changed);

  setui_service_->SetTimeZone("Asia/Shanghai");
  RunLoopUntilIdle();
  ASSERT_TRUE(changed);

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

TEST_F(IntlPropertyProviderImplTest, NotifiesOnLocaleChange) {
  setui_service_->SetIntl(NewSettings({"nl-NL"}, HourCycle::H12, TemperatureUnit::CELSIUS));
  setui_service_->SetTimeZone("UTC");
  RunLoopUntilIdle();

  Profile expected_a{};
  expected_a.set_locales({LocaleId{.id = "nl-NL-u"
                                         "-ca-gregory"
                                         "-fw-mon"
                                         "-hc-h12"
                                         "-ms-metric"
                                         "-nu-latn"
                                         "-tz-utc"}});
  expected_a.set_calendars({{CalendarId{.id = "und-u-ca-gregory"}}});
  expected_a.set_time_zones({TimeZoneId{.id = "UTC"}});
  expected_a.set_temperature_unit(TemperatureUnit::CELSIUS);

  auto client = GetClient();

  Profile actual;
  client->GetProfile([&](Profile profile) { actual = std::move(profile); });
  RunLoopUntilIdle();
  ASSERT_EQ(expected_a, actual);

  bool changed = false;
  client.events().OnChange = [&]() { changed = true; };
  RunLoopUntilIdle();
  ASSERT_FALSE(changed);

  setui_service_->SetIntl(NewSettings({"ru-RU"}, HourCycle::H23, TemperatureUnit::CELSIUS));
  RunLoopUntilIdle();
  ASSERT_TRUE(changed);

  Profile expected_b{};
  expected_b.set_locales({LocaleId{.id = "ru-RU-u"
                                         "-ca-gregory"
                                         "-fw-mon"
                                         "-hc-h23"
                                         "-ms-metric"
                                         "-nu-latn"
                                         "-tz-utc"}});
  expected_b.set_calendars({{CalendarId{.id = "und-u-ca-gregory"}}});
  expected_b.set_time_zones({TimeZoneId{.id = "UTC"}});
  expected_b.set_temperature_unit(TemperatureUnit::CELSIUS);

  client->GetProfile([&](Profile profile) { actual = std::move(profile); });
  RunLoopUntilIdle();
  EXPECT_EQ(expected_b, actual);
}

TEST_F(IntlPropertyProviderImplTest, SettingMix) {
  setui_service_->SetIntl(NewSettings({"nl-NL"}, HourCycle::H12, TemperatureUnit::CELSIUS));
  setui_service_->SetTimeZone("Europe/Amsterdam");
  RunLoopUntilIdle();

  auto client = GetClient();

  Profile actual;
  client->GetProfile([&](Profile profile) { actual = std::move(profile); });
  RunLoopUntilIdle();

  Profile expected{};
  expected.set_locales({LocaleId{.id = "nl-NL-u"
                                       "-ca-gregory"
                                       "-fw-mon"
                                       "-hc-h12"
                                       "-ms-metric"
                                       "-nu-latn"
                                       "-tz-nlams"}});
  expected.set_calendars({{CalendarId{.id = "und-u-ca-gregory"}}});
  expected.set_time_zones({TimeZoneId{.id = "Europe/Amsterdam"}});
  expected.set_temperature_unit(TemperatureUnit::CELSIUS);

  EXPECT_EQ(expected, actual);

  setui_service_->SetIntl(NewSettings({"nl-NL"}, HourCycle::H23, TemperatureUnit::CELSIUS));
  RunLoopUntilIdle();

  client->GetProfile([&](Profile profile) { actual = std::move(profile); });
  RunLoopUntilIdle();

  expected.set_locales({LocaleId{.id = "nl-NL-u"
                                       "-ca-gregory"
                                       "-fw-mon"
                                       "-hc-h23"
                                       "-ms-metric"
                                       "-nu-latn"
                                       "-tz-nlams"}});
  expected.set_calendars({{CalendarId{.id = "und-u-ca-gregory"}}});
  expected.set_time_zones({TimeZoneId{.id = "Europe/Amsterdam"}});
  expected.set_temperature_unit(TemperatureUnit::CELSIUS);

  EXPECT_EQ(expected, actual);
}

TEST_F(IntlPropertyProviderImplTest, Multilocale) {
  setui_service_->SetIntl(
      NewSettings({"nl-NL", "nl-BE", "nl", "fr-FR"}, HourCycle::H12, TemperatureUnit::CELSIUS));
  setui_service_->SetTimeZone("Europe/Amsterdam");
  RunLoopUntilIdle();

  auto client = GetClient();

  Profile actual;
  client->GetProfile([&](Profile profile) { actual = std::move(profile); });
  RunLoopUntilIdle();

  Profile expected{};
  expected.set_locales({
      LocaleId{.id = "nl-NL-u-ca-gregory-fw-mon-hc-h12-ms-metric-nu-latn-tz-nlams"},
      LocaleId{.id = "nl-BE-u-ca-gregory-fw-mon-hc-h12-ms-metric-nu-latn-tz-nlams"},
      LocaleId{.id = "nl-u-ca-gregory-fw-mon-hc-h12-ms-metric-nu-latn-tz-nlams"},
      LocaleId{.id = "fr-FR-u-ca-gregory-fw-mon-hc-h12-ms-metric-nu-latn-tz-nlams"},
  });
  expected.set_calendars({{CalendarId{.id = "und-u-ca-gregory"}}});
  expected.set_time_zones({TimeZoneId{.id = "Europe/Amsterdam"}});
  expected.set_temperature_unit(TemperatureUnit::CELSIUS);

  EXPECT_EQ(expected, actual);
}

}  // namespace
}  // namespace modular_testing

// This test has its own main because we want the logger to be turned on.
int main(int argc, char** argv) {
  syslog::InitLogger();
  if (!fxl::SetTestSettings(argc, argv)) {
    FXL_LOG(ERROR) << "Failed to parse log settings from command-line";
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  tracing::test::InitComponentContext();

  return RUN_ALL_TESTS();
}
