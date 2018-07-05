// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/bss.h>

#include <wlan/common/channel.h>

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
        buf, sizeof(buf), "BSSID %s Infra %s  RSSI %3d  Country %3s Channel %4s Cap %04x SSID [%s]",
        bssid_.ToString().c_str(), GetBssType() == wlan_mlme::BSSTypes::INFRASTRUCTURE ? "Y" : "N",
        rssi_dbm_.val, country_.c_str(), common::ChanStr(bcn_rx_chan_).c_str(), cap_.val(),
        SsidToString().c_str());
    return std::string(buf);
}

bool Bss::IsBeaconValid(const Beacon& beacon) const {
    // TODO(porce): Place holder. Add sanity logics.

    if (timestamp_ > beacon.timestamp) {
        // Suspicious. Wrap around?
        // TODO(porce): deauth if the client was in association.
    }

    // TODO(porce): Size check.
    // Note: Some beacons in 5GHz may not include DSSS Parameter Set IE

    return true;
}

void Bss::Renew(const Beacon& beacon, const wlan_rx_info_t* rx_info) {
    timestamp_ = beacon.timestamp;

    // TODO(porce): Take a deep look. Which resolution do we want to track?
    if (zx::clock::get(&ts_refreshed_) != ZX_OK) { ts_refreshed_ = zx::time_utc(); }

    // Radio statistics.
    if (rx_info == nullptr) return;

    bcn_rx_chan_ = rx_info->chan;

    // If the latest beacons lack measurements, keep the last report.
    // TODO(NET-856): Change DDK wlan_rx_info_t and do translation in the vendor device driver.
    if (rx_info->valid_fields & WLAN_RX_INFO_VALID_RSSI) { rssi_dbm_.val = rx_info->rssi_dbm; }
    if (rx_info->valid_fields & WLAN_RX_INFO_VALID_RCPI) { rcpi_dbmh_.val = rx_info->rcpi_dbmh; }
    if (rx_info->valid_fields & WLAN_RX_INFO_VALID_SNR) { rsni_dbh_.val = rx_info->snr_dbh; }
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
    bcn_interval_ = beacon.beacon_interval;
    cap_ = beacon.cap;

    // IE's.
    auto ie_chains = beacon.elements;
    auto ie_chains_len = frame_len - sizeof(Beacon);  // Subtract the beacon header len.

    ZX_DEBUG_ASSERT(ie_chains != nullptr);
    ZX_DEBUG_ASSERT(ie_chains_len <= frame_len);
    return ParseIE(ie_chains, ie_chains_len);
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

        // TODO(porce): Process HT Capabilities IE's HT Capabilites Info to get CBW announcement.

        snprintf(dbgmsghdr, sizeof(dbgmsghdr), "  IE %3u (Len %3u): ", hdr->id, hdr->len);
        switch (hdr->id) {
        case element_id::kSsid: {
            auto ie = reader.read<SsidElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            ssid_len_ = ie->hdr.len;
            memcpy(ssid_, ie->ssid, ssid_len_);

            debugbcn("%s SSID: [%s]\n", dbgmsghdr, SsidToString().c_str());
            break;
        }
        case element_id::kSuppRates: {
            auto ie = reader.read<SupportedRatesElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            for (uint8_t idx = 0; idx < ie->hdr.len; idx++) {
                supported_rates_.push_back(ie->rates[idx]);
            }
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

            char buf[CountryElement::kCountryLen + 1];
            snprintf(buf, sizeof(buf), "%s", ie->country);
            country_ = std::string(buf);
            debugbcn("%s Country: %s\n", dbgmsghdr, country_.c_str());
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

fidl::StringPtr Bss::SsidToFidlString() {
    // TODO(porce): Merge into SSID Element upon IE revamp.
    return fidl::StringPtr(reinterpret_cast<const char*>(ssid_), ssid_len_);
}

wlan_mlme::BSSDescription Bss::ToFidl() {
    // Translates the Bss object into FIDL message.
    // Note, this API does not directly handle Beacon frame or ProbeResponse frame.

    wlan_mlme::BSSDescription fidl;

    std::memcpy(fidl.bssid.mutable_data(), bssid_.byte, common::kMacAddrLen);

    fidl.bss_type = GetBssType();
    fidl.ssid = SsidToFidlString();

    fidl.beacon_period = bcn_interval_;  // TODO(porce): consistent naming.
    fidl.timestamp = timestamp_;

    if (has_dsss_param_set_chan_ == true) {
        // Channel was explicitly announced by the AP
        fidl.chan.primary = dsss_param_set_chan_;
    } else {
        // Fallback to the inference
        fidl.chan.primary = bcn_rx_chan_.primary;
    }
    fidl.chan.cbw = static_cast<wlan_mlme::CBW>(bcn_rx_chan_.cbw);

    // Stats
    fidl.rssi_dbm = rssi_dbm_.val;
    fidl.rcpi_dbmh = rcpi_dbmh_.val;
    fidl.rsni_dbh = rsni_dbh_.val;

    // RSN
    fidl.rsn.reset();
    if (rsne_len_ > 0) {
        fidl.rsn = fidl::VectorPtr<uint8_t>::New(rsne_len_);
        memcpy(fidl.rsn->data(), rsne_.get(), rsne_len_);
    }

    return fidl;
}

std::string Bss::SsidToString() const {
    // SSID is of UTF-8 codepoints that may include NULL character.
    // Convert that to human-readable form in best effort.

    // TODO(porce): Implement this half-baked PoC code.

    bool is_printable = true;  // printable ascii
    for (size_t idx = 0; idx < ssid_len_; idx++) {
        if (ssid_[idx] < 32 || ssid_[idx] > 127) {
            is_printable = false;
            break;
        }
    }

    if (is_printable) {
        char buf[SsidElement::kMaxLen + 1];
        memcpy(buf, ssid_, ssid_len_);
        buf[ssid_len_] = '\0';  // NULL termination.
        return std::string(buf);
    }

    // Good luck
    char utf_buf[SsidElement::kMaxLen * 3];
    char* ptr = utf_buf;
    ptr += std::snprintf(ptr, 10, "[utf8] ");
    for (size_t idx = 0; idx < ssid_len_; idx++) {
        ptr += std::snprintf(ptr, 4, "%02x ", ssid_[idx]);
    }
    return std::string(utf_buf);
}

std::string Bss::SupportedRatesToString() const {
    // TODO(porce): Distinguish BSSBasicRateSet, OperationalRateSet, BSSMembershipSelectorSet.
    return "NOT_IMPLEMENTED";
}

}  // namespace wlan
