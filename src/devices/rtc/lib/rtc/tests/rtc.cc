// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <librtc.h>
#include <stdint.h>
#include <string.h>

#include <zxtest/zxtest.h>

#include "fuchsia/hardware/rtc/c/fidl.h"
#include "zircon/errors.h"

static fuchsia_hardware_rtc_Time make_rtc(uint16_t year, uint8_t month, uint8_t day, uint8_t hours,
                                          uint8_t minutes, uint8_t seconds) {
  return fuchsia_hardware_rtc_Time{seconds, minutes, hours, day, month, year};
}

enum months {
  JANUARY = 1,
  FEBRUARY = 2,
  MARCH = 3,
  APRIL = 4,
  MAY = 5,
  JUNE = 6,
  JULY = 7,
  AUGUST = 8,
  SEPTEMBER = 9,
  OCTOBER = 10,
  NOVEMBER = 11,
  DECEMBER = 12
};

TEST(RTCLibTest, BCD) {
  EXPECT_EQ(0x00, to_bcd(0));
  EXPECT_EQ(0x16, to_bcd(16));
  EXPECT_EQ(0x99, to_bcd(99));

  EXPECT_EQ(0, from_bcd(0x00));
  EXPECT_EQ(16, from_bcd(0x16));
  EXPECT_EQ(99, from_bcd(0x99));
}

TEST(RTCLibTest, RTCYearsValid) {
  auto t0 = make_rtc(1999, 1, 1, 0, 0, 0);
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.year = 2000;
  EXPECT_FALSE(rtc_is_invalid(&t0));

  t0.year = 2100;
  EXPECT_TRUE(rtc_is_invalid(&t0));
}

TEST(RTCLibTest, RTCMonthsValid) {
  auto t0 = make_rtc(2001, 7, 1, 0, 0, 0);
  EXPECT_FALSE(rtc_is_invalid(&t0));

  t0.month = 13;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = 0;
  EXPECT_TRUE(rtc_is_invalid(&t0));
}

TEST(RTCLibTest, RTCDaysValid) {
  auto t0 = make_rtc(2001, 1, 1, 0, 0, 0);
  EXPECT_FALSE(rtc_is_invalid(&t0));

  t0.month = JANUARY;
  t0.day = 0;
  EXPECT_TRUE(rtc_is_invalid(&t0));
  t0.day = 31;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 32;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = FEBRUARY;
  t0.day = 28;  // not a leap year
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 29;  // not a leap year
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = MARCH;
  t0.day = 31;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.month = MARCH;
  t0.day = 32;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = APRIL;
  t0.day = 30;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 31;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = MAY;
  t0.day = 31;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 32;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = JUNE;
  t0.day = 30;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 31;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = JULY;
  t0.day = 31;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 32;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = AUGUST;
  t0.day = 31;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 32;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = SEPTEMBER;
  t0.day = 30;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 31;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = OCTOBER;
  t0.day = 31;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 32;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = NOVEMBER;
  t0.day = 30;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 31;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.month = DECEMBER;
  t0.day = 31;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.day = 32;
  EXPECT_TRUE(rtc_is_invalid(&t0));
  t0.day = 99;
  EXPECT_TRUE(rtc_is_invalid(&t0));
}

TEST(RTCLibTest, HoursMinutesSecondsValid) {
  auto t0 = make_rtc(2001, 1, 1, 0, 0, 0);
  EXPECT_FALSE(rtc_is_invalid(&t0));

  t0.day = 1;
  t0.hours = 0;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.hours = 23;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.hours = 24;
  EXPECT_TRUE(rtc_is_invalid(&t0));
  t0.hours = 25;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.hours = 1;
  t0.minutes = 0;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.minutes = 59;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.minutes = 60;
  EXPECT_TRUE(rtc_is_invalid(&t0));
  t0.minutes = 61;
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.minutes = 1;
  t0.seconds = 0;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.seconds = 59;
  EXPECT_FALSE(rtc_is_invalid(&t0));
  t0.seconds = 60;
  EXPECT_TRUE(rtc_is_invalid(&t0));
  t0.seconds = 61;
  EXPECT_TRUE(rtc_is_invalid(&t0));
}

TEST(RTCLibTest, LeapYears) {
  auto t0 = make_rtc(2000, 2, 28, 0, 0, 0);  // Is a leap year
  EXPECT_FALSE(rtc_is_invalid(&t0));

  t0.day = 29;
  EXPECT_FALSE(rtc_is_invalid(&t0));

  t0.year = 2001;  // NOT a leap year
  EXPECT_TRUE(rtc_is_invalid(&t0));

  t0.year = 2004;  // A leap year
  EXPECT_FALSE(rtc_is_invalid(&t0));

  t0.year = 2020;  // A leap year
  EXPECT_FALSE(rtc_is_invalid(&t0));

  // Sadly 2100 is out of range to test as a non-leap year
}

TEST(RTCLibTest, SecondsSinceEpoch) {
  auto t0 = make_rtc(2018, 8, 4, 1, 19, 1);
  EXPECT_EQ(1533345541, seconds_since_epoch(&t0));

  auto t1 = make_rtc(2000, 1, 1, 0, 0, 0);
  EXPECT_EQ(946684800, seconds_since_epoch(&t1));
}

