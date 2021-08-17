// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "time_zone_info_service.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <cmath>
#include <limits>

#include <src/lib/icu_data/cpp/icu_data.h>

#include "lib/fostr/fidl/fuchsia/intl/formatting.h"
#include "third_party/icu/source/common/unicode/errorcode.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/calendar.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace intl {

namespace {

using fuchsia::intl::TimeZones_AbsoluteToCivilTime_Response;
using fuchsia::intl::TimeZones_AbsoluteToCivilTime_Result;
using fuchsia::intl::TimeZones_CivilToAbsoluteTime_Response;
using fuchsia::intl::TimeZones_CivilToAbsoluteTime_Result;

static constexpr uint64_t kMillisecondsPerSecond = 1000;
static constexpr uint64_t kNanosecondsPerMillisecond = 1'000'000;
static constexpr uint64_t kNanosecondsPerSecond =
    kNanosecondsPerMillisecond * kMillisecondsPerSecond;

// The earliest date, in milliseconds from the Epoch, that can fit in a `zx_time_t`.
static constexpr int64_t kMinEpochMilliseconds =
    std::numeric_limits<int64_t>::min() / static_cast<int64_t>(kNanosecondsPerMillisecond);
// The latest date, in milliseconds from the Epoch, that can fit in a `zx_time_t`.
static constexpr int64_t kMaxEpochMilliseconds =
    std::numeric_limits<int64_t>::max() / static_cast<int64_t>(kNanosecondsPerMillisecond);

fuchsia::intl::TimeZoneId DefaultTimeZoneId() {
  return fuchsia::intl::TimeZoneId{.id = fuchsia::intl::DEFAULT_TIME_ZONE_ID};
}

// Safely converts from ICU's 1-based day of year to Fuchsia's 0-based day of year.
//
// `icu_status` should be passed in from the previous ICU operation in order to verify that it was
// successful and that `icu_year_day` is expected to be valid.
uint16_t ICUYearDayToFuchsiaYearDay(int32_t icu_year_day, const UErrorCode icu_status) {
  if (U_FAILURE(icu_status)) {
    return 0;
  }
  FX_DCHECK(icu_year_day > 0);
  return static_cast<uint16_t>(icu_year_day - 1);
}

// Safely converts ICU `UCalendarMonths`, which is 0-based, to `fuchsia::intl::Month`, which is
// 1-based.
fuchsia::intl::Month ICUMonthToFuchsiaMonth(const int32_t icu_month, const UErrorCode icu_status) {
  if (U_FAILURE(icu_status)) {
    // Return an invalid enum value
    return static_cast<fuchsia::intl::Month>(0);
  }
  return static_cast<fuchsia::intl::Month>(static_cast<uint8_t>(icu_month) + 1);
}

// Safely converts `fuchsia::intl::Month` to ICU's `UCalendarMonths`.
UCalendarMonths FuchsiaMonthToICUMonth(const fuchsia::intl::Month fuchsia_month) {
  uint8_t fuchsia_month_uint = static_cast<uint8_t>(fuchsia_month);
  FX_DCHECK(fuchsia_month_uint > 0);
  return static_cast<UCalendarMonths>(fuchsia_month_uint - 1);
}

// Performs basic checks on required `CivilTime` fields. The rest will be checked by
// `icu::Calendar`.
bool AreRequiredFieldsValid(const fuchsia::intl::CivilTime& civil_time) {
  return civil_time.has_year() && civil_time.has_month() && civil_time.has_day();
}

// If the client supplied redundant fields (weekday, year_day), verifies that they are consistent
// with the date in `calendar`. This helps prevent accidentally shuttling bad data back and forth.
bool AreRedundantFieldsCorrect(const fuchsia::intl::CivilTime& civil_time,
                               const icu::Calendar& calendar) {
  UErrorCode icu_status = UErrorCode::U_ZERO_ERROR;
  if (civil_time.has_weekday() &&
      civil_time.weekday() != static_cast<fuchsia::intl::DayOfWeek>(calendar.get(
                                  UCalendarDateFields::UCAL_DAY_OF_WEEK, icu_status))) {
    return false;
  }
  if (civil_time.has_year_day() &&
      civil_time.year_day() !=
          ICUYearDayToFuchsiaYearDay(
              calendar.get(UCalendarDateFields::UCAL_DAY_OF_YEAR, icu_status), icu_status)) {
    return false;
  }
  return true;
}

// Fills in defaults for fields that are allowed to be omitted.
void PopulateDefaults(fuchsia::intl::CivilTime& civil_time) {
  if (!civil_time.has_hour()) {
    civil_time.set_hour(0);
  }
  if (!civil_time.has_minute()) {
    civil_time.set_minute(0);
  }
  if (!civil_time.has_second()) {
    civil_time.set_second(0);
  }
  if (!civil_time.has_nanos()) {
    civil_time.set_nanos(0);
  }
  if (!civil_time.has_time_zone_id()) {
    civil_time.set_time_zone_id(DefaultTimeZoneId());
  }
}

// Fills in default options.
void PopulateDefaults(fuchsia::intl::CivilToAbsoluteTimeOptions& options) {
  if (!options.has_repeated_time_conversion()) {
    options.set_repeated_time_conversion(fuchsia::intl::RepeatedTimeConversion::BEFORE_TRANSITION);
  }
  if (!options.has_skipped_time_conversion()) {
    options.set_skipped_time_conversion(fuchsia::intl::SkippedTimeConversion::NEXT_VALID_TIME);
  }
}

// Returns `true` if the give ICU date will fit into the range of a `zx_time_t` without under- or
// overflowing.
bool IsInZxTimeRange(UDate icu_date) {
  int64_t absolute_time_ms = static_cast<int64_t>(icu_date);
  return (absolute_time_ms >= kMinEpochMilliseconds && absolute_time_ms <= kMaxEpochMilliseconds);
}

// Load initial ICU data if this hasn't been done already.
//
// TODO(kpozin): Eventually, this should solely be the responsibility of the client component that
// links `TimeZoneInfoService`, which has a better idea of what parameters ICU should be
// initialized with.
zx_status_t InitializeIcuIfNeeded() {
  // It's okay if something else in the same process has already initialized
  // ICU.
  static zx_status_t status = icu_data::Initialize();
  switch (status) {
    case ZX_OK:
    case ZX_ERR_ALREADY_BOUND:
      return ZX_OK;
    default:
      return status;
  }
}

TimeZones_AbsoluteToCivilTime_Result AbsoluteToCivilTimeResult(
    fuchsia::intl::CivilTime civil_time) {
  return TimeZones_AbsoluteToCivilTime_Result::WithResponse(
      TimeZones_AbsoluteToCivilTime_Response(std::move(civil_time)));
}

TimeZones_AbsoluteToCivilTime_Result AbsoluteToCivilTimeResult(
    const fuchsia::intl::TimeZonesError& error) {
  return TimeZones_AbsoluteToCivilTime_Result::WithErr(fuchsia::intl::TimeZonesError(error));
}

TimeZones_CivilToAbsoluteTime_Result CivilToAbsoluteTimeResult(zx_time_t absolute_time) {
  return TimeZones_CivilToAbsoluteTime_Result::WithResponse(
      TimeZones_CivilToAbsoluteTime_Response(absolute_time));
}

TimeZones_CivilToAbsoluteTime_Result CivilToAbsoluteTimeResult(
    const fuchsia::intl::TimeZonesError& error) {
  return TimeZones_CivilToAbsoluteTime_Result::WithErr(fuchsia::intl::TimeZonesError(error));
}

// Returns true if the civil time set on the `calendar` is invalid because it should be skipped
// during a forward DST transition.
bool IsSkippedTime(const icu::Calendar& calendar, UErrorCode& icu_status) {
  icu_status = UErrorCode::U_ZERO_ERROR;

  // Create a clone that allows nonexistent times.
  std::unique_ptr<icu::Calendar> lenient(calendar.clone());
  lenient->setLenient(true);

  // Create another clone, with a different skipped time rule.
  std::unique_ptr<icu::Calendar> lenient_walltime_first(lenient->clone());
  lenient_walltime_first->setSkippedWallTimeOption(UCalendarWallTimeOption::UCAL_WALLTIME_FIRST);

  return lenient->getTime(icu_status) != lenient_walltime_first->getTime(icu_status);
}

// Converts an `icu::Calendar` (with some additional values) to a `fuchsia::intl::CivilTime`.
//
// Note: Fractional seconds should be passed in as `nanoseconds`, not using `Calendar`'s
// milliseconds.
fuchsia::intl::CivilTime ICUCalendarToCivilTime(const icu::Calendar& calendar,
                                                const uint64_t nanoseconds,
                                                const fuchsia::intl::TimeZoneId time_zone_id,
                                                UErrorCode& icu_status) {
  FX_DCHECK(nanoseconds < kNanosecondsPerSecond);

  fuchsia::intl::CivilTime civil_time;
  civil_time
      .set_year(static_cast<uint16_t>(calendar.get(UCalendarDateFields::UCAL_YEAR, icu_status)))
      .set_month(ICUMonthToFuchsiaMonth(calendar.get(UCalendarDateFields::UCAL_MONTH, icu_status),
                                        icu_status))
      .set_day(
          static_cast<uint8_t>(calendar.get(UCalendarDateFields::UCAL_DAY_OF_MONTH, icu_status)))
      .set_hour(
          static_cast<uint8_t>(calendar.get(UCalendarDateFields::UCAL_HOUR_OF_DAY, icu_status)))
      .set_minute(static_cast<uint8_t>(calendar.get(UCalendarDateFields::UCAL_MINUTE, icu_status)))
      .set_second(static_cast<uint8_t>(calendar.get(UCalendarDateFields::UCAL_SECOND, icu_status)))
      .set_nanos(nanoseconds)
      .set_weekday(static_cast<fuchsia::intl::DayOfWeek>(
          calendar.get(UCalendarDateFields::UCAL_DAY_OF_WEEK, icu_status)))
      .set_year_day(ICUYearDayToFuchsiaYearDay(
          calendar.get(UCalendarDateFields::UCAL_DAY_OF_YEAR, icu_status), icu_status))
      .set_time_zone_id(time_zone_id);

  return civil_time;
}

}  // namespace

