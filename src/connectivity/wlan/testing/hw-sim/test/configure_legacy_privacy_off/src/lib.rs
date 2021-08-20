// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ieee80211::Ssid,
    lazy_static::lazy_static,
    wlan_common::{bss::Protection, mac::Bssid},
    wlan_hw_sim::*,
};

const CHANNEL: u8 = 1;
const BSS_WPA1: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);
const BSS_WEP: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x72]);
const BSS_MIXED: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x7a]);

lazy_static! {
    static ref SSID_WPA1: Ssid = Ssid::from("wpa1___how_nice");
    static ref SSID_WEP: Ssid = Ssid::from("wep_is_soooo_secure");
    static ref SSID_MIXED: Ssid = Ssid::from("this_is_fine");
}

/// Test a client cannot connect to a wep or wpa network when configured off.
#[fuchsia_async::run_singlethreaded(test)]
async fn configure_legacy_privacy_off() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;

    let () = loop_until_iface_is_found().await;

    let phy = helper.proxy();
    let beacons = [
        test_utils::ScanTestBeacon {
            channel: CHANNEL,
            bssid: BSS_WPA1,
            ssid: SSID_WPA1.clone(),
            protection: Protection::Wpa1,
            rssi: None,
        },
        test_utils::ScanTestBeacon {
            channel: CHANNEL,
            bssid: BSS_WEP,
            ssid: SSID_WEP.clone(),
            protection: Protection::Wep,
            rssi: None,
        },
        test_utils::ScanTestBeacon {
            channel: CHANNEL,
            bssid: BSS_MIXED,
            ssid: SSID_MIXED.clone(),
            protection: Protection::Wpa1Wpa2Personal,
            rssi: None,
        },
    ];
    let mut scan_results = test_utils::scan_for_networks(&phy, &beacons, &mut helper).await;
    scan_results.sort();

    let mut expected_aps = [
        (SSID_WPA1.clone(), BSS_WPA1.0, false, 0),
        (SSID_WEP.clone(), BSS_WEP.0, false, 0),
        (SSID_MIXED.clone(), BSS_MIXED.0, true, 0),
    ];
    expected_aps.sort();
    assert_eq!(&expected_aps, &scan_results[..]);
    helper.stop().await;
}
