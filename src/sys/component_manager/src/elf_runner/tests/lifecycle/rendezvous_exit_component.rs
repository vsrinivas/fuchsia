// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_test_components as test_protocol, fuchsia_component::client as component,
    tracing::info,
};

/// Connects to the Trigger protocol, sends a request, and exits.
#[fuchsia::component]
async fn main() {
    info!("Rendezvous starting");
    let trigger = component::connect_to_protocol::<test_protocol::TriggerMarker>()
        .expect("failed to connect to Trigger service");
    trigger.run().await.expect("failed to invoke Trigger");
    info!("Rendezvous complete, exiting");
}
