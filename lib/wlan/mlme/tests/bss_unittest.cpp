// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/element.h>
#include <wlan/mlme/client/bss.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace wlan {
namespace {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

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

TEST(BssTest, HtMcsBitmaskToFidl) {
    SupportedMcsRxMcsHead smrmh;
    wlan_mlme::HtMcs fidl;
    zx_status_t status;

    smrmh.set_bitmask(0);
    status = HtMcsBitmaskToFidl(smrmh, &fidl);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(fidl, wlan_mlme::HtMcs::MCS_INVALID);

    smrmh.set_bitmask(0b11111110);
    status = HtMcsBitmaskToFidl(smrmh, &fidl);
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
    EXPECT_EQ(fidl, wlan_mlme::HtMcs::MCS_INVALID);

    smrmh.set_bitmask(0x01111111);
    status = HtMcsBitmaskToFidl(smrmh, &fidl);
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
    EXPECT_EQ(fidl, wlan_mlme::HtMcs::MCS_INVALID);

    smrmh.set_bitmask(0xff);
    status = HtMcsBitmaskToFidl(smrmh, &fidl);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(fidl, wlan_mlme::HtMcs::MCS0_7);

    smrmh.set_bitmask(0xffff);
    status = HtMcsBitmaskToFidl(smrmh, &fidl);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(fidl, wlan_mlme::HtMcs::MCS0_15);

    smrmh.set_bitmask(0xffffff);
    status = HtMcsBitmaskToFidl(smrmh, &fidl);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(fidl, wlan_mlme::HtMcs::MCS0_23);

    smrmh.set_bitmask(0xffffffff);
    status = HtMcsBitmaskToFidl(smrmh, &fidl);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(fidl, wlan_mlme::HtMcs::MCS0_31);
}

TEST(BssTest, HtCapabilitiesBitFieldOrHuman) {
    HtCapabilities hc;

    hc.mcs_set.tx_mcs.set_max_ss_human(3);
    hc.txbf_cap.set_csi_antennas_human(4);
    hc.txbf_cap.set_noncomp_steering_ants_human(2);
    hc.txbf_cap.set_comp_steering_ants_human(3);
    hc.txbf_cap.set_csi_rows_human(2);
    hc.txbf_cap.set_chan_estimation_human(4);

    EXPECT_EQ(hc.mcs_set.tx_mcs.max_ss(), 2);
    EXPECT_EQ(hc.txbf_cap.csi_antennas(), 3);
    EXPECT_EQ(hc.txbf_cap.noncomp_steering_ants(), 1);
    EXPECT_EQ(hc.txbf_cap.comp_steering_ants(), 2);
    EXPECT_EQ(hc.txbf_cap.csi_rows(), 1);
    EXPECT_EQ(hc.txbf_cap.chan_estimation(), 3);

    hc.mcs_set.tx_mcs.set_max_ss(3);
    hc.txbf_cap.set_csi_antennas(2);
    hc.txbf_cap.set_noncomp_steering_ants(1);
    hc.txbf_cap.set_comp_steering_ants(1);
    hc.txbf_cap.set_csi_rows(2);
    hc.txbf_cap.set_chan_estimation(3);

    EXPECT_EQ(hc.mcs_set.tx_mcs.max_ss_human(), 4);
    EXPECT_EQ(hc.txbf_cap.csi_antennas_human(), 3);
    EXPECT_EQ(hc.txbf_cap.noncomp_steering_ants_human(), 2);
    EXPECT_EQ(hc.txbf_cap.comp_steering_ants_human(), 2);
    EXPECT_EQ(hc.txbf_cap.csi_rows_human(), 3);
    EXPECT_EQ(hc.txbf_cap.chan_estimation_human(), 4);
}

TEST(BssTest, HtCapabilitiesToFidl_Human) {
    HtCapabilities hc;

    hc.mcs_set.tx_mcs.set_max_ss_human(3);
    hc.txbf_cap.set_csi_antennas_human(4);
    hc.txbf_cap.set_noncomp_steering_ants_human(2);
    hc.txbf_cap.set_comp_steering_ants_human(1);
    hc.txbf_cap.set_csi_rows_human(2);
    hc.txbf_cap.set_chan_estimation_human(3);

    auto fidl = HtCapabilitiesToFidl(hc);

    EXPECT_NE(fidl, nullptr);
    EXPECT_EQ(fidl->mcs_set.tx_max_ss, 3);
    EXPECT_EQ(fidl->txbf_cap.csi_antennas, 4);
    EXPECT_EQ(fidl->txbf_cap.noncomp_steering_ants, 2);
    EXPECT_EQ(fidl->txbf_cap.comp_steering_ants, 1);
    EXPECT_EQ(fidl->txbf_cap.csi_rows, 2);
    EXPECT_EQ(fidl->txbf_cap.chan_estimation, 3);
}
}  // namespace
}  // namespace wlan
