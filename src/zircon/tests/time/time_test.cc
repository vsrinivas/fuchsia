// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/time.h>

#include <zxtest/zxtest.h>

TEST(TimeTest, TimeAddDuration) {
  EXPECT_EQ(0, zx_time_add_duration(0, 0));

  EXPECT_EQ(918741562, zx_time_add_duration(918729180, 12382));

  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_add_duration(ZX_TIME_INFINITE_PAST, 0));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_add_duration(ZX_TIME_INFINITE_PAST, -1));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_add_duration(ZX_TIME_INFINITE_PAST, -3298901));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST,
            zx_time_add_duration(ZX_TIME_INFINITE_PAST, ZX_TIME_INFINITE_PAST));

  EXPECT_EQ(ZX_TIME_INFINITE, zx_time_add_duration(ZX_TIME_INFINITE, 0));
  EXPECT_EQ(ZX_TIME_INFINITE, zx_time_add_duration(ZX_TIME_INFINITE, 1));
  EXPECT_EQ(ZX_TIME_INFINITE, zx_time_add_duration(ZX_TIME_INFINITE, 3298901));
  EXPECT_EQ(ZX_TIME_INFINITE, zx_time_add_duration(ZX_TIME_INFINITE, ZX_TIME_INFINITE));
}

TEST(TimeTest, TimeSubDuration) {
  EXPECT_EQ(-1, zx_time_sub_duration(1, 2));
  EXPECT_EQ(-1, zx_time_sub_duration(0, 1));

  EXPECT_EQ(0, zx_time_sub_duration(0, 0));
  EXPECT_EQ(0, zx_time_sub_duration(ZX_TIME_INFINITE_PAST, ZX_TIME_INFINITE_PAST));
  EXPECT_EQ(0, zx_time_sub_duration(ZX_TIME_INFINITE, ZX_TIME_INFINITE));

  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_sub_duration(ZX_TIME_INFINITE_PAST, 0));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_sub_duration(ZX_TIME_INFINITE_PAST, 1));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_sub_duration(ZX_TIME_INFINITE_PAST, ZX_TIME_INFINITE));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_sub_duration(INT64_MIN, INT64_MAX));

  EXPECT_EQ((ZX_TIME_INFINITE - 1), zx_time_sub_duration(ZX_TIME_INFINITE, 1));

  EXPECT_EQ(918716798, zx_time_sub_duration(918729180, 12382));
}

TEST(TimeTest, TimeSubTime) {
  EXPECT_EQ(-1, zx_time_sub_time(1, 2));
  EXPECT_EQ(-1, zx_time_sub_time(0, 1));

  EXPECT_EQ(0, zx_time_sub_time(0, 0));
  EXPECT_EQ(0, zx_time_sub_time(ZX_TIME_INFINITE_PAST, ZX_TIME_INFINITE_PAST));
  EXPECT_EQ(0, zx_time_sub_time(ZX_TIME_INFINITE, ZX_TIME_INFINITE));

  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_sub_time(ZX_TIME_INFINITE_PAST, 0));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_sub_time(ZX_TIME_INFINITE_PAST, 1));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_sub_time(ZX_TIME_INFINITE_PAST, ZX_TIME_INFINITE));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_time_sub_time(INT64_MIN, INT64_MAX));

  EXPECT_EQ((ZX_TIME_INFINITE - 1), zx_time_sub_time(ZX_TIME_INFINITE, 1));

  EXPECT_EQ(918716798, zx_time_sub_time(918729180, 12382));
}

TEST(TimeTest, DurationAddDuration) {
  EXPECT_EQ(0, zx_duration_add_duration(0, 0));

  EXPECT_EQ(918741562, zx_duration_add_duration(918729180, 12382));

  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_add_duration(ZX_TIME_INFINITE_PAST, 0));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_add_duration(ZX_TIME_INFINITE_PAST, -1));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_add_duration(ZX_TIME_INFINITE_PAST, -3298901));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST,
            zx_duration_add_duration(ZX_TIME_INFINITE_PAST, ZX_TIME_INFINITE_PAST));

  EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, 0));
  EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, 1));
  EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, 3298901));
  EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, ZX_TIME_INFINITE));
  EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, INT64_MAX));
}

