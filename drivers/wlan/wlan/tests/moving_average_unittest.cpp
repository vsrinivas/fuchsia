// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <drivers/wifi/common/moving_average.h>

namespace wlan {
namespace {

class MovingAverageTest : public ::testing::Test {
   protected:
    common::MovingAverage<uint8_t, uint16_t, 3> avg_;
};

TEST_F(MovingAverageTest, Some) {
    EXPECT_EQ(0u, avg_.avg());

    avg_.add(10);
    EXPECT_EQ(10u, avg_.avg());

    avg_.add(20);
    EXPECT_EQ(15u, avg_.avg());

    avg_.add(40);
    EXPECT_EQ(23u, avg_.avg());

    avg_.add(30);
    EXPECT_EQ(30u, avg_.avg());

    avg_.add(5);
    EXPECT_EQ(25u, avg_.avg());

    avg_.reset();
    EXPECT_EQ(0u, avg_.avg());

    avg_.add(3);
    EXPECT_EQ(3u, avg_.avg());
}

}  // namespace
}  // namespace wlan