std::unique_ptr<TimeZoneInfoService> TimeZoneInfoService::Create() {
  return std::make_unique<TimeZoneInfoService>();
}

fidl::InterfaceRequestHandler<fuchsia::intl::TimeZones> TimeZoneInfoService::GetHandler(
    async_dispatcher_t* dispatcher) {
  return bindings_.GetHandler(this, dispatcher);
}

void TimeZoneInfoService::Start() {
  if (InitializeIcuIfNeeded() != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize ICU data";
    return;
  }
}

std::variant<std::unique_ptr<icu::Calendar>, fuchsia::intl::TimeZonesError>
TimeZoneInfoService::LoadCalendar(const fuchsia::intl::TimeZoneId& time_zone_id) {
  // `calendar` will take ownership of time_zone. Do not delete it manually.
  std::unique_ptr<icu::TimeZone> time_zone(icu::TimeZone::createTimeZone(time_zone_id.id.c_str()));
  // If the loaded time zone's ID is "Etc/Unknown" and the client did not explicitly request it,
  // that means that the requested time zone was not found.
  if (time_zone_id.id != UCAL_UNKNOWN_ZONE_ID) {
    icu::UnicodeString loaded_id;
    time_zone->getID(loaded_id);
    if (loaded_id == UCAL_UNKNOWN_ZONE_ID) {
      FX_LOGS(ERROR) << "Unknown time zone ID: " << time_zone_id.id;
      return fuchsia::intl::TimeZonesError::UNKNOWN_TIME_ZONE;
    }
  }

  UErrorCode icu_status = UErrorCode::U_ZERO_ERROR;
  std::unique_ptr<icu::Calendar> calendar(
      icu::Calendar::createInstance(time_zone.release(), icu_status));
  auto icu_error = ConvertAndLogICUError(icu_status);
  if (icu_error.has_value()) {
    return icu_error.value();
  }

  return calendar;
}

