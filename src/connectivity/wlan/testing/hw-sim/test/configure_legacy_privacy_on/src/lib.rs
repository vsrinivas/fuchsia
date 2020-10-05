// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    wlan_common::{bss::Protection, mac::Bssid},
    wlan_hw_sim::*,
};

const CHANNEL: u8 = 1;
const BSS_WPA1: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);
const SSID_WPA1: &[u8] = b"wpa1___how_nice";
const BSS_WEP: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x72]);
const SSID_WEP: &[u8] = b"wep_is_soooo_secure";
const BSS_MIXED: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x7a]);
const SSID_MIXED: &[u8] = b"this_is_fine";

/// Test a client can connect to a wep or wpa network only when configured on.
#[fuchsia_async::run_singlethreaded(test)]
async fn configure_legacy_privacy_on() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;

    let () = loop_until_iface_is_found().await;

    let phy = helper.proxy();
    let beacons = [
        test_utils::ScanTestBeacon {
            channel: CHANNEL,
            bssid: BSS_WPA1,
            ssid: SSID_WPA1.to_vec(),
            protection: Protection::Wpa1,
            rssi: None,
        },
        test_utils::ScanTestBeacon {
            channel: CHANNEL,
            bssid: BSS_WEP,
            ssid: SSID_WEP.to_vec(),
            protection: Protection::Wep,
            rssi: None,
        },
        test_utils::ScanTestBeacon {
            channel: CHANNEL,
            bssid: BSS_MIXED,
            ssid: SSID_MIXED.to_vec(),
            protection: Protection::Wpa1Wpa2Personal,
            rssi: None,
        },
    ];
    let mut scan_results = test_utils::scan_for_networks(&phy, &beacons, &mut helper).await;
    scan_results.sort();

    let mut expected_aps = [
        (String::from_utf8_lossy(SSID_WPA1).to_string(), BSS_WPA1.0, true, 0),
        (String::from_utf8_lossy(SSID_WEP).to_string(), BSS_WEP.0, true, 0),
        (String::from_utf8_lossy(SSID_MIXED).to_string(), BSS_MIXED.0, true, 0),
    ];
    expected_aps.sort();
    assert_eq!(&expected_aps, &scan_results[..]);
    helper.stop().await;
}
