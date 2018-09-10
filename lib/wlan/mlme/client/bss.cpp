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
    snprintf(buf, sizeof(buf), "BSSID %s Infra %s  RSSI %3d  Country %3s Channel %4s SSID [%.*s]",
             bssid_.ToString().c_str(),
             bss_desc_.bss_type == wlan_mlme::BSSTypes::INFRASTRUCTURE ? "Y" : "N",
             bss_desc_.rssi_dbm,
             (bss_desc_.country != nullptr) ? bss_desc_.country->c_str() : "---",
             common::ChanStr(bcn_rx_chan_).c_str(), static_cast<int>(bss_desc_.ssid->size()),
             bss_desc_.ssid->data());
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
    size_t ie_chains_len = frame_len - beacon.len();
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
            std::vector<uint8_t> ssid(ie->ssid, ie->ssid + ie->hdr.len);
            bss_desc_.ssid.reset(std::move(ssid));
            // TODO(NET-698): Not all SSIDs are ASCII-printable. Write a designated printer module.
            debugbcn("%s SSID: [%.*s]\n", dbgmsghdr, static_cast<int>(bss_desc_.ssid->size()),
                     bss_desc_.ssid->data());
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

wlan_mlme::BSSDescription Bss::ToFidl() const {
    // Translates the Bss object into FIDL message.

    wlan_mlme::BSSDescription fidl;
    // TODO(NET-1170): Decommission Bss::ToFidl()
    bss_desc_.Clone(&fidl);

    std::memcpy(fidl.bssid.mutable_data(), bssid_.byte, common::kMacAddrLen);

    if (has_dsss_param_set_chan_) {
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

}  // namespace wlan
