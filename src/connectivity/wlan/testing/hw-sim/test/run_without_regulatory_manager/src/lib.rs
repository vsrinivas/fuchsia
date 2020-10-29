// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_policy as wlan_policy,
    fuchsia_component::client::connect_to_service,
    wlan_hw_sim::*,
};

/// Test that we can connect to the policy service to discover a client interface when the
/// RegulatoryRegionWatcher cannot be reached.
#[fuchsia_async::run_singlethreaded(test)]
async fn run_without_regulatory_manager() {
    init_syslog();
    let helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    // Connect to the client policy service and get a client controller.
    let policy_provider = connect_to_service::<wlan_policy::ClientProviderMarker>()
        .expect("connecting to wlan policy");
    let (client_controller, server_end) = create_proxy().expect("creating client controller");
    let (update_client_end, _update_server_end) =
        create_endpoints().expect("creating client listener");
    let () =
        policy_provider.get_controller(server_end, update_client_end).expect("getting controller");

    // Issue a scan request to verify that the scan module can function in the absence of the
    // regulatory manager.
    let (scan_proxy, server_end) = create_proxy().unwrap();
    client_controller.scan_for_networks(server_end).expect("requesting scan");
    scan_proxy.get_next().await.expect("getting scan results").expect("scan failed");

    helper.stop().await;
}
