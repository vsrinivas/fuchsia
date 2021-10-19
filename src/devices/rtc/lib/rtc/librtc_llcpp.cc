// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "librtc_llcpp.h"

#include <lib/ddk/driver.h>

namespace rtc {

namespace {

constexpr uint64_t kDaysInMonth[] = {
    0,
    31,  // January
    28,  // February (not leap year)
    31,  // March
    30,  // April
    31,  // May
    30,  // June
    31,  // July
    31,  // August
    30,  // September
    31,  // October
    30,  // November
    31,  // December
};

// Start with seconds from the Unix epoch to 2000/1/1T00:00:00.
constexpr int kLocalEpoch = 946684800;
constexpr int kLocalEpochYear = 2000;

constexpr int kDefaultYear = 2020;
constexpr int kMaxYear = 2099;

// January 1, 2020 00:00:00.
constexpr FidlRtc::wire::Time kDefaultRtc = {
    .seconds = 0,
    .minutes = 0,
    .hours = 0,
    .day = 1,
    .month = JANUARY,
    .year = kDefaultYear,
};

bool IsLeapYear(uint64_t year) {
  return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

uint64_t DaysInYear(uint64_t year) { return IsLeapYear(year) ? 366 : 365; }

uint64_t DaysInMonth(uint64_t month, uint64_t year) {
  uint64_t days = kDaysInMonth[month];
  if (month == FEBRUARY && IsLeapYear(year)) {
    days++;
  }

  return days;
}

uint64_t RtcBackstopSeconds(zx_device_t* device) {
  char str[32];
  zx_status_t status = device_get_variable(device, "clock.backstop", str, sizeof(str), nullptr);
  if (status != ZX_OK) {
    return 0;
  }
  return strtoll(str, NULL, 10);
}

}  // namespace

bool IsRtcValid(FidlRtc::wire::Time rtc) {
  if (rtc.year < kLocalEpochYear || rtc.year > kMaxYear) {
    return false;
  }

  if (rtc.month < JANUARY || rtc.month > DECEMBER) {
    return false;
  }

  if (rtc.day > DaysInMonth(rtc.month, rtc.year)) {
    return false;
  }

  if (rtc.day == 0) {
    return false;
  }

  if (rtc.hours > 23 || rtc.minutes > 59 || rtc.seconds > 59) {
    return false;
  }

  return true;
}

FidlRtc::wire::Time SecondsToRtc(uint64_t seconds) {
  if (seconds < kLocalEpoch) {
    fprintf(stderr, "SecondsToRtc: Seconds value is out of range, returning default");
    return kDefaultRtc;
  }

  // Subtract the local epoch offset to get to RTC time.
  uint64_t epoch = seconds - kLocalEpoch;

  FidlRtc::wire::Time rtc;
  rtc.seconds = epoch % 60;
  epoch /= 60;
  rtc.minutes = epoch % 60;
  epoch /= 60;
  rtc.hours = epoch % 24;
  epoch /= 24;

  for (rtc.year = kLocalEpochYear;; rtc.year++) {
    const uint64_t kDaysPerYear = DaysInYear(rtc.year);
    if (epoch < kDaysPerYear) {
      break;
    }

    epoch -= kDaysPerYear;
  }

  for (rtc.month = 1;; rtc.month++) {
    const uint64_t kDaysInMonth = DaysInMonth(rtc.month, rtc.year);
    if (epoch < kDaysInMonth) {
      break;
    }

    epoch -= kDaysInMonth;
  }

  // Remaining epoch is a whole number of days so just make it one-indexed.
  rtc.day = (uint8_t)epoch + 1;

  return rtc;
}

uint64_t SecondsSinceEpoch(FidlRtc::wire::Time rtc) {
  // First add all of the prior years.
  uint64_t days_since_local_epoch = 0;
  for (uint16_t year = kLocalEpochYear; year < rtc.year; year++) {
    days_since_local_epoch += DaysInYear(year);
  }

  // Next add all the prior complete months this year.
  for (size_t month = JANUARY; month < rtc.month; month++) {
    days_since_local_epoch += DaysInMonth(month, rtc.year);
  }

  // Add all the prior complete days.
  days_since_local_epoch += rtc.day - 1;

  // Hours, minutes, and seconds are 0 indexed.
  uint64_t const kHoursSinceLocalEpoch = (days_since_local_epoch * 24) + rtc.hours;
  uint64_t const kMinutesSinceLocalEpoch = (kHoursSinceLocalEpoch * 60) + rtc.minutes;
  uint64_t const kSecondsSinceLocalEpoch = (kMinutesSinceLocalEpoch * 60) + rtc.seconds;

  return kLocalEpoch + kSecondsSinceLocalEpoch;
}

FidlRtc::wire::Time SanitizeRtc(zx_device_t* device, FidlRtc::wire::Time rtc) {
  const uint64_t kBackstop = RtcBackstopSeconds(device);
  if (!IsRtcValid(rtc) || rtc.year < kDefaultYear || SecondsSinceEpoch(rtc) < kBackstop) {
    // Return a backstop value read from the environment.
    if (kBackstop > 0) {
      fprintf(stderr, "RTC is sanitized to clock.backstop=%ld\n", kBackstop);
      return SecondsToRtc(kBackstop);
    }

    fprintf(stderr, "RTC is sanitized to constant default\n");
    return kDefaultRtc;
  }

  return rtc;
}

}  // namespace rtc
