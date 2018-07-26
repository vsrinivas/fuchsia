// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/bss.h>

#include <wlan/common/channel.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <string>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

// TODO(NET-500): This file needs some clean-up.

zx_status_t Bss::ProcessBeacon(const Beacon& beacon, size_t frame_len,
                               const wlan_rx_info_t* rx_info) {
    if (!IsBeaconValid(beacon)) { return ZX_ERR_INTERNAL; }

    Renew(beacon, rx_info);

    if (!HasBeaconChanged(beacon, frame_len)) {
        // If unchanged, it is sufficient to renew the BSS. Bail out.
        return ZX_OK;
    }

    if (bcn_len_ != 0) {
        // TODO(porce): Identify varying IE, and do IE-by-IE comparison.
        // BSS had been discovered, but the beacon changed.
        // Suspicious situation. Consider Deauth if in assoc.
        debugbcn("BSSID %s beacon change detected. (len %zu -> %zu)\n", bssid_.ToString().c_str(),
                 bcn_len_, frame_len);
    }

    auto status = Update(beacon, frame_len);
    if (status != ZX_OK) {
        debugbcn("BSSID %s failed to update its BSS object: (%d)\n", bssid_.ToString().c_str(),
                 status);

        return status;
    }

    return ZX_OK;
}

std::string Bss::ToString() const {
    // TODO(porce): Convert to finspect Describe()
    char buf[1024];
    snprintf(buf, sizeof(buf), "BSSID %s Infra %s  RSSI %3d  Country %3s Channel %4s SSID [%s]",
             bssid_.ToString().c_str(),
             bss_desc_.bss_type == wlan_mlme::BSSTypes::INFRASTRUCTURE ? "Y" : "N",
             bss_desc_.rssi_dbm,
             (bss_desc_.country != nullptr) ? bss_desc_.country->c_str() : "---",
             common::ChanStr(bcn_rx_chan_).c_str(), bss_desc_.ssid->c_str());
    return std::string(buf);
}

bool Bss::IsBeaconValid(const Beacon& beacon) const {
    // TODO(porce): Place holder. Add sanity logics.

    if (bss_desc_.timestamp > beacon.timestamp) {
        // Suspicious. Wrap around?
        // TODO(porce): deauth if the client was in association.
    }

    // TODO(porce): Size check.
    // Note: Some beacons in 5GHz may not include DSSS Parameter Set IE

    return true;
}

void Bss::Renew(const Beacon& beacon, const wlan_rx_info_t* rx_info) {
    bss_desc_.timestamp = beacon.timestamp;

    // TODO(porce): Take a deep look. Which resolution do we want to track?
    if (zx::clock::get(&ts_refreshed_) != ZX_OK) { ts_refreshed_ = zx::time_utc(); }

    // Radio statistics.
    if (rx_info == nullptr) return;

    bcn_rx_chan_ = rx_info->chan;

    // If the latest beacons lack measurements, keep the last report.
    // TODO(NET-856): Change DDK wlan_rx_info_t and do translation in the vendor device driver.
    // TODO(porce): Don't trust instantaneous values. Keep history.
    bss_desc_.rssi_dbm = (rx_info->valid_fields & WLAN_RX_INFO_VALID_RSSI) ? rx_info->rssi_dbm
                                                                           : WLAN_RSSI_DBM_INVALID;
    bss_desc_.rcpi_dbmh = (rx_info->valid_fields & WLAN_RX_INFO_VALID_RCPI)
                              ? rx_info->rcpi_dbmh
                              : WLAN_RCPI_DBMH_INVALID;
    bss_desc_.rsni_dbh =
        (rx_info->valid_fields & WLAN_RX_INFO_VALID_SNR) ? rx_info->snr_dbh : WLAN_RSNI_DBH_INVALID;
}

bool Bss::HasBeaconChanged(const Beacon& beacon, size_t frame_len) const {
    // Test changes in beacon, except for the timestamp field.
    if (frame_len != bcn_len_) { return true; }
    auto sig = GetBeaconSignature(beacon, frame_len);
    if (sig != bcn_hash_) { return true; }
    return false;
}

