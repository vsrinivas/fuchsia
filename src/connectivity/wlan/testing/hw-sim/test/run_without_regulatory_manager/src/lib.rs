// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_policy as fidl_policy,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon::DurationNum,
    pin_utils::pin_mut,
    wlan_hw_sim::*,
};

/// Test that we can connect to the policy service to discover a client interface when the
/// RegulatoryRegionWatcher cannot be reached.
#[fuchsia_async::run_singlethreaded(test)]
async fn run_without_regulatory_manager() {
    init_syslog();
    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found(&mut helper).await;

    // Connect to the client policy service and get a client controller.
    let policy_provider = connect_to_protocol::<fidl_policy::ClientProviderMarker>()
        .expect("connecting to wlan policy");
    let (client_controller, server_end) = create_proxy().expect("creating client controller");
    let (update_client_end, _update_server_end) =
        create_endpoints().expect("creating client listener");
    let () =
        policy_provider.get_controller(server_end, update_client_end).expect("getting controller");

    // Issue a scan request to verify that the scan module can function in the absence of the
    // regulatory manager.
    let phy = helper.proxy();
    let scan_event =
        EventHandlerBuilder::new().on_start_scan(ScanResults::new(&phy, vec![])).build();
    let fut = async move {
        let (scan_proxy, server_end) = create_proxy().unwrap();
        client_controller.scan_for_networks(server_end).expect("requesting scan");
        loop {
            let result = scan_proxy.get_next().await.expect("getting scan results");
            let new_scan_results = result.expect("scanning failed");
            if new_scan_results.is_empty() {
                break;
            }
        }
        return;
    };
    pin_mut!(fut);
    helper
        .run_until_complete_or_timeout(70.seconds(), "receive a scan response", scan_event, fut)
        .await;

    helper.stop().await;
}
