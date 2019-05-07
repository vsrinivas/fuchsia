// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../timestamp_extrapolator.h"
#include "gtest/gtest.h"

constexpr uint64_t k48000HzDualPCMBytesPerSecond = 48000 * sizeof(uint16_t) * 2;
constexpr uint64_t k48000HzDualPCMByteDuration =
    ZX_SEC(1) / 48000 / sizeof(uint16_t) / 2;

TEST(TimestampExtrapolator, InformIsSuperceded) {
  TimestampExtrapolator extrapolator(ZX_SEC(1), k48000HzDualPCMBytesPerSecond);

  extrapolator.Inform(10, 0);
  extrapolator.Inform(100, 101);
  EXPECT_EQ(*extrapolator.Extrapolate(100), 101u);
}

TEST(TimestampExtrapolator, EmptyWithoutInformation) {
  TimestampExtrapolator extrapolator(ZX_SEC(1), k48000HzDualPCMBytesPerSecond);

  EXPECT_EQ(extrapolator.Extrapolate(1), std::nullopt);
}

TEST(TimestampExtrapolator, RealTime) {
  TimestampExtrapolator extrapolator(ZX_SEC(1), k48000HzDualPCMBytesPerSecond);

  extrapolator.Inform(0, 0);
  EXPECT_EQ(*extrapolator.Extrapolate(1), k48000HzDualPCMByteDuration);

  extrapolator.Inform(0, 200);
  EXPECT_EQ(*extrapolator.Extrapolate(1), 200 + k48000HzDualPCMByteDuration);
}

TEST(TimestampExtrapolator, FastTime) {
  TimestampExtrapolator extrapolator(ZX_SEC(2), k48000HzDualPCMBytesPerSecond);

  extrapolator.Inform(1000, 0);
  EXPECT_EQ(*extrapolator.Extrapolate(1001), k48000HzDualPCMByteDuration * 2);
}

TEST(TimestampExtrapolator, SlowTime) {
  TimestampExtrapolator extrapolator(ZX_SEC(1) / 2,
                                     k48000HzDualPCMBytesPerSecond);

  extrapolator.Inform(0, 0);
  EXPECT_EQ(*extrapolator.Extrapolate(2), k48000HzDualPCMByteDuration);
}

TEST(TimestampExtrapolator, TimestampIsConsumed) {
  TimestampExtrapolator extrapolator(ZX_SEC(1), k48000HzDualPCMBytesPerSecond);

  extrapolator.Inform(0, 0);
  EXPECT_TRUE(extrapolator.Extrapolate(0).has_value());
  EXPECT_FALSE(extrapolator.Extrapolate(1).has_value());
}

TEST(TimestampExtrapolator, TimestampOnlyCarriesWithoutTimebase) {
  TimestampExtrapolator extrapolator;

  extrapolator.Inform(100, 234);
  EXPECT_TRUE(extrapolator.has_information());
  EXPECT_FALSE(extrapolator.Extrapolate(101).has_value());
  // Should not have value because all extrapolation attempts are consuming.
  EXPECT_FALSE(extrapolator.Extrapolate(100).has_value());

  extrapolator.Inform(100, 234);
  EXPECT_EQ(*extrapolator.Extrapolate(100), 234u);
}
