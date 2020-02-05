// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intl_property_provider_impl.h"

#include <fuchsia/intl/cpp/fidl.h>

#include <iterator>

#include <src/lib/icu_data/cpp/icu_data.h>
#include <src/modular/lib/fidl/clone.h>

#include "fuchsia/setui/cpp/fidl.h"
#include "lib/fostr/fidl/fuchsia/intl/formatting.h"
#include "locale_util.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/calendar.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace modular {

using fuchsia::intl::CalendarId;
using fuchsia::intl::LocaleId;
using fuchsia::intl::Profile;
using fuchsia::intl::TemperatureUnit;
using fuchsia::intl::TimeZoneId;
using fuchsia::modular::intl::internal::HourCycle;
using fuchsia::modular::intl::internal::RawProfileData;
using intl::ExpandLocaleId;
using intl::ExtractBcp47CalendarId;
using intl::LocaleIdToIcuLocale;
using intl::LocaleKeys;

namespace {

const std::string kDefaultTimeZoneId = "America/Los_Angeles";

// Returns the basis from which final values for RawProfileData are obtained.
RawProfileData GetDefaultRawData(const std::optional<RawProfileData>& prototype) {
  return prototype.has_value() ? CloneStruct(*prototype)
                               : RawProfileData{
                                     .language_tags = {LocaleId{.id = "en-US"}},
                                     .time_zone_ids = {TimeZoneId{.id = kDefaultTimeZoneId}},
                                     .calendar_ids = {CalendarId{.id = "und-u-ca-gregory"}},
                                     .temperature_unit = TemperatureUnit::FAHRENHEIT,
                                 };
}

// Collect key-value pairs of Unicode locale properties that will be applied to
// each locale ID.
fit::result<std::map<std::string, std::string>, zx_status_t> GetUnicodeExtensionsForDenormalization(
    const modular::RawProfileData& raw_data) {
  auto primary_calendar_id_result = ExtractBcp47CalendarId(raw_data.calendar_ids[0]);
  if (primary_calendar_id_result.is_error()) {
    FX_LOGS(ERROR) << "Bad calendar ID: " << raw_data.calendar_ids[0];
    return fit::error(primary_calendar_id_result.error());
  }
  const std::string& primary_calendar_id = primary_calendar_id_result.value();

  const std::string& primary_tz_id_iana = raw_data.time_zone_ids[0].id;
  const char* primary_tz_id =
      uloc_toUnicodeLocaleType(LocaleKeys::kTimeZone.c_str(), primary_tz_id_iana.c_str());
  if (primary_tz_id == nullptr) {
    FX_LOGS(ERROR) << "Bad time zone ID: " << primary_tz_id_iana;
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  std::map<std::string, std::string> extensions{{LocaleKeys::kCalendar, primary_calendar_id},
                                                {LocaleKeys::kTimeZone, primary_tz_id}};
  if (raw_data.hour_cycle != nullptr) {
    switch (raw_data.hour_cycle->setting) {
      case fuchsia::setui::HourCycle::H12:
        extensions[LocaleKeys::kHourCycle] = "h12";
        break;
      case fuchsia::setui::HourCycle::H23:
        extensions[LocaleKeys::kHourCycle] = "h23";
        break;
      default:
        // If we ever add a different hour cycle setting, e.g. "locale default", this will work.
        // So, a bit of future-proofing here.  I wonder if it's going to be such.
        break;
    }
  }
  return fit::ok(extensions);
}

fit::result<Profile, zx_status_t> GenerateProfile(const modular::RawProfileData& raw_data) {
  if (raw_data.language_tags.empty()) {
    FX_LOGS(ERROR) << "GenerateProfile called with empty raw locale IDs";
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  auto unicode_extensions_result = GetUnicodeExtensionsForDenormalization(raw_data);
  if (unicode_extensions_result.is_error()) {
    return fit::error(unicode_extensions_result.error());
  }

  const auto unicode_extensions = unicode_extensions_result.value();

  std::vector<icu::Locale> icu_locales;
  for (const auto& locale_id : raw_data.language_tags) {
    auto icu_locale_result = LocaleIdToIcuLocale(locale_id, unicode_extensions);
    if (icu_locale_result.is_error()) {
      FX_LOGS(WARNING) << "Failed to build locale for " << locale_id;
    } else {
      icu_locales.push_back(icu_locale_result.value());
    }
  }

  Profile profile;
  // Update locales
  for (auto& icu_locale : icu_locales) {
    fit::result<LocaleId, zx_status_t> locale_id_result = ExpandLocaleId(icu_locale);
    if (locale_id_result.is_ok()) {
      profile.mutable_locales()->push_back(locale_id_result.value());
    }
    // Errors are logged inside ExpandLocaleId
  }

  if (!profile.has_locales() || profile.locales().empty()) {
    FX_LOGS(ERROR) << "No valid locales could be built";
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  // Update calendars
  auto mutable_calendars = profile.mutable_calendars();
  mutable_calendars->insert(std::end(*mutable_calendars), std::begin(raw_data.calendar_ids),
                            std::end(raw_data.calendar_ids));

  // Update time zones
  auto mutable_time_zones = profile.mutable_time_zones();
  mutable_time_zones->insert(std::end(*mutable_time_zones), std::begin(raw_data.time_zone_ids),
                             std::end(raw_data.time_zone_ids));

  // Update rest
  profile.set_temperature_unit(raw_data.temperature_unit);
  // TODO(kpozin): Consider inferring temperature unit from region if missing.

  return fit::ok(std::move(profile));
}

// Extracts just the timezone ID from the setting object.  If the setting is not
// well-formed or not valid, no value is returned.
std::optional<std::string> TimeZoneIdFrom(const fuchsia::setui::SettingsObject& setting) {
  if (setting.setting_type != fuchsia::setui::SettingType::TIME_ZONE) {
    // Should never happen since the Watch/Listen protocol ensures the setting matches.
    return std::nullopt;
  }
  const fuchsia::setui::TimeZoneInfo& timezone_info = setting.data.time_zone_value();
  if (timezone_info.current == nullptr) {
    return std::nullopt;
  }
  const auto* timezone = setting.data.time_zone_value().current.get();
  if (timezone_info.current->id.empty()) {
    // Weird data in the timezone field causes us to not update anything.
    return std::nullopt;
  }
  return std::string(timezone->id);
}

// Safely extracts intl settings from a union.
std::optional<fuchsia::setui::IntlSettings> IntlSettingsFrom(
    const fuchsia::setui::SettingsObject& setting) {
  if (!setting.data.is_intl()) {
    return std::nullopt;
  }
  return setting.data.intl();
}

// Merges the timezone settings into new profile data.
void MergeTimeZone(const std::optional<std::string>& timezone_id,
                   RawProfileData* new_profile_data) {
  if (!timezone_id.has_value()) {
    return;
  }
  // Merge the new value with the old.
  new_profile_data->time_zone_ids = {TimeZoneId{.id = *timezone_id}};
}

// Merges the intl settings into the new profile data.
void MergeIntl(const std::optional<fuchsia::setui::IntlSettings>& intl_settings,
               RawProfileData* new_profile_data) {
  if (!intl_settings.has_value()) {
    return;
  }
  // Replace the old settings with the new.
  switch (intl_settings->temperature_unit) {
    case fuchsia::setui::TemperatureUnit::CELSIUS:
      new_profile_data->temperature_unit = fuchsia::intl::TemperatureUnit::CELSIUS;
      break;
    case fuchsia::setui::TemperatureUnit::FAHRENHEIT:
      new_profile_data->temperature_unit = fuchsia::intl::TemperatureUnit::FAHRENHEIT;
      break;
    default:
      FX_LOGS(WARNING) << "fuchsia.setui gave us an unknown temperature unit enum value: "
                       << static_cast<uint32_t>(intl_settings->temperature_unit);
  }
  if (!intl_settings->locales.empty()) {
    // Do not touch the current locale settings if setui tells us there are no languages
    // set.
    new_profile_data->language_tags.clear();
    for (const auto& locale : intl_settings->locales) {
      new_profile_data->language_tags.emplace_back(fuchsia::intl::LocaleId{.id = locale});
    }
  } else {
    FX_LOGS(WARNING)
        << "fuchsia.setui returned locale settings with no locales; this is not a valid "
           "fuchsia.intl.Profile; not touching the current language settings and proceeding.";
  }
  // Setui does not have a way to leave hour cycle setting to the locale, so we always set it
  // here.  However, if an option comes in to set it, we can do that too.
  new_profile_data->hour_cycle = std::make_unique<HourCycle>(HourCycle{
      .setting = intl_settings->hour_cycle,
  });
}

// Sinks the setting into new_profile_data, by overwriting the content of new_profile_data with the
// content provided by setting.
void Merge(const fuchsia::setui::SettingsObject& setting, RawProfileData* new_profile_data) {
  FX_CHECK(new_profile_data != nullptr);
  // Using the same Notify function for all settings type, here we process on
  // a case by case basis.
  const auto setting_type = setting.setting_type;
  switch (setting_type) {
    case fuchsia::setui::SettingType::TIME_ZONE: {
      const auto timezone_id = TimeZoneIdFrom(setting);
      MergeTimeZone(timezone_id, new_profile_data);
    } break;
    case fuchsia::setui::SettingType::INTL: {
      const auto intl = IntlSettingsFrom(setting);
      MergeIntl(intl, new_profile_data);
    } break;
    default:
      // The default branch should, in theory, not trigger since in the setup code we subscribe
      // only to specific SettingsType values.   If it does, it could be a bug on the server side,
      // or could be that we have a new setting interest but have not registered to process it.

      // operator<< is not implemented for SettingType, so we just print the corresponding
      // unsigned value.  See FIDL definition of the enum for the value mapping.
      FX_LOGS(WARNING) << "Got unexpected setting type: " << static_cast<uint32_t>(setting_type);
      break;
  }
}

// Load initial ICU data if this hasn't been done already.
//
// TODO(kpozin): Eventually, this should solely be the responsibility of the client component that
// links `IntlPropertyProviderImpl`, which has a better idea of what parameters ICU should be
// initialized with.
zx_status_t InitializeIcuIfNeeded() {
  // It's okay if something else in the same process has already initialized
  // ICU.
  zx_status_t status = icu_data::Initialize();
  switch (status) {
    case ZX_OK:
    case ZX_ERR_ALREADY_BOUND:
      return ZX_OK;
    default:
      return status;
  }
}

// Used to initialize the first, "empty" timezone info.
fuchsia::setui::SettingsObject InitialSettingsObject() {
  auto data = fuchsia::setui::SettingData::New();
  data->set_time_zone_value(fuchsia::setui::TimeZoneInfo{});
  fuchsia::setui::SettingsObject object{
      .setting_type = fuchsia::setui::SettingType::TIME_ZONE,
  };
  FX_CHECK(data->Clone(&object.data) == ZX_OK);
  return object;
}

}  // namespace

IntlPropertyProviderImpl::IntlPropertyProviderImpl(fuchsia::setui::SetUiServicePtr setui_client)
    : intl_profile_(std::nullopt),
      raw_profile_data_(std::nullopt),
      setui_client_(std::move(setui_client)),
      setting_listener_binding_(this),
      setting_listener_binding_intl_(this),
      initial_settings_object_(InitialSettingsObject()) {
  Start();
}

// static
std::unique_ptr<IntlPropertyProviderImpl> IntlPropertyProviderImpl::Create(
    const std::shared_ptr<sys::ServiceDirectory>& incoming_services) {
  fuchsia::setui::SetUiServicePtr setui_client =
      incoming_services->Connect<fuchsia::setui::SetUiService>();
  return std::make_unique<IntlPropertyProviderImpl>(std::move(setui_client));
}

fidl::InterfaceRequestHandler<fuchsia::intl::PropertyProvider> IntlPropertyProviderImpl::GetHandler(
    async_dispatcher_t* dispatcher) {
  return property_provider_bindings_.GetHandler(this, dispatcher);
}

void IntlPropertyProviderImpl::Start() {
  if (InitializeIcuIfNeeded() != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize ICU data";
    return;
  }
  LoadInitialValues();
}

void IntlPropertyProviderImpl::GetProfile(
    fuchsia::intl::PropertyProvider::GetProfileCallback callback) {
  FX_VLOGS(1) << "Received GetProfile request";
  get_profile_queue_.push(std::move(callback));
  ProcessGetProfileQueue();
}

void IntlPropertyProviderImpl::LoadInitialValues() {
  auto set_initial_data = [this](const fuchsia::setui::SettingsObject& setting) {
    NotifyInternal(setting);
    // TODO(kpozin): Consider setting some other error handler for non-initial errors.
    setui_client_.set_error_handler(nullptr);
    StartSettingsWatcher(setting.setting_type);
  };

  setui_client_.set_error_handler(
      [this, set_initial_data](zx_status_t status __attribute__((unused))) {
        // Sending an initial data request with empty time zone will initialize the time
        // zone and other settings to their default empty values.
        set_initial_data(initial_settings_object_);
      });

  auto watch_callback = [set_initial_data](fuchsia::setui::SettingsObject setting) {
    set_initial_data(setting);
  };

  setui_client_->Watch(fuchsia::setui::SettingType::TIME_ZONE, watch_callback);
  setui_client_->Watch(fuchsia::setui::SettingType::INTL, watch_callback);
}

void IntlPropertyProviderImpl::StartSettingsWatcher(fuchsia::setui::SettingType type) {
  fidl::InterfaceHandle<fuchsia::setui::SettingListener> handle;
  auto& binding = (type == fuchsia::setui::SettingType::TIME_ZONE) ? setting_listener_binding_
                                                                   : setting_listener_binding_intl_;
  binding.Bind(handle.NewRequest());
  setui_client_->Listen(type, std::move(handle));
}

fit::result<Profile, zx_status_t> IntlPropertyProviderImpl::GetProfileInternal() {
  if (!intl_profile_) {
    Profile profile;
    if (!IsRawDataInitialized()) {
      return fit::error(ZX_ERR_SHOULD_WAIT);
    }
    auto result = GenerateProfile(*raw_profile_data_);
    if (result.is_ok()) {
      intl_profile_ = result.take_value();
    } else {
      FX_LOGS(WARNING) << "Couldn't generate profile: " << result.error();
      return result;
    }
  }
  return fit::ok(CloneStruct(*intl_profile_));
}

bool IntlPropertyProviderImpl::IsRawDataInitialized() { return raw_profile_data_.has_value(); }

bool IntlPropertyProviderImpl::UpdateRawData(modular::RawProfileData& new_raw_data) {
  if (IsRawDataInitialized() && fidl::Equals(*raw_profile_data_, new_raw_data)) {
    return false;
  }
  raw_profile_data_ = std::move(new_raw_data);
  // Invalidate the existing cached profile.
  intl_profile_ = std::nullopt;
  FX_VLOGS(1) << "Updated raw data";
  NotifyOnChange();
  ProcessGetProfileQueue();
  return true;
}

void IntlPropertyProviderImpl::Notify(fuchsia::setui::SettingsObject setting) {
  NotifyInternal(setting);
}

void IntlPropertyProviderImpl::NotifyInternal(const fuchsia::setui::SettingsObject& setting) {
  RawProfileData new_profile_data = GetDefaultRawData(raw_profile_data_);
  Merge(setting, &new_profile_data);
  UpdateRawData(new_profile_data);
}

void IntlPropertyProviderImpl::NotifyOnChange() {
  FX_VLOGS(1) << "NotifyOnChange";
  for (auto& binding : property_provider_bindings_.bindings()) {
    binding->events().OnChange();
  }
}

void IntlPropertyProviderImpl::ProcessGetProfileQueue() {
  if (!IsRawDataInitialized()) {
    FX_VLOGS(1) << "Raw data not yet initialized";
    return;
  }

  auto profile_result = GetProfileInternal();
  if (profile_result.is_error()) {
    FX_VLOGS(1) << "Profile not updated: error was: " << profile_result.error();
    return;
  }

  FX_VLOGS(1) << "Processing request queue (" << get_profile_queue_.size() << ")";
  while (!get_profile_queue_.empty()) {
    auto& callback = get_profile_queue_.front();
    auto var = CloneStruct(profile_result.value());
    callback(std::move(var));
    get_profile_queue_.pop();
  }
}

}  // namespace modular
