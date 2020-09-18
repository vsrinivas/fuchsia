// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    echo_factory_interposer::EchoFactoryInterposer,
    echo_interposer::EchoInterposer,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::StreamExt,
    test_utils_lib::{echo_capability::EchoCapability, events::*, opaque_test::*},
};

#[fasync::run_singlethreaded(test)]
async fn async_event_source_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/async_reporter.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source.install_injector(capability, None).await.unwrap();

    event_source.start_component_tree().await;

    let mut events = vec![];
    for _ in 1..=6 {
        let event = echo_rx.next().await.unwrap();
        events.push(event.message.clone());
        event.resume();
    }
    assert_eq!(
        vec!["Started", "Started", "Started", "Destroyed", "Destroyed", "Destroyed"],
        events
    );
    injector.abort();
}

#[fasync::run_singlethreaded(test)]
async fn echo_interposer_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/interpose_echo_realm.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    // Setup the interposer
    let (echo_interposer, mut rx) = EchoInterposer::new();
    let interposer = event_source.install_interposer(echo_interposer).await.unwrap();

    event_source.start_component_tree().await;

    // Ensure that the string "Interposed: Hippos rule!" is sent 10 times as a response
    // from server to client.
    for _ in 1..=10 {
        let echo_string = rx.next().await.expect("local tx/rx channel was closed");
        assert_eq!(echo_string, "Interposed: Hippos rule!");
    }
    interposer.abort();
}

#[fasync::run_singlethreaded(test)]
async fn scoped_events_test() {
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/events_integration_test#meta/echo_realm.cm")
            .await
            .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    // Inject an echo capability for `echo_reporter` so that we can observe its messages here.
    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source
        .install_injector(capability, Some(EventMatcher::new().expect_moniker("./echo_reporter:0")))
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Wait to receive the start trigger that echo_reporter recieved. This
    // indicates to `echo_reporter` that it should start collecting `CapabilityRouted`
    // events.
    let start_trigger_echo = echo_rx.next().await.unwrap();
    assert_eq!(start_trigger_echo.message, "Start trigger");
    start_trigger_echo.resume();

    // This indicates that `echo_reporter` will stop receiving `CapabilityRouted`
    // events.
    let stop_trigger_echo = echo_rx.next().await.unwrap();
    assert_eq!(stop_trigger_echo.message, "Stop trigger");
    stop_trigger_echo.resume();

    // Verify that the `echo_reporter` sees `Started` and
    // a `CapabilityRouted` event to itself (routing the ELF runner
    // capability at startup), but not other `CapabilityRouted` events
    // because the target of other `CapabilityRouted` events are outside
    // its realm.
    let events_echo = echo_rx.next().await.unwrap();
    assert_eq!(
        events_echo.message,
        concat!(
            "Events: [",
            "EventDescriptor { event_type: Some(Started), capability_id: None, target_moniker: Some(\"./echo_server:0\"), exit_status: None }",
            "]"
        )
    );
    events_echo.resume();

    injector.abort();
}

#[fasync::run_singlethreaded(test)]
async fn realm_offered_event_source_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/realm_offered_root.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    // Inject echo capability for `root/nested_realm/reporter` so that we can observe its messages
    // here.
    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source
        .install_injector(
            capability,
            Some(EventMatcher::new().expect_moniker("./nested_realm:0/reporter:0")),
        )
        .await
        .unwrap();
    event_source.start_component_tree().await;

    // Verify that the `reporter` sees `Started` for the three components started under the
    // `nested_realm`.
    for child in vec!["a", "b", "c"] {
        let events_echo = echo_rx.next().await.unwrap();
        assert_eq!(events_echo.message, format!("./child_{}:0", child));
        events_echo.resume();
    }

    injector.abort();
}

#[fasync::run_singlethreaded(test)]
async fn nested_event_source_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/nested_reporter.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source.install_injector(capability, None).await.unwrap();

    event_source.start_component_tree().await;

    let mut children = vec![];
    for _ in 1..=3 {
        let child = echo_rx.next().await.unwrap();
        println!("child: {}", child.message);
        children.push(child.message.clone());
        child.resume();
    }
    children.sort_unstable();
    assert_eq!(vec!["./child_a:0", "./child_b:0", "./child_c:0"], children);
    injector.abort();
}

