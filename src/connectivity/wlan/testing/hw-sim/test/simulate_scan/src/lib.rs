// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    fidl_fuchsia_wlan_service::{ErrCode, ScanRequest, ScanResult, WlanMarker, WlanProxy},
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
    wlan_hw_sim::*,
};

const BSS_FOO: [u8; 6] = [0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f];
const SSID_FOO: &[u8] = b"foo";
const BSS_BAR: [u8; 6] = [0x62, 0x73, 0x73, 0x62, 0x61, 0x72];
const SSID_BAR: &[u8] = b"bar";
const BSS_BAZ: [u8; 6] = [0x62, 0x73, 0x73, 0x62, 0x61, 0x7a];
const SSID_BAZ: &[u8] = b"baz";

async fn scan(
    wlan_service: &WlanProxy,
    phy: &WlantapPhyProxy,
    helper: &mut test_utils::TestHelper,
) -> ScanResult {
    helper
        .run_until_complete_or_timeout(
            10.seconds(),
            "receive a scan response",
            |event| {
                if let WlantapPhyEvent::SetChannel { args } = event {
                    println!("set channel to {:?}", args.chan);
                    let network = match args.chan.primary {
                        1 => Some((&BSS_FOO, SSID_FOO)),
                        6 => Some((&BSS_BAR, SSID_BAR)),
                        11 => Some((&BSS_BAZ, SSID_BAZ)),
                        _ => None,
                    };
                    if let Some((bssid, ssid)) = network {
                        send_beacon(&mut vec![], &args.chan, bssid, ssid, false, &phy).unwrap();
                    }
                }
            },
            wlan_service.scan(&mut ScanRequest { timeout: 5 }),
        )
        .await
        .unwrap()
}

/// Test scan is working by simulating some fake APs that sends out beacon frames on specific
/// channel and verify all beacon frames are correctly reported as valid networks.
#[fuchsia_async::run_singlethreaded(test)]
async fn simulate_scan() {
    let mut helper =
        test_utils::TestHelper::begin_test(create_wlantap_config_client(HW_MAC_ADDR)).await;

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
        .map(|ap| (ap.ssid, ap.bssid))
        .collect();
    aps.sort();
    let mut expected_aps = [
        (String::from_utf8_lossy(SSID_FOO).to_string(), BSS_FOO.to_vec()),
        (String::from_utf8_lossy(SSID_BAR).to_string(), BSS_BAR.to_vec()),
        (String::from_utf8_lossy(SSID_BAZ).to_string(), BSS_BAZ.to_vec()),
    ];
    expected_aps.sort();
    assert_eq!(&expected_aps, &aps[..]);
}
