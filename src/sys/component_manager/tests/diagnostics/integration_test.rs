// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{injectors::CapabilityInjector, matcher::EventMatcher},
    fuchsia_async as fasync,
    test_utils_lib::{echo_capability::EchoCapability, opaque_test::*},
};

#[fasync::run_singlethreaded(test)]
async fn component_manager_exposes_inspect() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/diagnostics-integration-test#meta/component-manager-inspect.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let (capability, mut echo_rx) = EchoCapability::new();
    capability.inject(&event_source, EventMatcher::ok()).await;
    event_source.start_component_tree().await;

    let message = echo_rx.next().await.map(|m| m.message.clone()).unwrap();
    assert_eq!("OK", message);
}
