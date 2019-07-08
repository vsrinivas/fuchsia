// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intl_property_provider_impl.h"

#include <peridot/lib/fidl/clone.h>
#include <src/lib/icu_data/cpp/icu_data.h>

#include <iterator>

#include "lib/fostr/fidl/fuchsia/intl/formatting.h"
#include "lib/syslog/cpp/logger.h"
#include "locale_util.h"
#include "src/lib/fxl/macros.h"
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
using fuchsia::modular::intl::internal::RawProfileData;
using icu::UnicodeString;
using intl::ExpandLocaleId;
using intl::ExtractBcp47CalendarId;
using intl::LocaleIdToIcuLocale;
using intl::LocaleKeys;

namespace {

const std::string kDefaultTimeZoneId = "America/Los_Angeles";

// In the absence of real user preferences, make some very myopic assumptions.
RawProfileData GetDefaultRawData() {
  return RawProfileData{
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

}  // namespace

IntlPropertyProviderImpl::IntlPropertyProviderImpl(
    fuchsia::deprecatedtimezone::TimezonePtr time_zone_client)
    : intl_profile_(std::nullopt),
      raw_profile_data_(std::nullopt),
      time_zone_client_(std::move(time_zone_client)),
      tz_watcher_binding_(this) {
  Start();
}

// static
std::unique_ptr<IntlPropertyProviderImpl> IntlPropertyProviderImpl::Create(
    const std::shared_ptr<sys::ServiceDirectory>& incoming_services) {
  fuchsia::deprecatedtimezone::TimezonePtr time_zone_client =
      incoming_services->Connect<fuchsia::deprecatedtimezone::Timezone>();
  return std::make_unique<IntlPropertyProviderImpl>(std::move(time_zone_client));
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

void IntlPropertyProviderImpl::OnTimezoneOffsetChange(std::string time_zone_id) {
  FX_VLOGS(1) << "Incoming time zone changed to " << time_zone_id;
  // TODO(kpozin): When there are multiple update sources, we might want some
  // sort of "mark dirty" behavior that will coalesce actual recalculations of
  // of the Profile, instead going through the whole process on every change.
  RawProfileData new_profile_data = CloneStruct(*raw_profile_data_);
  new_profile_data.time_zone_ids = {TimeZoneId{.id = time_zone_id}};
  UpdateRawData(new_profile_data);
}

zx_status_t IntlPropertyProviderImpl::InitializeIcuIfNeeded() {
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

void IntlPropertyProviderImpl::LoadInitialValues() {
  auto set_initial_data = [this](std::string time_zone_id) {
    // There is no stable source for this data right now, so we use arbitrary
    // US-centric defaults.
    RawProfileData new_data = GetDefaultRawData();
    new_data.time_zone_ids = {TimeZoneId{.id = time_zone_id}};
    UpdateRawData(new_data);

    // TODO: Consider setting some other error handler for non-initial errors.
    time_zone_client_.set_error_handler(nullptr);
    StartSettingsWatchers();
  };

  time_zone_client_.set_error_handler(
      [set_initial_data](zx_status_t status) { set_initial_data(kDefaultTimeZoneId); });

  time_zone_client_->GetTimezoneId(
      [set_initial_data](std::string timezone_id) { set_initial_data(timezone_id); });
}

zx_status_t IntlPropertyProviderImpl::StartSettingsWatchers() {
  // TODO(MF-168): Watch other settings.
  fidl::InterfaceHandle<fuchsia::deprecatedtimezone::TimezoneWatcher> handle;
  tz_watcher_binding_.Bind(handle.NewRequest());
  time_zone_client_->Watch(std::move(handle));
  return ZX_OK;
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
  if (!IsRawDataInitialized() || (!fidl::Equals(*raw_profile_data_, new_raw_data))) {
    raw_profile_data_ = std::move(new_raw_data);
    // Invalidate the existing cached profile.
    intl_profile_ = std::nullopt;
    FX_VLOGS(1) << "Updated raw data";
    NotifyOnChange();
    ProcessGetProfileQueue();
    return true;
  }
  return false;
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
    return;
  }

  FX_VLOGS(1) << "Processing request queue (" << get_profile_queue_.size() << ")";
  while (!get_profile_queue_.empty()) {
    auto& callback = get_profile_queue_.front();
    callback(CloneStruct(profile_result.value()));
    get_profile_queue_.pop();
  }
}

}  // namespace modular