zx_status_t Bss::Update(const Beacon& beacon, size_t frame_len) {
    // To be used to detect a change in Beacon.
    bcn_len_ = frame_len;
    bcn_hash_ = GetBeaconSignature(beacon, frame_len);

    // Fields that are always present.
    bss_desc_.beacon_period = beacon.beacon_interval;  // name mismatch is spec-compliant.
    ParseCapabilityInfo(beacon.cap);
    bss_desc_.bss_type = GetBssType(beacon.cap);

    // IE's.
    auto ie_chains = beacon.elements;
    auto ie_chains_len = frame_len - sizeof(Beacon);  // Subtract the beacon header len.

    ZX_DEBUG_ASSERT(ie_chains != nullptr);
    ZX_DEBUG_ASSERT(ie_chains_len <= frame_len);
    return ParseIE(ie_chains, ie_chains_len);
}

void Bss::ParseCapabilityInfo(const CapabilityInfo& cap) {
    auto& c = bss_desc_.cap;
    c.ess = (cap.ess() == 1);
    c.ibss = (cap.ibss() == 1);
    c.cf_pollable = (cap.cf_pollable() == 1);
    c.cf_poll_req = (cap.cf_poll_req() == 1);
    c.privacy = (cap.privacy() == 1);
    c.short_preamble = (cap.short_preamble() == 1);
    c.spectrum_mgmt = (cap.spectrum_mgmt() == 1);
    c.qos = (cap.qos() == 1);
    c.short_slot_time = (cap.short_slot_time() == 1);
    c.apsd = (cap.apsd() == 1);
    c.radio_msmt = (cap.radio_msmt() == 1);
    c.delayed_block_ack = (cap.delayed_block_ack() == 1);
    c.immediate_block_ack = (cap.immediate_block_ack() == 1);
}

