// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_test_components as ftest, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
};

#[fasync::run_singlethreaded]
async fn main() {
    let trigger =
        connect_to_protocol::<ftest::TriggerMarker>().expect("error connecting to trigger");

    trigger.run().await.expect("start trigger failed");
}
