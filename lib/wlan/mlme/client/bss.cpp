// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/bss.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/parse_beacon.h>

#include <wlan/common/channel.h>
#include <wlan/common/element_splitter.h>
#include <wlan/common/parse_element.h>

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
    bss_desc_.cap = beacon.cap.ToFidl();
    bss_desc_.bss_type = GetBssType(beacon.cap);

    // IE's.
    auto ie_chains = beacon.elements;
    size_t ie_chains_len = frame_len - beacon.len();

    ParseBeaconElements({ie_chains, ie_chains_len}, bcn_rx_chan_.primary, &bss_desc_);
    debugbcn("beacon BSSID %s Chan %u CBW %u Sec80 %u\n",
             common::MacAddr(bss_desc_.bssid).ToString().c_str(), bss_desc_.chan.primary,
             bss_desc_.chan.cbw, bss_desc_.chan.secondary80);

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
