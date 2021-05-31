// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    test_utils_lib::opaque_test::*,
};

#[fasync::run_singlethreaded(test)]
async fn async_event_source_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/async_reporter.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Expect the static child to stop
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn scoped_events_test() {
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/events_integration_test#meta/echo_realm.cm")
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
        .moniker("./echo_reporter:0")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn realm_offered_event_source_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/realm_offered_root.cm",
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
        .moniker("./nested_realm:0/reporter:0")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn nested_event_source_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/nested_reporter.cm",
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
        .moniker(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

async fn expect_and_get_timestamp<T: Event>(
    event_stream: &mut EventStream,
    moniker: &str,
) -> zx::Time {
    let event = EventMatcher::ok().moniker(moniker).expect_match::<T>(event_stream).await;
    event.timestamp()
}

#[ignore]
#[fasync::run_singlethreaded(test)]
async fn event_dispatch_order_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/event_dispatch_order_root.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Discovered::NAME, Resolved::NAME],
            EventMode::Sync,
        )])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // "Discovered" is the first stage of a component's lifecycle so it must
    // be dispatched before "Resolved". Also, a child is not discovered until
    // the parent is resolved and its manifest is processed.
    let timestamp_a = expect_and_get_timestamp::<Discovered>(&mut event_stream, ".").await;
    let timestamp_b = expect_and_get_timestamp::<Resolved>(&mut event_stream, ".").await;
    let timestamp_c = expect_and_get_timestamp::<Discovered>(&mut event_stream, "./child:0").await;
    let timestamp_d = expect_and_get_timestamp::<Resolved>(&mut event_stream, "./child:0").await;

    assert!(timestamp_a < timestamp_b);
    assert!(timestamp_b < timestamp_c);
    assert!(timestamp_c < timestamp_d);
}

#[fasync::run_singlethreaded(test)]
async fn event_directory_ready() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/directory_ready_root.cm",
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
        .moniker(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn resolved_error_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/resolved_error_reporter.cm",
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
        .moniker(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn synthesis_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/synthesis_reporter.cm",
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
        .moniker(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn static_event_stream_capability_requested_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/trigger_realm.cm",
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
        .moniker("./components:0/trigger_server:0")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
