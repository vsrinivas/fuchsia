// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::ServiceMarker,
    fidl_fidl_examples_routing_echo as fecho, fidl_fidl_test_components as ftest,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    futures::{channel::mpsc, SinkExt, StreamExt},
    std::sync::Arc,
    test_utils_lib::{
        events::{CapabilityRequested, CapabilityRequestedError, Event, EventStream},
        matcher::EventMatcher,
        trigger_capability::{TriggerCapability, TriggerReceiver},
    },
};

// `trigger_server` does the following:
// 1. Connect to the echo capability served by the integration test.
// 2. Wait for a start trigger to arrive from `trigger_client`.
// 3. Unblock `trigger_client` once an echo has been sent to the integration
//    test.
async fn start_trigger_server(
    mut trigger_receiver: TriggerReceiver,
    mut rx: mpsc::UnboundedReceiver<()>,
) -> Result<(), Error> {
    let echo = connect_to_service::<fecho::EchoMarker>()?;

    let start_logging_trigger = trigger_receiver.next().await.unwrap();
    let _ = echo.echo_string(Some("Start trigger")).await?;
    start_logging_trigger.resume();

    // These will only succeed if both EventStreams are handled.
    rx.next().await.unwrap();
    rx.next().await.unwrap();

    Ok(())
}

fn run_main_event_stream(
    stream: fsys::EventStreamRequestStream,
    trigger_capability: Arc<TriggerCapability>,
    mut tx: mpsc::UnboundedSender<()>,
) {
    fasync::Task::spawn(async move {
        let mut event_stream = EventStream::new(stream);
        let mut capability_request =
            EventMatcher::ok().expect_match::<CapabilityRequested>(&mut event_stream).await;
        assert_eq!(".\\trigger_server:0/trigger_client:0", capability_request.target_moniker());
        assert_eq!(
            "fuchsia-pkg://fuchsia.com/events_integration_test#meta/static_event_stream_trigger_client.cm",
            capability_request.component_url());
        assert_eq!(
            format!("/svc/{}", ftest::TriggerMarker::NAME),
            capability_request.unwrap_payload().path
        );
        if let Some(trigger_stream) = capability_request.take_capability::<ftest::TriggerMarker>() {
            trigger_capability.serve_async(trigger_stream);
            tx.send(()).await.expect("Could not send response");
        }
    }).detach();
}

fn run_second_event_stream(
    stream: fsys::EventStreamRequestStream,
    mut tx: mpsc::UnboundedSender<()>,
) {
    fasync::Task::spawn(async move {
        let mut event_stream = EventStream::new(stream);
        let capability_request =
            EventMatcher::err().expect_match::<CapabilityRequested>(&mut event_stream).await;
        let trigger_path = format!("/svc/{}", ftest::TriggerMarker::NAME);
        // Verify that the second stream gets an error.
        match capability_request.result {
            Err(CapabilityRequestedError { path, .. }) if path == trigger_path => {
                tx.send(()).await.expect("Could not send response");
            }
            _ => panic!("Incorrect event received"),
        }
    })
    .detach();
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new()?;
    let mut fs = ServiceFs::new_local();
    let (capability, receiver) = TriggerCapability::new();
    let (tx, rx) = mpsc::unbounded();
    let tx1 = tx.clone();
    let tx2 = tx.clone();
    fs.dir("svc")
        .add_fidl_service(move |stream| {
            run_main_event_stream(stream, capability.clone(), tx1.clone());
        })
        .add_fidl_service_at("second_stream".to_string(), move |stream| {
            run_second_event_stream(stream, tx2.clone());
        });
    fs.take_and_serve_directory_handle()?;
    let fut = async move {
        fasync::Task::local(async move {
            fs.collect::<()>().await;
        })
        .detach();
        start_trigger_server(receiver, rx).await.expect("failed running trigger_server");
    };
    executor.run_singlethreaded(fut);

    Ok(())
}
