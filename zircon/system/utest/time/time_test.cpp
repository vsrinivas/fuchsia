// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/time.h>

static bool time_add_duration_test() {
    BEGIN_TEST;

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

    END_TEST;
}

static bool time_sub_duration_test() {
    BEGIN_TEST;

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

    END_TEST;
}

static bool time_sub_time_test() {
    BEGIN_TEST;

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

    END_TEST;
}

static bool duration_add_duration_test() {
    BEGIN_TEST;

    EXPECT_EQ(0, zx_duration_add_duration(0, 0));

    EXPECT_EQ(918741562, zx_duration_add_duration(918729180, 12382));

    EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_add_duration(ZX_TIME_INFINITE_PAST, 0));
    EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_add_duration(ZX_TIME_INFINITE_PAST, -1));
    EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_add_duration(ZX_TIME_INFINITE_PAST, -3298901));
    EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_add_duration(ZX_TIME_INFINITE_PAST, ZX_TIME_INFINITE_PAST));

    EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, 0));
    EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, 1));
    EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, 3298901));
    EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, ZX_TIME_INFINITE));
    EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_add_duration(ZX_TIME_INFINITE, INT64_MAX));

    END_TEST;
}

static bool duration_sub_duration_test() {
    BEGIN_TEST;

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

    END_TEST;
}

static bool duration_mul_int64_test() {
    BEGIN_TEST;

    EXPECT_EQ(0, zx_duration_mul_int64(0, 0));
    EXPECT_EQ(39284291, zx_duration_mul_int64(39284291, 1));
    EXPECT_EQ(220499082795, zx_duration_mul_int64(23451, 9402545));
    EXPECT_EQ(-39284291, zx_duration_mul_int64(39284291, -1));
    EXPECT_EQ(-220499082795, zx_duration_mul_int64(23451, -9402545));
    EXPECT_EQ(220499082795, zx_duration_mul_int64(-23451, -9402545));

    EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_mul_int64(ZX_TIME_INFINITE, 2));
    EXPECT_EQ(ZX_TIME_INFINITE, zx_duration_mul_int64(ZX_TIME_INFINITE_PAST, -2));
    EXPECT_EQ(ZX_TIME_INFINITE_PAST, zx_duration_mul_int64(ZX_TIME_INFINITE_PAST, 2));

    END_TEST;
}

BEGIN_TEST_CASE(time_test)
RUN_TEST(time_add_duration_test)
RUN_TEST(time_sub_duration_test)
RUN_TEST(time_sub_time_test)
RUN_TEST(duration_add_duration_test)

RUN_TEST(duration_sub_duration_test)
RUN_TEST(duration_mul_int64_test)
END_TEST_CASE(time_test)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
