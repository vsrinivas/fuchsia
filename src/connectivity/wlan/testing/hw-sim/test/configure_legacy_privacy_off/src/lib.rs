// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_service::{ErrCode, ScanRequest, ScanResult, WlanMarker, WlanProxy},
    fidl_fuchsia_wlan_tap::WlantapPhyProxy,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
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

// TODO(fxbug.dev/36399): Refactor this test and other config tests to remove duplicate scan code.
async fn scan(
    wlan_service: &WlanProxy,
    phy: &WlantapPhyProxy,
    helper: &mut test_utils::TestHelper,
) -> ScanResult {
    helper
        .run_until_complete_or_timeout(
            10.seconds(),
            "receive a scan response",
            EventHandlerBuilder::new()
                .on_set_channel(
                    Sequence::start()
                        .then(
                            Beacon::send_on_primary_channel(CHANNEL, &phy)
                                .bssid(BSS_WPA1)
                                .ssid(SSID_WPA1.to_vec())
                                .protection(Protection::Wpa1),
                        )
                        .then(
                            Beacon::send_on_primary_channel(CHANNEL, &phy)
                                .bssid(BSS_WEP)
                                .ssid(SSID_WEP.to_vec())
                                .protection(Protection::Wep),
                        )
                        .then(
                            Beacon::send_on_primary_channel(CHANNEL, &phy)
                                .bssid(BSS_MIXED)
                                .ssid(SSID_MIXED.to_vec())
                                .protection(Protection::Wpa1Wpa2Personal),
                        ),
                )
                .build(),
            wlan_service.scan(&mut ScanRequest { timeout: 5 }),
        )
        .await
        .unwrap()
}

/// Test a client cannot connect to a wep or wpa network when configured off.
#[fuchsia_async::run_singlethreaded(test)]
async fn configure_legacy_privacy_off() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;

    let wlan_service =
        connect_to_service::<WlanMarker>().expect("Failed to connect to wlan service");
    let () = loop_until_iface_is_found().await;

    let proxy = helper.proxy();
    let scan_result = scan(&wlan_service, &proxy, &mut helper).await;

    assert_eq!(
        ErrCode::Ok,
        scan_result.error.code,
        "The error message was: {}",
        scan_result.error.description
    );
    let mut aps: Vec<_> = scan_result
        .aps
        .expect("Got empty scan results")
        .into_iter()
        .map(|ap| (ap.bssid, ap.is_compatible))
        .collect();
    aps.sort();
    let mut expected_aps =
        [(BSS_WPA1.0.to_vec(), false), (BSS_WEP.0.to_vec(), false), (BSS_MIXED.0.to_vec(), true)];
    expected_aps.sort();
    assert_eq!(&expected_aps, &aps[..]);
    helper.stop().await;
}
