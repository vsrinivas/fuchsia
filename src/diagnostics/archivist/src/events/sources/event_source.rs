// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::events::types::Event;
use crate::events::{
    error::EventError,
    router::{Dispatcher, EventProducer},
};
use anyhow::Error;
use fidl_fuchsia_sys2 as fsys;
use fsys::EventStream2Proxy;
use fuchsia_async::Task;
use fuchsia_component::client::connect_to_protocol_at_path;
use futures::channel::mpsc;
use futures::channel::mpsc::UnboundedSender;
use futures::future::join_all;
use futures::StreamExt;
use std::convert::TryInto;
use tracing::{debug, info, warn};

pub struct EventSource {
    dispatcher: Dispatcher,
    legacy_proxy: Option<fsys::EventSourceProxy>,
    event_streams: Option<Vec<EventStream2Proxy>>,
}

impl EventSource {
    pub async fn new(legacy_proxy: fsys::EventSourceProxy) -> Result<Self, Error> {
        // Connect to /events/event_stream which contains our newer FIDL protocol
        let event_streams = Some(vec![
            connect_to_protocol_at_path::<fsys::EventStream2Marker>(
                "/events/lifecycle_event_stream",
            )?,
            connect_to_protocol_at_path::<fsys::EventStream2Marker>(
                "/events/log_sink_requested_event_stream",
            )?,
            connect_to_protocol_at_path::<fsys::EventStream2Marker>(
                "/events/diagnostics_ready_event_stream",
            )?,
        ]);
        Ok(Self {
            event_streams,
            dispatcher: Dispatcher::default(),
            legacy_proxy: Some(legacy_proxy),
        })
    }

    pub async fn new_for_test(event_stream: EventStream2Proxy) -> Result<Self, EventError> {
        // Connect to /events/event_stream which contains our newer FIDL protocol
        Ok(Self {
            event_streams: Some(vec![event_stream]),
            dispatcher: Dispatcher::default(),
            legacy_proxy: None,
        })
    }

    async fn spawn_legacy(mut self, mut stream: fsys::EventStreamRequestStream) {
        while let Some(request) = stream.next().await {
            match request {
                Ok(fsys::EventStreamRequest::OnEvent { event, .. }) => match event.try_into() {
                    Ok(event) => {
                        if let Err(err) = self.dispatcher.emit(event).await {
                            if err.is_disconnected() {
                                break;
                            }
                        }
                    }
                    Err(err) => {
                        warn!(?err, "Failed to interpret event");
                    }
                },
                other => {
                    debug!(?other, "Unexpected EventStream request");
                }
            }
        }
        warn!("Legacy EventSource stream server closed.");
    }

    pub async fn spawn(mut self) -> Result<(), Error> {
        let (tx, mut rx) = mpsc::unbounded();
        let event_streams = self.event_streams.take();
        let default_value = vec![];
        let _task = Task::spawn(async move {
            let tx = tx.clone();
            let tasks = event_streams
                .map(|event_streams| {
                    let tx = tx.clone();
                    event_streams
                        .into_iter()
                        .map(|event_stream| {
                            let tx = tx.clone();
                            async move { handle_event_stream(tx, event_stream).await }
                        })
                        .collect()
                })
                .unwrap_or(default_value);
            join_all(tasks).await;
        });
        while let Some(event) = rx.next().await {
            if let Err(err) = self.dispatcher.emit(event).await {
                if err.is_disconnected() {
                    break;
                }
            }
        }
        info!("Attempting legacy fallback");
        if let Some(event_source) = self.legacy_proxy.take() {
            match event_source.take_static_event_stream("EventStream").await {
                Ok(Ok(event_stream)) => self.spawn_legacy(event_stream.into_stream()?).await,
                Ok(Err(err)) => warn!(?err, "Error taking legacy event stream"),
                Err(err) => warn!(?err, "Error taking legacy event stream"),
            }
        } else {
            warn!("Legacy fallback failed (no capability)");
        }

        Ok(())
    }
}