zx_status_t Bss::ParseIE(const uint8_t* ie_chains, size_t ie_chains_len) {
    ElementReader reader(ie_chains, ie_chains_len);

    debugbcn("Parsing IEs for BSSID %s\n", bssid_.ToString().c_str());
    uint8_t ie_cnt = 0;
    uint8_t ie_unparsed_cnt = 0;

    has_dsss_param_set_chan_ = false;

    char dbgmsghdr[128];
    while (reader.is_valid()) {
        ie_cnt++;

        const ElementHeader* hdr = reader.peek();
        if (hdr == nullptr) break;

        // TODO(porce): Process HT Capabilities IE's HT Capabilities Info to get CBW announcement.

        snprintf(dbgmsghdr, sizeof(dbgmsghdr), "  IE %3u (Len %3u): ", hdr->id, hdr->len);
        switch (hdr->id) {
        case element_id::kSsid: {
            auto ie = reader.read<SsidElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }
            bss_desc_.ssid = fidl::StringPtr(reinterpret_cast<const char*>(ie->ssid), ie->hdr.len);
            // TODO(NET-698): Not all SSIDs are ASCII-printable. Write a designated printer module.
            debugbcn("%s SSID: [%s]\n", dbgmsghdr, bss_desc_.ssid->c_str());
            break;
        }
        case element_id::kSuppRates: {
            auto ie = reader.read<SupportedRatesElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            if (!supported_rates_.empty()) { supported_rates_.clear(); }
            for (uint8_t idx = 0; idx < ie->hdr.len; idx++) {
                supported_rates_.push_back(ie->rates[idx]);
            }
            ZX_DEBUG_ASSERT(supported_rates_.size() <= SupportedRatesElement::kMaxLen);
            debugbcn("%s Supported rates: %s\n", dbgmsghdr, SupportedRatesToString().c_str());
            break;
        }
        case element_id::kDsssParamSet: {
            auto ie = reader.read<DsssParamSetElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            has_dsss_param_set_chan_ = true;
            dsss_param_set_chan_ = ie->current_chan;
            debugbcn("%s Current channel: %u\n", dbgmsghdr, ie->current_chan);
            break;
        }
        case element_id::kCountry: {
            // TODO(porce): Handle Subband Triplet Sequence field.
            auto ie = reader.read<CountryElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            bss_desc_.country = fidl::StringPtr(reinterpret_cast<const char*>(ie->country),
                                                CountryElement::kCountryLen);
            debugbcn("%s Country: %s\n", dbgmsghdr, bss_desc_.country->c_str());
            break;
        }
        case element_id::kRsn: {
            auto ie = reader.read<RsnElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            // TODO(porce): Consider pre-allocate max memory and recycle it.
            // if (rsne_) delete[] rsne_;
            size_t ie_len = sizeof(ElementHeader) + ie->hdr.len;
            rsne_.reset(new uint8_t[ie_len]);
            memcpy(rsne_.get(), ie, ie_len);
            rsne_len_ = ie_len;
            debugbcn("%s RSN\n", dbgmsghdr);
            break;
        }
        case element_id::kHtCapabilities: {
            auto ie = reader.read<HtCapabilities>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            bss_desc_.ht_cap = HtCapabilitiesToFidl(*ie);
            if (bss_desc_.ht_cap->mcs_set.rx_mcs_set == wlan_mlme::HtMcs::MCS_INVALID) {
                errorf("Empty MCS Set not allowed in HtCapabilities IE.\n");
            }
            ZX_DEBUG_ASSERT(bss_desc_.ht_cap->mcs_set.rx_mcs_set != wlan_mlme::HtMcs::MCS_INVALID);

            debugbcn("%s HtCapabilities parsed\n", dbgmsghdr);
            break;
        }
        case element_id::kHtOperation: {
            auto ie = reader.read<HtOperation>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            bss_desc_.ht_op = HtOperationToFidl(*ie);
            debugbcn("%s HtOperation parsed\n", dbgmsghdr);
            break;
        }
        case element_id::kVhtCapabilities: {
            auto ie = reader.read<VhtCapabilities>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            bss_desc_.vht_cap = VhtCapabilitiesToFidl(*ie);
            debugbcn("%s VhtCapabilities parsed\n", dbgmsghdr);
            break;
        }
        case element_id::kVhtOperation: {
            auto ie = reader.read<VhtOperation>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            bss_desc_.vht_op = VhtOperationToFidl(*ie);
            debugbcn("%s VhtOperation parsed\n", dbgmsghdr);
            break;
        }

        default:
            ie_unparsed_cnt++;
            debugbcn("%s Unparsed\n", dbgmsghdr);
            reader.skip(sizeof(ElementHeader) + hdr->len);
            break;
        }
    }

    debugbcn("  IE Summary: parsed %u / all %u\n", (ie_cnt - ie_unparsed_cnt), ie_cnt);
    return ZX_OK;
}

wlan_mlme::HtCapabilityInfo HtCapabilityInfoToFidl(const HtCapabilityInfo& hci) {
    wlan_mlme::HtCapabilityInfo fidl;

    fidl.ldpc_coding_cap = (hci.ldpc_coding_cap() == 1);
    fidl.chan_width_set = static_cast<wlan_mlme::ChanWidthSet>(hci.chan_width_set());
    fidl.sm_power_save = static_cast<wlan_mlme::SmPowerSave>(hci.sm_power_save());
    fidl.greenfield = (hci.greenfield() == 1);
    fidl.short_gi_20 = (hci.short_gi_20() == 1);
    fidl.short_gi_40 = (hci.short_gi_40() == 1);
    fidl.tx_stbc = (hci.tx_stbc() == 1);
    fidl.rx_stbc = hci.rx_stbc();
    fidl.delayed_block_ack = (hci.delayed_block_ack() == 1);
    fidl.max_amsdu_len = static_cast<wlan_mlme::MaxAmsduLen>(hci.max_amsdu_len());
    fidl.dsss_in_40 = (hci.dsss_in_40() == 1);
    fidl.intolerant_40 = (hci.intolerant_40() == 1);
    fidl.lsig_txop_protect = (hci.lsig_txop_protect() == 1);

    return fidl;
}

