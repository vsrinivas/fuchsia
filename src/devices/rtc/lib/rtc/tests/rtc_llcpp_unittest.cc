// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <librtc_llcpp.h>

#include <zxtest/zxtest.h>

#include "fuchsia/hardware/rtc/c/fidl.h"

namespace rtc {

namespace {

// Default values copied from librtc_llcpp.cc.
constexpr int kDefaultYear = 2020;
constexpr FidlRtc::wire::Time kDefaultRtc = {
    .seconds = 0,
    .minutes = 0,
    .hours = 0,
    .day = 1,
    .month = JANUARY,
    .year = kDefaultYear,
};

FidlRtc::wire::Time MakeRtc(uint16_t year, uint8_t month, uint8_t day, uint8_t hours,
                            uint8_t minutes, uint8_t seconds) {
  return FidlRtc::wire::Time{seconds, minutes, hours, day, month, year};
}

bool IsRtcEqual(FidlRtc::wire::Time t0, FidlRtc::wire::Time t1) {
  return (t0.year == t1.year && t0.month == t1.month && t0.day == t1.day && t0.hours == t1.hours &&
          t0.minutes == t1.minutes && t0.seconds == t1.seconds);
}

}  // namespace

TEST(RtcLlccpTest, RTCYearsValid) {
  auto t0 = MakeRtc(1999, 1, 1, 0, 0, 0);
  EXPECT_FALSE(IsRtcValid(t0));

  t0.year = 2000;
  EXPECT_TRUE(IsRtcValid(t0));

  t0.year = 2100;
  EXPECT_FALSE(IsRtcValid(t0));
}

TEST(RtcLlccpTest, RTCMonthsValid) {
  auto t0 = MakeRtc(2001, 7, 1, 0, 0, 0);
  EXPECT_TRUE(IsRtcValid(t0));

  t0.month = 13;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = 0;
  EXPECT_FALSE(IsRtcValid(t0));
}

TEST(RtcLlccpTest, RTCDaysValid) {
  auto t0 = MakeRtc(2001, 1, 1, 0, 0, 0);
  EXPECT_TRUE(IsRtcValid(t0));

  t0.month = JANUARY;
  t0.day = 0;
  EXPECT_FALSE(IsRtcValid(t0));
  t0.day = 31;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 32;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = FEBRUARY;
  t0.day = 28;  // not a leap year
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 29;  // not a leap year
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = MARCH;
  t0.day = 31;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.month = MARCH;
  t0.day = 32;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = APRIL;
  t0.day = 30;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 31;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = MAY;
  t0.day = 31;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 32;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = JUNE;
  t0.day = 30;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 31;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = JULY;
  t0.day = 31;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 32;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = AUGUST;
  t0.day = 31;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 32;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = SEPTEMBER;
  t0.day = 30;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 31;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = OCTOBER;
  t0.day = 31;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 32;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = NOVEMBER;
  t0.day = 30;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 31;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.month = DECEMBER;
  t0.day = 31;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.day = 32;
  EXPECT_FALSE(IsRtcValid(t0));
  t0.day = 99;
  EXPECT_FALSE(IsRtcValid(t0));
}

TEST(RtcLlccpTest, HoursMinutesSecondsValid) {
  auto t0 = MakeRtc(2001, 1, 1, 0, 0, 0);
  EXPECT_TRUE(IsRtcValid(t0));

  t0.day = 1;
  t0.hours = 0;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.hours = 23;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.hours = 24;
  EXPECT_FALSE(IsRtcValid(t0));
  t0.hours = 25;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.hours = 1;
  t0.minutes = 0;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.minutes = 59;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.minutes = 60;
  EXPECT_FALSE(IsRtcValid(t0));
  t0.minutes = 61;
  EXPECT_FALSE(IsRtcValid(t0));

  t0.minutes = 1;
  t0.seconds = 0;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.seconds = 59;
  EXPECT_TRUE(IsRtcValid(t0));
  t0.seconds = 60;
  EXPECT_FALSE(IsRtcValid(t0));
  t0.seconds = 61;
  EXPECT_FALSE(IsRtcValid(t0));
}

TEST(RtcLlccpTest, LeapYears) {
  auto t0 = MakeRtc(2000, 2, 28, 0, 0, 0);  // Is a leap year
  EXPECT_TRUE(IsRtcValid(t0));

  t0.day = 29;
  EXPECT_TRUE(IsRtcValid(t0));

  t0.year = 2001;  // NOT a leap year
  EXPECT_FALSE(IsRtcValid(t0));

  t0.year = 2004;  // A leap year
  EXPECT_TRUE(IsRtcValid(t0));

  t0.year = 2020;  // A leap year
  EXPECT_TRUE(IsRtcValid(t0));
}

TEST(RtcLlccpTest, SecondsSinceEpoch) {
  auto t0 = MakeRtc(2018, 8, 4, 1, 19, 1);
  EXPECT_EQ(1533345541, SecondsSinceEpoch(t0));

  auto t1 = MakeRtc(2000, 1, 1, 0, 0, 0);
  EXPECT_EQ(946684800, SecondsSinceEpoch(t1));
}

TEST(RtcLlccpTest, SanitizeRtc) {
  unsetenv("clock.backstop");

  // Backstop seconds for March 6, 2001.
  const FidlRtc::wire::Time kBackstop = MakeRtc(2001, 3, 6, 0, 0, 0);
  const uint64_t kBackstopSeconds = SecondsSinceEpoch(kBackstop);
  setenv("clock.backstop", std::to_string(kBackstopSeconds).c_str(), 1);

  // Test with a valid RTC value. The same value should be returned.
  FidlRtc::wire::Time t0 = MakeRtc(2020, 10, 3, 0, 0, 0);
  EXPECT_TRUE(IsRtcValid(t0));
  EXPECT_TRUE(IsRtcEqual(t0, SanitizeRtc(t0)));

  // Test with a valid RTC value earlier than the backstop. The backstop
  // value should be returned.
  auto t1 = MakeRtc(2001, 1, 1, 0, 0, 0);
  EXPECT_TRUE(IsRtcValid(t1));
  EXPECT_TRUE(IsRtcEqual(kBackstop, SanitizeRtc(t1)));

  // Test with an invalid RTC value. The backstop value should be returned.
  auto t2 = MakeRtc(1999, 13, 1, 0, 0, 0);
  EXPECT_FALSE(IsRtcValid(t2));
  EXPECT_TRUE(IsRtcEqual(kBackstop, SanitizeRtc(t2)));

  // Test with a RTC value earlier than the default year. The backstop
  // value should be returned.
  auto t3 = MakeRtc(2011, 1, 1, 0, 0, 0);
  EXPECT_TRUE(IsRtcValid(t3));
  EXPECT_TRUE(IsRtcEqual(kBackstop, SanitizeRtc(t3)));
}

// Sanitize an invalid RTC with an invalid backstop.git  The default RTC
// should be returned.
TEST(RtcLlccpTest, SanitizeRtcWithInvalidBackstop) {
  unsetenv("clock.backstop");

  const FidlRtc::wire::Time kInvalidRtc = MakeRtc(2000, 13, 1, 0, 0, 0);
  EXPECT_FALSE(IsRtcValid(kInvalidRtc));

  // Test with an invalid backstop.
  setenv("clock.backstop", "invalid", 1);
  EXPECT_TRUE(IsRtcEqual(kDefaultRtc, SanitizeRtc(kInvalidRtc)));
}

}  // namespace rtc
