// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/element.h>
#include <wlan/mlme/client/bss.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <gtest/gtest.h>

#include <memory>

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

TEST(BssTest, HtCapabilitiesToFidlHuman) {
    HtCapabilities hc;

    hc.mcs_set.rx_mcs_head.set_bitmask(0xfedcba9876543210);
    hc.mcs_set.tx_mcs.set_max_ss_human(3);
    hc.txbf_cap.set_csi_antennas_human(4);
    hc.txbf_cap.set_noncomp_steering_ants_human(2);
    hc.txbf_cap.set_comp_steering_ants_human(1);
    hc.txbf_cap.set_csi_rows_human(2);
    hc.txbf_cap.set_chan_estimation_human(3);

    auto fidl = HtCapabilitiesToFidl(hc);
    ASSERT_NE(fidl, nullptr);

    EXPECT_EQ(fidl->mcs_set.rx_mcs_set, static_cast<uint64_t>(0xfedcba9876543210));
    EXPECT_EQ(fidl->mcs_set.tx_max_ss, 3);
    EXPECT_EQ(fidl->txbf_cap.csi_antennas, 4);
    EXPECT_EQ(fidl->txbf_cap.noncomp_steering_ants, 2);
    EXPECT_EQ(fidl->txbf_cap.comp_steering_ants, 1);
    EXPECT_EQ(fidl->txbf_cap.csi_rows, 2);
    EXPECT_EQ(fidl->txbf_cap.chan_estimation, 3);
}

TEST(BssTest, HtOperationToFidl) {
    HtOperation hto;

    hto.primary_chan = 169;

    hto.head.set_secondary_chan_offset(1);
    hto.head.set_sta_chan_width(1);
    hto.head.set_rifs_mode(1);
    hto.head.set_ht_protect(2);
    hto.head.set_nongreenfield_present(1);
    hto.head.set_obss_non_ht(1);
    hto.head.set_center_freq_seg2(155);
    hto.head.set_dual_beacon(1);
    hto.head.set_dual_cts_protect(1);

    hto.tail.set_stbc_beacon(1);
    hto.tail.set_lsig_txop_protect(1);
    hto.tail.set_pco_active(1);
    hto.tail.set_pco_phase(1);

    hto.basic_mcs_set.rx_mcs_head.set_bitmask(0x89abcdef01234567);

    auto fidl = HtOperationToFidl(hto);
    ASSERT_NE(fidl, nullptr);

    EXPECT_EQ(fidl->primary_chan, 169);

    const auto& htoi = fidl->ht_op_info;
    EXPECT_EQ(htoi.secondary_chan_offset, wlan_mlme::SecChanOffset::SECONDARY_ABOVE);
    EXPECT_EQ(htoi.sta_chan_width, wlan_mlme::StaChanWidth::ANY);
    EXPECT_TRUE(htoi.rifs_mode);
    EXPECT_EQ(htoi.ht_protect, wlan_mlme::HtProtect::TWENTY_MHZ);
    EXPECT_TRUE(htoi.nongreenfield_present);
    EXPECT_TRUE(htoi.obss_non_ht);
    EXPECT_EQ(htoi.center_freq_seg2, 155);
    EXPECT_TRUE(htoi.dual_beacon);
    EXPECT_TRUE(htoi.dual_cts_protect);

    EXPECT_TRUE(htoi.stbc_beacon);
    EXPECT_TRUE(htoi.lsig_txop_protect);
    EXPECT_TRUE(htoi.pco_active);
    EXPECT_TRUE(htoi.pco_phase);

    EXPECT_EQ(fidl->basic_mcs_set.rx_mcs_set, static_cast<uint64_t>(0x89abcdef01234567));
}
}  // namespace
}  // namespace wlan
