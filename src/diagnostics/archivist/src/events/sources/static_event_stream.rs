// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::{
        error::EventError,
        types::{ComponentEvent, ComponentEventChannel, EventSource},
    },
    async_trait::async_trait,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::{channel::mpsc, SinkExt, TryStreamExt},
    std::convert::TryInto,
    tracing::{debug, error, warn},
};

pub struct StaticEventStream {
    state: State,
}

enum State {
    ReadyToListen(Option<fsys::EventStreamRequestStream>),
    Listening(fasync::Task<()>),
}

impl StaticEventStream {
    pub fn new(stream: fsys::EventStreamRequestStream) -> Self {
        Self { state: State::ReadyToListen(Some(stream)) }
    }
}

#[async_trait]
impl EventSource for StaticEventStream {
    async fn listen(&mut self, sender: mpsc::Sender<ComponentEvent>) -> Result<(), EventError> {
        match &mut self.state {
            State::Listening(_) => Err(EventError::StreamAlreadyTaken),
            State::ReadyToListen(ref mut stream) => {
                // unwrap safe since we initialize to Some().
                debug_assert!(stream.is_some());
                let stream = stream.take().unwrap();
                self.state = State::Listening(EventStreamServer::new(sender).spawn(stream));
                Ok(())
            }
        }
    }
}

pub struct EventStreamServer {
    sender: ComponentEventChannel,
}

impl EventStreamServer {
    pub fn new(sender: ComponentEventChannel) -> Self {
        Self { sender }
    }
}

impl EventStreamServer {
    pub fn spawn(self, stream: fsys::EventStreamRequestStream) -> fasync::Task<()> {
        fasync::Task::spawn(async move {
            self.handle_request_stream(stream)
                .await
                .unwrap_or_else(|e: EventError| error!(?e, "failed to run event stream server"));
        })
    }

    async fn handle_request_stream(
        mut self,
        mut stream: fsys::EventStreamRequestStream,
    ) -> Result<(), EventError> {
        while let Some(request) =
            stream.try_next().await.map_err(|e| EventError::Fidl("EventStream stream", e))?
        {
            match request {
                fsys::EventStreamRequest::OnEvent { event, .. } => match event.try_into() {
                    Ok(event) => {
                        self.send(event).await;
                    }
                    Err(err) => {
                        debug!(?err, "Failed to interpret event");
                    }
                },
            }
        }
        warn!("EventSource stream server closed");
        Ok(())
    }

    async fn send(&mut self, event: ComponentEvent) {
        // Ignore Err(SendError) result. If we fail to send it means that the archivist has
        // been stopped and therefore the receiving end of this channel is closed. A send operation
        // can only fail if this is the case.
        let _ = self.sender.send(event).await;
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::{container::ComponentIdentity, events::types::*},
        fidl_fuchsia_io::NodeMarker,
        fidl_fuchsia_sys2::EventStreamMarker,
        fuchsia_zircon as zx,
        futures::StreamExt,
    };

    #[fuchsia::test]
    async fn event_stream() {
        let (sender, mut event_stream) = mpsc::channel(CHANNEL_CAPACITY);
        let (stream_server, event_stream_requests) =
            fidl::endpoints::create_proxy_and_stream::<EventStreamMarker>().unwrap();
        let server = EventStreamServer::new(sender);
        let _task = server.spawn(event_stream_requests);

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

        let shared_data = EventMetadata {
            identity: expected_identity.clone(),
            timestamp: zx::Time::get_monotonic(),
        };

        // Assert the first received event was a Start event.
        let event = event_stream.next().await.unwrap();
        compare_events_ignore_timestamp_and_payload(
            &event,
            &ComponentEvent::Start(StartEvent { metadata: shared_data.clone() }),
        );

        // Assert the second received event was a Running event.
        let event = event_stream.next().await.unwrap();
        compare_events_ignore_timestamp_and_payload(
            &event,
            &ComponentEvent::Running(RunningEvent {
                metadata: shared_data.clone(),
                component_start_time: zx::Time::from_nanos(0),
            }),
        );

        // Assert the third received event was a DirectoryReady event for diagnostics.
        let event = event_stream.next().await.unwrap();
        match event {
            ComponentEvent::DiagnosticsReady(DiagnosticsReadyEvent {
                metadata,
                directory: Some(_),
            }) => assert_eq!(metadata.identity, expected_identity),
            _ => assert!(false),
        }

        // Assert the last received event was a Stop event.
        let event = event_stream.next().await.unwrap();
        compare_events_ignore_timestamp_and_payload(
            &event,
            &ComponentEvent::Stop(StopEvent { metadata: shared_data.clone() }),
        );
    }

    pub fn compare_events_ignore_timestamp_and_payload(
        event1: &ComponentEvent,
        event2: &ComponentEvent,
    ) {
        // Need to explicitly check every case despite the logic being the same since rust
        // requires multi-case match arms to have variable bindings be the same type in every
        // case. This isn't doable in our polymorphic event enums.
        match (event1, event2) {
            (ComponentEvent::Start(x), ComponentEvent::Start(y)) => {
                assert_eq!(x.metadata.identity, y.metadata.identity);
            }
            (ComponentEvent::Stop(x), ComponentEvent::Stop(y)) => {
                assert_eq!(x.metadata.identity, y.metadata.identity);
            }
            (ComponentEvent::Running(x), ComponentEvent::Running(y)) => {
                assert_eq!(x.metadata.identity, y.metadata.identity);
            }
            (ComponentEvent::DiagnosticsReady(x), ComponentEvent::DiagnosticsReady(y)) => {
                assert_eq!(x.metadata.identity, y.metadata.identity);
            }
            (ComponentEvent::LogSinkRequested(x), ComponentEvent::LogSinkRequested(y)) => {
                assert_eq!(x.metadata.identity, y.metadata.identity);
            }
            _ => panic!(
                "Events are not equal, they are different enumerations: {:?}, {:?}",
                event1, event2
            ),
        }
    }
}
