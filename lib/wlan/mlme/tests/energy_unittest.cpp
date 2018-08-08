// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <wlan/common/energy.h>
#include <cmath>

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

    FemtoWatt fw(4);
    fw += FemtoWatt(3);
    EXPECT_EQ(fw.val, 7u);

    fw -= FemtoWatt(2);
    EXPECT_EQ(fw.val, 5u);
}

TEST_F(EnergyTest, DbmToFemtoWatt) {
    using i8_limits = std::numeric_limits<int8_t>;

    for (int dbm = -100; dbm <= 48; ++dbm) {
        uint64_t got = to_femtoWatt_approx(dBm(dbm)).val;

        // Relative error should be within 3%
        double want = pow(10.0, 12 + dbm * 0.1);
        double rel_err = fabs((static_cast<double>(got) - want) / want);
        EXPECT_LT(rel_err, 0.03);

        // Converting back to dBm should produce the original number
        int back_to_dbm = round(10.0 * (log10(want) - 12.0));
        EXPECT_EQ(back_to_dbm, dbm);
    }

    for (int dbm = i8_limits::min(); dbm < -100; ++dbm) {
        uint64_t res = to_femtoWatt_approx(dBm(dbm)).val;
        // For input below 100 dBm, the result should be below 100 femtowatts
        EXPECT_LT(res, 100u);
    }

    for (int dbm = 49; dbm < i8_limits::max(); ++dbm) {
        uint64_t res = to_femtoWatt_approx(dBm(dbm)).val;
        uint64_t res_48 = to_femtoWatt_approx(dBm(48)).val;
        // For inputs above 48 dBm, the result should be >= the result for 48 dBm
        EXPECT_GE(res, res_48);
    }

    for (int dbm = i8_limits::min(); dbm <= i8_limits::max(); ++dbm) {
        uint64_t res = to_femtoWatt_approx(dBm(dbm)).val;
        // For all inputs, the output should be less than 2^56
        EXPECT_LT(res, 1ull << 56);
    }
}

TEST_F(EnergyTest, FemtoWattToDbm) {
    EXPECT_EQ(to_dBm(FemtoWatt(0)).val, std::numeric_limits<int8_t>::min());
    EXPECT_EQ(to_dBm(FemtoWatt(1)).val, -120);                       // 1 femtowatt = -120 dBm
    EXPECT_EQ(to_dBm(FemtoWatt(1000)).val, -90);                     // 1 picowatt = -90 dBm
    EXPECT_EQ(to_dBm(FemtoWatt(1'000'000'000ull)).val, -30);         // 1 microwatt = -30 dBm
    EXPECT_EQ(to_dBm(FemtoWatt(1'000'000'000'000ull)).val, 0);       // 1 milliwatt = 0 dBm
    EXPECT_EQ(to_dBm(FemtoWatt(1'000'000'000'000'000ull)).val, 30);  // 1 watt = 30 dBm
    // 2^64-1 femtowatts ~= 1.84e7 milliwats ~= 73 dBm
    EXPECT_EQ(to_dBm(FemtoWatt(std::numeric_limits<uint64_t>::max())).val, 73);
}

}  // namespace
}  // namespace common
}  // namespace wlan