async fn handle_event_stream(tx: UnboundedSender<Event>, event_stream: EventStream2Proxy) {
    let tx = tx.clone();
    while let Ok(events) = event_stream.get_next().await {
        for event in events {
            match event.try_into() {
                Ok(event) => {
                    if tx.unbounded_send(event).is_err() {
                        return;
                    }
                }
                Err(EventError::UnknownResult(fsys::EventResult::Error(fsys::EventError {
                    error_payload:
                        Some(fsys::EventErrorPayload::DirectoryReady(fsys::DirectoryReadyError {
                            ..
                        })),
                    ..
                }))) => {
                    // The error was intended for cases when a component
                    // declared it exposes inspect but doesn't actually serve it. Turns out this is
                    // not very common and leads to spam in tests that end up having to include the
                    // inspect shard transitevely and correctly do not expose inspect.
                    // Instead of logging a spammy and confusing error, ignore it, with RFC168 this
                    // error will be obsolete also.
                }
                Err(err) => {
                    warn!(?err, "Failed to interpret event");
                }
            }
        }
    }
    warn!("EventSourceV2 stream server closed");
}

impl EventProducer for EventSource {
    fn set_dispatcher(&mut self, dispatcher: Dispatcher) {
        self.dispatcher = dispatcher;
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::{events::types::*, identity::ComponentIdentity};
    use fidl_fuchsia_io as fio;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::{channel::mpsc::UnboundedSender, StreamExt};
    use std::collections::BTreeSet;

    #[fuchsia::test]
    async fn event_stream() {
        let events = BTreeSet::from([
            AnyEventType::General(EventType::ComponentStarted),
            AnyEventType::General(EventType::ComponentStopped),
            AnyEventType::Singleton(SingletonEventType::DiagnosticsReady),
        ]);
        let (mut event_stream, dispatcher) = Dispatcher::new_for_test(events);
        let (stream_server, _server_task, sender) = spawn_fake_event_stream();
        let mut source = EventSource::new_for_test(stream_server).await.unwrap();
        source.set_dispatcher(dispatcher);
        let _task = fasync::Task::spawn(async move { source.spawn().await });

        // Send a `Started` event.
        sender
            .unbounded_send(fsys::Event {
                header: Some(fsys::EventHeader {
                    event_type: Some(fsys::EventType::Started),
                    moniker: Some("./foo/bar".to_string()),
                    component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string()),
                    timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                    ..fsys::EventHeader::EMPTY
                }),
                ..fsys::Event::EMPTY
            })
            .expect("send started event ok");

        // Send a `Running` event.
        sender
            .unbounded_send(fsys::Event {
                header: Some(fsys::EventHeader {
                    event_type: Some(fsys::EventType::Running),
                    moniker: Some("./foo/bar".to_string()),
                    component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string()),
                    timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                    ..fsys::EventHeader::EMPTY
                }),
                event_result: Some(fsys::EventResult::Payload(fsys::EventPayload::Running(
                    fsys::RunningPayload {
                        started_timestamp: Some(0),
                        ..fsys::RunningPayload::EMPTY
                    },
                ))),
                ..fsys::Event::EMPTY
            })
            .expect("send running event ok");

