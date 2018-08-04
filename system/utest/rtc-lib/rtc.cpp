// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/device/rtc.h>
#include <librtc.h>
#include <stdint.h>

rtc_t make_rtc(uint16_t year, uint8_t month,
               uint8_t day, uint8_t hours,
               uint8_t minutes, uint8_t seconds) {
    return rtc_t { seconds, minutes, hours, day, month, year };
}

bool santitize_rtc_test() {
    BEGIN_TEST;

    auto t0 = make_rtc(1999, 1, 1, 0, 0, 0);
    EXPECT_TRUE(rtc_is_invalid(&t0));

    t0.year = 2000;
    EXPECT_FALSE(rtc_is_invalid(&t0));

    t0.month = 13;
    EXPECT_TRUE(rtc_is_invalid(&t0));
    t0.month = 1;
    t0.day = 32;
    EXPECT_TRUE(rtc_is_invalid(&t0));
    t0.day = 1;
    t0.hours = 25;
    EXPECT_TRUE(rtc_is_invalid(&t0));
    t0.hours = 1;
    t0.minutes = 61;
    EXPECT_TRUE(rtc_is_invalid(&t0));
    t0.minutes = 1;
    t0.seconds = 61;
    EXPECT_TRUE(rtc_is_invalid(&t0));

    END_TEST;
}

bool seconds_since_epoc_test() {
    BEGIN_TEST;

    auto t0 = make_rtc(2018, 8, 4, 1, 19, 1);
    EXPECT_EQ(1533345541, seconds_since_epoch(&t0));

    auto t1 = make_rtc(2000, 1, 1, 0, 0, 0);
    EXPECT_EQ(946684800, seconds_since_epoch(&t1));

    END_TEST;
}

BEGIN_TEST_CASE(rtc_lib_tests)
RUN_TEST(santitize_rtc_test)
RUN_TEST(seconds_since_epoc_test)
END_TEST_CASE(rtc_lib_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
