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

#include <src/modular/lib/fidl/clone.h>

#include "lib/fostr/fidl/fuchsia/intl/formatting.h"
#include "src/lib/fidl_fuchsia_intl_ext/cpp/fidl_ext.h"
#include "src/lib/fxl/log_settings.h"

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

class FakeSetUiService : public fuchsia::setui::testing::SetUiService_TestBase {
 public:
  FakeSetUiService() : settings_(SettingFromTimezone("UTC")) {}

  fidl::InterfaceRequestHandler<fuchsia::setui::SetUiService> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return bindings_.GetHandler(static_cast<fuchsia::setui::SetUiService*>(this), dispatcher);
  }

  // Test method, used to modify the timezone identifier served by the fake setui service.
  void SetTimeZone(const std::string& iana_tz_id) {
    auto& current_time_zone = settings_.data.time_zone_value().current;
    ASSERT_NE(current_time_zone, nullptr);
    if (iana_tz_id != current_time_zone->id) {
      fuchsia::setui::SettingsObject new_settings = SettingFromTimezone(iana_tz_id);
      ASSERT_EQ(ZX_OK, new_settings.Clone(&settings_));
      NotifyAll();
    }
  }

  // Called on all other methods that this fake does not implement.
  void NotImplemented_(const std::string& name) override {
    FAIL() << "Method not implemented: " << name;
  }

  // `fuchsia.setuiservice.Watch`
  void Watch(fuchsia::setui::SettingType unused, WatchCallback callback) override {
    callback(modular::CloneStruct(settings_));
  }

  // `fuchsia.setuiservice.Listen`.  Forwards all changes for the setting `type` to the supplied
  // listener by calling its Notify method.
  void Listen(fuchsia::setui::SettingType type,
              ::fidl::InterfaceHandle<class fuchsia::setui::SettingListener> listener) override {
    auto listener_ptr = listener.Bind();
    listener_ptr->Notify(modular::CloneStruct(settings_));
    listeners_.emplace_back(std::move(listener_ptr));
  }

 private:
  // Constructs a SettingsObject to be returned to the watchers based on the supplied IANA timezone
  // ID (e.g. "America/New_York").
  static fuchsia::setui::SettingsObject SettingFromTimezone(const std::string& tz_id) {
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

  // Notifies all listeners of the current settings.  May only be called immediately after, the
  // settings actually have been modified.
  void NotifyAll() {
    for (auto& listener : listeners_) {
      listener->Notify(modular::CloneStruct(settings_));
    }
  }

  // The server-side connection for SetUiService.
  fidl::BindingSet<fuchsia::setui::SetUiService> bindings_;

  // The listeners that have registered themselves through the `Listen` call.  These listeners will
  // be notified of any changes.
  std::vector<fuchsia::setui::SettingListenerPtr> listeners_;

  // The current settings object, for the time being only containing the settings for the time zone.
  fuchsia::setui::SettingsObject settings_;
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

}  // namespace
}  // namespace modular_testing
