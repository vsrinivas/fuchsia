// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/intl/time_zone_info/time_zone_info_service.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <optional>
#include <variant>

#include <gtest/gtest.h>

#include "garnet/bin/trace/tests/component_context.h"
#include "lib/fostr/fidl/fuchsia/intl/formatting.h"

namespace intl {
namespace testing {

using fuchsia::intl::CivilTime;
using fuchsia::intl::CivilToAbsoluteTimeOptions;
using fuchsia::intl::DayOfWeek;
using fuchsia::intl::Month;
using fuchsia::intl::RepeatedTimeConversion;
using fuchsia::intl::SkippedTimeConversion;
using fuchsia::intl::TimeZoneId;
using fuchsia::intl::TimeZonesError;
using sys::testing::ComponentContextProvider;
using AbsoluteToCivilTime_Result = fuchsia::intl::TimeZones_AbsoluteToCivilTime_Result;
using CivilToAbsoluteTime_Result = fuchsia::intl::TimeZones_CivilToAbsoluteTime_Result;

static constexpr uint64_t kNanosecondsPerSecond = 1'000'000'000;
static const std::string kNyc = "America/New_York";

class TimeZoneInfoServiceTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    instance_ = std::make_unique<TimeZoneInfoService>();
    instance_->Start();
    // Makes the service under test available in the outgoing directory, so that the tests can
    // connect to it.
    ASSERT_EQ(ZX_OK, provider_.context()->outgoing()->AddPublicService(
                         instance_->GetHandler(dispatcher())));
  }

  // Creates a client of `fuchsia.intl.TimeZones`, which can be instantiated in a test case to
  // connect to the service under test.
  fuchsia::intl::TimeZonesPtr GetClient() {
    return provider_.ConnectToPublicService<fuchsia::intl::TimeZones>();
  }

  void AssertAbsoluteToCivilTime(const std::string time_zone_id, const zx_time_t absolute_time,
                                 std::variant<CivilTime, TimeZonesError> expected) {
    TimeZoneId tz_id{.id = time_zone_id};

    auto client = GetClient();
    std::optional<AbsoluteToCivilTime_Result> result;
    client->AbsoluteToCivilTime(tz_id, absolute_time, [&result](auto r) { result = std::move(r); });
    RunLoopUntil([&result] { return result.has_value(); });

    if (std::holds_alternative<CivilTime>(expected)) {
      ASSERT_TRUE(result.value().is_response());
      auto actual = std::move(result.value().response().civil_time);
      CivilTime expected_time = std::move(std::get<CivilTime>(expected));
      ASSERT_TRUE(fidl::Equals(expected_time, actual)) << "expected:\n"
                                                       << expected_time << "\nactual:\n"
                                                       << actual;
    } else {
      ASSERT_TRUE(result.value().is_err()) << "Actually got: " << result.value().response();
      auto actual = result.value().err();
      TimeZonesError expected_err = std::get<TimeZonesError>(expected);
      ASSERT_TRUE(fidl::Equals(expected_err, actual)) << "expected:\n"
                                                      << expected_err << "\nactual:\n"
                                                      << actual;
    }
  }

  void AssertCivilToAbsoluteTime(CivilTime civil_time, CivilToAbsoluteTimeOptions options,
                                 std::variant<zx_time_t, TimeZonesError> expected) {
    auto client = GetClient();
    std::optional<CivilToAbsoluteTime_Result> result;
    client->CivilToAbsoluteTime(std::move(civil_time), std::move(options),
                                [&result](auto r) { result = std::move(r); });
    RunLoopUntil([&result] { return result.has_value(); });

    if (std::holds_alternative<zx_time_t>(expected)) {
      ASSERT_TRUE(result.value().is_response());
      auto actual = result.value().response().absolute_time;
      zx_time_t expected_time = std::get<zx_time_t>(expected);
      ASSERT_EQ(expected_time, actual) << "difference: "
                                       << static_cast<double>(expected_time - actual) /
                                              static_cast<double>(kNanosecondsPerSecond)
                                       << " seconds";
    } else {
      ASSERT_TRUE(result.value().is_err()) << "Actually got: " << result.value().response();
      auto actual = result.value().err();
      TimeZonesError expected_err = std::get<TimeZonesError>(expected);
      ASSERT_TRUE(fidl::Equals(expected_err, actual)) << "expected:\n"
                                                      << expected_err << "\nactual:\n"
                                                      << actual;
    }
  }

