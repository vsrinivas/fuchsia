// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/librtc.h"

#include <ddk/driver.h>
#include <zircon/syscalls.h>

// Leading 0 allows using the 1-indexed month values from rtc.
static const uint64_t days_in_month[] = {
    0,
    31, // January
    28, // February (not leap year)
    31, // March
    30, // April
    31, // May
    30, // June
    31, // July
    31, // August
    30, // September
    31, // October
    30, // November
    31, // December
};

// Start with seconds from the Unix epoch to 2000/1/1T00:00:00.
static const uint64_t local_epoch = 946684800;
static const uint16_t local_epoch_year = 2000;
static const uint16_t default_year = 2018;

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
    for (size_t month = 1; month < rtc->month; month++) {
        days_since_local_epoch += days_in_month[month];
    }
    if (rtc->month > 2 && is_leap_year(rtc->year)) {
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

    rtc->seconds = epoch % 60; epoch /= 60;
    rtc->minutes = epoch % 60; epoch /= 60;
    rtc->hours = epoch % 24; epoch /= 24;

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
        if ((rtc->month == 2) && is_leap_year(rtc->year)) {
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

uint8_t to_bcd(uint8_t binary) {
    return ((binary / 10) << 4) | (binary % 10);
}

uint8_t from_bcd(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0xf);
}

bool rtc_is_invalid(const fuchsia_hardware_rtc_Time* rtc) {
    return rtc->seconds > 59 ||
        rtc->minutes > 59 ||
        rtc->hours > 23 ||
        rtc->day > 31 ||
        rtc->month > 12 ||
        rtc->year < local_epoch_year ||
        rtc->year > 2099;
}

// Validate that the RTC is set to a valid time, and to a relatively
// sane one. Report the validated or reset time back via rtc.
void sanitize_rtc(void* ctx, fuchsia_hardware_rtc_Time* rtc,
                  zx_status_t (*rtc_get)(void*, fuchsia_hardware_rtc_Time*),
                  zx_status_t (*rtc_set)(void*, const fuchsia_hardware_rtc_Time*)) {
    // January 1, 2016 00:00:00
    static const fuchsia_hardware_rtc_Time default_rtc = {
        .day = 1,
        .month = 1,
        .year = default_year,
    };
    rtc_get(ctx, rtc);
    if (rtc_is_invalid(rtc) || rtc->year < default_year) {
        rtc_set(ctx, &default_rtc);
        *rtc = default_rtc;
    }
}
