// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/energy.h>
#include <wlan/common/logging.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/macaddr_map.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

namespace wlan {

// BeaconHash is a signature to compare consecutive beacons without memcmp().
// TODO(porce): Revamp to exclude varying IEs.
typedef uint32_t BeaconHash;

class Bss : public fbl::RefCounted<Bss> {
   public:
    Bss(const common::MacAddr& bssid) : bssid_(bssid) {
        memset(&cap_, 0, sizeof(cap_));
        supported_rates_.reserve(SupportedRatesElement::kMaxLen);
    }

    zx_status_t ProcessBeacon(const Beacon& beacon, size_t len, const wlan_rx_info_t* rx_info);

    std::string ToString() const;

    // TODO(porce): Move these out of Bss class.
    std::string SsidToString() const;
    std::string SupportedRatesToString() const;

    ::fuchsia::wlan::mlme::BSSTypes GetBssType() const {
        // Note. This is in Beacon / Probe Response frames context.
        // IEEE Std 802.11-2016, 9.4.1.4
        if (cap_.ess() == 0x1 && cap_.ibss() == 0x0) {
            return ::fuchsia::wlan::mlme::BSSTypes::INFRASTRUCTURE;
        } else if (cap_.ess() == 0x0 && cap_.ibss() == 0x1) {
            return ::fuchsia::wlan::mlme::BSSTypes::INDEPENDENT;
        } else if (cap_.ess() == 0x0 && cap_.ibss() == 0x0) {
            return ::fuchsia::wlan::mlme::BSSTypes::MESH;
        } else {
            // Undefined
            return ::fuchsia::wlan::mlme::BSSTypes::ANY_BSS;
        }
    }

    ::fuchsia::wlan::mlme::BSSDescription ToFidl() const;

    const common::MacAddr& bssid() { return bssid_; }
    zx::time_utc ts_refreshed() { return ts_refreshed_; }

   private:
    bool IsBeaconValid(const Beacon& beacon) const;

    // Refreshes timestamp and signal strength.
    void Renew(const Beacon& beacon, const wlan_rx_info_t* rx_info);
    bool HasBeaconChanged(const Beacon& beacon, size_t len) const;

    // Update content such as IEs.
    zx_status_t Update(const Beacon& beacon, size_t len);
    zx_status_t ParseIE(const uint8_t* ie_chains, size_t ie_chains_len);

    // TODO(porce): Move Beacon method into Beacon class.
    uint32_t GetBeaconSignature(const Beacon& beacon, size_t len) const;

    common::MacAddr bssid_;      // From Addr3 of Mgmt Header.
    zx::time_utc ts_refreshed_;  // Last time of Bss object update.

    // TODO(porce): Separate into class BeaconTracker.
    BeaconHash bcn_hash_{0};
    size_t bcn_len_{0};
    wlan_channel_t bcn_rx_chan_;

    // TODO(porce): Add ProbeResponse.

    ::fuchsia::wlan::mlme::BSSDescription bss_desc_;

    // TODO(porce): Unify into FIDL data structure
    CapabilityInfo cap_;  // IEEE Std 802.11-2016, 9.4.1.4
    // TODO(porce): Store IEs AS-IS without translation.
    uint8_t ssid_[SsidElement::kMaxLen]{0};
    size_t ssid_len_{0};
    std::vector<uint8_t> supported_rates_{};

    // Conditionally present. See IEEE Std 802.11-2016, 9.3.3.3 Table 9-27
    bool has_dsss_param_set_chan_ = false;
    uint8_t dsss_param_set_chan_;

    std::string country_{""};
    std::unique_ptr<uint8_t[]> rsne_;
    size_t rsne_len_{0};

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Bss);
};

using BssMap = MacAddrMap<fbl::RefPtr<Bss>, macaddr_map_type::kBss>;

::fuchsia::wlan::mlme::VhtMcsNss VhtMcsNssToFidl(const VhtMcsNss& vmn);
::fuchsia::wlan::mlme::VhtCapabilitiesInfo VhtCapabilitiesInfoToFidl(
    const VhtCapabilitiesInfo& vci);
std::unique_ptr<::fuchsia::wlan::mlme::VhtCapabilities> VhtCapabilitiesToFidl(
    const VhtCapabilities& ie);
std::unique_ptr<::fuchsia::wlan::mlme::VhtOperation> VhtOperationToFidl(const VhtOperation& ie);

}  // namespace wlan
