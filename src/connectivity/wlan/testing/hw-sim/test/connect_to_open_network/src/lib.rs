// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_service::WlanMarker, fuchsia_component::client::connect_to_service,
    wlan_common::mac::Bssid, wlan_hw_sim::*,
};

/// Test a client can connect to a network with no protection by simulating an AP that sends out
/// hard coded authentication and association response frames.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_to_open_network() {
    init_syslog();

    const BSS: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);
    const SSID: &[u8] = b"open";

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let wlan_service =
        connect_to_service::<WlanMarker>().expect("Failed to connect to wlan service");

    let is_protected = false;
    let () = wlan_hw_sim::connect_to_open_ap(&wlan_service, &mut helper, SSID, &BSS).await;
    let status = wlan_service.status().await.expect("getting wlan status");
    assert_associated_state(status, &BSS, SSID, &CHANNEL, is_protected);
    helper.stop().await;
}
