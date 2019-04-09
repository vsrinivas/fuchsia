// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#include "intl_wisdom_server_impl.h"

#include <lib/sys/cpp/component_context.h>
#include <zircon/assert.h>

#include <iostream>
#include <sstream>

#include "src/lib/icu_data/cpp/icu_data.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/calendar.h"
#include "third_party/icu/source/i18n/unicode/datefmt.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace intl_wisdom {

using fuchsia::intl::CalendarId;
using fuchsia::intl::LocaleId;
using fuchsia::intl::TimeZoneId;
using icu::Calendar;
using icu::DateFormat;
using icu::Locale;
using icu::TimeZone;
using icu::UnicodeString;
using AskForWisdomCallback =
    fuchsia::examples::intl::wisdom::IntlWisdomServer::AskForWisdomCallback;
using fuchsia::intl::Profile;

namespace {
const Locale LocaleIdToLocale(const std::string locale_id) {
  return Locale::createCanonical(locale_id.c_str());
}

const Locale LocaleIdToLocale(const LocaleId& locale_id) {
  return LocaleIdToLocale(locale_id.id);
}

std::unique_ptr<TimeZone> TimeZoneIdToTimeZone(const TimeZoneId& time_zone_id) {
  return std::unique_ptr<TimeZone>(
      TimeZone::createTimeZone(UnicodeString::fromUTF8(time_zone_id.id)));
}

std::unique_ptr<Calendar> CalendarIdToCalendar(const CalendarId& calendar_id,
                                               const TimeZone& time_zone) {
  // Calendar ID strings are just locale IDs with an undefined language
  Locale as_locale = LocaleIdToLocale(calendar_id.id);
  UErrorCode error_code = U_ZERO_ERROR;
  return std::unique_ptr<Calendar>(
      Calendar::createInstance(time_zone, as_locale, error_code));
}
}  // namespace

IntlWisdomServerImpl::IntlWisdomServerImpl(
    std::unique_ptr<sys::ComponentContext> startup_context)
    : startup_context_(std::move(startup_context)) {
  ZX_ASSERT(icu_data::Initialize());
  startup_context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void IntlWisdomServerImpl::AskForWisdom(Profile intl_profile,
                                        int64_t timestamp_ms,
                                        AskForWisdomCallback callback) {
  // Parse the requested locale IDs
  auto& locale_ids = intl_profile.locales;
  std::vector<Locale> locales;
  std::transform(
      locale_ids.begin(), locale_ids.end(), std::back_inserter(locales),
      [](LocaleId locale_id) { return LocaleIdToLocale(locale_id); });

  std::unique_ptr<TimeZone> time_zone;
  if (intl_profile.time_zones.size() > 0) {
    time_zone = TimeZoneIdToTimeZone(intl_profile.time_zones.at(0));
  } else {
    time_zone = std::unique_ptr<TimeZone>(TimeZone::detectHostTimeZone());
  }

  // Parse the requested calendar IDs, using the first requested timezone (or
  // device timezone as a fallback).
  auto& calendar_ids = intl_profile.calendars;
  std::vector<std::unique_ptr<Calendar>> calendars;
  std::transform(calendar_ids.begin(), calendar_ids.end(),
                 std::back_inserter(calendars), [&](CalendarId calendar_id) {
                   return CalendarIdToCalendar(calendar_id, *time_zone);
                 });
  if (calendars.size() == 0) {
    UErrorCode error_code = U_ZERO_ERROR;
    calendars.push_back(std::unique_ptr<Calendar>(
        Calendar::createInstance(time_zone.get(), locales[0], error_code)));
  }

  std::string response = BuildResponse(timestamp_ms, locales, calendars);
  callback(response);
}

std::string IntlWisdomServerImpl::BuildResponse(
    const long timestamp_ms, const std::vector<Locale>& locales,
    const std::vector<std::unique_ptr<Calendar>>& calendars) const {
  std::ostringstream response;

  response << "\nA wise one knows the time...\n\n";

  for (auto& locale : locales) {
    for (auto& calendar : calendars) {
      auto date_format =
          std::unique_ptr<DateFormat>(DateFormat::createDateTimeInstance(
              DateFormat::kFull, DateFormat::kFull, locale));
      ZX_ASSERT(date_format);
      date_format->setCalendar(*calendar);
      UnicodeString formatted;
      date_format->format(static_cast<UDate>(timestamp_ms), formatted);
      std::string formatted_utf8;
      formatted.toUTF8String(formatted_utf8);

      response << formatted_utf8 << "\n";
    }
  }

  response << "\nBut is it the 𝒄𝒐𝒓𝒓𝒆𝒄𝒕 time?\n";
  return response.str();
}

}  // namespace intl_wisdom