#[fasync::run_singlethreaded(test)]
async fn chained_interposer_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/chained_interpose_echo_realm.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let (capability, mut echo_rx) = EchoFactoryInterposer::new();
    let injector = event_source.install_interposer(capability).await.unwrap();

    event_source.start_component_tree().await;

    let mut messages = vec![];
    for _ in 1..=3 {
        let message = echo_rx.next().await.unwrap();
        messages.push(message.clone());
    }
    messages.sort_unstable();
    assert_eq!(vec!["Interposed: a", "Interposed: b", "Interposed: c"], messages);
    injector.abort();
}

async fn expect_and_get_timestamp<T: Event>(
    event_stream: &mut EventStream,
    moniker: &str,
) -> Result<zx::Time, Error> {
    let event = event_stream.expect_exact::<T>(EventMatcher::new().expect_moniker(moniker)).await;
    let timestamp = event.timestamp();
    event.resume().await.unwrap();
    Ok(timestamp)
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
    let mut event_stream =
        event_source.subscribe(vec![Discovered::NAME, Resolved::NAME]).await.unwrap();

    event_source.start_component_tree().await;

    // "Discovered" is the first stage of a component's lifecycle so it must
    // be dispatched before "Resolved". Also, a child is not discovered until
    // the parent is resolved and its manifest is processed.
    let timestamp_a = expect_and_get_timestamp::<Discovered>(&mut event_stream, ".").await.unwrap();
    let timestamp_b = expect_and_get_timestamp::<Resolved>(&mut event_stream, ".").await.unwrap();
    let timestamp_c =
        expect_and_get_timestamp::<Discovered>(&mut event_stream, "./child:0").await.unwrap();
    let timestamp_d =
        expect_and_get_timestamp::<Resolved>(&mut event_stream, "./child:0").await.unwrap();

    assert!(timestamp_a < timestamp_b);
    assert!(timestamp_b < timestamp_c);
    assert!(timestamp_c < timestamp_d);
}

#[fasync::run_singlethreaded(test)]
async fn event_capability_ready() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/capability_ready_root.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source.install_injector(capability, None).await.unwrap();

    event_source.start_component_tree().await;

    let mut messages = vec![];
    for _ in 0..3 {
        let event = echo_rx.next().await.unwrap();
        messages.push(event.message.clone());
        event.resume();
    }
    messages.sort_unstable();
    assert_eq!(
        vec![
            "[fuchsia-pkg://fuchsia.com/events_integration_test#meta/capability_ready_child.cm] Saw bar on ./child:0",
            "[fuchsia-pkg://fuchsia.com/events_integration_test#meta/capability_ready_child.cm] Saw foo on ./child:0",
            "[fuchsia-pkg://fuchsia.com/events_integration_test#meta/capability_ready_child.cm] error bleep on ./child:0",
        ],
        messages
    );
    injector.abort();
}

#[fasync::run_singlethreaded(test)]
async fn resolved_error_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/resolved_error_reporter.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source.install_injector(capability, None).await.unwrap();

    event_source.start_component_tree().await;

    let message = echo_rx.next().await.map(|m| m.message).unwrap();
    assert_eq!("ERROR", message);
    injector.abort();
}

#[fasync::run_singlethreaded(test)]
async fn synthesis_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/synthesis_reporter.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source.install_injector(capability, None).await.unwrap();

    event_source.start_component_tree().await;
    let mut events = vec![];
    for _ in 0..8 {
        let event = echo_rx.next().await.unwrap();
        events.push(event.message.clone());
        event.resume();
    }
    assert_eq!(
        vec![
            "Running",
            "Running",
            "Running",
            "Running",
            "CapabilityReady",
            "MarkedForDestruction",
            "MarkedForDestruction",
            "MarkedForDestruction"
        ],
        events
    );
    injector.abort();
}

#[fasync::run_singlethreaded(test)]
async fn static_event_stream_capability_requested_test() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/trigger_realm.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source.install_injector(capability, None).await.unwrap();

    event_source.start_component_tree().await;

    // Wait to receive the start trigger that echo_reporter recieved. This
    // indicates to `echo_reporter` that it should start collecting `CapabilityRouted`
    // events.
    let start_trigger_echo = echo_rx.next().await.unwrap();
    assert_eq!(start_trigger_echo.message, "Start trigger");
    start_trigger_echo.resume();

    injector.abort();
}