static fuchsia_hardware_rtc_Time default_rtc;

static bool rtc_equal(fuchsia_hardware_rtc_Time* t0, fuchsia_hardware_rtc_Time* t1) {
  return (t0->year == t1->year && t0->month == t1->month && t0->day == t1->day &&
          t0->hours == t1->hours && t0->minutes == t1->minutes && t0->seconds == t1->seconds);
}

static void rtc_clear() { memset((void*)&default_rtc, 0, sizeof(default_rtc)); }

static zx_status_t rtc_get(void* ctx, fuchsia_hardware_rtc_Time* rtc) {
  memcpy((void*)rtc, (void*)&default_rtc, sizeof(*rtc));
  return ZX_OK;
}

static zx_status_t rtc_set(void*, const fuchsia_hardware_rtc_Time* rtc) {
  memcpy((void*)&default_rtc, (void*)rtc, sizeof(*rtc));
  return ZX_OK;
}

static zx_status_t rtc_bad_get(void* ctx, fuchsia_hardware_rtc_Time* rtc) {
  return ZX_ERR_ACCESS_DENIED;
}

static zx_status_t rtc_bad_set(void*, const fuchsia_hardware_rtc_Time* rtc) {
  return ZX_ERR_ACCESS_DENIED;
}

static void backstop_clear() { unsetenv("clock.backstop"); }

TEST(RTCLibTest, rtc_backstop_seconds) {
  backstop_clear();
  EXPECT_EQ(0, rtc_backstop_seconds());
  setenv("clock.backstop", "invalid", 1);
  EXPECT_EQ(0, rtc_backstop_seconds());
  setenv("clock.backstop", "1563584646", 1);
  EXPECT_EQ(1563584646, rtc_backstop_seconds());
}

TEST(RTCLibTest, SanitizeRTCPreservesGoodValue) {
  backstop_clear();
  auto t0 = make_rtc(2018, 8, 4, 1, 19, 1);  // good value
  EXPECT_FALSE(rtc_is_invalid(&t0));
  rtc_clear();

  sanitize_rtc(NULL, &t0, rtc_get, rtc_set);
  EXPECT_TRUE(rtc_equal(&t0, &default_rtc));
}

TEST(RTCLibTest, SanitizeRTCCorrectsBadValue) {
  backstop_clear();
  auto t0 = make_rtc(2018, 8, 4, 99, 19, 1);  // bad value
  EXPECT_TRUE(rtc_is_invalid(&t0));
  rtc_clear();

  sanitize_rtc(NULL, &t0, rtc_get, rtc_set);
  // don't actually care what value it's set to
  EXPECT_FALSE(rtc_is_invalid(&t0));
  EXPECT_FALSE(rtc_is_invalid(&default_rtc));
}

TEST(RTCLibTest, SanitizeRTCCheecksGetError) {
  auto t0 = make_rtc(2018, 8, 4, 99, 19, 1);
  EXPECT_TRUE(rtc_is_invalid(&t0));
  rtc_clear();

  sanitize_rtc(NULL, &t0, rtc_bad_get, rtc_set);
  EXPECT_TRUE(rtc_is_invalid(&default_rtc));
  EXPECT_TRUE(rtc_is_invalid(&t0));
}

TEST(RTCLibTest, SanitizeRTCCheecksSetError) {
  backstop_clear();
  auto t0 = make_rtc(2018, 8, 4, 99, 19, 1);
  EXPECT_TRUE(rtc_is_invalid(&t0));
  rtc_clear();

  sanitize_rtc(NULL, &t0, rtc_get, rtc_bad_set);
  EXPECT_TRUE(rtc_is_invalid(&default_rtc));
  EXPECT_TRUE(rtc_is_invalid(&t0));
}

TEST(RTCLibTest, SanitizeRTCSetsBackstop) {
  backstop_clear();

  // There's no backstop value, but we're ahead of the hardcoded backstop.
  auto before_backstop = make_rtc(2019, 2, 2, 0, 0, 0);
  sanitize_rtc(nullptr, &before_backstop, &rtc_get, &rtc_set);
  EXPECT_TRUE(rtc_equal(&before_backstop, &default_rtc));

  // There's no backstop, and the clock is behind the hardcoded backstop.
  auto before_default = make_rtc(2018, 1, 1, 0, 0, 0);
  sanitize_rtc(nullptr, &before_default, &rtc_get, &rtc_set);
  auto constant_rtc = make_rtc(2019, 1, 1, 0, 0, 0);
  EXPECT_TRUE(rtc_equal(&constant_rtc, &default_rtc));

  // There's a backstop, so any date prior will be moved to the backstop.
  auto backstop = make_rtc(2019, 7, 20, 1, 4, 6);
  setenv("clock.backstop", "1563584646", 1);
  sanitize_rtc(nullptr, &before_backstop, &rtc_get, &rtc_set);
  EXPECT_TRUE(rtc_equal(&backstop, &default_rtc));
}
