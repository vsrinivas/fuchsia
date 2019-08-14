// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {fuchsia_async::Executor, fuchsia_zircon::DurationNum, wlan_hw_sim::*};

/// Test ethernet device is created successfully by verifying a ethernet client with specified
/// MAC address can be created succesfully.
#[test]
fn verify_ethernet() {
    let mut exec = Executor::new().expect("Failed to create an executor");
    // Make sure there is no existing ethernet device.
    let client = exec
        .run_singlethreaded(create_eth_client(&HW_MAC_ADDR))
        .expect(&format!("creating ethernet client: {:?}", &HW_MAC_ADDR));
    assert!(client.is_none());

    // Create wlan_tap device which will in turn create ethernet device.
    let _helper =
        test_utils::TestHelper::begin_test(&mut exec, create_wlantap_config_client(HW_MAC_ADDR));
    loop_until_iface_is_found(&mut exec);

    let mut retry = test_utils::RetryWithBackoff::new(5.seconds());
    loop {
        let client = exec
            .run_singlethreaded(create_eth_client(&HW_MAC_ADDR))
            .expect(&format!("creating ethernet client: {:?}", &HW_MAC_ADDR));
        if client.is_some() {
            break;
        }
        let slept = retry.sleep_unless_timed_out();
        assert!(slept, "No ethernet client with mac_addr {:?} found in time", &HW_MAC_ADDR);
    }
}
