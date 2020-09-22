// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include <wlan/common/element.h>

namespace wlan {

// The macros below assumes that the two data structures being intersected be
// named lhs and rhs. Both of them must be the same sub-class of
// common::BitField<>.
#define SET_BITFIELD_MIN(element, field) \
  element.set_##field(std::min(lhs.element.field(), rhs.element.field()))
#define SET_BITFIELD_MAX(element, field) \
  element.set_##field(std::max(lhs.element.field(), rhs.element.field()))
#define SET_BITFIELD_AND(element, field) \
  element.set_##field(lhs.element.field() & rhs.element.field())

SupportedMcsSet IntersectMcs(const SupportedMcsSet& lhs, const SupportedMcsSet& rhs) {
  // Find an intersection.
  // Perform bitwise-AND on bitmask fields, which represent MCS
  // Take minimum of numeric values

  auto result = SupportedMcsSet{};
  auto& rx_mcs_head = result.rx_mcs_head;
  SET_BITFIELD_AND(rx_mcs_head, bitmask);

  auto& rx_mcs_tail = result.rx_mcs_tail;
  SET_BITFIELD_AND(rx_mcs_tail, bitmask);
  SET_BITFIELD_MIN(rx_mcs_tail, highest_rate);

  auto& tx_mcs = result.tx_mcs;
  SET_BITFIELD_AND(tx_mcs, set_defined);
  SET_BITFIELD_AND(tx_mcs, rx_diff);
  SET_BITFIELD_MIN(tx_mcs, max_ss);
  SET_BITFIELD_AND(tx_mcs, ueqm);

  return result;
}

// Takes two HtCapabilities/VhtCapabilities, typically, one from the device and
// the other from the air, and find the capabilities supported by both of them.
HtCapabilities IntersectHtCap(const HtCapabilities& lhs, const HtCapabilities& rhs) {
  auto htc = HtCapabilities{};

  auto& ht_cap_info = htc.ht_cap_info;
  SET_BITFIELD_AND(ht_cap_info, ldpc_coding_cap);
  SET_BITFIELD_AND(ht_cap_info, chan_width_set);

  // TODO(fxbug.dev/29404): Revisit SM power save mode when necessary. IEEE
  // 802.11-2016 11.2.6
  if (lhs.ht_cap_info.sm_power_save() == HtCapabilityInfo::SmPowerSave::DISABLED ||
      rhs.ht_cap_info.sm_power_save() == HtCapabilityInfo::SmPowerSave::DISABLED) {
    ht_cap_info.set_sm_power_save(HtCapabilityInfo::SmPowerSave::DISABLED);
  } else {
    // Assuming a device supporting dynamic power save will support static power
    // save
    SET_BITFIELD_MIN(ht_cap_info, sm_power_save);
  }

  SET_BITFIELD_AND(ht_cap_info, greenfield);
  SET_BITFIELD_AND(ht_cap_info, short_gi_20);
  SET_BITFIELD_AND(ht_cap_info, short_gi_40);
  SET_BITFIELD_AND(ht_cap_info, tx_stbc);

  SET_BITFIELD_MIN(ht_cap_info, rx_stbc);

  SET_BITFIELD_AND(ht_cap_info, delayed_block_ack);
  SET_BITFIELD_AND(ht_cap_info, max_amsdu_len);
  SET_BITFIELD_AND(ht_cap_info, dsss_in_40);
  SET_BITFIELD_AND(ht_cap_info, intolerant_40);
  SET_BITFIELD_AND(ht_cap_info, lsig_txop_protect);

  auto& ampdu_params = htc.ampdu_params;
  SET_BITFIELD_MIN(ampdu_params, exponent);

  SET_BITFIELD_MAX(ampdu_params, min_start_spacing);

  htc.mcs_set = IntersectMcs(lhs.mcs_set, rhs.mcs_set);

  auto& ht_ext_cap = htc.ht_ext_cap;
  SET_BITFIELD_AND(ht_ext_cap, pco);

  if (lhs.ht_ext_cap.pco_transition() == HtExtCapabilities::PcoTransitionTime::PCO_RESERVED ||
      rhs.ht_ext_cap.pco_transition() == HtExtCapabilities::PcoTransitionTime::PCO_RESERVED) {
    ht_ext_cap.set_pco_transition(HtExtCapabilities::PcoTransitionTime::PCO_RESERVED);
  } else {
    SET_BITFIELD_MAX(ht_ext_cap, pco_transition);
  }
  SET_BITFIELD_MIN(ht_ext_cap, mcs_feedback);

  SET_BITFIELD_AND(ht_ext_cap, htc_ht_support);
  SET_BITFIELD_AND(ht_ext_cap, rd_responder);

  auto& txbf_cap = htc.txbf_cap;
  SET_BITFIELD_AND(txbf_cap, implicit_rx);
  SET_BITFIELD_AND(txbf_cap, rx_stag_sounding);
  SET_BITFIELD_AND(txbf_cap, tx_stag_sounding);
  SET_BITFIELD_AND(txbf_cap, rx_ndp);
  SET_BITFIELD_AND(txbf_cap, tx_ndp);
  SET_BITFIELD_AND(txbf_cap, implicit);

  SET_BITFIELD_MIN(txbf_cap, calibration);

  SET_BITFIELD_AND(txbf_cap, csi);

  SET_BITFIELD_AND(txbf_cap, noncomp_steering);
  SET_BITFIELD_AND(txbf_cap, comp_steering);

  // IEEE 802.11-2016 Table 9-166
  // xxx_feedback behaves like bitmask for delayed and immediate feedback
  SET_BITFIELD_AND(txbf_cap, csi_feedback);
  SET_BITFIELD_AND(txbf_cap, noncomp_feedback);
  SET_BITFIELD_AND(txbf_cap, comp_feedback);

  SET_BITFIELD_MIN(txbf_cap, min_grouping);
  SET_BITFIELD_MIN(txbf_cap, csi_antennas);

  SET_BITFIELD_MIN(txbf_cap, noncomp_steering_ants);
  SET_BITFIELD_MIN(txbf_cap, comp_steering_ants);
  SET_BITFIELD_MIN(txbf_cap, csi_rows);
  SET_BITFIELD_MIN(txbf_cap, chan_estimation);

  auto& asel_cap = htc.asel_cap;
  SET_BITFIELD_AND(asel_cap, asel);
  SET_BITFIELD_AND(asel_cap, csi_feedback_tx_asel);
  SET_BITFIELD_AND(asel_cap, ant_idx_feedback_tx_asel);
  SET_BITFIELD_AND(asel_cap, explicit_csi_feedback);
  SET_BITFIELD_AND(asel_cap, antenna_idx_feedback);
  SET_BITFIELD_AND(asel_cap, rx_asel);
  SET_BITFIELD_AND(asel_cap, tx_sounding_ppdu);

  return htc;
}

VhtCapabilities IntersectVhtCap(const VhtCapabilities& lhs, const VhtCapabilities& rhs) {
  auto vhtc = VhtCapabilities{};

  auto& vht_cap_info = vhtc.vht_cap_info;
  SET_BITFIELD_MIN(vht_cap_info, max_mpdu_len);
  // TODO(fxbug.dev/29404): IEEE 802.11-2016 Table 9-250. Revisit when necessary
  // supported_cbw_set needs to be considered in conjunction with ext_nss_bw
  // below
  SET_BITFIELD_MIN(vht_cap_info, supported_cbw_set);

  SET_BITFIELD_AND(vht_cap_info, rx_ldpc);
  SET_BITFIELD_AND(vht_cap_info, sgi_cbw80);
  SET_BITFIELD_AND(vht_cap_info, sgi_cbw160);
  SET_BITFIELD_AND(vht_cap_info, tx_stbc);

  SET_BITFIELD_MIN(vht_cap_info, rx_stbc);

  SET_BITFIELD_AND(vht_cap_info, su_bfer);
  SET_BITFIELD_AND(vht_cap_info, su_bfee);

  SET_BITFIELD_MIN(vht_cap_info, bfee_sts);
  SET_BITFIELD_MIN(vht_cap_info, num_sounding);

  SET_BITFIELD_AND(vht_cap_info, mu_bfer);
  SET_BITFIELD_AND(vht_cap_info, mu_bfee);
  SET_BITFIELD_AND(vht_cap_info, txop_ps);
  SET_BITFIELD_AND(vht_cap_info, htc_vht);

  SET_BITFIELD_MIN(vht_cap_info, max_ampdu_exp);
  SET_BITFIELD_MIN(vht_cap_info, link_adapt);

  SET_BITFIELD_AND(vht_cap_info, rx_ant_pattern);
  SET_BITFIELD_AND(vht_cap_info, tx_ant_pattern);

  SET_BITFIELD_MIN(vht_cap_info, ext_nss_bw);

  auto& vht_mcs_nss = vhtc.vht_mcs_nss;
  SET_BITFIELD_MIN(vht_mcs_nss, rx_max_mcs_ss1);
  SET_BITFIELD_MIN(vht_mcs_nss, rx_max_mcs_ss2);
  SET_BITFIELD_MIN(vht_mcs_nss, rx_max_mcs_ss3);
  SET_BITFIELD_MIN(vht_mcs_nss, rx_max_mcs_ss4);
  SET_BITFIELD_MIN(vht_mcs_nss, rx_max_mcs_ss5);
  SET_BITFIELD_MIN(vht_mcs_nss, rx_max_mcs_ss6);
  SET_BITFIELD_MIN(vht_mcs_nss, rx_max_mcs_ss7);
  SET_BITFIELD_MIN(vht_mcs_nss, rx_max_mcs_ss8);
  SET_BITFIELD_MIN(vht_mcs_nss, rx_max_data_rate);
  SET_BITFIELD_MIN(vht_mcs_nss, max_nsts);
  SET_BITFIELD_MIN(vht_mcs_nss, tx_max_mcs_ss1);
  SET_BITFIELD_MIN(vht_mcs_nss, tx_max_mcs_ss2);
  SET_BITFIELD_MIN(vht_mcs_nss, tx_max_mcs_ss3);
  SET_BITFIELD_MIN(vht_mcs_nss, tx_max_mcs_ss4);
  SET_BITFIELD_MIN(vht_mcs_nss, tx_max_mcs_ss5);
  SET_BITFIELD_MIN(vht_mcs_nss, tx_max_mcs_ss6);
  SET_BITFIELD_MIN(vht_mcs_nss, tx_max_mcs_ss7);
  SET_BITFIELD_MIN(vht_mcs_nss, tx_max_mcs_ss8);
  SET_BITFIELD_MIN(vht_mcs_nss, tx_max_data_rate);

  SET_BITFIELD_AND(vht_mcs_nss, ext_nss_bw);

  return vhtc;
}

#undef SET_BITFIELD_AND
#undef SET_BITFIELD_MIN
#undef SET_BITFIELD_MAX

std::vector<SupportedRate> IntersectRatesAp(const std::vector<SupportedRate>& ap_rates,
                                            const std::vector<SupportedRate>& client_rates) {
  std::set<SupportedRate> ap(ap_rates.cbegin(), ap_rates.cend());
  std::set<SupportedRate> client(client_rates.cbegin(), client_rates.cend());

  std::vector<SupportedRate> result;
  // C++11 Standard 25.4.5.3 - set_intersection ALWAYS takes elements from the
  // first input.
  std::set_intersection(ap.cbegin(), ap.cend(), client.cbegin(), client.cend(),
                        std::back_inserter(result));
  return result;
}
}  // namespace wlan
