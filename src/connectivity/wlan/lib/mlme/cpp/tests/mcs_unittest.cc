// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/common/element.h>

namespace wlan {
namespace {

TEST(McsTest, Intersect) {
  SupportedMcsSet lhs;
  SupportedMcsSet rhs;
  SupportedMcsSet result;

  lhs.set_rx_mcs_bitmask1(0xffff);
  rhs.set_rx_mcs_bitmask1(0x00ff);
  lhs.set_rx_mcs_bitmask2(0x0f0f);
  rhs.set_rx_mcs_bitmask2(0x1fff);

  lhs.set_rx_highest_rate(1023);  // Max Mbps defined
  rhs.set_rx_highest_rate(543);

  lhs.set_tx_set_defined(1);
  rhs.set_tx_set_defined(1);

  lhs.set_tx_rx_diff(1);
  rhs.set_tx_rx_diff(0);

  lhs.set_tx_max_ss(3);
  rhs.set_tx_max_ss(1);

  lhs.set_tx_unequal_mod(0);
  rhs.set_tx_unequal_mod(0);

  result = IntersectMcs(lhs, rhs);
  EXPECT_EQ(0xfful, result.rx_mcs_bitmask1());
  EXPECT_EQ(0x0f0ful, result.rx_mcs_bitmask2());
  EXPECT_EQ(543, result.rx_highest_rate());
  EXPECT_EQ(1, result.tx_set_defined());
  EXPECT_EQ(0, result.tx_rx_diff());
  EXPECT_EQ(0, result.tx_max_ss());
  EXPECT_EQ(0, result.tx_unequal_mod());

  // Tx values only set if tx_rx_diff is true.
  rhs.set_tx_rx_diff(1);
  result = IntersectMcs(lhs, rhs);
  EXPECT_EQ(1, result.tx_set_defined());
  EXPECT_EQ(1, result.tx_rx_diff());
  EXPECT_EQ(1, result.tx_max_ss());
  EXPECT_EQ(0, result.tx_unequal_mod());

  // Double check mcs intersection.
  lhs.set_rx_mcs_bitmask1(0xfff0fff);
  rhs.set_rx_mcs_bitmask1(0x001fff0);
  result = IntersectMcs(lhs, rhs);
  EXPECT_EQ(0x0010ff0ul, result.rx_mcs_bitmask1());
}
}  // namespace
}  // namespace wlan