wlan_mlme::AmpduParams AmpduParamsToFidl(const AmpduParams& ap) {
    wlan_mlme::AmpduParams fidl;

    fidl.exponent = ap.exponent();
    fidl.min_start_spacing = static_cast<wlan_mlme::MinMpduStartSpacing>(ap.min_start_spacing());

    return fidl;
}

bool inline ExactMatch(uint32_t bitmask, uint32_t val) {
    return (val & bitmask) == bitmask && (val & ~bitmask) == 0;
}

zx_status_t HtMcsBitmaskToFidl(const SupportedMcsRxMcsHead& smrmh, wlan_mlme::HtMcs* fidl) {
    // Support only MCS 0-31 and the supported MCS Set should be either all 1 or all 0 for a group
    // of 8 corresponding to the number of spatial stream because MCS >= 32 are optional and not
    // widely adopted.
    uint32_t bitmask = 0xFFFFFFFF;
    uint32_t mcs_set_bitmask = smrmh.bitmask();

    if (ExactMatch(bitmask, mcs_set_bitmask)) {
        *fidl = wlan_mlme::HtMcs::MCS0_31;
        return ZX_OK;
    }
    bitmask >>= 8;
    if (ExactMatch(bitmask, mcs_set_bitmask)) {
        *fidl = wlan_mlme::HtMcs::MCS0_23;
        return ZX_OK;
    }
    bitmask >>= 8;
    if (ExactMatch(bitmask, mcs_set_bitmask)) {
        *fidl = wlan_mlme::HtMcs::MCS0_15;
        return ZX_OK;
    }
    bitmask >>= 8;
    if (ExactMatch(bitmask, mcs_set_bitmask)) {
        *fidl = wlan_mlme::HtMcs::MCS0_7;
        return ZX_OK;
    }
    *fidl = wlan_mlme::HtMcs::MCS_INVALID;
    if (mcs_set_bitmask == 0) { return ZX_OK; } // Empty MCS set OK for HTOperation, not an error
    return ZX_ERR_NOT_SUPPORTED;
}

wlan_mlme::SupportedMcsSet SupportedMcsSetToFidl(const SupportedMcsSet& sms) {
    wlan_mlme::SupportedMcsSet fidl;

    zx_status_t status = HtMcsBitmaskToFidl(sms.rx_mcs_head, &fidl.rx_mcs_set);
    if (status != ZX_OK) {
        errorf("Error parsing MCS Set: %zu. Error: %d\n", sms.rx_mcs_head.bitmask(), status);
    }
    ZX_DEBUG_ASSERT(status == ZX_OK);
    fidl.rx_highest_rate = sms.rx_mcs_tail.highest_rate();
    fidl.tx_mcs_set_defined = (sms.tx_mcs.set_defined() == 1);
    fidl.tx_rx_diff = (sms.tx_mcs.rx_diff() == 1);
    fidl.tx_max_ss = sms.tx_mcs.max_ss_human();  // Converting to human readable
    fidl.tx_ueqm = (sms.tx_mcs.ueqm() == 1);

    return fidl;
}

wlan_mlme::HtExtCapabilities HtExtCapabilitiesToFidl(const HtExtCapabilities& hec) {
    wlan_mlme::HtExtCapabilities fidl;

    fidl.pco = (hec.pco() == 1);
    fidl.pco_transition = static_cast<wlan_mlme::PcoTransitionTime>(hec.pco_transition());
    fidl.mcs_feedback = static_cast<wlan_mlme::McsFeedback>(hec.mcs_feedback());
    fidl.htc_ht_support = (hec.htc_ht_support() == 1);
    fidl.rd_responder = (hec.rd_responder() == 1);

    return fidl;
}

