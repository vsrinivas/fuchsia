// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{
            CapabilityRequested, CapabilityRequestedError, Event, EventSource, EventStream,
            Resolved,
        },
        matcher::EventMatcher,
    },
    fidl::endpoints::ProtocolMarker,
    fidl_fidl_test_components as ftest, fuchsia_async as fasync,
    futures::{channel::mpsc, SinkExt, StreamExt},
    std::sync::Arc,
    test_utils_lib::trigger_capability::{TriggerCapability, TriggerReceiver},
};

// `trigger_server` does the following:
// 1. Connect to the echo capability served by the integration test.
// 2. Wait for a start trigger to arrive from `trigger_client`.
// 3. Unblock `trigger_client` once an echo has been sent to the integration
//    test.
async fn start_trigger_server(
    mut trigger_receiver: TriggerReceiver,
    mut rx: mpsc::UnboundedReceiver<()>,
) {
    let start_logging_trigger = trigger_receiver.next().await.unwrap();
    start_logging_trigger.resume();
    // These will only succeed if all EventStreams are handled.
    rx.next().await.unwrap();
    rx.next().await.unwrap();
    rx.next().await.unwrap();
}

fn run_main_event_stream(
    mut event_stream: EventStream,
    trigger_capability: Arc<TriggerCapability>,
    mut tx: mpsc::UnboundedSender<()>,
) {
    fasync::Task::spawn(async move {
        let mut capability_request =
            EventMatcher::ok().expect_match::<CapabilityRequested>(&mut event_stream).await;
        assert_eq!("./trigger_client:0", capability_request.target_moniker());
        assert_eq!(
            "fuchsia-pkg://fuchsia.com/events_integration_test#meta/static_event_stream_trigger_client.cm",
            capability_request.component_url());

        assert_eq!(
            format!("{}", ftest::TriggerMarker::NAME),
            capability_request.result().unwrap().name
        );

        if let Some(trigger_stream) = capability_request.take_capability::<ftest::TriggerMarker>() {
            trigger_capability.serve_async(trigger_stream);
            tx.send(()).await.expect("Could not send response");
        }
    }).detach();
}

fn run_second_event_stream(mut event_stream: EventStream, mut tx: mpsc::UnboundedSender<()>) {
    fasync::Task::spawn(async move {
        let capability_request =
            EventMatcher::err().expect_match::<CapabilityRequested>(&mut event_stream).await;

        // Verify that the second stream gets an error.
        match capability_request.result() {
            Err(CapabilityRequestedError { name, .. }) if name == ftest::TriggerMarker::NAME => {
                tx.send(()).await.expect("Could not send response");
            }
            _ => panic!("Incorrect event received"),
        }
    })
    .detach();
}

fn run_resolved_event_stream(mut event_stream: EventStream, mut tx: mpsc::UnboundedSender<()>) {
    fasync::Task::spawn(async move {
        EventMatcher::ok().moniker("./stub:0").expect_match::<Resolved>(&mut event_stream).await;
        tx.send(()).await.expect("Could not send response");
    })
    .detach();
}

#[fasync::run_singlethreaded]
async fn main() {
    let (capability, receiver) = TriggerCapability::new();
    let (tx, rx) = mpsc::unbounded();
    let tx1 = tx.clone();
    let tx2 = tx.clone();
    let tx3 = tx.clone();
    let event_source = EventSource::new().unwrap();

    let event_stream = event_source.take_static_event_stream("EventStream").await.unwrap();
    run_main_event_stream(event_stream, capability.clone(), tx1.clone());

    let event_stream = event_source.take_static_event_stream("second_stream").await.unwrap();
    run_second_event_stream(event_stream, tx2.clone());

    let event_stream = event_source.take_static_event_stream("resolved_stream").await.unwrap();
    run_resolved_event_stream(event_stream, tx3.clone());

    start_trigger_server(receiver, rx).await;
}
