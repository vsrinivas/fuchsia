// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    fidl_fuchsia_wlan_service::WlanMarker, fuchsia_async::Executor,
    fuchsia_component::client::connect_to_service, wlan_hw_sim::*,
};

/// Test a client can connect to a network protected by WPA2-PSK by simulating an AP that
/// authenticates, associates, as well as initiating and completing EAPOL exchange.
/// In this test, no data is being sent after the link becomes up.
#[test]
fn connect_to_wpa2_network() {
    const BSS: &[u8; 6] = b"wpa2ok";
    const SSID: &[u8] = b"wpa2ssid";

    let mut exec = Executor::new().expect("Create executor");
    let wlan_service = connect_to_service::<WlanMarker>().expect("Connect to WLAN service");
    let mut helper =
        test_utils::TestHelper::begin_test(&mut exec, create_wlantap_config_client(HW_MAC_ADDR));
    loop_until_iface_is_found(&mut exec);

    let phy = helper.proxy();
    connect(&mut exec, &wlan_service, &phy, &mut helper, SSID, BSS, Some(&"wpa2good"));

    let status = status(&mut exec, &wlan_service);
    let is_protected = true;
    assert_associated_state(status, &BSS, SSID, &CHANNEL, is_protected);
}