  // The default component context provider.
  ComponentContextProvider provider_;

  // The instance of server under test.
  std::unique_ptr<TimeZoneInfoService> instance_;
};

TEST_F(TimeZoneInfoServiceTest, AbsoluteToCivilTime) {
  // 2021-08-15T20:17:42-04:00
  zx_time_t absolute_time = 1629073062 * kNanosecondsPerSecond + 123'456'789;
  CivilTime expected;
  expected.set_year(2021)
      .set_month(Month::AUGUST)
      .set_day(15)
      .set_hour(20)
      .set_minute(17)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_weekday(DayOfWeek::SUNDAY)
      .set_year_day(226)
      .set_time_zone_id(TimeZoneId{.id = kNyc});
  AssertAbsoluteToCivilTime(kNyc, absolute_time, std::move(expected));
}

TEST_F(TimeZoneInfoServiceTest, CivilToAbsoluteTime) {
  CivilTime civil_time;
  civil_time.set_year(2021)
      .set_month(Month::AUGUST)
      .set_day(15)
      .set_hour(20)
      .set_minute(17)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_time_zone_id(TimeZoneId{.id = kNyc});

  CivilToAbsoluteTimeOptions options;
  options.set_repeated_time_conversion(RepeatedTimeConversion::BEFORE_TRANSITION)
      .set_skipped_time_conversion(SkippedTimeConversion::NEXT_VALID_TIME);

  // 2021-08-15T20:17:42-04:00
  zx_time_t expected = 1629073062 * kNanosecondsPerSecond + 123'456'789;
  AssertCivilToAbsoluteTime(std::move(civil_time), std::move(options), expected);
}

TEST_F(TimeZoneInfoServiceTest, CivilToAbsoluteTime_RepeatedTime) {
  CivilTime civil_time;
  civil_time.set_year(2021)
      .set_month(Month::NOVEMBER)
      .set_day(7)
      .set_hour(1)
      .set_minute(30)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_time_zone_id(TimeZoneId{.id = kNyc});

  CivilToAbsoluteTimeOptions options;
  options.set_repeated_time_conversion(RepeatedTimeConversion::BEFORE_TRANSITION)
      .set_skipped_time_conversion(SkippedTimeConversion::NEXT_VALID_TIME);

  // 2021-11-07T01:30:42.123-04:00 = 2021-11-07T05:30:42.123Z, which is the earlier option.
  zx_time_t expected = 1636263042 * kNanosecondsPerSecond + 123'456'789;
  AssertCivilToAbsoluteTime(std::move(civil_time), std::move(options), expected);
}

TEST_F(TimeZoneInfoServiceTest, CivilToAbsoluteTime_SkippedTime_NextValidTime) {
  CivilTime civil_time;
  civil_time.set_year(2021)
      .set_month(Month::MARCH)
      .set_day(14)
      .set_hour(2)
      .set_minute(30)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_time_zone_id(TimeZoneId{.id = kNyc});

  CivilToAbsoluteTimeOptions options;
  options.set_repeated_time_conversion(RepeatedTimeConversion::BEFORE_TRANSITION)
      .set_skipped_time_conversion(SkippedTimeConversion::NEXT_VALID_TIME);

  // 2021-11-07T03:00:00-04:00
  // No fractional seconds when we jump to the next valid time.
  zx_time_t expected = 1615705200 * kNanosecondsPerSecond;
  AssertCivilToAbsoluteTime(std::move(civil_time), std::move(options), expected);
}

TEST_F(TimeZoneInfoServiceTest, CivilToAbsoluteTime_SkippedTime_Reject) {
  CivilTime civil_time;
  civil_time.set_year(2021)
      .set_month(Month::MARCH)
      .set_day(14)
      .set_hour(2)
      .set_minute(30)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_time_zone_id(TimeZoneId{.id = kNyc});

  CivilToAbsoluteTimeOptions options;
  options.set_repeated_time_conversion(RepeatedTimeConversion::BEFORE_TRANSITION)
      .set_skipped_time_conversion(SkippedTimeConversion::REJECT);

  AssertCivilToAbsoluteTime(std::move(civil_time), std::move(options),
                            TimeZonesError::INVALID_DATE);
}

