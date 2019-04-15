// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ht.h>

namespace wlan {

HtCapabilities BuildHtCapabilities(const HtConfig& config) {
  // TODO(porce): Find intersection of
  // - BSS capabilities
  // - Client radio capabilities
  // - Client configuration

  // Static cooking for Proof-of-Concept
  HtCapabilities htc;
  HtCapabilityInfo& hci = htc.ht_cap_info;

  hci.set_ldpc_coding_cap(0);  // Ralink RT5370 is incapable of LDPC.

  if (config.cbw_40_rx_ready) {
    hci.set_chan_width_set(HtCapabilityInfo::TWENTY_FORTY);
  } else {
    hci.set_chan_width_set(HtCapabilityInfo::TWENTY_ONLY);
  }

  hci.set_sm_power_save(HtCapabilityInfo::DISABLED);
  hci.set_greenfield(0);
  hci.set_short_gi_20(1);
  hci.set_short_gi_40(1);
  hci.set_tx_stbc(0);  // No plan to support STBC Tx
  hci.set_rx_stbc(1);  // one stream.
  hci.set_delayed_block_ack(0);
  hci.set_max_amsdu_len(HtCapabilityInfo::OCTETS_7935);  // Aruba
  // hci.set_max_amsdu_len(HtCapabilityInfo::OCTETS_3839);  // TP-Link
  hci.set_dsss_in_40(0);
  hci.set_intolerant_40(0);
  hci.set_lsig_txop_protect(0);

  AmpduParams& ampdu = htc.ampdu_params;
  ampdu.set_exponent(3);                                // 65535 bytes
  ampdu.set_min_start_spacing(AmpduParams::FOUR_USEC);  // Aruba
  // ampdu.set_min_start_spacing(AmpduParams::EIGHT_USEC);  // TP-Link
  // ampdu.set_min_start_spacing(AmpduParams::SIXTEEN_USEC);

  SupportedMcsSet& mcs = htc.mcs_set;
  mcs.rx_mcs_head.set_bitmask(0xff);  // MCS 0-7
  // mcs.rx_mcs_head.set_bitmask(0xffff);  // MCS 0-15

  HtExtCapabilities& hec = htc.ht_ext_cap;
  hec.set_pco(0);
  hec.set_pco_transition(HtExtCapabilities::PCO_RESERVED);
  hec.set_mcs_feedback(HtExtCapabilities::MCS_NOFEEDBACK);
  hec.set_htc_ht_support(0);
  hec.set_rd_responder(0);

  TxBfCapability& txbf = htc.txbf_cap;
  txbf.set_implicit_rx(0);
  txbf.set_rx_stag_sounding(0);
  txbf.set_tx_stag_sounding(0);
  txbf.set_rx_ndp(0);
  txbf.set_tx_ndp(0);
  txbf.set_implicit(0);
  txbf.set_calibration(TxBfCapability::CALIBRATION_NONE);
  txbf.set_csi(0);
  txbf.set_noncomp_steering(0);
  txbf.set_comp_steering(0);
  txbf.set_csi_feedback(TxBfCapability::FEEDBACK_NONE);
  txbf.set_noncomp_feedback(TxBfCapability::FEEDBACK_NONE);
  txbf.set_comp_feedback(TxBfCapability::FEEDBACK_NONE);
  txbf.set_min_grouping(TxBfCapability::MIN_GROUP_ONE);
  txbf.set_csi_antennas_human(1);           // 1 antenna
  txbf.set_noncomp_steering_ants_human(1);  // 1 antenna
  txbf.set_comp_steering_ants_human(1);     // 1 antenna
  txbf.set_csi_rows_human(1);               // 1 antenna
  txbf.set_chan_estimation_human(1);        // # space-time stream

  AselCapability& asel = htc.asel_cap;
  asel.set_asel(0);
  asel.set_csi_feedback_tx_asel(0);
  asel.set_explicit_csi_feedback(0);
  asel.set_antenna_idx_feedback(0);
  asel.set_rx_asel(0);
  asel.set_tx_sounding_ppdu(0);

  return htc;  // 28 bytes.
}

HtOperation BuildHtOperation(const wlan_channel_t& chan) {
  // TODO(porce): Query the BSS internal data to fill up the parameters.
  HtOperation hto;

  hto.primary_chan = chan.primary;
  HtOpInfoHead& head = hto.head;

  switch (chan.cbw) {
    case CBW40ABOVE:
      head.set_secondary_chan_offset(HtOpInfoHead::SECONDARY_ABOVE);
      head.set_sta_chan_width(HtOpInfoHead::ANY);
      break;
    case CBW40BELOW:
      head.set_secondary_chan_offset(HtOpInfoHead::SECONDARY_BELOW);
      head.set_sta_chan_width(HtOpInfoHead::ANY);
      break;
    case CBW20:
    default:
      head.set_secondary_chan_offset(HtOpInfoHead::SECONDARY_NONE);
      head.set_sta_chan_width(HtOpInfoHead::TWENTY);
      break;
  }

  head.set_rifs_mode(0);
  head.set_reserved1(
      0);  // TODO(porce): Tweak this for 802.11n D1.10 compatibility
  head.set_ht_protect(HtOpInfoHead::NONE);
  head.set_nongreenfield_present(1);
  head.set_reserved2(
      0);  // TODO(porce): Tweak this for 802.11n D1.10 compatibility
  head.set_obss_non_ht(0);
  head.set_center_freq_seg2(0);
  head.set_dual_beacon(0);
  head.set_dual_cts_protect(0);

  HtOpInfoTail& tail = hto.tail;
  tail.set_stbc_beacon(0);
  tail.set_lsig_txop_protect(0);
  tail.set_pco_active(0);
  tail.set_pco_phase(0);

  SupportedMcsSet& mcs = hto.basic_mcs_set;
  mcs.rx_mcs_head.set_bitmask(0xff);  // MCS 0-7

  return hto;
}

}  // namespace wlan
