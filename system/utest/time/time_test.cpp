// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/time.h>

static bool time_add_duration_test() {
    BEGIN_TEST;

    EXPECT_EQ(0, zx_time_add_duration(0, 0));

    EXPECT_EQ(918741562, zx_time_add_duration(918729180, 12382));

    EXPECT_EQ(ZX_TIME_INFINITE, zx_time_add_duration(ZX_TIME_INFINITE, 0));
    EXPECT_EQ(ZX_TIME_INFINITE, zx_time_add_duration(ZX_TIME_INFINITE, 1));
    EXPECT_EQ(ZX_TIME_INFINITE, zx_time_add_duration(ZX_TIME_INFINITE, 3298901));
    EXPECT_EQ(ZX_TIME_INFINITE, zx_time_add_duration(ZX_TIME_INFINITE, ZX_TIME_INFINITE));
    EXPECT_EQ(ZX_TIME_INFINITE, zx_time_add_duration(ZX_TIME_INFINITE, UINT64_MAX));

    END_TEST;
}

static bool time_sub_duration_test() {
    BEGIN_TEST;

    EXPECT_EQ(0, zx_time_sub_duration(0, 0));
    EXPECT_EQ(0, zx_time_sub_duration(0, 1));
    EXPECT_EQ(0, zx_time_sub_duration(1, 2));
    EXPECT_EQ(0, zx_time_sub_duration(0, ZX_TIME_INFINITE));
    EXPECT_EQ(0, zx_time_sub_duration(0, UINT64_MAX));
    EXPECT_EQ(0, zx_time_sub_duration(3980, 3980));
    EXPECT_EQ(0, zx_time_sub_duration(ZX_TIME_INFINITE, ZX_TIME_INFINITE));

    EXPECT_EQ((ZX_TIME_INFINITE - 1), zx_time_sub_duration(ZX_TIME_INFINITE, 1));

    EXPECT_EQ(918716798, zx_time_sub_duration(918729180, 12382));

    END_TEST;
}

static bool time_sub_time_test() {
    BEGIN_TEST;

    EXPECT_EQ(0, zx_time_sub_time(0, 0));
    EXPECT_EQ(0, zx_time_sub_time(0, 1));
    EXPECT_EQ(0, zx_time_sub_time(1, 2));
    EXPECT_EQ(0, zx_time_sub_time(0, ZX_TIME_INFINITE));
    EXPECT_EQ(0, zx_time_sub_time(0, UINT64_MAX));
    EXPECT_EQ(0, zx_time_sub_time(3980, 3980));
    EXPECT_EQ(0, zx_time_sub_time(ZX_TIME_INFINITE, ZX_TIME_INFINITE));

    EXPECT_EQ((ZX_TIME_INFINITE - 1), zx_time_sub_duration(ZX_TIME_INFINITE, 1));

    EXPECT_EQ(918716798, zx_time_sub_time(918729180, 12382));

    END_TEST;
}

BEGIN_TEST_CASE(time_test)
RUN_TEST(time_add_duration_test)
RUN_TEST(time_sub_duration_test)
RUN_TEST(time_sub_time_test)
END_TEST_CASE(time_test)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
