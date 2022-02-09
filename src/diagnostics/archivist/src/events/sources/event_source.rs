// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::events::{
    error::EventError,
    router::{Dispatcher, EventProducer},
};
use fidl_fuchsia_sys2 as fsys;
use futures::StreamExt;
use std::convert::TryInto;
use tracing::{debug, warn};

pub struct EventSource {
    request_stream: fsys::EventStreamRequestStream,
    dispatcher: Dispatcher,
}

impl EventSource {
    pub async fn new(event_source: fsys::EventSourceProxy) -> Result<Self, EventError> {
        match event_source.take_static_event_stream("EventStream").await {
            Ok(Ok(event_stream)) => Ok(Self {
                request_stream: event_stream.into_stream().unwrap(),
                dispatcher: Dispatcher::default(),
            }),
            Ok(Err(err)) => Err(EventError::ComponentError(err)),
            Err(err) => Err(err.into()),
        }
    }

    pub async fn spawn(mut self) {
        while let Some(request) = self.request_stream.next().await {
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
        warn!("EventSource stream server closed");
    }
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
    use fidl_fuchsia_io::NodeMarker;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::StreamExt;
    use std::collections::BTreeSet;

    #[fuchsia::test]
    async fn event_stream() {
        let (proxy, event_source_requests) =
            fidl::endpoints::create_proxy_and_stream::<fsys::EventSourceMarker>().unwrap();

        let events = BTreeSet::from([
            AnyEventType::General(EventType::ComponentStarted),
            AnyEventType::General(EventType::ComponentStopped),
            AnyEventType::Singleton(SingletonEventType::DiagnosticsReady),
        ]);
        let (mut event_stream, dispatcher) = Dispatcher::new_for_test(events);
        let (stream_server, _server_task) = spawn_fake_event_stream(event_source_requests);
        let mut source = EventSource::new(proxy).await.unwrap();
        source.set_dispatcher(dispatcher);
        let _task = fasync::Task::spawn(async move { source.spawn().await });

        // Send a `Started` event.
        stream_server
            .on_event(fsys::Event {
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
        stream_server
            .on_event(fsys::Event {
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
        let (node, _) = fidl::endpoints::create_request_stream::<NodeMarker>().unwrap();
        stream_server
            .on_event(fsys::Event {
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
        stream_server
            .on_event(fsys::Event {
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
        mut request_stream: fsys::EventSourceRequestStream,
    ) -> (fsys::EventStreamProxy, fasync::Task<()>) {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fsys::EventStreamMarker>().unwrap();
        let task = fasync::Task::spawn(async move {
            if let Some(Ok(request)) = request_stream.next().await {
                match request {
                    fsys::EventSourceRequest::Subscribe { .. } => {
                        unreachable!("No class to this method should ever happen");
                    }
                    fsys::EventSourceRequest::TakeStaticEventStream { responder, path } => {
                        assert_eq!(path, "EventStream");
                        responder.send(&mut Ok(server_end)).expect("responder send None");
                    }
                }
            }
        });
        (proxy, task)
    }
}
