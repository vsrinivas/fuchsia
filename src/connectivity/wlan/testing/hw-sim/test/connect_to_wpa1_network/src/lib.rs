// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl_fuchsia_wlan_policy as fidl_policy, ieee80211::Bssid, wlan_common::bss::Protection,
    wlan_hw_sim::*,
};

/// Test a client successfully connects to a network protected by WPA1-PSK.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_to_wpa1_network() {
    init_syslog();

    const BSS: Bssid = Bssid(*b"wpa1ok");

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found(&mut helper).await;

    let () = connect_with_security_type(
        &mut helper,
        &AP_SSID,
        &BSS,
        Some("wpa1good"),
        Protection::Wpa1,
        fidl_policy::SecurityType::Wpa,
    )
    .await;
    helper.stop().await;
}
