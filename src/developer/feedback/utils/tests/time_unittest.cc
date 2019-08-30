// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/time.h"

#include <string>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
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
// constexpr char kNegRndmNSecsString[] = "-3d5h17m12s";

TEST(TimeTest, ZeroDuration) { EXPECT_EQ(FormatDuration(kZero).value(), kZeroString); }

TEST(TimeTest, SecondOnly) { EXPECT_EQ(FormatDuration(kSecsOnly).value(), kSecsOnlyString); }

TEST(TimeTest, MinuteOnly) { EXPECT_EQ(FormatDuration(kMinsOnly).value(), kMinsOnlyString); }
TEST(TimeTest, HourOnly) { EXPECT_EQ(FormatDuration(kHoursOnly).value(), kHoursOnlyString); }

TEST(TimeTest, DayOnly) { EXPECT_EQ(FormatDuration(kDaysOnly).value(), kDaysOnlyString); }

TEST(TimeTest, SecondAndMinute) {
  EXPECT_EQ(FormatDuration(kSecsAndMins).value(), kSecsAndMinsString);
}

TEST(TimeTest, SecondAndHour) {
  EXPECT_EQ(FormatDuration(kSecsAndHours).value(), kSecsAndHoursString);
}

TEST(TimeTest, SecondAndDay) {
  EXPECT_EQ(FormatDuration(kSecsAndDays).value(), kSecsAndDaysString);
}

TEST(TimeTest, MinuteAndHour) {
  EXPECT_EQ(FormatDuration(kMinsAndHours).value(), kMinsAndHoursString);
}

TEST(TimeTest, MinuteAndDay) {
  EXPECT_EQ(FormatDuration(kMinsAndDays).value(), kMinsAndDaysString);
}

TEST(TimeTest, HourAndDay) {
  EXPECT_EQ(FormatDuration(kHoursAndDays).value(), kHoursAndDaysString);
}

TEST(TimeTest, SecAndMinAndHour) {
  EXPECT_EQ(FormatDuration(kSecsAndMinsAndHours).value(), kSecsAndMinsAndHoursString);
}

TEST(TimeTest, SecAndMinAndDay) {
  EXPECT_EQ(FormatDuration(kSecsAndMinsAndDays).value(), kSecsAndMinsAndDaysString);
}

TEST(TimeTest, SecAndHourAndDay) {
  EXPECT_EQ(FormatDuration(kSecsAndHoursAndDays).value(), kSecsAndHoursAndDaysString);
}

TEST(TimeTest, MinAndHourAndDay) {
  EXPECT_EQ(FormatDuration(kMinsAndHoursAndDays).value(), kMinsAndHoursAndDaysString);
}

TEST(TimeTest, AllUnits) { EXPECT_EQ(FormatDuration(kAllUnits).value(), kAllUnitsString); }

TEST(TimeTest, RandomNSec) { EXPECT_EQ(FormatDuration(kRndmNSecs).value(), kRndmNSecsString); }

TEST(TimeTest, NegativeRandomNSec) { EXPECT_EQ(FormatDuration(kNegRndmNSecs), std::nullopt); }

}  // namespace
}  // namespace feedback
