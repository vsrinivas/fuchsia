// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fidl_test_components as ftest, fuchsia_component::client, tracing::*};

#[fuchsia::component]
async fn main() {
    // This root component connects to the trigger.
    // The trigger checks all component-manager-namespace capabilities routed to it.
    info!("Started");
    let trigger = client::connect_to_protocol::<ftest::TriggerMarker>()
        .expect("failed to open trigger service");
    info!("Invoking trigger");
    let out = trigger.run().await.expect(&format!("trigger failed"));
    assert_eq!(out, "Triggered");
    info!("Done");
}
