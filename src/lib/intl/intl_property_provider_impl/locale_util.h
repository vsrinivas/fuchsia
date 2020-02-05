// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_INTL_INTL_PROPERTY_PROVIDER_IMPL_LOCALE_UTIL_H_
#define SRC_LIB_INTL_INTL_PROPERTY_PROVIDER_IMPL_LOCALE_UTIL_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/fit/result.h>

#include <string>

#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/i18n/unicode/ucal.h"

namespace intl {

struct LocaleKeys {
  static const std::string kCalendar;
  static const std::string kFirstDayOfWeek;
  static const std::string kHourCycle;
  static const std::string kMeasurementSystem;
  static const std::string kNumbers;
  static const std::string kTimeZone;
};

// Convert the given locale ID to an `icu::Locale`.
fit::result<icu::Locale, zx_status_t> LocaleIdToIcuLocale(
    const fuchsia::intl::LocaleId& locale_id,
    const std::map<std::string, std::string>& unicode_extensions = {});

// Convert the given locale ID to an `icu::Locale`.
//
// Parameters:
//   locale_id: A Unicode BCP-47 Locale ID
//   unicode_extensions: Optional Unicode extension keys and values to add to
//     the locale.
fit::result<icu::Locale, zx_status_t> LocaleIdToIcuLocale(
    const std::string& locale_id,
    const std::map<std::string, std::string>& unicode_extensions = {});

// For the given `icu::Locale`, generate a Unicode BCP-47 Locale ID that
// includes extension keys and values for supported Unicode extensions.
fit::result<fuchsia::intl::LocaleId, zx_status_t> ExpandLocaleId(const icu::Locale& icu_locale);

// Extract just the calendar value from a `CalendarId`, which is of the form
// `"und-u-ca-<calendarid>"`.
fit::result<std::string, zx_status_t> ExtractBcp47CalendarId(
    const fuchsia::intl::CalendarId& calendar_id);

// Get a Unicode locale ID extension value ("sun", "mon", "tue", etc.) for the
// given day of the week. Used for "First day of week" extension in locale IDs.
//
// See
// https://github.com/unicode-org/cldr/blob/master/common/bcp47/calendar.xml.
std::string ToDayOfWeekString(UCalendarDaysOfWeek day_of_week);

}  // namespace intl

#endif  // SRC_LIB_INTL_INTL_PROPERTY_PROVIDER_IMPL_LOCALE_UTIL_H_
