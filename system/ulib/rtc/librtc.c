// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/librtc.h"

#include <ddk/driver.h>
#include <zircon/device/rtc.h>
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

// Start with seconds from the Unix epoch to 2016/1/1T00:00:00.
static const uint64_t local_epoc = 1451606400;
static const uint16_t local_epoc_year = 2016;

static bool is_leap_year(uint16_t year) {
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

// This is run on boot (after validation of the RTC) and whenever the
// RTC is adjusted.
zx_status_t set_utc_offset(const rtc_t* rtc) {
    // First add all of the prior years
    uint64_t days_since_local_epoc = 0;
    for (uint16_t year = local_epoc_year; year < rtc->year; year++) {
        days_since_local_epoc += is_leap_year(year) ? 366 : 365;
    }

    // Next add all the prior complete months this year.
    for (size_t month = 1; month < rtc->month; month++) {
        days_since_local_epoc += days_in_month[month];
    }
    if (rtc->month > 2 && is_leap_year(rtc->year)) {
        days_since_local_epoc++;
    }

    // Add all the prior complete days.
    days_since_local_epoc += rtc->day - 1;

    // Hours, minutes, and seconds are 0 indexed.
    uint64_t hours_since_local_epoc = (days_since_local_epoc * 24) + rtc->hours;
    uint64_t minutes_since_local_epoc = (hours_since_local_epoc * 60) + rtc->minutes;
    uint64_t seconds_since_local_epoc = (minutes_since_local_epoc * 60) + rtc->seconds;

    uint64_t rtc_seconds = local_epoc + seconds_since_local_epoc;
    uint64_t rtc_nanoseconds = rtc_seconds * 1000000000;

    uint64_t monotonic_nanoseconds = zx_clock_get(ZX_CLOCK_MONOTONIC);
    int64_t offset = rtc_nanoseconds - monotonic_nanoseconds;

    zx_status_t status = zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, offset);
    return status;
}

uint8_t to_bcd(uint8_t binary) {
    return ((binary / 10) << 4) | (binary % 10);
}

uint8_t from_bcd(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0xf);
}

bool rtc_is_invalid(const rtc_t* rtc) {
    return rtc->seconds > 59 ||
        rtc->minutes > 59 ||
        rtc->hours > 23 ||
        rtc->day > 31 ||
        rtc->month > 12 ||
        rtc->year < 2000 ||
        rtc->year > 2099;
}

// Validate that the RTC is set to a valid time, and to a relatively
// sane one. Report the validated or reset time back via rtc.
void sanitize_rtc(void* ctx, zx_protocol_device_t* dev, rtc_t* rtc) {
    // January 1, 2016 00:00:00
    static const rtc_t default_rtc = {
        .day = 1,
        .month = 1,
        .year = 2016,
    };
    size_t out_actual;
    dev->ioctl(ctx, IOCTL_RTC_GET, NULL, 0, rtc, sizeof *rtc, &out_actual);
    if (rtc_is_invalid(rtc) || rtc->year < local_epoc_year) {
        dev->ioctl(ctx, IOCTL_RTC_SET, &default_rtc, sizeof default_rtc, NULL, 0, NULL);
        *rtc = default_rtc;
    }
}
