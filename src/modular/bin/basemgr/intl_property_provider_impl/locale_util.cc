// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "locale_util.h"

#include <ostream>
#include <unordered_set>

#include "src/lib/syslog/cpp/logger.h"
#include "third_party/icu/source/common/unicode/errorcode.h"
#include "third_party/icu/source/common/unicode/localebuilder.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/calendar.h"
#include "third_party/icu/source/i18n/unicode/dtptngen.h"
#include "third_party/icu/source/i18n/unicode/numsys.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "third_party/icu/source/i18n/unicode/ulocdata.h"

namespace intl {

using fuchsia::intl::CalendarId;
using fuchsia::intl::LocaleId;

namespace {

fit::result<std::string, zx_status_t> GetHourCycleValue(const icu::Locale& locale) {
  UErrorCode error_code = U_ZERO_ERROR;
  std::unique_ptr<icu::DateTimePatternGenerator> pattern_generator(
      icu::DateTimePatternGenerator::createInstance(locale, error_code));
  if (U_FAILURE(error_code)) {
    FX_LOGS(WARNING) << "Couldn't create DateTimePatternGenerator:" << u_errorName(error_code);
    return fit::error(ZX_ERR_INTERNAL);
  }
  auto pattern_unicode = pattern_generator->getBestPattern("j", error_code);
  std::string pattern;
  pattern_unicode.toUTF8String(pattern);
  if (pattern.find('h') != std::string::npos) {
    return fit::ok("h12");
  } else if (pattern.find('H') != std::string::npos) {
    return fit::ok("h23");
  } else if (pattern.find('k') != std::string::npos) {
    return fit::ok("h24");
  } else if (pattern.find('K') != std::string::npos) {
    return fit::ok("h11");
  } else {
    FX_LOGS(WARNING) << "Failed to get hour cycle for pattern: \"" << pattern << "\"";
    return fit::error(ZX_ERR_INTERNAL);
  }
}

fit::result<std::string, zx_status_t> GetMeasurementSystemValue(const icu::Locale& locale) {
  UErrorCode error_code = U_ZERO_ERROR;
  auto locale_id = locale.toLanguageTag<std::string>(error_code);
  UMeasurementSystem system = ulocdata_getMeasurementSystem(locale_id.c_str(), &error_code);
  if (U_FAILURE(error_code)) {
    FX_LOGS(WARNING) << "Failed to get measurement system: " << u_errorName(error_code);
    return fit::error(ZX_ERR_INTERNAL);
  }
  switch (system) {
    case UMS_SI:
      return fit::ok("metric");
    case UMS_UK:
      return fit::ok("uksystem");
    case UMS_US:
      return fit::ok("ussystem");
    default:
      return fit::error(ZX_ERR_INTERNAL);
  }
}

fit::result<std::string, zx_status_t> GetNumbersValue(const icu::Locale& locale) {
  UErrorCode error_code = U_ZERO_ERROR;
  std::unique_ptr<icu::NumberingSystem> numbering_system(
      icu::NumberingSystem::createInstance(locale, error_code));
  if (U_FAILURE(error_code)) {
    FX_LOGS(WARNING) << "Couldn't create NumberingSystem:" << u_errorName(error_code);
    return fit::error(ZX_ERR_INTERNAL);
  }
  return fit::ok(std::string(numbering_system->getName()));
}

}  // namespace

const std::string LocaleKeys::kCalendar = "ca";
const std::string LocaleKeys::kFirstDayOfWeek = "fw";
const std::string LocaleKeys::kHourCycle = "hc";
const std::string LocaleKeys::kMeasurementSystem = "ms";
const std::string LocaleKeys::kNumbers = "nu";
const std::string LocaleKeys::kTimeZone = "tz";

fit::result<icu::Locale, zx_status_t> LocaleIdToIcuLocale(
    const std::string& locale_id, const std::map<std::string, std::string>& unicode_extensions) {
  UErrorCode error_code = U_ZERO_ERROR;
  icu::LocaleBuilder locale_builder{};
  locale_builder.setLanguageTag(locale_id);
  for (const auto& [key, value] : unicode_extensions) {
    locale_builder.setUnicodeLocaleKeyword(key, value);
  }
  icu::Locale locale = locale_builder.build(error_code);
  if (U_FAILURE(error_code)) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  return fit::ok(std::move(locale));
}

fit::result<icu::Locale, zx_status_t> LocaleIdToIcuLocale(
    const LocaleId& locale_id, const std::map<std::string, std::string>& unicode_extensions) {
  return LocaleIdToIcuLocale(locale_id.id, unicode_extensions);
}

fit::result<std::string, zx_status_t> ExtractBcp47CalendarId(const CalendarId& calendar_id) {
  size_t start = calendar_id.id.find("-" + LocaleKeys::kCalendar + "-");
  if (start == std::string::npos) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  return fit::ok(calendar_id.id.substr(start + 4));
}

fit::result<LocaleId, zx_status_t> ExpandLocaleId(const icu::Locale& unexpanded_locale) {
  UErrorCode error_code = U_ZERO_ERROR;

  std::unordered_set<std::string> present_keys{};
  unexpanded_locale
      .getUnicodeKeywords<std::string, std::insert_iterator<std::unordered_set<std::string>>>(
          std::inserter(present_keys, present_keys.end()), error_code);

  if (U_FAILURE(error_code)) {
    FX_LOGS(WARNING) << "Couldn't read unexpanded locale";
  }

  icu::LocaleBuilder locale_builder{};
  locale_builder.setLocale(unexpanded_locale);

  if (present_keys.count(LocaleKeys::kCalendar) == 0 ||
      present_keys.count(LocaleKeys::kFirstDayOfWeek) == 0) {
    std::unique_ptr<icu::Calendar> calendar(
        icu::Calendar::createInstance(unexpanded_locale, error_code));
    if (U_FAILURE(error_code)) {
      FX_LOGS(WARNING) << "Failed to load calendar data: " << u_errorName(error_code);
      return fit::error(ZX_ERR_INTERNAL);
    }

    if (present_keys.count(LocaleKeys::kCalendar) == 0) {
      auto calendar_value =
          uloc_toUnicodeLocaleType(LocaleKeys::kCalendar.c_str(), calendar->getType());
      if (calendar_value == nullptr) {
        FX_LOGS(WARNING) << "Bad calendar ID";
        return fit::error(ZX_ERR_INTERNAL);
      }
      locale_builder.setUnicodeLocaleKeyword(LocaleKeys::kCalendar, calendar_value);
    }

    if (present_keys.count(LocaleKeys::kFirstDayOfWeek) == 0) {
      UCalendarDaysOfWeek first_day = calendar->getFirstDayOfWeek(error_code);
      if (U_FAILURE(error_code)) {
        FX_LOGS(WARNING) << "Failed to get first day of week: " << u_errorName(error_code);
        return fit::error(ZX_ERR_INTERNAL);
      }
      std::string first_day_string = ToDayOfWeekString(first_day);
      locale_builder.setUnicodeLocaleKeyword(LocaleKeys::kFirstDayOfWeek, first_day_string);
    }
  }

  if (present_keys.count(LocaleKeys::kHourCycle) == 0) {
    auto hour_cycle_result = GetHourCycleValue(unexpanded_locale);
    if (hour_cycle_result.is_error()) {
      // Errors logged in GetHourCycleValue
      return fit::error(hour_cycle_result.error());
    }
    locale_builder.setUnicodeLocaleKeyword(LocaleKeys::kHourCycle.c_str(),
                                           hour_cycle_result.value());
  }

  if (present_keys.count(LocaleKeys::kMeasurementSystem) == 0) {
    auto measurement_system_result = GetMeasurementSystemValue(unexpanded_locale);
    if (measurement_system_result.is_error()) {
      // Errors logged in GetMeasurementSystemValue
      return fit::error(measurement_system_result.error());
    }
    locale_builder.setUnicodeLocaleKeyword(LocaleKeys::kMeasurementSystem.c_str(),
                                           measurement_system_result.value());
  }

  if (present_keys.count(LocaleKeys::kNumbers) == 0) {
    auto numbers_result = GetNumbersValue(unexpanded_locale);
    if (numbers_result.is_error()) {
      return fit::error(numbers_result.error());
    }
    locale_builder.setUnicodeLocaleKeyword(LocaleKeys::kNumbers.c_str(), numbers_result.value());
  }

  auto expanded_locale = locale_builder.build(error_code);
  if (U_FAILURE(error_code)) {
    FX_LOGS(WARNING) << "Failed to build expanded locale: " << u_errorName(error_code);
    return fit::error(ZX_ERR_INTERNAL);
  }

  auto id_str = expanded_locale.toLanguageTag<std::string>(error_code);
  if (U_FAILURE(error_code)) {
    FX_LOGS(WARNING) << "Failed to build language tag: " << u_errorName(error_code);
    return fit::error(ZX_ERR_INTERNAL);
  } else {
    return fit::ok(LocaleId{.id = id_str});
  }
}

std::string ToDayOfWeekString(UCalendarDaysOfWeek day_of_week) {
  switch (day_of_week) {
    case UCalendarDaysOfWeek::UCAL_SUNDAY:
      return "sun";
    case UCalendarDaysOfWeek::UCAL_MONDAY:
      return "mon";
    case UCalendarDaysOfWeek::UCAL_TUESDAY:
      return "tue";
    case UCalendarDaysOfWeek::UCAL_WEDNESDAY:
      return "wed";
    case UCalendarDaysOfWeek::UCAL_THURSDAY:
      return "thu";
    case UCalendarDaysOfWeek::UCAL_FRIDAY:
      return "fri";
    case UCalendarDaysOfWeek::UCAL_SATURDAY:
      return "sat";
  }
}

}  // namespace intl
