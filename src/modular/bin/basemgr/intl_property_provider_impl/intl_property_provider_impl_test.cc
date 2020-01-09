// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/intl_property_provider_impl/intl_property_provider_impl.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/setui/cpp/fidl.h>
#include <fuchsia/setui/cpp/fidl_test_base.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "garnet/bin/trace/tests/component_context.h"
#include "lib/fostr/fidl/fuchsia/intl/formatting.h"
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
using modular::IntlPropertyProviderImpl;
using sys::testing::ComponentContextProvider;

// Constructs a SettingsObject to be returned to the watchers based on the supplied IANA timezone
// ID (e.g. "America/New_York").
fuchsia::setui::SettingsObject SettingFromTimezone(const std::string& tz_id) {
  fuchsia::setui::TimeZoneInfo time_zone_info;
  time_zone_info.current = fuchsia::setui::TimeZone::New();
  time_zone_info.current->id = tz_id;

  fuchsia::setui::SettingsObject object{
      .setting_type = fuchsia::setui::SettingType::TIME_ZONE,
  };

  fuchsia::setui::SettingData setting_data;
  setting_data.set_time_zone_value(std::move(time_zone_info));

  EXPECT_EQ(ZX_OK, setting_data.Clone(&object.data));
  return object;
}

// Constructs a valid settings object based on the intl settings.
fuchsia::setui::SettingsObject SettingFromIntl(const fuchsia::setui::IntlSettings& settings) {
  EXPECT_FALSE(settings.locales.empty())
      << "Locales must have at least one entry by fuchsia.intl.ProfileProvider spec";
  fuchsia::setui::SettingData setting_data;
  setting_data.set_intl(modular::CloneStruct(settings));

  fuchsia::setui::SettingsObject object{
      .setting_type = fuchsia::setui::SettingType::INTL,
  };
  EXPECT_EQ(ZX_OK, setting_data.Clone(&object.data));
  return object;
}

class FakeSetUiService : public fuchsia::setui::testing::SetUiService_TestBase {
 public:
  FakeSetUiService()
      : timezone_settings_(SettingFromTimezone("UTC")),
        intl_settings_(SettingFromIntl(fuchsia::setui::IntlSettings{
            // At least one locale must be present.
            .locales = {"en-US"},
            .hour_cycle = fuchsia::setui::HourCycle::H12,
            .temperature_unit = fuchsia::setui::TemperatureUnit::FAHRENHEIT,
        })) {}

