// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intl_property_provider_impl.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/intl/merge/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <zircon/status.h>

#include <iterator>

#include <src/lib/icu_data/cpp/icu_data.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fostr/fidl/fuchsia/intl/formatting.h"
#include "lib/fostr/fidl/fuchsia/settings/formatting.h"
#include "lib/zx/time.h"
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
using fuchsia::settings::HourCycle;
using intl::ExpandLocaleId;
using intl::ExtractBcp47CalendarId;
using intl::LocaleIdToIcuLocale;
using intl::LocaleKeys;

namespace {

// Returns the default settings for the merged data.
fuchsia::intl::merge::Data DataDefaults() {
  fuchsia::intl::merge::Data ret;
  ret.set_language_tags({LocaleId{.id = "en-US"}});
  ret.set_time_zone_ids({TimeZoneId{.id = "America/Los_Angeles"}});
  ret.set_calendar_ids({CalendarId{.id = "und-u-ca-gregory"}});
  ret.set_temperature_unit(TemperatureUnit::FAHRENHEIT);
  return ret;
}

// Returns the basis from which final values for RawProfileData are obtained.
fuchsia::intl::merge::Data GetDefaultRawData(
    const std::optional<fuchsia::intl::merge::Data>& prototype) {
  static const fuchsia::intl::merge::Data default_merge_data = DataDefaults();
  return prototype.has_value() ? fidl::Clone(*prototype) : fidl::Clone(default_merge_data);
}

// Collect key-value pairs of Unicode locale properties that will be applied to
// each locale ID.
fit::result<std::map<std::string, std::string>, zx_status_t> GetUnicodeExtensionsForDenormalization(
    const fuchsia::intl::merge::Data& raw_data) {
  auto primary_calendar_id_result = ExtractBcp47CalendarId(raw_data.calendar_ids()[0]);
  if (primary_calendar_id_result.is_error()) {
    FX_LOGS(ERROR) << "Bad calendar ID: " << raw_data.calendar_ids()[0];
    return fit::error(primary_calendar_id_result.error());
  }
  const std::string& primary_calendar_id = primary_calendar_id_result.value();

  const std::string& primary_tz_id_iana = raw_data.time_zone_ids()[0].id;
  const char* primary_tz_id =
      uloc_toUnicodeLocaleType(LocaleKeys::kTimeZone.c_str(), primary_tz_id_iana.c_str());
  if (primary_tz_id == nullptr) {
    FX_LOGS(ERROR) << "Bad time zone ID: " << primary_tz_id_iana;
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  std::map<std::string, std::string> extensions{{LocaleKeys::kCalendar, primary_calendar_id},
                                                {LocaleKeys::kTimeZone, primary_tz_id}};
  if (raw_data.has_hour_cycle()) {
    switch (raw_data.hour_cycle()) {
      case HourCycle::H12:
        extensions[LocaleKeys::kHourCycle] = "h12";
        break;
      case HourCycle::H23:
        extensions[LocaleKeys::kHourCycle] = "h23";
        break;
      default:
        // Unknown.
        break;
    }
  }
  return fit::ok(extensions);
}

fit::result<Profile, zx_status_t> GenerateProfile(const fuchsia::intl::merge::Data& raw_data) {
  if (raw_data.language_tags().empty()) {
    FX_LOGS(ERROR) << "GenerateProfile called with empty raw locale IDs";
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  auto unicode_extensions_result = GetUnicodeExtensionsForDenormalization(raw_data);
  if (unicode_extensions_result.is_error()) {
    return fit::error(unicode_extensions_result.error());
  }

  const auto unicode_extensions = unicode_extensions_result.value();

  std::vector<icu::Locale> icu_locales;
  for (const auto& locale_id : raw_data.language_tags()) {
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
  auto* mutable_calendars = profile.mutable_calendars();
  const auto& calendars = raw_data.calendar_ids();
  mutable_calendars->insert(std::end(*mutable_calendars), std::begin(calendars),
                            std::end(calendars));

  // Update time zones
  auto* mutable_time_zones = profile.mutable_time_zones();
  const auto& time_zones = raw_data.time_zone_ids();
  mutable_time_zones->insert(std::end(*mutable_time_zones), std::begin(time_zones),
                             std::end(time_zones));

  // Update rest
  if (raw_data.has_temperature_unit()) {
    profile.set_temperature_unit(raw_data.temperature_unit());
  }
  // TODO(kpozin): Consider inferring temperature unit from region if missing.

  return fit::ok(std::move(profile));
}

// Extracts just the timezone ID from the setting object.  If the setting is not
// well-formed or not valid, no value is returned.
std::optional<std::string> TimeZoneIdFrom(const fuchsia::settings::IntlSettings& setting) {
  if (!setting.has_time_zone_id()) {
    return std::nullopt;
  }
  return setting.time_zone_id().id;
}

// Merges the timezone settings into new profile data.
void MergeTimeZone(const std::optional<std::string>& timezone_id,
                   fuchsia::intl::merge::Data* new_profile_data) {
  if (!timezone_id.has_value()) {
    return;
  }
  // Merge the new value with the old.
  new_profile_data->set_time_zone_ids({TimeZoneId{.id = *timezone_id}});
}

// Merges the intl settings into the new profile data.
void MergeIntl(const fuchsia::settings::IntlSettings& intl_settings,
               fuchsia::intl::merge::Data* new_profile_data) {
  // Replace the old settings with the new.
  new_profile_data->set_temperature_unit(intl_settings.temperature_unit());
  // Do not touch the current locale settings if setui tells us there are no languages
  // set.
  const std::vector<fuchsia::intl::LocaleId>& locale_ids = intl_settings.locales();
  if (!locale_ids.empty()) {
    new_profile_data->set_language_tags(locale_ids);
  } else {
    FX_LOGS(WARNING)
        << "fuchsia.setui returned locale settings with no locales; this is not a valid "
           "fuchsia.intl.Profile; not touching the current language settings and proceeding.";
  }
  if (intl_settings.has_hour_cycle()) {
    new_profile_data->set_hour_cycle(intl_settings.hour_cycle());
  }
}

// Sinks the setting into new_profile_data, by overwriting the content of new_profile_data with the
// content provided by setting.
void Merge(const fuchsia::settings::IntlSettings& setting,
           fuchsia::intl::merge::Data* new_profile_data) {
  FX_CHECK(new_profile_data != nullptr);
  const auto timezone_id = TimeZoneIdFrom(setting);
  MergeTimeZone(timezone_id, new_profile_data);
  MergeIntl(setting, new_profile_data);
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

}  // namespace

IntlPropertyProviderImpl::IntlPropertyProviderImpl(fuchsia::settings::IntlPtr settings_client_)
    : intl_profile_(std::nullopt),
      raw_profile_data_(std::nullopt),
      settings_client_(std::move(settings_client_)) {
  Start();
}

// static
std::unique_ptr<IntlPropertyProviderImpl> IntlPropertyProviderImpl::Create(
    const std::shared_ptr<sys::ServiceDirectory>& incoming_services) {
  fuchsia::settings::IntlPtr client = incoming_services->Connect<fuchsia::settings::Intl>();
  return std::make_unique<IntlPropertyProviderImpl>(std::move(client));
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
  settings_client_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "settings_client error: " << zx_status_get_string(status);
  });
  StartSettingsWatcher();
}

void IntlPropertyProviderImpl::GetProfile(
    fuchsia::intl::PropertyProvider::GetProfileCallback callback) {
  FX_VLOGS(1) << "Received GetProfile request";
  get_profile_queue_.push(std::move(callback));
  ProcessProfileRequests();
}

void IntlPropertyProviderImpl::StartSettingsWatcher() {
  settings_client_->Watch(
      [this](fit::result<fuchsia::settings::IntlSettings, fuchsia::settings::Error> result) {
        if (result.is_error()) {
          FX_LOGS(ERROR) << "watch failed: " << result.error();
        } else {
          const fuchsia::settings::IntlSettings& response = result.value();
          FX_VLOGS(2) << "New settings value: " << response;
          fuchsia::intl::merge::Data new_profile_data = GetDefaultRawData(raw_profile_data_);
          Merge(response, &new_profile_data);
          UpdateRawData(std::move(new_profile_data));
        }
        StartSettingsWatcher();
      });
}

fit::result<Profile, zx_status_t> IntlPropertyProviderImpl::GetProfileInternal() {
  if (!intl_profile_) {
    Profile profile;
    if (!IsRawDataInitialized()) {
      return fit::error(ZX_ERR_SHOULD_WAIT);
    }
    auto result = GenerateProfile(*raw_profile_data_);
    if (result.is_error()) {
      FX_LOGS(WARNING) << "Couldn't generate profile: " << result.error();
      return result;
    }
    intl_profile_ = result.take_value();
  }
  return fit::ok(fidl::Clone(*intl_profile_));
}

bool IntlPropertyProviderImpl::IsRawDataInitialized() { return raw_profile_data_.has_value(); }

bool IntlPropertyProviderImpl::UpdateRawData(fuchsia::intl::merge::Data new_raw_data) {
  if (IsRawDataInitialized() && fidl::Equals(*raw_profile_data_, new_raw_data)) {
    return false;
  }
  raw_profile_data_ = std::move(new_raw_data);
  // Invalidate the existing cached profile.
  intl_profile_ = std::nullopt;
  FX_VLOGS(1) << "Updated raw data";
  for (const auto& binding : property_provider_bindings_.bindings()) {
    binding->events().OnChange();
  }
  ProcessProfileRequests();
  return true;
}

void IntlPropertyProviderImpl::ProcessProfileRequests() {
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
    auto var = fidl::Clone(profile_result.value());
    callback(std::move(var));
    get_profile_queue_.pop();
  }
}

}  // namespace modular
