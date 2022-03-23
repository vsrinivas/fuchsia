// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fidl_test_components as ftest, fuchsia_component::client};

#[fuchsia::test]
async fn custom_resolved_component_serves_protocol_using_sdk_protocol() {
    let trigger = client::connect_to_protocol_at_path::<ftest::TriggerMarker>(
        "/svc/fidl.test.components.Trigger.sdk",
    )
    .expect("failed to open trigger service");
    let out = trigger.run().await.expect("trigger failed");
    assert_eq!(out, "Triggered");
}

#[fuchsia::test]
async fn custom_resolved_component_serves_protocol_using_internal_protocol() {
    let trigger = client::connect_to_protocol_at_path::<ftest::TriggerMarker>(
        "/svc/fidl.test.components.Trigger.internal",
    )
    .expect("failed to open trigger service");
    let out = trigger.run().await.expect("trigger failed");
    assert_eq!(out, "Triggered");
}