  fidl::InterfaceRequestHandler<fuchsia::setui::SetUiService> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return bindings_.GetHandler(static_cast<fuchsia::setui::SetUiService*>(this), dispatcher);
  }

  // Test method, used to modify the timezone identifier served by the fake setui service.
  void SetTimeZone(const std::string& iana_tz_id) {
    auto& current_time_zone = timezone_settings_.data.time_zone_value().current;
    ASSERT_NE(current_time_zone, nullptr);
    if (iana_tz_id == current_time_zone->id) {
      return;
    }
    fuchsia::setui::SettingsObject new_settings = SettingFromTimezone(iana_tz_id);
    ASSERT_EQ(ZX_OK, new_settings.Clone(&timezone_settings_));
    NotifyAll(fuchsia::setui::SettingType::TIME_ZONE);
  }

  // Test method, used to modify the fake intl data that this fake service implementation will
  // serve.
  void SetIntl(const fuchsia::setui::IntlSettings& intl_settings) {
    // TODO(fmil): Sort out the naming here.
    fuchsia::setui::SettingsObject intl_object = SettingFromIntl(intl_settings);
    if (fidl::Equals(intl_settings_, intl_object)) {
      return;
    }
    EXPECT_EQ(ZX_OK, intl_object.Clone(&intl_settings_));
    NotifyAll(fuchsia::setui::SettingType::INTL);
  }

  // Called on all other methods that this fake does not implement.
  void NotImplemented_(const std::string& name) override {
    FAIL() << "Method not implemented: " << name;
  }

  // Returns the fake settings that are associated with the supplied setting type.
  const fuchsia::setui::SettingsObject& TypedSettings(fuchsia::setui::SettingType type) {
    // This is a bit simplistic, but should be enough for our use.
    return (type == fuchsia::setui::SettingType::TIME_ZONE) ? timezone_settings_ : intl_settings_;
  }

  // `fuchsia.setuiservice.Watch`
  void Watch(fuchsia::setui::SettingType type, WatchCallback callback) override {
    callback(modular::CloneStruct(TypedSettings(type)));
  }

  // `fuchsia.setuiservice.Listen`.  Forwards all changes for the setting `type` to the supplied
  // listener by calling its Notify method.
  void Listen(fuchsia::setui::SettingType type,
              ::fidl::InterfaceHandle<class fuchsia::setui::SettingListener> listener) override {
    auto listener_ptr = listener.Bind();
    listener_ptr->Notify(modular::CloneStruct(TypedSettings(type)));
    listeners_[type].emplace_back(std::move(listener_ptr));
  }

 private:
  // Notifies all listeners of the current settings.  May only be called immediately after, the
  // settings actually have been modified.
  void NotifyAll(fuchsia::setui::SettingType type) {
    const auto& typed_listeners = listeners_.find(type);
    if (typed_listeners == end(listeners_)) {
      // No listeners registered for this type.
      return;
    }
    for (auto& listener : typed_listeners->second) {
      listener->Notify(modular::CloneStruct(TypedSettings(type)));
    }
  }

  // The server-side connection for SetUiService.
  fidl::BindingSet<fuchsia::setui::SetUiService> bindings_;

  // The listeners that have registered for receiving a particular event type.
  std::map<fuchsia::setui::SettingType, std::vector<fuchsia::setui::SettingListenerPtr>> listeners_;

  // The current timezone settings.
  fuchsia::setui::SettingsObject timezone_settings_;
  // The current intl settings.
  fuchsia::setui::SettingsObject intl_settings_;
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
    setui_service_ = std::make_unique<FakeSetUiService>();
    ASSERT_EQ(ZX_OK, provider_.service_directory_provider()->AddService(
                         setui_service_->GetHandler(dispatcher())));
    fuchsia::setui::SetUiServicePtr setui_client =
        provider_.context()->svc()->Connect<fuchsia::setui::SetUiService>();

    instance_ = std::make_unique<IntlPropertyProviderImpl>(std::move(setui_client));
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
  std::unique_ptr<FakeSetUiService> setui_service_;

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
  setui_service_->SetIntl(fuchsia::setui::IntlSettings{
      .locales = {"nl-NL"},
      .hour_cycle = fuchsia::setui::HourCycle::H12,
      .temperature_unit = fuchsia::setui::TemperatureUnit::CELSIUS,
  });
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

  setui_service_->SetIntl(fuchsia::setui::IntlSettings{
      .locales = {"ru-RU"},
      .hour_cycle = fuchsia::setui::HourCycle::H23,
      .temperature_unit = fuchsia::setui::TemperatureUnit::CELSIUS,
  });
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
  setui_service_->SetTimeZone("Europe/Amsterdam");
  setui_service_->SetIntl(fuchsia::setui::IntlSettings{
      .locales = {"nl-NL"},
      .hour_cycle = fuchsia::setui::HourCycle::H12,
      .temperature_unit = fuchsia::setui::TemperatureUnit::CELSIUS,
  });
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

  setui_service_->SetIntl(fuchsia::setui::IntlSettings{
      .locales = {"nl-NL"},
      .hour_cycle = fuchsia::setui::HourCycle::H23,
      .temperature_unit = fuchsia::setui::TemperatureUnit::CELSIUS,
  });
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
  setui_service_->SetTimeZone("Europe/Amsterdam");
  setui_service_->SetIntl(fuchsia::setui::IntlSettings{
      .locales = {"nl-NL", "nl-BE", "nl", "fr-FR"},
      .hour_cycle = fuchsia::setui::HourCycle::H12,
      .temperature_unit = fuchsia::setui::TemperatureUnit::CELSIUS,
  });
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