wlan_mlme::TxBfCapability TxBfCapabilityToFidl(const TxBfCapability& tbc) {
    wlan_mlme::TxBfCapability fidl;

    fidl.implicit_rx = (tbc.implicit_rx() == 1);
    fidl.rx_stag_sounding = (tbc.rx_stag_sounding() == 1);
    fidl.tx_stag_sounding = (tbc.tx_stag_sounding() == 1);
    fidl.rx_ndp = (tbc.rx_ndp() == 1);
    fidl.tx_ndp = (tbc.tx_ndp() == 1);
    fidl.implicit = (tbc.implicit() == 1);
    fidl.calibration = static_cast<wlan_mlme::Calibration>(tbc.calibration());
    fidl.csi = (tbc.csi() == 1);
    fidl.noncomp_steering = (tbc.noncomp_steering() == 1);
    fidl.comp_steering = (tbc.comp_steering() == 1);
    fidl.csi_feedback = static_cast<wlan_mlme::Feedback>(tbc.csi_feedback());
    fidl.noncomp_feedback = static_cast<wlan_mlme::Feedback>(tbc.noncomp_feedback());
    fidl.comp_feedback = static_cast<wlan_mlme::Feedback>(tbc.comp_feedback());
    fidl.min_grouping = static_cast<wlan_mlme::MinGroup>(tbc.min_grouping());
    fidl.csi_antennas = tbc.csi_antennas_human();                    // Converting to human readable
    fidl.noncomp_steering_ants = tbc.noncomp_steering_ants_human();  // Converting to human readable
    fidl.comp_steering_ants = tbc.comp_steering_ants_human();        // Converting to human readable
    fidl.csi_rows = tbc.csi_rows_human();                            // Converting to human readable
    fidl.chan_estimation = tbc.chan_estimation_human();              // Converting to human readable

    return fidl;
}

wlan_mlme::AselCapability AselCapabilityToFidl(const AselCapability& ac) {
    wlan_mlme::AselCapability fidl;

    fidl.asel = (ac.asel() == 1);
    fidl.csi_feedback_tx_asel = (ac.csi_feedback_tx_asel() == 1);
    fidl.ant_idx_feedback_tx_asel = (ac.ant_idx_feedback_tx_asel() == 1);
    fidl.explicit_csi_feedback = (ac.explicit_csi_feedback() == 1);
    fidl.antenna_idx_feedback = (ac.antenna_idx_feedback() == 1);
    fidl.rx_asel = (ac.rx_asel() == 1);
    fidl.tx_sounding_ppdu = (ac.tx_sounding_ppdu() == 1);

    return fidl;
}

std::unique_ptr<wlan_mlme::HtCapabilities> HtCapabilitiesToFidl(const HtCapabilities& ie) {
    auto fidl = wlan_mlme::HtCapabilities::New();

    fidl->ht_cap_info = HtCapabilityInfoToFidl(ie.ht_cap_info);
    fidl->ampdu_params = AmpduParamsToFidl(ie.ampdu_params);
    fidl->mcs_set = SupportedMcsSetToFidl(ie.mcs_set);
    fidl->ht_ext_cap = HtExtCapabilitiesToFidl(ie.ht_ext_cap);
    fidl->txbf_cap = TxBfCapabilityToFidl(ie.txbf_cap);
    fidl->asel_cap = AselCapabilityToFidl(ie.asel_cap);

    return fidl;
}

wlan_mlme::HTOperationInfo HtOpInfoToFidl(const HtOpInfoHead& head, const HtOpInfoTail tail) {
    wlan_mlme::HTOperationInfo fidl;

    fidl.secondary_chan_offset =
        static_cast<wlan_mlme::SecChanOffset>(head.secondary_chan_offset());
    fidl.sta_chan_width = static_cast<wlan_mlme::StaChanWidth>(head.sta_chan_width());
    fidl.rifs_mode = (head.rifs_mode() == 1);
    fidl.ht_protect = static_cast<wlan_mlme::HtProtect>(head.ht_protect());
    fidl.nongreenfield_present = (head.nongreenfield_present() == 1);
    fidl.obss_non_ht = (head.obss_non_ht() == 1);
    fidl.center_freq_seg2 = head.center_freq_seg2();
    fidl.dual_beacon = (head.dual_beacon() == 1);
    fidl.dual_cts_protect = (head.dual_cts_protect() == 1);

    fidl.stbc_beacon = (tail.stbc_beacon() == 1);
    fidl.lsig_txop_protect = (tail.lsig_txop_protect() == 1);
    fidl.pco_active = (tail.pco_active() == 1);
    fidl.pco_phase = (tail.pco_phase() == 1);

    return fidl;
}

