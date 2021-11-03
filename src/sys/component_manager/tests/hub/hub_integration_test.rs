// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_async as fasync,
    test_utils_lib::opaque_test::*,
};

#[fasync::run_singlethreaded(test)]
async fn advanced_routing() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/advanced_routing_echo_realm.cm",
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
        .moniker_regex("./reporter:0")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn dynamic_child() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/dynamic_child_reporter.cm",
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
        .moniker_regex(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn visibility() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/visibility_reporter.cm",
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
        .moniker_regex(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn resolver() {
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/hub_integration_test#meta/resolver.cm")
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
        .moniker_regex(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
