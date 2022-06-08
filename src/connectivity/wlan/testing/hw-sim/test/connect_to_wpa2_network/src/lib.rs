// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_policy as fidl_policy, ieee80211::Bssid, wlan_common::bss::Protection,
    wlan_hw_sim::*,
};

/// Test a client can connect to a network protected by WPA2-PSK by simulating an AP that
/// authenticates, associates, as well as initiating and completing EAPOL exchange.
/// In this test, no data is being sent after the link becomes up.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_to_wpa2_network() {
    init_syslog();

    const BSS: Bssid = Bssid(*b"wpa2ok");

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let () = connect_with_security_type(
        &mut helper,
        &AP_SSID,
        &BSS,
        Some("wpa2good"),
        Protection::Wpa2Personal,
        fidl_policy::SecurityType::Wpa2,
    )
    .await;
    helper.stop().await;
}