std::string ToString(const fuchsia::intl::CivilTime* civil_time) {
  std::ostringstream buffer;
  if (civil_time != nullptr) {
    buffer << "\ncivil_time: " << *civil_time;
  }
  return buffer.str();
}

std::string ToString(const std::optional<zx_time_t> absolute_time) {
  std::ostringstream buffer;
  if (absolute_time.has_value()) {
    buffer << "\nabsolute_time: " << absolute_time.value();
  }
  return buffer.str();
}

std::optional<fuchsia::intl::TimeZonesError> TimeZoneInfoService::ConvertAndLogICUError(
    const UErrorCode icu_status, const fuchsia::intl::CivilTime* civil_time,
    const std::optional<zx_time_t> absolute_time) {
  if (!U_FAILURE(icu_status)) {
    return std::nullopt;
  }

  icu::ErrorCode error;
  error.set(icu_status);

  ((icu_status == UErrorCode::U_ILLEGAL_ARGUMENT_ERROR) ? FX_LOG_STREAM(WARNING, nullptr)
                                                        : FX_LOG_STREAM(ERROR, nullptr))
      << "ICU error: " << error.errorName() << ToString(civil_time) << ToString(absolute_time);

  switch (icu_status) {
    case UErrorCode::U_ILLEGAL_ARGUMENT_ERROR:
      return fuchsia::intl::TimeZonesError::INVALID_DATE;
    default:
      return fuchsia::intl::TimeZonesError::INTERNAL_ERROR;
  }
}

