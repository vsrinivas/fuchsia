// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    breakpoint_system_client::{
        BeforeStartInstance, BreakpointSystemClient, Invocation, RouteCapability,
    },
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    futures::StreamExt,
    trigger_capability::{TriggerCapability, TriggerReceiver},
};

// `echo_reporter` does the following:
// 1. Connect to the echo capability served the integration test.
// 2. Wait for a start trigger to arrive from `echo_client` to indicate when to start
//    soaking events in an `EventSink`.
// 3. Unblock `echo_client` once the `EventSink` has been created and starts soaking events.
// 4. Waits for a stop trigger to arrive from `echo_client` to indicate when to drain
//    the EventSink. This trigger is sent after the `echo_client` has sent a number of
//    messages over the Echo protocol.
// 5. Sends out a string representation of the Events captured by the `EventSink` to the
//    integration test.
async fn start_echo_reporter(mut trigger_receiver: TriggerReceiver) -> Result<(), Error> {
    // Connect to the echo capability served by the integration test.
    // TODO(fsamuel): It's a bit confusing to have two implementations of the echo capability:
    // one by `echo_server` and one by the integration test. We really want some kind of
    // event reporting system.
    let echo = connect_to_service::<fecho::EchoMarker>().expect("error connecting to echo");

    let start_logging_trigger = trigger_receiver.next().await.unwrap();
    let _ = echo.echo_string(Some("Start trigger")).await.expect("echo_string failed");

    // Register breakpoints for relevant events
    let breakpoint_system = BreakpointSystemClient::new()?;
    let sink = breakpoint_system
        .soak_events(vec![BeforeStartInstance::TYPE, RouteCapability::TYPE])
        .await?;

    start_logging_trigger.resume();

    let stop_logging_trigger = trigger_receiver.next().await.unwrap();
    let _ = echo.echo_string(Some("Stop trigger")).await.expect("echo_string failed");
    let events = sink.drain().await;
    let _ =
        echo.echo_string(Some(&format!("Events: {:?}", events))).await.expect("echo_string failed");
    stop_logging_trigger.resume();

    Ok(())
}

fn main() {
    let mut executor = fasync::Executor::new().expect("error creating executor");
    let mut fs = ServiceFs::new_local();
    let (capability, receiver) = TriggerCapability::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        capability.serve_async(stream);
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    let fut = async move {
        fasync::spawn_local(async move {
            fs.collect::<()>().await;
        });
        start_echo_reporter(receiver).await.expect("failed running echo_reporter");
    };
    executor.run_singlethreaded(fut);
}
