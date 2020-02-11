// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    echo_factory_interposer::EchoFactoryInterposer,
    echo_interposer::EchoInterposer,
    fuchsia_async as fasync,
    futures::StreamExt,
    test_utils_lib::{echo_capability::EchoCapability, events::*, test_utils::*},
};

#[fasync::run_singlethreaded(test)]
async fn echo_interposer_test() -> Result<(), Error> {
    let test = BlackBoxTest::default(
        "fuchsia-pkg://fuchsia.com/breakpoints_system_integration_test#meta/interpose_echo_realm.cm",
    )
    .await?;

    let event_source = test.connect_to_event_source().await?;

    // Setup the interposer
    let (echo_interposer, mut rx) = EchoInterposer::new();
    let interposer = event_source.install_interposer(echo_interposer).await?;

    event_source.start_component_tree().await?;

    // Ensure that the string "Interposed: Hippos rule!" is sent 10 times as a response
    // from server to client.
    for _ in 1..=10 {
        let echo_string = rx.next().await.expect("local tx/rx channel was closed");
        assert_eq!(echo_string, "Interposed: Hippos rule!");
    }
    interposer.abort();

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn scoped_breakpoints_test() -> Result<(), Error> {
    let test = BlackBoxTest::default(
        "fuchsia-pkg://fuchsia.com/breakpoints_system_integration_test#meta/echo_realm.cm",
    )
    .await?;

    let event_source = test.connect_to_event_source().await?;

    // Inject an echo capability for `echo_reporter` so that we can observe its messages here.
    let mut echo_rx = {
        let receiver = event_source.subscribe(vec![RouteCapability::TYPE]).await?;

        event_source.start_component_tree().await?;

        // Wait for `echo_reporter` to attempt to connect to the Echo service
        let invocation = receiver
            .wait_until_framework_capability(
                "./echo_reporter:0",
                "/svc/fidl.examples.routing.echo.Echo",
                Some("./echo_reporter:0"),
            )
            .await?;

        // Setup the echo capability.
        let (capability, echo_rx) = EchoCapability::new();
        invocation.inject(capability).await?;
        invocation.resume().await?;

        echo_rx
    };

    // Wait to receive the start trigger that echo_reporter recieved. This
    // indicates to `echo_reporter` that it should start collecting `RouteCapability`
    // events.
    let start_trigger_echo = echo_rx.next().await.unwrap();
    assert_eq!(start_trigger_echo.message, "Start trigger");
    start_trigger_echo.resume();

    // This indicates that `echo_reporter` will stop receiving `RouteCapability`
    // events.
    let stop_trigger_echo = echo_rx.next().await.unwrap();
    assert_eq!(stop_trigger_echo.message, "Stop trigger");
    stop_trigger_echo.resume();

    // Verify that the `echo_reporter` sees `BeforeStartInstance` and
    // a `RouteCapability` event to itself (routing the ELF runner
    // capability at startup), but not other `RouteCapability` events
    // because the target of other `RouteCapability` events are outside
    // its realm.
    let events_echo = echo_rx.next().await.unwrap();
    assert_eq!(
        events_echo.message,
        concat!(
            "Events: [",
            "DrainedEvent { event_type: RouteCapability, target_moniker: \"./echo_server:0\" }, ",
            "DrainedEvent { event_type: BeforeStartInstance, target_moniker: \"./echo_server:0\" }",
            "]"
        )
    );
    events_echo.resume();

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn nested_breakpoint_test() -> Result<(), Error> {
    let test = BlackBoxTest::default(
        "fuchsia-pkg://fuchsia.com/breakpoints_system_integration_test#meta/nested_reporter.cm",
    )
    .await?;

    let event_source = test.connect_to_event_source().await?;

    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source.install_injector(capability).await?;

    event_source.start_component_tree().await?;

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

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn chained_interpose_breakpoint_test() -> Result<(), Error> {
    let test = BlackBoxTest::default(
        "fuchsia-pkg://fuchsia.com/breakpoints_system_integration_test#meta/chained_interpose_echo_realm.cm",
    )
    .await?;

    let event_source = test.connect_to_event_source().await?;

    let (capability, mut echo_rx) = EchoFactoryInterposer::new();
    let injector = event_source.install_interposer(capability).await?;

    event_source.start_component_tree().await?;

    let mut messages = vec![];
    for _ in 1..=3 {
        let message = echo_rx.next().await.unwrap();
        messages.push(message.clone());
    }
    messages.sort_unstable();
    assert_eq!(vec!["Interposed: a", "Interposed: b", "Interposed: c"], messages);
    injector.abort();

    Ok(())
}