void TimeZoneInfoService::AbsoluteToCivilTime(const fuchsia::intl::TimeZoneId time_zone_id,
                                              const zx_time_t absolute_time,
                                              const AbsoluteToCivilTimeCallback callback) {
  auto calendar_result = LoadCalendar(time_zone_id);
  if (std::holds_alternative<fuchsia::intl::TimeZonesError>(calendar_result)) {
    callback(AbsoluteToCivilTimeResult(std::get<fuchsia::intl::TimeZonesError>(calendar_result)));
    return;
  }
  auto calendar = std::move(std::get<std::unique_ptr<icu::Calendar>>(calendar_result));

  UDate epoch_millis = static_cast<double>(absolute_time / kNanosecondsPerMillisecond);
  UErrorCode icu_status = UErrorCode::U_ZERO_ERROR;

  calendar->setTime(epoch_millis, icu_status);
  fuchsia::intl::CivilTime civil_time(ICUCalendarToCivilTime(
      *calendar, absolute_time % kNanosecondsPerSecond, std::move(time_zone_id), icu_status));

  auto icu_error = ConvertAndLogICUError(icu_status);
  auto result = icu_error.has_value() ? AbsoluteToCivilTimeResult(icu_error.value())
                                      : AbsoluteToCivilTimeResult(std::move(civil_time));
  callback(std::move(result));
}

