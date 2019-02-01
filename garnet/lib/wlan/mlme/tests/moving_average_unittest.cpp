// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <wlan/common/moving_average.h>

using namespace wlan::common;

namespace wlan {
namespace {

class MovingAverageTest : public ::testing::Test {};

TEST_F(MovingAverageTest, MovingAverage) {
    MovingAverage<uint8_t, uint16_t, 3> avg;
    EXPECT_EQ(0u, avg.avg());

    avg.add(10);
    EXPECT_EQ(10u, avg.avg());

    avg.add(20);
    EXPECT_EQ(15u, avg.avg());

    avg.add(40);
    EXPECT_EQ(23u, avg.avg());

    avg.add(30);
    EXPECT_EQ(30u, avg.avg());

    avg.add(5);
    EXPECT_EQ(25u, avg.avg());

    avg.reset();
    EXPECT_EQ(0u, avg.avg());

    avg.add(3);
    EXPECT_EQ(3u, avg.avg());
}

TEST_F(MovingAverageTest, MovingAverageDbm) {
    MovingAverageDbm<3> d;

    EXPECT_EQ(0u, d.avg().val);

    d.add(dBm(-30));
    EXPECT_EQ(-30, to_dBm(d.avg()).val);

    d.add(dBm(-30));
    EXPECT_EQ(-30, to_dBm(d.avg()).val);

    d.add(dBm(-20));
    EXPECT_EQ(-24, to_dBm(d.avg()).val);

    d.add(dBm(-20));
    EXPECT_EQ(-22, to_dBm(d.avg()).val);

    d.reset();
    EXPECT_EQ(0u, d.avg().val);

    d.add(dBm(-30));
    EXPECT_EQ(-30, to_dBm(d.avg()).val);
}

}  // namespace
}  // namespace wlan
