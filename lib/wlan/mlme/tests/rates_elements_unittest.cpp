// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/rates_elements.h>
#include <gtest/gtest.h>

namespace wlan {

template<size_t N>
struct Buf {
    uint8_t data[N] = {};
    ElementWriter w = { data, N };
};

TEST(WriteSupportedRates, Null) {
    Buf<32> buf;

    EXPECT_TRUE(RatesWriter(nullptr, 3).WriteSupportedRates(&buf.w));
    EXPECT_EQ(0u, buf.w.size());
}

TEST(WriteSupportedRates, Zero) {
    Buf<32> buf;

    SupportedRate rate;
    EXPECT_TRUE(RatesWriter(&rate, 0).WriteSupportedRates(&buf.w));
    EXPECT_EQ(0u, buf.w.size());
}

TEST(WriteSupportedRates, Three) {
    Buf<32> buf;

    SupportedRate rates[3] = { SupportedRate{ 10 }, SupportedRate{ 20 }, SupportedRate{ 30 } };
    EXPECT_TRUE(RatesWriter(rates, 3).WriteSupportedRates(&buf.w));
    EXPECT_EQ(5u, buf.w.size()); // element header + 3 rates

    EXPECT_EQ(element_id::kSuppRates, buf.data[0]);
    EXPECT_EQ(3u, buf.data[1]);

    EXPECT_EQ(10u, buf.data[2]);
    EXPECT_EQ(20u, buf.data[3]);
    EXPECT_EQ(30u, buf.data[4]);
}

TEST(WriteSupportedRates, Nine) {
    Buf<10> buf;

    SupportedRate rates[9] = { SupportedRate{10}, SupportedRate{20}, SupportedRate{30},
                               SupportedRate{40}, SupportedRate{50}, SupportedRate{60},
                               SupportedRate{70}, SupportedRate{80}, SupportedRate{90} };
    EXPECT_TRUE(RatesWriter(rates, 9).WriteSupportedRates(&buf.w));
    EXPECT_EQ(10u, buf.w.size()); // element header + 8 rates

    EXPECT_EQ(element_id::kSuppRates, buf.data[0]);

    uint8_t expected[10] = { element_id::kSuppRates, 8, 10, 20, 30, 40, 50, 60, 70, 80 };
    EXPECT_EQ(0, memcmp(expected, buf.data, sizeof(expected)));
}

TEST(WriteSupportedRates, BufferTooSmall) {
    Buf<4> buf;

    SupportedRate rates[3] = { SupportedRate{10}, SupportedRate{20}, SupportedRate{30} };
    EXPECT_FALSE(RatesWriter(rates, 3).WriteSupportedRates(&buf.w));
    EXPECT_EQ(0u, buf.w.size());
}

TEST(WriteExtendedSupportedRates, Null) {
    Buf<32> buf;

    EXPECT_TRUE(RatesWriter(nullptr, 3).WriteExtendedSupportedRates(&buf.w));
    EXPECT_EQ(0u, buf.w.size());
}

TEST(WriteExtendedSupportedRates, TooFew) {
    Buf<32> buf;

    SupportedRate rates[8] = { SupportedRate{10}, SupportedRate{20}, SupportedRate{30},
                               SupportedRate{40}, SupportedRate{50}, SupportedRate{60},
                               SupportedRate{70}, SupportedRate{80} };
    EXPECT_TRUE(RatesWriter(rates, 8).WriteExtendedSupportedRates(&buf.w));
    EXPECT_EQ(0u, buf.w.size());
}

TEST(WriteExtendedSupportedRates, One) {
    Buf<3> buf;

    SupportedRate rates[9] = { SupportedRate{10}, SupportedRate{20}, SupportedRate{30},
                               SupportedRate{40}, SupportedRate{50}, SupportedRate{60},
                               SupportedRate{70}, SupportedRate{80}, SupportedRate{90} };
    EXPECT_TRUE(RatesWriter(rates, 9).WriteExtendedSupportedRates(&buf.w));
    EXPECT_EQ(3u, buf.w.size());

    EXPECT_EQ(element_id::kExtSuppRates, buf.data[0]);
    EXPECT_EQ(1u, buf.data[1]);
    EXPECT_EQ(90u, buf.data[2]);
}

TEST(WriteExtendedSupportedRates, BufferTooSmall) {
    Buf<2> buf;

    SupportedRate rates[9] = { SupportedRate{10}, SupportedRate{20}, SupportedRate{30},
                               SupportedRate{40}, SupportedRate{50}, SupportedRate{60},
                               SupportedRate{70}, SupportedRate{80}, SupportedRate{90} };
    EXPECT_FALSE(RatesWriter(rates, 9).WriteExtendedSupportedRates(&buf.w));
    EXPECT_EQ(0u, buf.w.size());
}

} // namespace wlan
