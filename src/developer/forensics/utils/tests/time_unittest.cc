// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/time.h"

#include <string>

#include <gtest/gtest.h>

#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace {
constexpr zx::duration kZero(zx::sec(0));

constexpr zx::duration kSecsOnly(zx::sec(1));
constexpr zx::duration kMinsOnly(zx::min(2));
constexpr zx::duration kHoursOnly(zx::hour(3));
constexpr zx::duration kDaysOnly(zx::hour(4 * 24));

constexpr zx::duration kSecsAndMins(kSecsOnly + kMinsOnly);
constexpr zx::duration kSecsAndHours(kSecsOnly + kHoursOnly);
constexpr zx::duration kSecsAndDays(kSecsOnly + kDaysOnly);
constexpr zx::duration kMinsAndHours(kMinsOnly + kHoursOnly);
constexpr zx::duration kMinsAndDays(kMinsOnly + kDaysOnly);
constexpr zx::duration kHoursAndDays(kHoursOnly + kDaysOnly);

constexpr zx::duration kSecsAndMinsAndHours(kSecsOnly + kMinsOnly + kHoursOnly);
constexpr zx::duration kSecsAndMinsAndDays(kSecsOnly + kMinsOnly + kDaysOnly);
constexpr zx::duration kSecsAndHoursAndDays(kSecsOnly + kHoursOnly + kDaysOnly);
constexpr zx::duration kMinsAndHoursAndDays(kMinsOnly + kHoursOnly + kDaysOnly);

constexpr zx::duration kAllUnits(kSecsOnly + kMinsOnly + kHoursOnly + kDaysOnly);
constexpr zx::duration kRndmNSecs(zx::nsec(278232000000000));
constexpr zx::duration kNegRndmNSecs(zx::nsec(-278232000000000));

constexpr char kZeroString[] = "0d0h0m0s";
constexpr char kSecsOnlyString[] = "0d0h0m1s";
constexpr char kMinsOnlyString[] = "0d0h2m0s";
constexpr char kHoursOnlyString[] = "0d3h0m0s";
constexpr char kDaysOnlyString[] = "4d0h0m0s";
constexpr char kSecsAndMinsString[] = "0d0h2m1s";
constexpr char kSecsAndHoursString[] = "0d3h0m1s";
constexpr char kSecsAndDaysString[] = "4d0h0m1s";
constexpr char kMinsAndHoursString[] = "0d3h2m0s";
constexpr char kMinsAndDaysString[] = "4d0h2m0s";
constexpr char kHoursAndDaysString[] = "4d3h0m0s";
constexpr char kSecsAndMinsAndHoursString[] = "0d3h2m1s";
constexpr char kSecsAndMinsAndDaysString[] = "4d0h2m1s";
constexpr char kSecsAndHoursAndDaysString[] = "4d3h0m1s";
constexpr char kMinsAndHoursAndDaysString[] = "4d3h2m0s";
constexpr char kAllUnitsString[] = "4d3h2m1s";
constexpr char kRndmNSecsString[] = "3d5h17m12s";

constexpr zx::time_utc kTime1(0);
constexpr zx::time_utc kTime2((zx::hour(7) + zx::min(14) + zx::sec(52)).get());
constexpr zx::time_utc kTime3((zx::hour(3) * 24 + zx::hour(15) + zx::min(33) + zx::sec(17)).get());

constexpr char kTime1Str[] = "1970-01-01 00:00:00 GMT";
constexpr char kTime2Str[] = "1970-01-01 07:14:52 GMT";
constexpr char kTime3Str[] = "1970-01-04 15:33:17 GMT";

TEST(TimeTest, FormatDuration_ZeroDuration) {
  EXPECT_EQ(FormatDuration(kZero).value(), kZeroString);
}

TEST(TimeTest, FormatDuration_SecondOnly) {
  EXPECT_EQ(FormatDuration(kSecsOnly).value(), kSecsOnlyString);
}