std::unique_ptr<wlan_mlme::HtOperation> HtOperationToFidl(const HtOperation& ie) {
    auto fidl = wlan_mlme::HtOperation::New();

    fidl->primary_chan = ie.primary_chan;
    fidl->ht_op_info = HtOpInfoToFidl(ie.head, ie.tail);
    fidl->basic_mcs_set = SupportedMcsSetToFidl(ie.basic_mcs_set);

    return fidl;
}

wlan_mlme::VhtMcsNss VhtMcsNssToFidl(const VhtMcsNss& vmn) {
    wlan_mlme::VhtMcsNss fidl;

    for (uint8_t ss_num = 1; ss_num <= 8; ss_num++) {
        fidl.rx_max_mcs[ss_num - 1] = static_cast<wlan_mlme::VhtMcs>(vmn.get_rx_max_mcs_ss(ss_num));
    }
    fidl.rx_max_data_rate = vmn.rx_max_data_rate();
    fidl.max_nsts = vmn.max_nsts();

    for (uint8_t ss_num = 1; ss_num <= 8; ss_num++) {
        fidl.tx_max_mcs[ss_num - 1] = static_cast<wlan_mlme::VhtMcs>(vmn.get_tx_max_mcs_ss(ss_num));
    }
    fidl.tx_max_data_rate = vmn.tx_max_data_rate();
    fidl.ext_nss_bw = (vmn.ext_nss_bw() == 1);

    return fidl;
}

wlan_mlme::VhtCapabilitiesInfo VhtCapabilitiesInfoToFidl(const VhtCapabilitiesInfo& vci) {
    wlan_mlme::VhtCapabilitiesInfo fidl;

    fidl.max_mpdu_len = static_cast<wlan_mlme::MaxMpduLen>(vci.max_mpdu_len());
    fidl.supported_cbw_set = vci.supported_cbw_set();
    fidl.rx_ldpc = (vci.rx_ldpc() == 1);
    fidl.sgi_cbw80 = (vci.sgi_cbw80() == 1);
    fidl.sgi_cbw160 = (vci.sgi_cbw160() == 1);
    fidl.tx_stbc = (vci.tx_stbc() == 1);
    fidl.rx_stbc = (vci.rx_stbc() == 1);
    fidl.su_bfer = (vci.su_bfer() == 1);
    fidl.su_bfee = (vci.su_bfee() == 1);
    fidl.bfee_sts = vci.bfee_sts();
    fidl.num_sounding = vci.num_sounding();
    fidl.mu_bfer = (vci.mu_bfer() == 1);
    fidl.mu_bfee = (vci.mu_bfee() == 1);
    fidl.txop_ps = (vci.txop_ps() == 1);
    fidl.htc_vht = (vci.htc_vht() == 1);
    fidl.max_ampdu_exp = vci.max_ampdu_exp();
    fidl.link_adapt = static_cast<wlan_mlme::VhtLinkAdaptation>(vci.link_adapt());
    fidl.rx_ant_pattern = (vci.rx_ant_pattern() == 1);
    fidl.tx_ant_pattern = (vci.tx_ant_pattern() == 1);
    fidl.ext_nss_bw = vci.ext_nss_bw();

    return fidl;
}

