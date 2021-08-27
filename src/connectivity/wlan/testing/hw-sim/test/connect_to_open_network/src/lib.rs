// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {ieee80211::Bssid, wlan_hw_sim::*};

/// Test a client can connect to a network with no protection by simulating an AP that sends out
/// hard coded authentication and association response frames.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_to_open_network() {
    init_syslog();

    const BSS: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let () = connect_open(&mut helper, &AP_SSID, &BSS).await;
    helper.stop().await;
}
