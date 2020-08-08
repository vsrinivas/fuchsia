// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_service::WlanMarker, fuchsia_component::client::connect_to_service,
    wlan_common::mac::Bssid, wlan_hw_sim::*,
};

/// Test a client can connect to a network protected by WPA2-PSK by simulating an AP that
/// authenticates, associates, as well as initiating and completing EAPOL exchange.
/// In this test, no data is being sent after the link becomes up.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_to_wpa2_network() {
    init_syslog();

    const BSS: Bssid = Bssid(*b"wpa2ok");
    const SSID: &[u8] = b"wpa2ssid";

    let wlan_service = connect_to_service::<WlanMarker>().expect("Connect to WLAN service");
    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let () =
        wlan_hw_sim::connect_to_wpa2_ap(&wlan_service, &mut helper, SSID, &BSS, "wpa2good").await;

    let status = wlan_service.status().await.expect("getting wlan status");
    let is_protected = true;
    assert_associated_state(status, &BSS, SSID, &CHANNEL, is_protected);
    helper.stop().await;
}
