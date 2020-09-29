// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    futures::StreamExt,
    test_utils_lib::{
        events::{CapabilityRouted, Event, EventSource, Started},
        log::EventLog,
        trigger_capability::{TriggerCapability, TriggerReceiver},
    },
};

// `echo_reporter` does the following:
// 1. Connect to the echo capability served the integration test.
// 2. Wait for a start trigger to arrive from `echo_client` to indicate when to start
//    soaking events in an `EventLog`.
// 3. Unblock `echo_client` once the `EventLog` has been created and starts soaking events.
// 4. Waits for a stop trigger to arrive from `echo_client` to indicate when to flush
//    the EventLog. This trigger is sent after the `echo_client` has sent a number of
//    messages over the Echo protocol.
// 5. Sends out a string representation of the Events captured by the `EventLog` to the
//    integration test.
async fn start_echo_reporter(mut trigger_receiver: TriggerReceiver) -> Result<(), Error> {
    // Connect to the echo capability served by the integration test.
    // TODO(fsamuel): It's a bit confusing to have two implementations of the echo capability:
    // one by `echo_server` and one by the integration test. We really want some kind of
    // event reporting system.
    let echo = connect_to_service::<fecho::EchoMarker>()?;

    let start_logging_trigger = trigger_receiver.next().await.unwrap();
    let _ = echo.echo_string(Some("Start trigger")).await?;

    // Subscribe to relevant events.
    let mut event_source = EventSource::new_sync()?;
    let event_log =
        EventLog::record_events(&mut event_source, vec![Started::NAME, CapabilityRouted::NAME])
            .await?;

    event_source.start_component_tree().await;

    start_logging_trigger.resume();

    let stop_logging_trigger = trigger_receiver.next().await.unwrap();
    let _ = echo.echo_string(Some("Stop trigger")).await?;
    let events = event_log.flush().await;
    let _ = echo.echo_string(Some(&format!("Events: {:?}", events))).await?;
    stop_logging_trigger.resume();

    Ok(())
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new()?;
    let mut fs = ServiceFs::new_local();
    let (capability, receiver) = TriggerCapability::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        capability.clone().serve_async(stream);
    });
    fs.take_and_serve_directory_handle()?;
    let fut = async move {
        fasync::Task::local(async move {
            fs.collect::<()>().await;
        })
        .detach();
        start_echo_reporter(receiver).await.expect("failed running echo_reporter");
    };
    executor.run_singlethreaded(fut);

    Ok(())
}