TEST(TimeTest, DurationSubDuration) {
  EXPECT_EQ(918716798, zx_duration_sub_duration(918729180, 12382));

  EXPECT_EQ(-1, zx_duration_sub_duration(1, 2));
  EXPECT_EQ(-1, zx_duration_sub_duration(0, 1));

  EXPECT_EQ(0, zx_duration_sub_duration(0, 0));
  EXPECT_EQ(0, zx_duration_sub_duration(3980, 3980));
  EXPECT_EQ(0, zx_duration_sub_duration(ZX_TIME_INFINITE_PAST, ZX_TIME_INFINITE_PAST));
  EXPECT_EQ(0, zx_duration_sub_duration(ZX_TIME_INFINITE, ZX_TIME_INFINITE));

  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_sub_duration(ZX_TIME_INFINITE_PAST, 0));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_sub_duration(ZX_TIME_INFINITE_PAST, 1));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST,
            zx_duration_sub_duration(ZX_TIME_INFINITE_PAST, ZX_TIME_INFINITE));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_sub_duration(INT64_MIN, INT64_MAX));

  EXPECT_EQ((ZX_TIME_INFINITE - 1), zx_duration_sub_duration(ZX_TIME_INFINITE, 1));

  EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_sub_duration(0, ZX_TIME_INFINITE_PAST));
}

TEST(TimeTest, DurationMulInt64) {
  EXPECT_EQ(0, zx_duration_mul_int64(0, 0));
  EXPECT_EQ(39284291, zx_duration_mul_int64(39284291, 1));
  EXPECT_EQ(220499082795, zx_duration_mul_int64(23451, 9402545));
  EXPECT_EQ(-39284291, zx_duration_mul_int64(39284291, -1));
  EXPECT_EQ(-220499082795, zx_duration_mul_int64(23451, -9402545));
  EXPECT_EQ(220499082795, zx_duration_mul_int64(-23451, -9402545));

  EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_mul_int64(ZX_TIME_INFINITE, 2));
  EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_mul_int64(ZX_TIME_INFINITE_PAST, -2));
  EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_mul_int64(ZX_TIME_INFINITE_PAST, 2));
}

TEST(TimeTest, DurationFrom) {
  // overflow saturates to ZX_TIME_INFINITE
  EXPECT_EQ(zx_duration_from_nsec(INT64_MAX), ZX_TIME_INFINITE);
  EXPECT_EQ(zx_duration_from_usec(9223372036854775), 9223372036854775000);
  EXPECT_EQ(zx_duration_from_usec(9223372036854776), ZX_TIME_INFINITE);
  EXPECT_EQ(zx_duration_from_msec(9223372036854), 9223372036854000000);
  EXPECT_EQ(zx_duration_from_msec(9223372036855), ZX_TIME_INFINITE);
  EXPECT_EQ(zx_duration_from_sec(9223372036), 9223372036000000000);
  EXPECT_EQ(zx_duration_from_sec(9223372037), ZX_TIME_INFINITE);
  EXPECT_EQ(zx_duration_from_min(153722867), 9223372020000000000);
  EXPECT_EQ(zx_duration_from_min(153722868), ZX_TIME_INFINITE);
  EXPECT_EQ(zx_duration_from_hour(2562047), 9223369200000000000);
  EXPECT_EQ(zx_duration_from_hour(2562048), ZX_TIME_INFINITE);
  EXPECT_EQ(zx_duration_from_timespec({9223372036, 1}), 9223372036000000001);
  EXPECT_EQ(zx_duration_from_timespec({9223372036, 900000000}), ZX_TIME_INFINITE);

  // underflow saturates to ZX_TIME_INFINITE_PAST
  EXPECT_EQ(zx_duration_from_nsec(INT64_MIN), ZX_TIME_INFINITE_PAST);
  EXPECT_EQ(zx_duration_from_usec(-9223372036854775), -9223372036854775000);
  EXPECT_EQ(zx_duration_from_usec(-9223372036854776), ZX_TIME_INFINITE_PAST);
  EXPECT_EQ(zx_duration_from_msec(-9223372036854), -9223372036854000000);
  EXPECT_EQ(zx_duration_from_msec(-9223372036855), ZX_TIME_INFINITE_PAST);
  EXPECT_EQ(zx_duration_from_sec(-9223372036), -9223372036000000000);
  EXPECT_EQ(zx_duration_from_sec(-9223372037), ZX_TIME_INFINITE_PAST);
  EXPECT_EQ(zx_duration_from_min(-153722867), -9223372020000000000);
  EXPECT_EQ(zx_duration_from_min(-153722868), ZX_TIME_INFINITE_PAST);
  EXPECT_EQ(zx_duration_from_hour(-2562047), -9223369200000000000);
  EXPECT_EQ(zx_duration_from_hour(-2562048), ZX_TIME_INFINITE_PAST);
  EXPECT_EQ(zx_duration_from_timespec({9223372036, 1}), 9223372036000000001);
  EXPECT_EQ(zx_duration_from_timespec({-9223372036, -900000000}), ZX_TIME_INFINITE_PAST);

  // Verify that when the argument is a constexpr the function can evaluated at compile time.
  static_assert(zx_duration_from_nsec(1) == 1);
  static_assert(zx_duration_from_usec(1) == 1000);
  static_assert(zx_duration_from_msec(1) == 1000000);
  static_assert(zx_duration_from_sec(1) == 1000000000);
  static_assert(zx_duration_from_min(1) == 60000000000);
  static_assert(zx_duration_from_hour(1) == 3600000000000);
  static_assert(zx_duration_from_timespec({123, 456}) == 123000000456);
}

