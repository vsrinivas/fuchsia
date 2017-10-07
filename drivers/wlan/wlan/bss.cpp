// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bss.h"
#include <string>

namespace wlan {

zx_status_t Bss::ProcessBeacon(const Beacon* beacon, size_t len, const wlan_rx_info_t* rx_info) {
    if (!IsBeaconValid(beacon, len)) { return ZX_ERR_INTERNAL; }

    Renew(beacon, rx_info);

    if (!HasBeaconChanged(beacon, len)) {
        // If unchanged, it is sufficient to renew the BSS. Bail out.
        return ZX_OK;
    }

    if (bcn_len_ != 0) {
        // TODO(porce): Identify varying IE, and do IE-by-IE comparison.
        // BSS had been discovered, but the beacon changed.
        // Suspicious situation. Consider Deauth if in assoc.
        debugbcn("BSSID %s beacon change detected. (len %zu -> %zu)\n", bssid_.ToString().c_str(),
                 bcn_len_, len);
    }

    auto status = Update(beacon, len);
    if (status != ZX_OK) {
        debugbcn("BSSID %s failed to update its BSS object: (%d)\n", bssid_.ToString().c_str(),
                 status);

        return status;
    }

    return ZX_OK;
}

std::string Bss::ToString() const {
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "BSSID %s Infra %s  RSSI %3d  Country %3s Channel %3u Cap %04x SSID [%s]",
             bssid_.ToString().c_str(), GetBssType() == BSSTypes::INFRASTRUCTURE ? "Y" : "N", rssi_,
             country_.c_str(), current_chan_.primary20, cap_.val(), SsidToString().c_str());
    return std::string(buf);
}

bool Bss::IsBeaconValid(const Beacon* beacon, size_t len) const {
    // TODO(porce): Place holder. Add sanity logics.

    if (timestamp_ > beacon->timestamp) {
        // Suspicious. Wrap around?
        // TODO(porce): deauth if the client was in association.
    }

    // TODO(porce): Size check.
    // TODO(porce): Drop if bcn_chan_ != current_chan_.primary20.
    return true;
}

void Bss::Renew(const Beacon* beacon, const wlan_rx_info_t* rx_info) {
    timestamp_ = beacon->timestamp;

    // TODO(porce): Take a deep look. Which resolution do we want to track?
    ts_refreshed_ = zx_time_get(ZX_CLOCK_UTC);

    // Radio statistics.
    if (rx_info == nullptr) return;

    bcn_chan_.primary20 = rx_info->chan.channel_num;

    // If the latest beacons lack measurements, keep the last report.
    if (rx_info->flags & WLAN_RX_INFO_RSSI_PRESENT) { rssi_ = rx_info->rssi; }
    if (rx_info->flags & WLAN_RX_INFO_RCPI_PRESENT) { rcpi_ = rx_info->rcpi; }
    if (rx_info->flags & WLAN_RX_INFO_SNR_PRESENT) { rsni_ = rx_info->snr; }
}

bool Bss::HasBeaconChanged(const Beacon* beacon, size_t len) const {
    // Test changes in beacon, except for the timestamp field.
    if (len != bcn_len_) { return true; }
    auto sig = GetBeaconSignature(beacon, len);
    if (sig != bcn_hash_) { return true; }
    return false;
}

zx_status_t Bss::Update(const Beacon* beacon, size_t len) {
    // To be used to detect a change in Beacon.
    bcn_len_ = len;
    bcn_hash_ = GetBeaconSignature(beacon, len);

    // Fields that are always present.
    bcn_interval_ = beacon->beacon_interval;
    cap_ = beacon->cap;

    // IE's.
    auto ie_chains = beacon->elements;
    auto ie_chains_len = len - sizeof(Beacon);  // Subtract the beacon header len.

    ZX_DEBUG_ASSERT(ie_chains != nullptr);
    ZX_DEBUG_ASSERT(ie_chains_len <= len);
    return ParseIE(ie_chains, ie_chains_len);
}

zx_status_t Bss::Update(const ProbeResponse* proberesp, size_t len) {
    // TODO(porce): Give distinctions.
    return Update(reinterpret_cast<const Beacon*>(proberesp), len);
}

