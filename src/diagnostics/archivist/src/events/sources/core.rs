// Copyright 2020 The Fuchsia Authors. All rights reserved.
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

#[async_trait]
impl EventSource for fsys::EventSourceProxy {
    /// Subscribe to component lifecycle events.
    /// |node| is the node where stats about events seen will be recorded.
    async fn listen(&mut self, sender: mpsc::Sender<ComponentEvent>) -> Result<(), EventError> {
        let (client_end, request_stream) =
            fidl::endpoints::create_request_stream::<fsys::EventStreamMarker>()?;
        let mut events = vec![
            fsys::EventSubscription {
                event_name: Some("running".to_string()),
                mode: Some(fsys::EventMode::Async),
                ..fsys::EventSubscription::EMPTY
            },
            fsys::EventSubscription {
                event_name: Some("started".to_string()),
                mode: Some(fsys::EventMode::Async),
                ..fsys::EventSubscription::EMPTY
            },
            fsys::EventSubscription {
                event_name: Some("stopped".to_string()),
                mode: Some(fsys::EventMode::Async),
                ..fsys::EventSubscription::EMPTY
            },
            fsys::EventSubscription {
                event_name: Some("diagnostics_ready".to_string()),
                mode: Some(fsys::EventMode::Async),
                ..fsys::EventSubscription::EMPTY
            },
        ]
        .into_iter();
        let subscription = self.subscribe(&mut events, client_end);
        subscription
            .await
            .map_err(|e| EventError::Fidl("Eventsource subscribe", e))?
            .map_err(|e| EventError::FidlComponent(format!("{:?}", e)))?;
        EventStreamServer::new(sender).spawn(request_stream);
        Ok(())
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
    pub fn spawn(self, stream: fsys::EventStreamRequestStream) {
        fasync::Task::spawn(async move {
            self.handle_request_stream(stream)
                .await
                .unwrap_or_else(|e: EventError| error!(?e, "failed to run event stream server"));
        })
        .detach();
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
        // Ignore Err(SendError) result. If we fail to send it means that the archivist has been
        // stopped and therefore the receving end of this channel is closed. A send operation can
        // only fail if this is the case.
        let _ = self.sender.send(event).await;
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::{container::ComponentIdentity, events::types::*},
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_component as fcomponent,
        fidl_fuchsia_io::NodeMarker,
        fuchsia_zircon as zx,
        futures::{future::RemoteHandle, FutureExt, StreamExt},
    };

    #[fuchsia::test]
    async fn event_stream() {
        let (mut source_proxy, stream_receiver) = spawn_fake_event_source();
        let (sender, mut event_stream) = mpsc::channel(CHANNEL_CAPACITY);
        source_proxy.listen(sender).await.expect("failed to listen");
        let stream_server = stream_receiver.await.into_proxy().expect("get stream proxy");

        // Send a `Started` event.
        stream_server
            .on_event(fsys::Event {
                header: Some(fsys::EventHeader {
                    event_type: Some(fsys::EventType::Started),
                    moniker: Some("./foo:0/bar:0".to_string()),
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
                    moniker: Some("./foo:0/bar:0".to_string()),
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
                    moniker: Some("./foo:0/bar:0".to_string()),
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
                    moniker: Some("./foo:0/bar:0".to_string()),
                    component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string()),
                    timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                    ..fsys::EventHeader::EMPTY
                }),

                ..fsys::Event::EMPTY
            })
            .expect("send stopped event ok");

        let expected_component_id =
            ComponentIdentifier::parse_from_moniker("./foo:0/bar:0").unwrap();
        let expected_identity = ComponentIdentity::from_identifier_and_url(
            &expected_component_id,
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

    fn spawn_fake_event_source(
    ) -> (fsys::EventSourceProxy, RemoteHandle<ClientEnd<fsys::EventStreamMarker>>) {
        let (source, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fsys::EventSourceMarker>().unwrap();
        let (f, stream_client_end_fut) = async move {
            if let Some(request) =
                request_stream.try_next().await.expect("error running fake event source")
            {
                match request {
                    fsys::EventSourceRequest::Subscribe { events, stream, responder } => {
                        let mut events_iter = events.into_iter();
                        {
                            let subscription_request = events_iter.next().unwrap();
                            assert_eq!(subscription_request.event_name.unwrap(), "running",);
                            assert_eq!(subscription_request.mode.unwrap(), fsys::EventMode::Async,);
                        }
                        {
                            let subscription_request = events_iter.next().unwrap();
                            assert_eq!(subscription_request.event_name.unwrap(), "started",);
                            assert_eq!(subscription_request.mode.unwrap(), fsys::EventMode::Async,);
                        }
                        {
                            let subscription_request = events_iter.next().unwrap();
                            assert_eq!(subscription_request.event_name.unwrap(), "stopped",);
                            assert_eq!(subscription_request.mode.unwrap(), fsys::EventMode::Async,);
                        }
                        {
                            let subscription_request = events_iter.next().unwrap();
                            assert_eq!(
                                subscription_request.event_name.unwrap(),
                                "diagnostics_ready",
                            );
                            assert_eq!(subscription_request.mode.unwrap(), fsys::EventMode::Async,);
                        }
                        responder.send(&mut Ok(())).expect("responder send ok");
                        return stream;
                    }
                    fsys::EventSourceRequest::TakeStaticEventStream { responder, .. } => {
                        responder
                            .send(&mut Err(fcomponent::Error::ResourceUnavailable))
                            .expect("responder send None");
                    }
                }
            }
            unreachable!("This shouldn't be exercised");
        }
        .remote_handle();
        fasync::Task::spawn(f).detach();
        (source, stream_client_end_fut)
    }
}
