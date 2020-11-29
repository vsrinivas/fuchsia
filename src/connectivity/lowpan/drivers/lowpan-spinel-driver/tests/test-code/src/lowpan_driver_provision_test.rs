// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_net_stack::StackMarker, fuchsia_component::client::connect_to_service,
    lowpan_test_utils::lowpan_driver_test_utils::*,
};

pub async fn lowpan_driver_provision_test() {
    // Issue provision command via lowpanctl
    let args = vec!["provision".to_string(), "--name".to_string(), "some_name".to_string()];
    call_lowpanctl_cmd(args).await;

    // Ensure lowpan0 is created
    let stack = connect_to_service::<StackMarker>().expect("failed to connect to netstack");
    let intfs = stack.list_interfaces().await.expect("getting interfaces");
    let _ = get_interface_id("lowpan0", &intfs).expect("getting id of lowpan0");
}
