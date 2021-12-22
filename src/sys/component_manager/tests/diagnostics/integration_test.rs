// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    test_utils_lib::opaque_test::*,
};

#[fuchsia::test]
async fn component_manager_exposes_inspect() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/diagnostics-integration-test#meta/component-manager-inspect.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker_regex("/reporter")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fuchsia::test]
async fn cleanup_of_components_without_diagnostics() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/diagnostics-integration-test#meta/cleanup-root.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker_regex("/cleanup")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
