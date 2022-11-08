// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_policy as fidl_policy,
    ieee80211::{Bssid, Ssid},
    lazy_static::lazy_static,
    pin_utils::pin_mut,
    std::convert::TryFrom,
    wlan_common::{
        bss::Protection,
        channel::{Cbw, Channel},
    },
    wlan_hw_sim::*,
};

const BSS_WPA1: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);
const BSS_WEP: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x72]);
const BSS_MIXED: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x7a]);

lazy_static! {
    static ref SSID_WPA1: Ssid = Ssid::try_from("wpa1___how_nice").unwrap();
    static ref SSID_WEP: Ssid = Ssid::try_from("wep_is_soooo_secure").unwrap();
    static ref SSID_MIXED: Ssid = Ssid::try_from("this_is_fine").unwrap();
}

/// Test a client cannot connect to a wep or wpa network when configured off.
#[fuchsia_async::run_singlethreaded(test)]
async fn scan_contemporary_privacy() {
    init_syslog();
    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found(&mut helper).await;
    let phy = helper.proxy();

    // Configure the scan event to return Beacon frames corresponding to each
    // BeaconInfo specified.
    let scan_event = EventHandlerBuilder::new()
        .on_start_scan(start_scan_handler(
            &phy,
            Ok(vec![
                Beacon {
                    channel: Channel::new(1, Cbw::Cbw20),
                    bssid: BSS_WPA1,
                    ssid: SSID_WPA1.clone(),
                    protection: Protection::Wpa1,
                    rssi_dbm: -30,
                },
                Beacon {
                    channel: Channel::new(1, Cbw::Cbw20),
                    bssid: BSS_WEP,
                    ssid: SSID_WEP.clone(),
                    protection: Protection::Wep,
                    rssi_dbm: -40,
                },
                Beacon {
                    channel: Channel::new(1, Cbw::Cbw20),
                    bssid: BSS_MIXED,
                    ssid: SSID_MIXED.clone(),
                    protection: Protection::Wpa1Wpa2Personal,
                    rssi_dbm: -50,
                },
            ]),
        ))
        .build();

    // Create a client controller.
    let (client_controller, _update_stream) = init_client_controller().await;

    let scan_result_list_fut = test_utils::policy_scan_for_networks(client_controller);
    pin_mut!(scan_result_list_fut);
    let scan_result_list = helper
        .run_until_complete_or_timeout(
            *SCAN_RESPONSE_TEST_TIMEOUT,
            "receive a scan response",
            scan_event,
            scan_result_list_fut,
        )
        .await;

    let expected_scan_result_list = test_utils::sort_policy_scan_result_list(vec![
        fidl_policy::ScanResult {
            id: Some(fidl_policy::NetworkIdentifier {
                ssid: SSID_MIXED.to_vec(),
                type_: fidl_policy::SecurityType::Wpa2,
            }),
            entries: Some(vec![fidl_policy::Bss {
                bssid: Some(BSS_MIXED.0.clone()),
                rssi: Some(-50),
                frequency: Some(2412),
                ..fidl_policy::Bss::EMPTY
            }]),
            compatibility: Some(fidl_policy::Compatibility::Supported),
            ..fidl_policy::ScanResult::EMPTY
        },
        fidl_policy::ScanResult {
            id: Some(fidl_policy::NetworkIdentifier {
                ssid: SSID_WEP.to_vec(),
                type_: fidl_policy::SecurityType::Wep,
            }),
            entries: Some(vec![fidl_policy::Bss {
                bssid: Some(BSS_WEP.0.clone()),
                rssi: Some(-40),
                frequency: Some(2412),
                ..fidl_policy::Bss::EMPTY
            }]),
            compatibility: Some(fidl_policy::Compatibility::DisallowedNotSupported),
            ..fidl_policy::ScanResult::EMPTY
        },
        fidl_policy::ScanResult {
            id: Some(fidl_policy::NetworkIdentifier {
                ssid: SSID_WPA1.to_vec(),
                type_: fidl_policy::SecurityType::Wpa,
            }),
            entries: Some(vec![fidl_policy::Bss {
                bssid: Some(BSS_WPA1.0.clone()),
                rssi: Some(-30),
                frequency: Some(2412),
                ..fidl_policy::Bss::EMPTY
            }]),
            compatibility: Some(fidl_policy::Compatibility::DisallowedNotSupported),
            ..fidl_policy::ScanResult::EMPTY
        },
    ]);

    // Compare one at a time for improved debuggability.
    assert_eq!(scan_result_list.len(), expected_scan_result_list.len());
    for i in 0..expected_scan_result_list.len() {
        assert_eq!(scan_result_list[i], expected_scan_result_list[i]);
    }

    helper.stop().await;
}
