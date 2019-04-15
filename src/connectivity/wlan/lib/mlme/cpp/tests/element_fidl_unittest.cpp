// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <gtest/gtest.h>
#include <wlan/common/element.h>
#include <wlan/mlme/client/bss.h>

#include <memory>

namespace wlan {
namespace {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

TEST(FidlToElement, VhtMcsNssFidlToBitField) {
  wlan_mlme::VhtMcsNss fidl;

  fidl.rx_max_mcs[0] = 1;
  fidl.rx_max_mcs[1] = 2;
  fidl.rx_max_mcs[2] = 3;
  fidl.rx_max_mcs[3] = 1;
  fidl.rx_max_mcs[4] = 2;
  fidl.rx_max_mcs[5] = 3;
  fidl.rx_max_mcs[6] = 1;
  fidl.rx_max_mcs[7] = 2;

  fidl.tx_max_mcs[0] = 3;
  fidl.tx_max_mcs[1] = 2;
  fidl.tx_max_mcs[2] = 1;
  fidl.tx_max_mcs[3] = 3;
  fidl.tx_max_mcs[4] = 2;
  fidl.tx_max_mcs[5] = 1;
  fidl.tx_max_mcs[6] = 3;
  fidl.tx_max_mcs[7] = 2;

  fidl.rx_max_data_rate = 1234;
  fidl.max_nsts = 7;

  fidl.tx_max_data_rate = 8191;
  fidl.ext_nss_bw = 1;

  VhtMcsNss out = VhtMcsNss::FromFidl(fidl);

  EXPECT_EQ(out.val(), 0x3FFFB6DBE4D29E79ULL);

  EXPECT_EQ(out.rx_max_mcs_ss1(), 1);
  EXPECT_EQ(out.rx_max_mcs_ss2(), 2);
  EXPECT_EQ(out.rx_max_mcs_ss3(), 3);
  EXPECT_EQ(out.rx_max_mcs_ss4(), 1);
  EXPECT_EQ(out.rx_max_mcs_ss5(), 2);
  EXPECT_EQ(out.rx_max_mcs_ss6(), 3);
  EXPECT_EQ(out.rx_max_mcs_ss7(), 1);
  EXPECT_EQ(out.rx_max_mcs_ss8(), 2);

  EXPECT_EQ(out.tx_max_mcs_ss1(), 3);
  EXPECT_EQ(out.tx_max_mcs_ss2(), 2);
  EXPECT_EQ(out.tx_max_mcs_ss3(), 1);
  EXPECT_EQ(out.tx_max_mcs_ss4(), 3);
  EXPECT_EQ(out.tx_max_mcs_ss5(), 2);
  EXPECT_EQ(out.tx_max_mcs_ss6(), 1);
  EXPECT_EQ(out.tx_max_mcs_ss7(), 3);
  EXPECT_EQ(out.tx_max_mcs_ss8(), 2);

  EXPECT_EQ(out.rx_max_data_rate(), 1234);
  EXPECT_EQ(out.max_nsts(), 7);

  EXPECT_EQ(out.tx_max_data_rate(), 8191);
  EXPECT_EQ(out.ext_nss_bw(), 1);
}

TEST(ElementToFidl, VhtMcsNssBitFieldToFidl) {
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

  auto fidl = vmn.ToFidl();

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

TEST(ElementHumanAccessor, HtCapabilitiesBitFieldOrHuman) {
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

TEST(FidlToElement, HtCapabilitiesFidlToBitField) {
  wlan_mlme::HtCapabilities fidl;

  fidl.mcs_set.rx_mcs_set = 0xfedcba9876543210;
  fidl.mcs_set.tx_max_ss = 3;
  fidl.txbf_cap.csi_antennas = 4;
  fidl.txbf_cap.noncomp_steering_ants = 2;
  fidl.txbf_cap.comp_steering_ants = 1;
  fidl.txbf_cap.csi_rows = 2;
  fidl.txbf_cap.chan_estimation = 3;

  HtCapabilities out = HtCapabilities::FromFidl(fidl);

  EXPECT_EQ(out.mcs_set.rx_mcs_head.bitmask(), 0xfedcba9876543210ULL);
  EXPECT_EQ(out.mcs_set.tx_mcs.max_ss_human(), 3);
  EXPECT_EQ(out.mcs_set.tx_mcs.max_ss(), 2);
  EXPECT_EQ(out.txbf_cap.csi_antennas_human(), 4);
  EXPECT_EQ(out.txbf_cap.csi_antennas(), 3);
  EXPECT_EQ(out.txbf_cap.noncomp_steering_ants_human(), 2);
  EXPECT_EQ(out.txbf_cap.noncomp_steering_ants(), 1);
  EXPECT_EQ(out.txbf_cap.comp_steering_ants_human(), 1);
  EXPECT_EQ(out.txbf_cap.comp_steering_ants(), 0);
  EXPECT_EQ(out.txbf_cap.chan_estimation_human(), 3);
  EXPECT_EQ(out.txbf_cap.chan_estimation(), 2);
}

TEST(ElementToFidl, HtCapabilitiesToFidlHuman) {
  HtCapabilities hc;

  hc.mcs_set.rx_mcs_head.set_bitmask(0xfedcba9876543210);
  hc.mcs_set.tx_mcs.set_max_ss_human(3);
  hc.txbf_cap.set_csi_antennas_human(4);
  hc.txbf_cap.set_noncomp_steering_ants_human(2);
  hc.txbf_cap.set_comp_steering_ants_human(1);
  hc.txbf_cap.set_csi_rows_human(2);
  hc.txbf_cap.set_chan_estimation_human(3);

  auto fidl = hc.ToFidl();
  EXPECT_EQ(fidl.mcs_set.rx_mcs_set, static_cast<uint64_t>(0xfedcba9876543210));
  EXPECT_EQ(fidl.mcs_set.tx_max_ss, 3);
  EXPECT_EQ(fidl.txbf_cap.csi_antennas, 4);
  EXPECT_EQ(fidl.txbf_cap.noncomp_steering_ants, 2);
  EXPECT_EQ(fidl.txbf_cap.comp_steering_ants, 1);
  EXPECT_EQ(fidl.txbf_cap.csi_rows, 2);
  EXPECT_EQ(fidl.txbf_cap.chan_estimation, 3);
}

TEST(FidlToElement, HtOperationFidlToBitField) {
  wlan_mlme::HtOperation fidl;

  fidl.primary_chan = 169;

  auto* ht_op_info = &fidl.ht_op_info;
  ht_op_info->secondary_chan_offset =
      to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_ABOVE);
  ht_op_info->sta_chan_width = to_enum_type(wlan_mlme::StaChanWidth::ANY);
  ht_op_info->rifs_mode = true;
  ht_op_info->ht_protect = to_enum_type(wlan_mlme::HtProtect::TWENTY_MHZ);
  ht_op_info->nongreenfield_present = true;
  ht_op_info->obss_non_ht = true;
  ht_op_info->center_freq_seg2 = 155;
  ht_op_info->dual_beacon = true;
  ht_op_info->dual_cts_protect = true;

  ht_op_info->stbc_beacon = true;
  ht_op_info->lsig_txop_protect = true;
  ht_op_info->pco_active = true;
  ht_op_info->pco_phase = true;

  fidl.basic_mcs_set.rx_mcs_set = 0x89abcdef01234567ULL;

  HtOperation elem = HtOperation::FromFidl(fidl);

  EXPECT_EQ(elem.primary_chan, 169);

  EXPECT_EQ(elem.head.secondary_chan_offset(), 1);
  EXPECT_EQ(elem.head.sta_chan_width(), 1);
  EXPECT_EQ(elem.head.rifs_mode(), 1);
  EXPECT_EQ(elem.head.ht_protect(), 2);
  EXPECT_EQ(elem.head.nongreenfield_present(), 1);
  EXPECT_EQ(elem.head.obss_non_ht(), 1);
  EXPECT_EQ(elem.head.center_freq_seg2(), 155);
  EXPECT_EQ(elem.head.dual_beacon(), 1);
  EXPECT_EQ(elem.head.dual_cts_protect(), 1);

  EXPECT_EQ(elem.tail.stbc_beacon(), 1);
  EXPECT_EQ(elem.tail.lsig_txop_protect(), 1);
  EXPECT_EQ(elem.tail.pco_active(), 1);
  EXPECT_EQ(elem.tail.pco_phase(), 1);

  EXPECT_EQ(elem.basic_mcs_set.rx_mcs_head.bitmask(), 0x89abcdef01234567ULL);
}

TEST(ElementToFidl, HtOperationToFidl) {
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

  auto fidl = hto.ToFidl();
  EXPECT_EQ(fidl.primary_chan, 169);

  const auto& htoi = fidl.ht_op_info;
  EXPECT_EQ(htoi.secondary_chan_offset,
            to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_ABOVE));
  EXPECT_EQ(htoi.sta_chan_width, to_enum_type(wlan_mlme::StaChanWidth::ANY));
  EXPECT_TRUE(htoi.rifs_mode);
  EXPECT_EQ(htoi.ht_protect, to_enum_type(wlan_mlme::HtProtect::TWENTY_MHZ));
  EXPECT_TRUE(htoi.nongreenfield_present);
  EXPECT_TRUE(htoi.obss_non_ht);
  EXPECT_EQ(htoi.center_freq_seg2, 155);
  EXPECT_TRUE(htoi.dual_beacon);
  EXPECT_TRUE(htoi.dual_cts_protect);

  EXPECT_TRUE(htoi.stbc_beacon);
  EXPECT_TRUE(htoi.lsig_txop_protect);
  EXPECT_TRUE(htoi.pco_active);
  EXPECT_TRUE(htoi.pco_phase);

  EXPECT_EQ(fidl.basic_mcs_set.rx_mcs_set,
            static_cast<uint64_t>(0x89abcdef01234567));
}

TEST(FidlToElement, VhtOperationFidlToBitField) {
  wlan_mlme::VhtOperation fidl;

  fidl.vht_cbw = to_enum_type(wlan_mlme::VhtCbw::CBW_160);
  fidl.center_freq_seg0 = 155;
  fidl.center_freq_seg1 = 169;
  fidl.basic_mcs.max_mcs[0] = to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_8);
  fidl.basic_mcs.max_mcs[1] = to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_9);
  fidl.basic_mcs.max_mcs[2] = to_enum_type(wlan_mlme::VhtMcs::SET_NONE);
  fidl.basic_mcs.max_mcs[3] = to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_9);
  fidl.basic_mcs.max_mcs[4] = to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_8);
  fidl.basic_mcs.max_mcs[5] = to_enum_type(wlan_mlme::VhtMcs::SET_NONE);
  fidl.basic_mcs.max_mcs[6] = to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_8);
  fidl.basic_mcs.max_mcs[7] = to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_9);

  VhtOperation elem = VhtOperation::FromFidl(fidl);

  EXPECT_EQ(elem.vht_cbw, 2);
  EXPECT_EQ(elem.center_freq_seg0, 155);
  EXPECT_EQ(elem.center_freq_seg1, 169);

  EXPECT_EQ(elem.basic_mcs.val(), 0x9db9);
  EXPECT_EQ(elem.basic_mcs.ss1(), 1);
  EXPECT_EQ(elem.basic_mcs.ss2(), 2);
  EXPECT_EQ(elem.basic_mcs.ss3(), 3);
  EXPECT_EQ(elem.basic_mcs.ss4(), 2);
  EXPECT_EQ(elem.basic_mcs.ss5(), 1);
  EXPECT_EQ(elem.basic_mcs.ss6(), 3);
  EXPECT_EQ(elem.basic_mcs.ss7(), 1);
  EXPECT_EQ(elem.basic_mcs.ss8(), 2);
}

