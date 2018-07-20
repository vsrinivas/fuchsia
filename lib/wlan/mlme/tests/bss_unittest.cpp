// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/element.h>
#include <wlan/mlme/client/bss.h>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace wlan {
namespace {

TEST(BssTest, VhtMcsNssBitFieldToFidl) {
    VhtMcsNss vmn;
    vmn.set_rx_max_mcs_ss1(1);
    vmn.set_rx_max_mcs_ss2(2);
    vmn.set_rx_max_mcs_ss3(3);
    vmn.set_rx_max_mcs_ss4(1);
    vmn.set_rx_max_mcs_ss5(2);
    vmn.set_rx_max_mcs_ss6(3);
    vmn.set_rx_max_mcs_ss7(1);
    vmn.set_rx_max_mcs_ss8(2);
    vmn.set_rx_max_data_rate(1234);
    vmn.set_max_nsts(7);

    vmn.set_tx_max_mcs_ss1(3);
    vmn.set_tx_max_mcs_ss2(2);
    vmn.set_tx_max_mcs_ss3(1);
    vmn.set_tx_max_mcs_ss4(3);
    vmn.set_tx_max_mcs_ss5(2);
    vmn.set_tx_max_mcs_ss6(1);
    vmn.set_tx_max_mcs_ss7(3);
    vmn.set_tx_max_mcs_ss8(2);
    vmn.set_tx_max_data_rate(8191);
    vmn.set_ext_nss_bw(1);

    auto fidl = VhtMcsNssToFidl(vmn);

    EXPECT_EQ(static_cast<uint8_t>(fidl.rx_max_mcs[0]), 1);
    EXPECT_EQ(static_cast<uint8_t>(fidl.rx_max_mcs[1]), 2);
    EXPECT_EQ(static_cast<uint8_t>(fidl.rx_max_mcs[2]), 3);
    EXPECT_EQ(static_cast<uint8_t>(fidl.rx_max_mcs[3]), 1);
    EXPECT_EQ(static_cast<uint8_t>(fidl.rx_max_mcs[4]), 2);
    EXPECT_EQ(static_cast<uint8_t>(fidl.rx_max_mcs[5]), 3);
    EXPECT_EQ(static_cast<uint8_t>(fidl.rx_max_mcs[6]), 1);
    EXPECT_EQ(static_cast<uint8_t>(fidl.rx_max_mcs[7]), 2);
    EXPECT_EQ(fidl.rx_max_data_rate, 1234);
    EXPECT_EQ(fidl.max_nsts, 7);

    EXPECT_EQ(static_cast<uint8_t>(fidl.tx_max_mcs[0]), 3);
    EXPECT_EQ(static_cast<uint8_t>(fidl.tx_max_mcs[1]), 2);
    EXPECT_EQ(static_cast<uint8_t>(fidl.tx_max_mcs[2]), 1);
    EXPECT_EQ(static_cast<uint8_t>(fidl.tx_max_mcs[3]), 3);
    EXPECT_EQ(static_cast<uint8_t>(fidl.tx_max_mcs[4]), 2);
    EXPECT_EQ(static_cast<uint8_t>(fidl.tx_max_mcs[5]), 1);
    EXPECT_EQ(static_cast<uint8_t>(fidl.tx_max_mcs[6]), 3);
    EXPECT_EQ(static_cast<uint8_t>(fidl.tx_max_mcs[7]), 2);
    EXPECT_EQ(fidl.tx_max_data_rate, 8191);
    EXPECT_EQ(fidl.ext_nss_bw, 1);
}

}  // namespace
}  // namespace wlan
