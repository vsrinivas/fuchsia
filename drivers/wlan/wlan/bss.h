// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "element.h"
#include "enum.h"
#include "logging.h"
#include "mac_frame.h"
#include "macaddr_map.h"

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <ddk/protocol/wlan.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
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

    zx_status_t ProcessBeacon(const Beacon* beacon, size_t len, const wlan_rx_info_t* rx_info);

    std::string ToString() const;

    // TODO(porce): Move these out of Bss class.
    std::string SsidToString() const;
    std::string SupportedRatesToString() const;

    BSSTypes GetBssType() const {
        // Note. This is in Beacon / Probe Response frames context.
        // IEEE Std 802.11-2016, 9.4.1.4
        if (cap_.ess() == 0x1 && cap_.ibss() == 0x0) {
            return BSSTypes::INFRASTRUCTURE;
        } else if (cap_.ess() == 0x0 && cap_.ibss() == 0x1) {
            return BSSTypes::INDEPENDENT;
        } else if (cap_.ess() == 0x0 && cap_.ibss() == 0x0) {
            return BSSTypes::MESH;
        } else {
            // Undefined
            return BSSTypes::ANY_BSS;
        }
    }

    BSSDescriptionPtr ToFidl();
    fidl::String SsidToFidlString();
    const common::MacAddr& bssid() { return bssid_; }
    zx_time_t ts_refreshed() { return ts_refreshed_; }

   private:
    bool IsBeaconValid(const Beacon* beacon, size_t len) const;

    // Refreshes timestamp and signal strength.
    void Renew(const Beacon* beacon, const wlan_rx_info_t* rx_info);
    bool HasBeaconChanged(const Beacon* beacon, size_t len) const;

    // Update content such as IEs.
    zx_status_t Update(const Beacon* beacon, size_t len);
    zx_status_t Update(const ProbeResponse* proberesp, size_t len);
    zx_status_t ParseIE(const uint8_t* ie_chains, size_t ie_chains_len);

    // TODO(porce): Move Beacon method into Beacon class.
    uint32_t GetBeaconSignature(const Beacon* beacon, size_t len) const;

    common::MacAddr bssid_;      // From Addr3 of Mgmt Header.
    zx_time_t ts_refreshed_{0};  // Last time of Bss object update.

    // TODO(porce): Don't trust instantaneous values. Keep history.
    uint8_t rssi_{0};
    uint8_t rcpi_{0};
    uint8_t rsni_{0};

    // TODO(porce): Separate into class BeaconTracker.
    BeaconHash bcn_hash_{0};
    size_t bcn_len_{0};
    // A channel from which the beacon is received.
    // Different from current_channel.primary.
    wlan_channel_t bcn_chan_;

    // TODO(porce): Add ProbeResponse.

    // Fixed fields.
    uint64_t timestamp_{0};     // IEEE Std 802.11-2016, 9.4.1.10, 11.1.3.1. usec.
    uint16_t bcn_interval_{0};  // IEEE Std 802.11-2016, 9.4.1.3.
                                // TUs between TBTTs. 1 TU is 1024 usec.
    CapabilityInfo cap_;        // IEEE Std 802.11-2016, 9.4.1.4

    // Info Elments.
    // TODO(porce): Store IEs AS-IS without translation.
    uint8_t ssid_[SsidElement::kMaxLen]{0};
    size_t ssid_len_{0};
    std::vector<uint8_t> supported_rates_{0};
    wlan_channel_t current_chan_;
    std::string country_{""};
    std::unique_ptr<uint8_t[]> rsne_;
    size_t rsne_len_{0};

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Bss);
};  // namespace wlan

using BssMap = MacAddrMap<fbl::RefPtr<Bss>, macaddr_map_type::kBss>;

}  // namespace wlan