// See that we can use the conversion macros as constexpr initializers.
static constexpr const zx_duration_t durations[] = {
    ZX_NSEC(1), ZX_USEC(1), ZX_MSEC(1), ZX_SEC(1), ZX_MIN(1), ZX_HOUR(1),
};

TEST(TimeTest, MacroConversion) {
  // Verify a few values just shy of overflow.
  EXPECT_EQ(ZX_NSEC(INT64_MAX), ZX_TIME_INFINITE);
  EXPECT_EQ(ZX_USEC(9223372036854775), 9223372036854775000);
  EXPECT_EQ(ZX_MSEC(9223372036854), 9223372036854000000);
  EXPECT_EQ(ZX_SEC(9223372036), 9223372036000000000);
  EXPECT_EQ(ZX_MIN(153722867), 9223372020000000000);
  EXPECT_EQ(ZX_HOUR(2562047), 9223369200000000000);
  EXPECT_EQ(ZX_NSEC(INT64_MIN), ZX_TIME_INFINITE_PAST);
  EXPECT_EQ(ZX_USEC(-9223372036854775), -9223372036854775000);
  EXPECT_EQ(ZX_MSEC(-9223372036854), -9223372036854000000);
  EXPECT_EQ(ZX_SEC(-9223372036), -9223372036000000000);
  EXPECT_EQ(ZX_MIN(-153722867), -9223372020000000000);
  EXPECT_EQ(ZX_HOUR(-2562047), -9223369200000000000);

  // Verify that the macro can be evaluated at compile time when the argument is a literal.
  static_assert(ZX_NSEC(1) == 1);
  static_assert(ZX_USEC(1) == 1000);
  static_assert(ZX_MSEC(1) == 1000000);
  static_assert(ZX_SEC(1) == 1000000000);
  static_assert(ZX_MIN(1) == 60000000000);
  static_assert(ZX_HOUR(1) == 3600000000000);

  // Verify that the macro argument is evaluated only once.
  zx_duration_t d = 0;
  EXPECT_EQ(ZX_NSEC(++d), 1LL);
  EXPECT_EQ(d, 1);
  EXPECT_EQ(ZX_USEC(++d), 2LL * 1000);
  EXPECT_EQ(d, 2);
  EXPECT_EQ(ZX_MSEC(++d), 3LL * 1000000);
  EXPECT_EQ(d, 3);
  EXPECT_EQ(ZX_SEC(++d), 4LL * 1000000000);
  EXPECT_EQ(d, 4);
  EXPECT_EQ(ZX_MIN(++d), 5LL * 60 * 1000000000);
  EXPECT_EQ(d, 5);
  EXPECT_EQ(ZX_HOUR(++d), 6LL * 60 * 60 * 1000000000);
  EXPECT_EQ(d, 6);

  // Refer to durations to make sure the compiler knows it's used.
  EXPECT_EQ(durations[0], ZX_NSEC(1));
  EXPECT_EQ(durations[1], ZX_USEC(1));
  EXPECT_EQ(durations[2], ZX_MSEC(1));
  EXPECT_EQ(durations[3], ZX_SEC(1));
  EXPECT_EQ(durations[4], ZX_MIN(1));
  EXPECT_EQ(durations[5], ZX_HOUR(1));
}