TEST(ElementToFidl, VhtOperationBitFieldToField) {
  VhtOperation elem;

  elem.vht_cbw = 2;
  elem.center_freq_seg0 = 155;
  elem.center_freq_seg1 = 169;

  elem.basic_mcs.set_ss1(1);
  elem.basic_mcs.set_ss2(2);
  elem.basic_mcs.set_ss3(3);
  elem.basic_mcs.set_ss4(2);
  elem.basic_mcs.set_ss5(1);
  elem.basic_mcs.set_ss6(3);
  elem.basic_mcs.set_ss7(1);
  elem.basic_mcs.set_ss8(2);

  wlan_mlme::VhtOperation fidl = elem.ToFidl();

  EXPECT_EQ(fidl.vht_cbw, to_enum_type(wlan_mlme::VhtCbw::CBW_160));
  EXPECT_EQ(fidl.center_freq_seg0, 155);
  EXPECT_EQ(fidl.center_freq_seg1, 169);

  EXPECT_EQ(fidl.basic_mcs.max_mcs[0],
            to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_8));
  EXPECT_EQ(fidl.basic_mcs.max_mcs[1],
            to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_9));
  EXPECT_EQ(fidl.basic_mcs.max_mcs[2],
            to_enum_type(wlan_mlme::VhtMcs::SET_NONE));
  EXPECT_EQ(fidl.basic_mcs.max_mcs[3],
            to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_9));
  EXPECT_EQ(fidl.basic_mcs.max_mcs[4],
            to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_8));
  EXPECT_EQ(fidl.basic_mcs.max_mcs[5],
            to_enum_type(wlan_mlme::VhtMcs::SET_NONE));
  EXPECT_EQ(fidl.basic_mcs.max_mcs[6],
            to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_8));
  EXPECT_EQ(fidl.basic_mcs.max_mcs[7],
            to_enum_type(wlan_mlme::VhtMcs::SET_0_TO_9));
}

}  // namespace
}  // namespace wlan