TEST(TimeTest, FormatDuration_MinuteOnly) {
  EXPECT_EQ(FormatDuration(kMinsOnly).value(), kMinsOnlyString);
}
TEST(TimeTest, FormatDuration_HourOnly) {
  EXPECT_EQ(FormatDuration(kHoursOnly).value(), kHoursOnlyString);
}

TEST(TimeTest, FormatDuration_DayOnly) {
  EXPECT_EQ(FormatDuration(kDaysOnly).value(), kDaysOnlyString);
}

TEST(TimeTest, FormatDuration_SecondAndMinute) {
  EXPECT_EQ(FormatDuration(kSecsAndMins).value(), kSecsAndMinsString);
}

TEST(TimeTest, FormatDuration_SecondAndHour) {
  EXPECT_EQ(FormatDuration(kSecsAndHours).value(), kSecsAndHoursString);
}

TEST(TimeTest, FormatDuration_SecondAndDay) {
  EXPECT_EQ(FormatDuration(kSecsAndDays).value(), kSecsAndDaysString);
}

TEST(TimeTest, FormatDuration_MinuteAndHour) {
  EXPECT_EQ(FormatDuration(kMinsAndHours).value(), kMinsAndHoursString);
}

TEST(TimeTest, FormatDuration_MinuteAndDay) {
  EXPECT_EQ(FormatDuration(kMinsAndDays).value(), kMinsAndDaysString);
}

TEST(TimeTest, FormatDuration_HourAndDay) {
  EXPECT_EQ(FormatDuration(kHoursAndDays).value(), kHoursAndDaysString);
}

TEST(TimeTest, FormatDuration_SecAndMinAndHour) {
  EXPECT_EQ(FormatDuration(kSecsAndMinsAndHours).value(), kSecsAndMinsAndHoursString);
}

TEST(TimeTest, FormatDuration_SecAndMinAndDay) {
  EXPECT_EQ(FormatDuration(kSecsAndMinsAndDays).value(), kSecsAndMinsAndDaysString);
}

TEST(TimeTest, FormatDuration_SecAndHourAndDay) {
  EXPECT_EQ(FormatDuration(kSecsAndHoursAndDays).value(), kSecsAndHoursAndDaysString);
}

TEST(TimeTest, FormatDuration_MinAndHourAndDay) {
  EXPECT_EQ(FormatDuration(kMinsAndHoursAndDays).value(), kMinsAndHoursAndDaysString);
}

TEST(TimeTest, FormatDuration_AllUnits) {
  EXPECT_EQ(FormatDuration(kAllUnits).value(), kAllUnitsString);
}

TEST(TimeTest, FormatDuration_RandomNSec) {
  EXPECT_EQ(FormatDuration(kRndmNSecs).value(), kRndmNSecsString);
}

TEST(TimeTest, FormatDuration_NegativeRandomNSec) {
  EXPECT_EQ(FormatDuration(kNegRndmNSecs), std::nullopt);
}

TEST(TimeTest, CurrentUTCTimeRaw) {
  timekeeper::TestClock clock;

  clock.Set(kTime1);
  EXPECT_EQ(CurrentUTCTimeRaw(&clock), kTime1);

  clock.Set(kTime2);
  EXPECT_EQ(CurrentUTCTimeRaw(&clock), kTime2);

  clock.Set(kTime3);
  EXPECT_EQ(CurrentUTCTimeRaw(&clock), kTime3);
}

TEST(TimeTest, CurrentUTCTime) {
  timekeeper::TestClock clock;

  clock.Set(kTime1);
  EXPECT_EQ(CurrentUTCTime(&clock), kTime1Str);

  clock.Set(kTime2);
  EXPECT_EQ(CurrentUTCTime(&clock), kTime2Str);

  clock.Set(kTime3);
  EXPECT_EQ(CurrentUTCTime(&clock), kTime3Str);
}

}  // namespace
}  // namespace forensics
