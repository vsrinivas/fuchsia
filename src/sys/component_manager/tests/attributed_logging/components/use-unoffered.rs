// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is goverened by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_test_components as ftest, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
/// Tries to connect to the Trigger service, which it should not have access
/// to. This should generate an expect log message from copmonent manager that
/// will be attributed to this component.
async fn main() {
    let trigger = match connect_to_service::<ftest::TriggerMarker>() {
        Ok(t) => t,
        Err(_) => return,
    };

    let _ = trigger.run().await;
}
