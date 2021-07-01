// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_net_interfaces as fnet_interfaces,
    fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext,
};

pub async fn lowpan_driver_provision_test() {
    // Issue provision command via lowpanctl
    let args = vec!["provision".to_string(), "--name".to_string(), "some_name".to_string()];
    let () = lowpan_test_utils::lowpan_driver_test_utils::call_lowpanctl_cmd(args).await;

    // Ensure lowpan0 is created
    let interface_state =
        fuchsia_component::client::connect_to_protocol::<fnet_interfaces::StateMarker>()
            .expect("connect to interfaces");
    let stream =
        fnet_interfaces_ext::event_stream_from_state(&interface_state).expect("interface stream");
    let intf = fnet_interfaces_ext::existing(stream, std::collections::HashMap::new())
        .await
        .expect("existing interfaces");
    let _: u64 = lowpan_test_utils::lowpan_driver_test_utils::get_interface_id("lowpan0", &intf)
        .expect("getting id of lowpan0");
}