std::unique_ptr<wlan_mlme::VhtCapabilities> VhtCapabilitiesToFidl(const VhtCapabilities& ie) {
    auto fidl = wlan_mlme::VhtCapabilities::New();

    fidl->vht_cap_info = VhtCapabilitiesInfoToFidl(ie.vht_cap_info);
    fidl->vht_mcs_nss = VhtMcsNssToFidl(ie.vht_mcs_nss);

    return fidl;
}

std::unique_ptr<wlan_mlme::VhtOperation> VhtOperationToFidl(const VhtOperation& ie) {
    auto fidl = wlan_mlme::VhtOperation::New();

    fidl->vht_cbw = static_cast<wlan_mlme::VhtCbw>(ie.vht_cbw);
    fidl->center_freq_seg0 = ie.center_freq_seg0;
    fidl->center_freq_seg1 = ie.center_freq_seg1;
    fidl->vht_mcs_nss = VhtMcsNssToFidl(ie.vht_mcs_nss);

    return fidl;
}

BeaconHash Bss::GetBeaconSignature(const Beacon& beacon, size_t frame_len) const {
    auto arr = reinterpret_cast<const uint8_t*>(&beacon);

    // Get a hash of the beacon except for its first field: timestamp.
    arr += sizeof(beacon.timestamp);
    frame_len -= sizeof(beacon.timestamp);

    BeaconHash hash = 0;
    // TODO(porce): Change to a less humble version.
    for (size_t idx = 0; idx < frame_len; idx++) {
        hash += *(arr + idx);
    }
    return hash;
}

wlan_mlme::BSSDescription Bss::ToFidl() const {
    // Translates the Bss object into FIDL message.

    wlan_mlme::BSSDescription fidl;
    // TODO(NET-1170): Decommission Bss::ToFidl()
    bss_desc_.Clone(&fidl);

    std::memcpy(fidl.bssid.mutable_data(), bssid_.byte, common::kMacAddrLen);

    if (has_dsss_param_set_chan_ == true) {
        // Channel was explicitly announced by the AP
        fidl.chan.primary = dsss_param_set_chan_;
    } else {
        // Fallback to the inference
        fidl.chan.primary = bcn_rx_chan_.primary;
    }
    fidl.chan.cbw = static_cast<wlan_mlme::CBW>(bcn_rx_chan_.cbw);

    // RSN
    fidl.rsn.reset();
    if (rsne_len_ > 0) {
        fidl.rsn = fidl::VectorPtr<uint8_t>::New(rsne_len_);
        memcpy(fidl.rsn->data(), rsne_.get(), rsne_len_);
    }

    return fidl;
}

std::string Bss::SupportedRatesToString() const {
    constexpr uint8_t kBasicRateMask = 0x80;
    char buf[SupportedRatesElement::kMaxLen * 6 + 1];
    char* ptr = buf;
    for (auto const& rate : supported_rates_) {
        // Rates are printed as Mbps, a preceding * indicates a basic rate
        ptr += std::snprintf(ptr, 6, "%s%.1f ", (rate & kBasicRateMask) ? "" : "*",
                             (rate & (kBasicRateMask - 1)) / 2.0);
    }

    // TODO: Support BSSMembershipSelectorSet.

    return std::string(buf);
}

wlan_mlme::BSSTypes GetBssType(const CapabilityInfo& cap) {
    // Note. This is in Beacon / Probe Response frames context.
    // IEEE Std 802.11-2016, 9.4.1.4
    if (cap.ess() == 0x1 && cap.ibss() == 0x0) {
        return ::fuchsia::wlan::mlme::BSSTypes::INFRASTRUCTURE;
    } else if (cap.ess() == 0x0 && cap.ibss() == 0x1) {
        return ::fuchsia::wlan::mlme::BSSTypes::INDEPENDENT;
    } else if (cap.ess() == 0x0 && cap.ibss() == 0x0) {
        return ::fuchsia::wlan::mlme::BSSTypes::MESH;
    } else {
        // Undefined
        return ::fuchsia::wlan::mlme::BSSTypes::ANY_BSS;
    }
}

}  // namespace wlan
