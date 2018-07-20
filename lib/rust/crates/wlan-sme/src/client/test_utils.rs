// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_mlme;
use Ssid;

pub fn fake_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid: [0, 0, 0, 0, 0, 0],
        ssid: String::from_utf8_lossy(&ssid).to_string(),
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        beacon_period: 100,
        dtim_period: 100,
        timestamp: 0,
        local_time: 0,
        cap: fidl_mlme::CapabilityInfo {
            ess: false,
            ibss: false,
            cf_pollable: false,
            cf_poll_req: false,
            privacy: false,
            short_preamble: false,
            spectrum_mgmt: false,
            qos: false,
            short_slot_time: false,
            apsd: false,
            radio_msmt: false,
            delayed_block_ack: false,
            immediate_block_ack: false,
        },
        country: None,
        rsn: None,
        vht_cap: None,
        vht_op: None,
        chan: fidl_mlme::WlanChan {
            primary: 1,
            secondary80: 0,
            cbw: fidl_mlme::Cbw::Cbw20,
        },
        rssi_dbm: 0,
        rcpi_dbmh: 0,
        rsni_dbh: 0,
    }
}