void TimeZoneInfoService::CivilToAbsoluteTime(fuchsia::intl::CivilTime civil_time,
                                              fuchsia::intl::CivilToAbsoluteTimeOptions options,
                                              const CivilToAbsoluteTimeCallback callback) {
  if (!AreRequiredFieldsValid(civil_time)) {
    callback(CivilToAbsoluteTimeResult(fuchsia::intl::TimeZonesError::INVALID_DATE));
    return;
  }
  PopulateDefaults(civil_time);
  PopulateDefaults(options);

  auto calendar_result = LoadCalendar(civil_time.time_zone_id());
  if (std::holds_alternative<fuchsia::intl::TimeZonesError>(calendar_result)) {
    callback(CivilToAbsoluteTimeResult(std::get<fuchsia::intl::TimeZonesError>(calendar_result)));
    return;
  }
  auto calendar = std::move(std::get<std::unique_ptr<icu::Calendar>>(calendar_result));
  calendar->clear();
  calendar->setLenient(false);

  switch (options.repeated_time_conversion()) {
    case fuchsia::intl::RepeatedTimeConversion::BEFORE_TRANSITION:
      calendar->setRepeatedWallTimeOption(UCalendarWallTimeOption::UCAL_WALLTIME_FIRST);
      break;
    default:
      FX_LOGS(FATAL) << "Unimplemented RepeatedTimeConversion: "
                     << options.repeated_time_conversion();
      break;
  }
  switch (options.skipped_time_conversion()) {
    case fuchsia::intl::SkippedTimeConversion::NEXT_VALID_TIME:
      calendar->setSkippedWallTimeOption(UCalendarWallTimeOption::UCAL_WALLTIME_NEXT_VALID);
      break;
    case fuchsia::intl::SkippedTimeConversion::REJECT:
      // Handled further down
      break;
    default:
      FX_LOGS(FATAL) << "Unimplemented SkippedTimeConversion: "
                     << options.skipped_time_conversion();
      break;
  }

  calendar->set(static_cast<int32_t>(civil_time.year()),
                static_cast<int32_t>(FuchsiaMonthToICUMonth(civil_time.month())),
                static_cast<int32_t>(civil_time.day()), static_cast<int32_t>(civil_time.hour()),
                static_cast<int32_t>(civil_time.minute()),
                static_cast<int32_t>(civil_time.second()));

  bool is_skipped_time = false;
  UErrorCode icu_status = UErrorCode::U_ZERO_ERROR;
  UDate time = calendar->getTime(icu_status);

  if (icu_status == UErrorCode::U_ILLEGAL_ARGUMENT_ERROR) {
    ConvertAndLogICUError(icu_status);
    if (options.skipped_time_conversion() != fuchsia::intl::SkippedTimeConversion::REJECT) {
      // If the given civil time would be skipped due to a DST transition, retry.
      UErrorCode retry_icu_status = UErrorCode::U_ZERO_ERROR;
      is_skipped_time = IsSkippedTime(*calendar, retry_icu_status);
      if (is_skipped_time) {
        calendar->setLenient(true);
        time = calendar->getTime(retry_icu_status);
        icu_status = retry_icu_status;
      }
    } else {
      FX_LOGS(INFO) << "Rejecting invalid date";
    }
  }

  auto icu_error = ConvertAndLogICUError(icu_status);
  if (icu_error.has_value()) {
    callback(CivilToAbsoluteTimeResult(icu_error.value()));
    return;
  }

  if (!AreRedundantFieldsCorrect(civil_time, *calendar)) {
    callback(CivilToAbsoluteTimeResult(fuchsia::intl::TimeZonesError::INVALID_DATE));
  }

  // Detect underflow and overflow.
  // The upper bound is reduced by 1 second to leave room for `civil_time.nanos`.
  if (!IsInZxTimeRange(time + 1.0 * kMillisecondsPerSecond)) {
    FX_LOGS(WARNING) << "Date is out of zx_time_t range: " << civil_time;
    callback(CivilToAbsoluteTimeResult(fuchsia::intl::TimeZonesError::INVALID_DATE));
  }
  zx_time_t absolute_time_nanos = static_cast<zx_time_t>(time) * kNanosecondsPerMillisecond;

  // If the conversion substituted the next valid time (e.g. 3:00:00 AM), the fractional second
  // must be dropped.
  if (!is_skipped_time ||
      options.skipped_time_conversion() != fuchsia::intl::SkippedTimeConversion::NEXT_VALID_TIME) {
    absolute_time_nanos += civil_time.nanos();
  }

  callback(CivilToAbsoluteTimeResult(absolute_time_nanos));
}

}  // namespace intl