TEST_F(TimeZoneInfoServiceTest, CivilToAbsoluteTime_InvalidTime) {
  CivilTime civil_time;
  civil_time.set_year(2021)
      .set_month(Month::FEBRUARY)
      .set_day(31)  // 🤨
      .set_hour(2)
      .set_minute(30)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_time_zone_id(TimeZoneId{.id = kNyc});

  CivilToAbsoluteTimeOptions options;
  options.set_repeated_time_conversion(RepeatedTimeConversion::BEFORE_TRANSITION)
      .set_skipped_time_conversion(SkippedTimeConversion::NEXT_VALID_TIME);

  AssertCivilToAbsoluteTime(std::move(civil_time), std::move(options),
                            TimeZonesError::INVALID_DATE);
}

TEST_F(TimeZoneInfoServiceTest, CivilToAbsoluteTime_OutOfRange) {
  CivilTime civil_time;
  civil_time
      .set_year(1321)  // Too early
      .set_month(Month::MARCH)
      .set_day(14)
      .set_hour(2)
      .set_minute(30)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_time_zone_id(TimeZoneId{.id = kNyc});

  CivilToAbsoluteTimeOptions options;
  options.set_repeated_time_conversion(RepeatedTimeConversion::BEFORE_TRANSITION)
      .set_skipped_time_conversion(SkippedTimeConversion::NEXT_VALID_TIME);

  AssertCivilToAbsoluteTime(std::move(civil_time), std::move(options),
                            TimeZonesError::INVALID_DATE);
}

TEST_F(TimeZoneInfoServiceTest, CivilToAbsoluteTime_CorrectWeekdayAndYearDay) {
  CivilTime civil_time;
  civil_time.set_year(2021)
      .set_month(Month::AUGUST)
      .set_day(15)
      .set_hour(20)
      .set_minute(17)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_weekday(DayOfWeek::SUNDAY)
      .set_year_day(226)
      .set_time_zone_id(TimeZoneId{.id = kNyc});

  CivilToAbsoluteTimeOptions options;
  options.set_repeated_time_conversion(RepeatedTimeConversion::BEFORE_TRANSITION)
      .set_skipped_time_conversion(SkippedTimeConversion::NEXT_VALID_TIME);

  // 2021-08-15T20:17:42-04:00
  zx_time_t expected = 1629073062 * kNanosecondsPerSecond + 123'456'789;
  AssertCivilToAbsoluteTime(std::move(civil_time), std::move(options), expected);
}

TEST_F(TimeZoneInfoServiceTest, CivilToAbsoluteTime_WrongWeekday) {
  CivilTime civil_time;
  civil_time.set_year(2021)
      .set_month(Month::AUGUST)
      .set_day(15)
      .set_hour(20)
      .set_minute(17)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_weekday(DayOfWeek::FRIDAY)  // Wrong day
      .set_time_zone_id(TimeZoneId{.id = kNyc});

  CivilToAbsoluteTimeOptions options;
  options.set_repeated_time_conversion(RepeatedTimeConversion::BEFORE_TRANSITION)
      .set_skipped_time_conversion(SkippedTimeConversion::NEXT_VALID_TIME);

  AssertCivilToAbsoluteTime(std::move(civil_time), std::move(options),
                            TimeZonesError::INVALID_DATE);
}

TEST_F(TimeZoneInfoServiceTest, CivilToAbsoluteTime_WrongYearDay) {
  CivilTime civil_time;
  civil_time.set_year(2021)
      .set_month(Month::AUGUST)
      .set_day(15)
      .set_hour(20)
      .set_minute(17)
      .set_second(42)
      .set_nanos(123'456'789)
      .set_year_day(17)  // Wrong day
      .set_time_zone_id(TimeZoneId{.id = kNyc});

  CivilToAbsoluteTimeOptions options;
  options.set_repeated_time_conversion(RepeatedTimeConversion::BEFORE_TRANSITION)
      .set_skipped_time_conversion(SkippedTimeConversion::NEXT_VALID_TIME);

  AssertCivilToAbsoluteTime(std::move(civil_time), std::move(options),
                            TimeZonesError::INVALID_DATE);
}

}  // namespace testing
}  // namespace intl