zx_status_t Bss::ParseIE(const uint8_t* ie_chains, size_t ie_chains_len) {
    ElementReader reader(ie_chains, ie_chains_len);

    debugbcn("Parsing IEs for BSSID %s\n", bssid_.ToString().c_str());
    uint8_t ie_cnt = 0;
    uint8_t ie_unparsed_cnt = 0;

    char dbgmsghdr[128];
    while (reader.is_valid()) {
        ie_cnt++;

        const ElementHeader* hdr = reader.peek();
        if (hdr == nullptr) break;

        snprintf(dbgmsghdr, sizeof(dbgmsghdr), "  IE %3u (Len %3u): ", hdr->id, hdr->len);
        switch (hdr->id) {
        case element_id::kSsid: {
            auto ie = reader.read<SsidElement>();
            if (ie == nullptr) {
                debugbcn("%s Failed to parse\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }

            if (ie->hdr.len > SsidElement::kMaxLen) {
                // Crush dark arts.
                debugbcn("%s Illegal len\n", dbgmsghdr);
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

            if (ie->hdr.len < 1 || ie->hdr.len > SupportedRatesElement::kMaxLen) {
                debugbcn("%s Illegal len\n", dbgmsghdr);
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
            if (ie->hdr.len != 1) {
                debugbcn("%s Illegal len\n", dbgmsghdr);
                return ZX_ERR_INTERNAL;
            }
            current_chan_.primary20 = ie->current_chan;
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

            if (ie->hdr.len < CountryElement::kCountryLen) {
                debugbcn("%s Illegal len\n", dbgmsghdr);
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

BeaconHash Bss::GetBeaconSignature(const Beacon* beacon, size_t len) const {
    auto arr = reinterpret_cast<const uint8_t*>(beacon);

    // Get a hash of the beacon except for its first field: timestamp.
    arr += sizeof(beacon->timestamp);
    len -= sizeof(beacon->timestamp);

    BeaconHash hash = 0;
    // TODO(porce): Change to a less humble version.
    for (size_t idx = 0; idx < len; idx++) {
        hash += *(arr + idx);
    }
    return hash;
}

fidl::String Bss::SsidToFidlString() {
    // TODO(porce): Merge into SSID Element upon IE revamp.
    return fidl::String(reinterpret_cast<const char*>(ssid_), ssid_len_);
}

BSSDescriptionPtr Bss::ToFidl() {
    // Translates the Bss object into FIDL message.
    // Note, this API does not directly handle Beacon frame or ProbeResponse frame.

    auto fidl_ptr = BSSDescription::New();
    auto fidl = fidl_ptr.get();

    fidl->bssid = fidl::Array<uint8_t>::New(kMacAddrLen);
    std::memcpy(fidl->bssid.data(), bssid_.byte, kMacAddrLen);

    fidl->bss_type = GetBssType();
    fidl->ssid = SsidToFidlString();

    fidl->beacon_period = bcn_interval_;  // TODO(porce): consistent naming.
    fidl->timestamp = timestamp_;
    fidl->channel = current_chan_.primary20;

    // Stats
    fidl->rssi_measurement = rssi_;
    fidl->rcpi_measurement = rcpi_;
    fidl->rsni_measurement = rsni_;

    // RSN
    fidl->rsn.reset();
    if (rsne_len_ > 0) {
        fidl->rsn = fidl::Array<uint8_t>::New(rsne_len_);
        memcpy(fidl->rsn.data(), rsne_.get(), rsne_len_);
    }

    return fidl_ptr;
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

bool BssMap::HasKey(const MacAddr& bssid) const {
    auto iter = map_.find(bssid.ToU64());
    return (iter != map_.end());
}

Bss* BssMap::Lookup(const MacAddr& bssid) const {
    if (!HasKey(bssid)) return nullptr;
    auto iter = map_.find(bssid.ToU64());
    return (iter->second);
}

// Update if exists, or Insert first then update.
zx_status_t BssMap::Upsert(const MacAddr& bssid, const Beacon* beacon, size_t bcn_len,
                           const wlan_rx_info_t* rx_info) {
    if (IsFull()) {
        Prune();
        if (IsFull()) return ZX_ERR_NO_RESOURCES;
    }

    // Insert if not there.
    if (!HasKey(bssid)) {
        auto* bss = new Bss(bssid);
        auto status = Insert(bssid, bss);
        if (status != ZX_OK) return status;
    }

    Bss* bss = Lookup(bssid);
    if (bss == nullptr) {
        debugbss("[BssMap] Failed to lookup BSSID %s\n", bssid.ToString().c_str());
        return ZX_ERR_UNAVAILABLE;
    }

    return bss->ProcessBeacon(beacon, bcn_len, rx_info);
}

void BssMap::Reset() {
    for (auto iter : map_) {
        delete iter.second;
    }
    map_.clear();
    // map_.~unordered_map();
}

bool BssMap::IsFull() const {
    return map_.size() > kMaxEntries;
}

zx_status_t BssMap::Prune() {
    // TODO(porce): call this periodically and delete stale entries.
    // TODO(porce): Implement a complex preemption logic here.

    static zx_time_t ts_last_prune = 0;

    zx_time_t now = zx_time_get(ZX_CLOCK_UTC);

    if (ts_last_prune + kPruneDelay > now) { return ZX_OK; }
    ts_last_prune = now;

    auto iter = map_.begin();
    while (iter != map_.end()) {
        Bss* bss = iter->second;
        if (bss == nullptr) {
            iter++;
            continue;
        }

        if (bss->ts_refreshed() + kExpiry >= now) {
            iter++;
            continue;
        }

        map_.erase(iter);
        delete bss;
    }

    return ZX_OK;
}

zx_status_t BssMap::Insert(const MacAddr& bssid, Bss* bss) {
    if (HasKey(bssid)) {
        debugbss("[BssMap] Duplicate insert declined for BSSID %s\n", bssid.ToString().c_str());
        return ZX_ERR_INVALID_ARGS;
    }

    // Insert and double check.
    auto iter = map_.emplace(bssid.ToU64(), bss);
    if (iter.second == false) {
        debugbss("[BssMap] Fail to add a new BSSID %s\n", bssid.ToString().c_str());
        return ZX_ERR_INTERNAL;
    }

    if (!HasKey(bssid)) {
        debugbss("[BssMap] BSSID %s just inserted but cannot be found\n", bssid.ToString().c_str());
        return ZX_ERR_INTERNAL;
    }

    debugbss("[BssMap] New BSSID %s inserted\n", bssid.ToString().c_str());
    return ZX_OK;
}

}  // namespace wlan