        // Send a `DirectoryReady` event for diagnostics.
        let (node, _) = fidl::endpoints::create_request_stream::<fio::NodeMarker>().unwrap();
        sender
            .unbounded_send(fsys::Event {
                header: Some(fsys::EventHeader {
                    event_type: Some(fsys::EventType::DirectoryReady),
                    moniker: Some("./foo/bar".to_string()),
                    component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string()),
                    timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                    ..fsys::EventHeader::EMPTY
                }),
                event_result: Some(fsys::EventResult::Payload(fsys::EventPayload::DirectoryReady(
                    fsys::DirectoryReadyPayload {
                        name: Some("diagnostics".to_string()),
                        node: Some(node),
                        ..fsys::DirectoryReadyPayload::EMPTY
                    },
                ))),
                ..fsys::Event::EMPTY
            })
            .expect("send diagnostics ready event ok");

        // Send a Stopped event.
        sender
            .unbounded_send(fsys::Event {
                header: Some(fsys::EventHeader {
                    event_type: Some(fsys::EventType::Stopped),
                    moniker: Some("./foo/bar".to_string()),
                    component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string()),
                    timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                    ..fsys::EventHeader::EMPTY
                }),

                ..fsys::Event::EMPTY
            })
            .expect("send stopped event ok");

        let expected_component_id = ComponentIdentifier::parse_from_moniker("./foo/bar").unwrap();
        let expected_identity = ComponentIdentity::from_identifier_and_url(
            expected_component_id,
            "fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx",
        );

        // Assert the first received event was a Start event.
        let event = event_stream.next().await.unwrap();
        compare_events_ignore_timestamp_and_payload(
            event,
            Event {
                timestamp: zx::Time::get_monotonic(),
                payload: EventPayload::ComponentStarted(ComponentStartedPayload {
                    component: expected_identity.clone(),
                }),
            },
        );

        // Assert the second received event was a Running event.
        let event = event_stream.next().await.unwrap();
        compare_events_ignore_timestamp_and_payload(
            event,
            Event {
                timestamp: zx::Time::from_nanos(0),
                payload: EventPayload::ComponentStarted(ComponentStartedPayload {
                    component: expected_identity.clone(),
                }),
            },
        );

        // Assert the third received event was a DirectoryReady event for diagnostics.
        let event = event_stream.next().await.unwrap();
        match event.payload {
            EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                component,
                directory: Some(_),
            }) => assert_eq!(component, expected_identity),
            _ => assert!(false),
        }

        // Assert the last received event was a Stop event.
        let event = event_stream.next().await.unwrap();
        compare_events_ignore_timestamp_and_payload(
            event,
            Event {
                timestamp: zx::Time::get_monotonic(),
                payload: EventPayload::ComponentStopped(ComponentStoppedPayload {
                    component: expected_identity.clone(),
                }),
            },
        );
    }

    pub fn compare_events_ignore_timestamp_and_payload(event1: Event, event2: Event) {
        // Need to explicitly check every case despite the logic being the same since rust
        // requires multi-case match arms to have variable bindings be the same type in every
        // case. This isn't doable in our polymorphic event enums.
        match (event1.payload, event2.payload) {
            (
                EventPayload::ComponentStarted(ComponentStartedPayload { component: x, .. }),
                EventPayload::ComponentStarted(ComponentStartedPayload { component: y, .. }),
            ) => {
                assert_eq!(x, y);
            }
            (
                EventPayload::ComponentStopped(ComponentStoppedPayload { component: x, .. }),
                EventPayload::ComponentStopped(ComponentStoppedPayload { component: y, .. }),
            ) => {
                assert_eq!(x, y);
            }
            (a, b) => unreachable!("({:?}, {:?}) should never be compared here", a, b),
        }
    }

    fn spawn_fake_event_stream(
    ) -> (fsys::EventStream2Proxy, fasync::Task<()>, UnboundedSender<fsys::Event>) {
        let (sender, mut receiver) = futures::channel::mpsc::unbounded();
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fsys::EventStream2Marker>().unwrap();
        let task = fasync::Task::spawn(async move {
            let mut request_stream = server_end.into_stream().unwrap();
            loop {
                if let Some(Ok(request)) = request_stream.next().await {
                    match request {
                        fsys::EventStream2Request::GetNext { responder } => {
                            if let Some(event) = receiver.next().await {
                                responder.send(&mut vec![event].into_iter()).unwrap();
                            } else {
                                break;
                            }
                        }
                        fsys::EventStream2Request::WaitForReady { responder } => {
                            responder.send().unwrap();
                        }
                    }
                }
            }
        });
        (proxy, task, sender)
    }
}
