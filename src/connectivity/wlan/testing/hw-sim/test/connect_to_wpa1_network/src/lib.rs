// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {wlan_common::mac::Bssid, wlan_hw_sim::*};

/// Test a client successfully connects to a network protected by WPA1-PSK.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_to_wpa1_network() {
    init_syslog();

    const BSS: Bssid = Bssid(*b"wpa1ok");

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let () = connect_deprecated_wpa1(&mut helper, &AP_SSID, &BSS, "wpa1good").await;
    helper.stop().await;
}
