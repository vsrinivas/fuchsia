// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme as fidl_mlme;

type Ssid = Vec<u8>;

pub fn fake_bss_description(
    ssid: Ssid,
    rsne_bytes: Option<Vec<u8>>,
    vendor_ies_bytes: Option<Vec<u8>>,
) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid: [7, 1, 2, 77, 53, 8],
        ssid,
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        beacon_period: 100,
        dtim_period: 100,
        timestamp: 0,
        local_time: 0,
        cap: crate::mac::CapabilityInfo(0).with_privacy(rsne_bytes.is_some()).0,
        rates: vec![],
        country: None,
        rsne: rsne_bytes,
        vendor_ies: vendor_ies_bytes,

        rcpi_dbmh: 0,
        rsni_dbh: 0,

        ht_cap: None,
        ht_op: None,
        vht_cap: None,
        vht_op: None,
        chan: fidl_common::WlanChan { primary: 1, secondary80: 0, cbw: fidl_common::Cbw::Cbw20 },
        rssi_dbm: 0,
        snr_db: 0,
    }
}

pub fn fake_unprotected_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    fake_bss_description(ssid, None, None)
}
