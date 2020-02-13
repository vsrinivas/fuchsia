// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon::DurationNum, wlan_hw_sim::*};

/// Test ethernet device is created successfully by verifying a ethernet client with specified
/// MAC address can be created succesfully.
#[fuchsia_async::run_singlethreaded(test)]
async fn verify_ethernet() {
    // Make sure there is no existing ethernet device.
    let client = create_eth_client(&CLIENT_MAC_ADDR)
        .await
        .expect(&format!("creating ethernet client: {:?}", &CLIENT_MAC_ADDR));
    assert!(client.is_none());

    // Create wlan_tap device which will in turn create ethernet device.
    let _helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let mut retry = test_utils::RetryWithBackoff::infinite_with_max_interval(5.seconds());
    loop {
        let client = create_eth_client(&CLIENT_MAC_ADDR)
            .await
            .expect(&format!("creating ethernet client: {:?}", &CLIENT_MAC_ADDR));
        if client.is_some() {
            break;
        }
        let slept = retry.sleep_unless_timed_out().await;
        assert!(slept, "No ethernet client with mac_addr {:?} found in time", &CLIENT_MAC_ADDR);
    }
}
