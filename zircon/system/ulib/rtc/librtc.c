// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/librtc.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

enum months {
  JANUARY = 1,  // 31 days
  FEBRUARY,     // 28 or 29
  MARCH,        // 31
  APRIL,        // 30
  MAY,          // 31
  JUNE,         // 30
  JULY,         // 31
  AUGUST,       // 31
  SEPTEMBER,    // 30
  OCTOBER,      // 31
  NOVEMBER,     // 30
  DECEMBER      // 31
};

// Leading 0 allows using the 1-indexed month values from rtc.
static const uint64_t days_in_month[] = {
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
static const uint64_t local_epoch = 946684800;
static const uint16_t local_epoch_year = 2000;
static const uint16_t default_year = 2019;
static const uint16_t max_year = 2099;

static bool is_leap_year(uint16_t year) {
  return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

uint64_t seconds_since_epoch(const fuchsia_hardware_rtc_Time* rtc) {
  // First add all of the prior years.
  uint64_t days_since_local_epoch = 0;
  for (uint16_t year = local_epoch_year; year < rtc->year; year++) {
    days_since_local_epoch += is_leap_year(year) ? 366 : 365;
  }

  // Next add all the prior complete months this year.
  for (size_t month = JANUARY; month < rtc->month; month++) {
    days_since_local_epoch += days_in_month[month];
  }
  if (rtc->month > FEBRUARY && is_leap_year(rtc->year)) {
    days_since_local_epoch++;
  }

  // Add all the prior complete days.
  days_since_local_epoch += rtc->day - 1;

  // Hours, minutes, and seconds are 0 indexed.
  uint64_t hours_since_local_epoch = (days_since_local_epoch * 24) + rtc->hours;
  uint64_t minutes_since_local_epoch = (hours_since_local_epoch * 60) + rtc->minutes;
  uint64_t seconds_since_local_epoch = (minutes_since_local_epoch * 60) + rtc->seconds;

  uint64_t rtc_seconds = local_epoch + seconds_since_local_epoch;
  return rtc_seconds;
}

void seconds_to_rtc(uint64_t seconds, fuchsia_hardware_rtc_Time* rtc) {
  // subtract local epoch offset to get to rtc time
  uint64_t epoch = seconds - local_epoch;

  rtc->seconds = epoch % 60;
  epoch /= 60;
  rtc->minutes = epoch % 60;
  epoch /= 60;
  rtc->hours = epoch % 24;
  epoch /= 24;

  for (rtc->year = local_epoch_year;; rtc->year++) {
    uint32_t days_per_year = 365;
    if (is_leap_year(rtc->year)) {
      days_per_year++;
    }

    if (epoch < days_per_year) {
      break;
    }

    epoch -= days_per_year;
  }

  for (rtc->month = 1;; rtc->month++) {
    uint32_t days_per_month = days_in_month[rtc->month];
    if ((rtc->month == FEBRUARY) && is_leap_year(rtc->year)) {
      days_per_month++;
    }

    if (epoch < days_per_month) {
      break;
    }

    epoch -= days_per_month;
  }

  // remaining epoch is a whole number of days so just make it one-indexed
  rtc->day = epoch + 1;
}

uint8_t to_bcd(uint8_t binary) { return ((binary / 10) << 4) | (binary % 10); }

uint8_t from_bcd(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0xf); }

// If "clock.backstop" is set in the environment, it is parsed as seconds
// since the Unix epoch and returned. If it is un-set, or parsing fails, 0 is
// returned.
uint64_t rtc_backstop_seconds(void) {
  const char* str = getenv("clock.backstop");
  if (str == NULL) {
    return 0;
  }
  return strtoll(str, NULL, 10);
}

// TODO: eventually swap rtc_is_invalid() for the positive version.
static bool rtc_is_valid(const fuchsia_hardware_rtc_Time* rtc) {
  if (rtc->year < local_epoch_year || rtc->year > max_year)
    return false;
  if (rtc->month < JANUARY || rtc->month > DECEMBER)
    return false;
  if (rtc->day == 0)
    return false;
  switch (rtc->month) {
    case JANUARY:
    case MARCH:
    case MAY:
    case JULY:
    case AUGUST:
    case OCTOBER:
    case DECEMBER:
      if (rtc->day > 31)
        return false;
      break;
    case FEBRUARY:
      if (rtc->day > (is_leap_year(rtc->year) ? 29 : 28))
        return false;
      break;
    default:
      if (rtc->day > 30)
        return false;
      break;
  }
  if (rtc->hours > 23 || rtc->minutes > 59 || rtc->seconds > 59)
    return false;

  return true;
}

bool rtc_is_invalid(const fuchsia_hardware_rtc_Time* rtc) { return !rtc_is_valid(rtc); }

// Validate that the RTC is set to a valid time, and to a relatively
// sane one. Report the validated or reset time back via rtc.
void sanitize_rtc(void* ctx, fuchsia_hardware_rtc_Time* rtc,
                  zx_status_t (*rtc_get)(void*, fuchsia_hardware_rtc_Time*),
                  zx_status_t (*rtc_set)(void*, const fuchsia_hardware_rtc_Time*)) {
  // January 1, 2019 00:00:00
  fuchsia_hardware_rtc_Time backstop_rtc = {
      .day = 1,
      .month = JANUARY,
      .year = default_year,
      .seconds = 0,
      .minutes = 0,
      .hours = 0,
  };
  zx_status_t result = rtc_get(ctx, rtc);
  if (result != ZX_OK) {
    fprintf(stderr, "sanitize_rtc: could not get RTC value (%d)\n", result);
    return;
  };
  uint64_t backstop = rtc_backstop_seconds();
  if (rtc_is_invalid(rtc) || rtc->year < default_year || seconds_since_epoch(rtc) < backstop) {
    if (backstop > 0) {
      fprintf(stderr, "sanitize_rtc: clock set to clock.backstop=%ld\n", backstop);
      seconds_to_rtc(backstop, &backstop_rtc);
    } else {
      fprintf(stderr, "sanitize_rtc: clock set to constant default, set clock.backstop\n");
    }
    result = rtc_set(ctx, &backstop_rtc);
    if (result != ZX_OK) {
      fprintf(stderr, "sanitize_rtc: could not set RTC value (%d)\n", result);
      return;
    }
    *rtc = backstop_rtc;
  }
}
