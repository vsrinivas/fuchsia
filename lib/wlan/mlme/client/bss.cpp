// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/bss.h>
#include <wlan/mlme/debug.h>

#include <wlan/common/channel.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <memory>
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
    snprintf(
        buf, sizeof(buf), "BSSID %s Infra %s  RSSI %3d  Country %3s Channel %4s SSID [%s]",
        bssid_.ToString().c_str(),
        bss_desc_.bss_type == wlan_mlme::BSSTypes::INFRASTRUCTURE ? "Y" : "N", bss_desc_.rssi_dbm,
        bss_desc_.country.is_null() ? "---"
                                    : reinterpret_cast<const char*>(bss_desc_.country->data()),
        common::ChanStr(bcn_rx_chan_).c_str(), debug::ToAsciiOrHexStr(*bss_desc_.ssid).c_str());
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
    bssid_.CopyTo(bss_desc_.bssid.mutable_data());

    bss_desc_.beacon_period = beacon.beacon_interval;  // name mismatch is spec-compliant.
    ParseCapabilityInfo(beacon.cap);
    bss_desc_.bss_type = GetBssType(beacon.cap);

    // IE's.
    auto ie_chains = beacon.elements;
    size_t ie_chains_len = frame_len - beacon.len();

    auto status = ParseIE(ie_chains, ie_chains_len);
    if (status != ZX_OK) { return status; }

    // Post processing after IE parsing

    BuildMlmeRateSets(supported_rates_, ext_supp_rates_, &bss_desc_.basic_rate_set,
                      &bss_desc_.op_rate_set);

    // Interop: Do not discard the beacon unless it is confirmed to be safe to do.
    // TODO(porce): Do something with the validation result.
    ValidateBssDesc(bss_desc_, has_dsss_param_set_chan_, dsss_param_set_chan_);

    auto chan = DeriveChanFromBssDesc(bss_desc_, bcn_rx_chan_.primary, has_dsss_param_set_chan_,
                                      dsss_param_set_chan_);
    bss_desc_.chan = common::ToFidl(chan);
    debugbcn("beacon BSSID %s Chan %u CBW %u Sec80 %u\n",
             common::MacAddr(bss_desc_.bssid).ToString().c_str(), bss_desc_.chan.primary,
             bss_desc_.chan.cbw, bss_desc_.chan.secondary80);

    return ZX_OK;
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
            std::vector<uint8_t> ssid(ie->ssid, ie->ssid + ie->hdr.len);
            bss_desc_.ssid.reset(std::move(ssid));
            debugbcn("%s SSID: [%s]\n", dbgmsghdr, debug::ToAsciiOrHexStr(*bss_desc_.ssid).c_str());
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
            debugbcn("%s Supported rates: %s\n", dbgmsghdr,
                     RatesToString(supported_rates_).c_str());
            break;
        }
        case element_id::kExtSuppRates: {
            auto ie = reader.read<ExtendedSupportedRatesElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            if (!ext_supp_rates_.empty()) { ext_supp_rates_.clear(); }
            for (uint8_t idx = 0; idx < ie->hdr.len; idx++) {
                ext_supp_rates_.push_back(ie->rates[idx]);
            }
            ZX_DEBUG_ASSERT(ext_supp_rates_.size() <= ExtendedSupportedRatesElement::kMaxLen);
            debugbcn("%s Ext supp rates: %s\n", dbgmsghdr, RatesToString(ext_supp_rates_).c_str());
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

            bss_desc_.country.resize(0);
            bss_desc_.country->assign(ie->country, ie->country + CountryElement::kCountryLen);

            debugbcn("%s Country: %3s\n", dbgmsghdr, bss_desc_.country->data());
            break;
        }
        case element_id::kRsn: {
            auto ie = reader.read<RsnElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            // TODO(porce): Consider pre-allocate max memory and recycle it.
            // Don't use a unique_ptr
            // if (rsne_) delete[] rsne_;
            size_t ie_len = sizeof(ElementHeader) + ie->hdr.len;
            rsne_.reset(new uint8_t[ie_len]);
            memcpy(rsne_.get(), ie, ie_len);
            rsne_len_ = ie_len;

            bss_desc_.rsn.reset();
            if (rsne_len_ > 0) {
                bss_desc_.rsn = fidl::VectorPtr<uint8_t>::New(rsne_len_);
                memcpy(bss_desc_.rsn->data(), rsne_.get(), rsne_len_);
            }

            debugbcn("%s RSN\n", dbgmsghdr);
            break;
        }
        case element_id::kHtCapabilities: {
            auto ie = reader.read<HtCapabilities>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            bss_desc_.ht_cap = std::make_unique<wlan_mlme::HtCapabilities>(ie->ToFidl());

            debugbcn("%s HtCapabilities parsed\n", dbgmsghdr);
            debugbcn("%s\n", debug::Describe(*ie).c_str());
            break;
        }
        case element_id::kHtOperation: {
            auto ie = reader.read<HtOperation>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            bss_desc_.ht_op = std::make_unique<wlan_mlme::HtOperation>(ie->ToFidl());
            debugbcn("%s HtOperation parsed\n", dbgmsghdr);
            break;
        }
        case element_id::kVhtCapabilities: {
            auto ie = reader.read<VhtCapabilities>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            bss_desc_.vht_cap = std::make_unique<wlan_mlme::VhtCapabilities>(ie->ToFidl());
            debugbcn("%s VhtCapabilities parsed\n", dbgmsghdr);
            break;
        }
        case element_id::kVhtOperation: {
            auto ie = reader.read<VhtOperation>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            bss_desc_.vht_op = std::make_unique<wlan_mlme::VhtOperation>(ie->ToFidl());
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

std::string Bss::RatesToString(const std::vector<uint8_t>& rates) const {
    constexpr uint8_t kBasicRateMask = 0x80;
    char buf[128];
    char* ptr = buf;
    for (auto const& rate : rates) {
        // Rates are printed as Mbps, a preceding * indicates a basic rate
        ptr += std::snprintf(ptr, 8, "%s%.1f ", (rate & kBasicRateMask) ? "" : "*",
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

// Validate by testing the intra-consistency of the presence and the values of the information.
bool ValidateBssDesc(const wlan_mlme::BSSDescription& bss_desc, bool has_dsss_param_set_chan,
                     uint8_t dsss_param_set_chan) {
    bool has_ht_cap = bss_desc.ht_cap != nullptr;
    bool has_ht_op = bss_desc.ht_op != nullptr;
    bool has_vht_cap = bss_desc.vht_cap != nullptr;
    bool has_vht_op = bss_desc.vht_op != nullptr;

#define DBGBCN(msg)                                                                       \
    debugbcn("beacon from BSSID %s malformed: " msg                                       \
             " : has_dsss_param %u dsss_chan %u ht_cap %u ht_op %u "                      \
             "vht_cap %u vht_op %u ht_op_primary_chan %u",                                \
             common::MacAddr(bss_desc.bssid).ToString().c_str(), has_dsss_param_set_chan, \
             dsss_param_set_chan, has_ht_cap, has_ht_op, has_vht_cap, has_vht_op,         \
             has_ht_op ? bss_desc.ht_op->primary_chan : 0);

    // IEEE Std 802.11-2016 Table 9-27, the use of MIB dot11HighThroughputOptionImplemented
    // Either both present or both absent
    if (has_ht_cap != has_ht_op) {
        DBGBCN("Inconsistent presence of ht_cap and ht_op");
        return false;
    }

    // IEEE Std 802.11-2016, B.4.2's CFHT, B.4.17.1's HTM1.1, B.4.25.1's VHTM1.1
    if (has_vht_cap && !has_ht_cap) {
        DBGBCN("Inconsistent presence of ht_cap and vht_cap");
        return false;
    }

    // See IEEE Std 802.11-2016 Table 9-27, the use of MIB dot11VHTOptionImplemented
    // Either both present or both absent
    if (has_vht_cap != has_vht_op) {
        DBGBCN("Inconsistent presence of vht_cap and vht_op");
        return false;
    }

    // No particular clause in the stadnard. See dot11CurrentChannel, and
    // related MIBs
    if (has_dsss_param_set_chan && bss_desc.ht_cap != nullptr) {
        if (dsss_param_set_chan != bss_desc.ht_op->primary_chan) {
            DBGBCN("dss param chan != ht_op chan");
            return false;
        }
    }

#undef DBCBCN
    return true;
}

wlan_channel_t DeriveChanFromBssDesc(const wlan_mlme::BSSDescription& bss_desc,
                                     uint8_t bcn_rx_chan_primary, bool has_dsss_param_set_chan,
                                     uint8_t dsss_param_set_chan) {
    wlan_channel_t chan = {
        .primary = has_dsss_param_set_chan ? dsss_param_set_chan : bcn_rx_chan_primary,
        .cbw = CBW20,  // default
        .secondary80 = 0,
    };

    // See IEEE 802.11-2016, Table 9-250, Table 11-24.

    auto has_ht = (bss_desc.ht_cap != nullptr && bss_desc.ht_op != nullptr);
    if (!has_ht) {
        // No HT or VHT support. Even if there was attached an incomplete set of
        // HT/VHT IEs, those are not be properly decodable.
        return chan;
    }

    chan.primary = bss_desc.ht_op->primary_chan;

    switch (bss_desc.ht_op->ht_op_info.secondary_chan_offset) {
    case to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_ABOVE):
        chan.cbw = CBW40ABOVE;
        break;
    case to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_BELOW):
        chan.cbw = CBW40BELOW;
    default:  // SECONDARY_NONE or RESERVED
        chan.cbw = CBW20;
        break;
    }

    // This overrides Secondary Channel Offset.
    // TODO(NET-677): Conditionally apply
    if (bss_desc.ht_op->ht_op_info.sta_chan_width ==
        to_enum_type(wlan_mlme::StaChanWidth::TWENTY)) {
        chan.cbw = CBW20;
        return chan;
    }

    auto has_vht = (bss_desc.vht_cap != nullptr && bss_desc.vht_op != nullptr);
    if (!has_vht) {
        // No VHT support. Even if there was attached an incomplete set of
        // VHT IEs, those are not be properly decodable.
        return chan;
    }

    // has_ht and has_vht
    switch (bss_desc.vht_op->vht_cbw) {
    case to_enum_type(wlan_mlme::VhtCbw::CBW_20_40):
        return chan;
    case to_enum_type(wlan_mlme::VhtCbw::CBW_80_160_80P80): {
        // See IEEE Std 802.11-2016, Table 9-253
        auto seg0 = bss_desc.vht_op->center_freq_seg0;
        auto seg1 = bss_desc.vht_op->center_freq_seg1;
        auto gap = (seg0 >= seg1) ? (seg0 - seg1) : (seg1 - seg0);

        if (seg1 > 0 && gap < 8) {
            // Reserved case. Fallback to HT CBW
        } else if (seg1 > 0 && (gap > 8 && gap <= 16)) {
            // Reserved case. Fallback to HT CBW
        } else if (seg1 == 0) {
            chan.cbw = CBW80;
        } else if (gap == 8) {
            chan.cbw = CBW160;
        } else if (gap > 16) {
            chan.cbw = CBW80P80;
        }

        return chan;
    }
    default:
        // Deprecated
        return chan;
    }
}

void ClassifyRateSets(const std::vector<uint8_t>& rates, ::fidl::VectorPtr<uint8_t>* basic,
                      ::fidl::VectorPtr<uint8_t>* op) {
    for (uint8_t r : rates) {
        uint8_t numeric_rate = SupportedRate{r}.rate();
        if (SupportedRate{r}.is_basic()) { (*basic).push_back(numeric_rate); }
        (*op).push_back(numeric_rate);
    }
}

void BuildMlmeRateSets(const std::vector<uint8_t>& supp_rates,
                       const std::vector<uint8_t>& ext_supp_rates,
                       ::fidl::VectorPtr<uint8_t>* basic, ::fidl::VectorPtr<uint8_t>* op) {
    basic->resize(0);
    op->resize(0);
    ClassifyRateSets(supp_rates, basic, op);
    ClassifyRateSets(ext_supp_rates, basic, op);
}

}  // namespace wlan
