// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <wlan/common/energy.h>

namespace wlan {
namespace common {
namespace {

class EnergyTest : public ::testing::Test {
   protected:
};

TEST_F(EnergyTest, Construct) {
    dB a(10);
    dB b(-10);
    dBh c(100);
    dBh d(-100);

    EXPECT_EQ(a.val, 10);
    EXPECT_EQ(b.val, -10);
    EXPECT_EQ(c.val, 100);
    EXPECT_EQ(d.val, -100);

    mWatt e(100);
    EXPECT_EQ(e.val, 100);
}

TEST_F(EnergyTest, Assign) {
    dB a(23);
    dB b(81);

    EXPECT_EQ(a.val, 23);
    EXPECT_EQ(b.val, 81);

    b = a;
    EXPECT_EQ(b.val, 23);

    mWatt e(100);
    mWatt f(200);
    e = f;
    EXPECT_EQ(e.val, 200);
}

TEST_F(EnergyTest, Compare) {
    dBm a(-70);
    dBm b(-80);
    dBm c(-70);

    EXPECT_EQ(a == c, true);
    EXPECT_EQ(a != c, false);
    EXPECT_EQ(a != b, true);
    EXPECT_EQ(a == b, false);
    EXPECT_EQ(a > b, true);
    EXPECT_EQ(a < b, false);
    EXPECT_EQ(b > a, false);
    EXPECT_EQ(b < a, true);
    EXPECT_EQ(a >= b, true);
    EXPECT_EQ(b >= a, false);
    EXPECT_EQ(a >= c, true);
    EXPECT_EQ(c >= b, true);
}

TEST_F(EnergyTest, Comparem_Watt) {
    mWatt a(200);
    mWatt b(100);
    mWatt c(200);

    EXPECT_EQ(a == c, true);
    EXPECT_EQ(a != c, false);
    EXPECT_EQ(a != b, true);
    EXPECT_EQ(a == b, false);
    EXPECT_EQ(a > b, true);
    EXPECT_EQ(a < b, false);
    EXPECT_EQ(b > a, false);
    EXPECT_EQ(b < a, true);
    EXPECT_EQ(a >= b, true);
    EXPECT_EQ(b >= a, false);
    EXPECT_EQ(a >= c, true);
    EXPECT_EQ(c >= b, true);
}

TEST_F(EnergyTest, Conversion) {
    dB c(25);
    dBh d(to_dBh(c));
    dB e(to_dB(d));
    EXPECT_EQ(d.val, 50);
    EXPECT_EQ(e.val, 25);
    EXPECT_EQ(e == c, true);

    dBm f(-30);
    dBmh g(to_dBmh(f));
    dBm h(to_dBm(g));
    EXPECT_EQ(g.val, -60);
    EXPECT_EQ(h.val, -30);
    EXPECT_EQ(f == h, true);

    Rcpi i = to_Rcpi(to_dBmh(f), true);
    EXPECT_EQ(i, 160);  // -30 dBm == 160 RCPI
    Rcpi j = to_Rcpi(to_dBmh(f), false);
    EXPECT_EQ(j, 255);  // not measured. Default to 255.
}

TEST_F(EnergyTest, Arithmetics) {
    dB a(10);
    dB b(25);
    dB c = a + b;
    EXPECT_EQ(c.val, 35);
    dB d = a - b;
    EXPECT_EQ(d.val, -15);

    dBm e(20);
    dBm f(20);
    dBm g = e + f;
    EXPECT_EQ(g.val, 23);  // 20 dBm + 20 dBm = 23 dBm

    dBm h(10);
    dBm i = e + h;
    EXPECT_EQ(i.val, 20);  // 20 dBm + 10 dBm = 20 dBm

    dBm j(-70);
    dBm k(-71);
    dBm l = j + k;
    EXPECT_EQ(l.val, -67);  // -70 dBm + (-71) dBm = -67 dBm
}

}  // namespace
}  // namespace common
}  // namespace wlan
