// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::types::{ComponentEvent, ComponentEventChannel, EventSource},
    anyhow::{format_err, Context, Error},
    async_trait::async_trait,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::{channel::mpsc, SinkExt, TryStreamExt},
    log::error,
    std::convert::TryInto,
};

#[async_trait]
impl EventSource for fsys::EventSourceProxy {
    /// Subscribe to component lifecycle events.
    /// |node| is the node where stats about events seen will be recorded.
    async fn listen(&self, sender: mpsc::Sender<ComponentEvent>) -> Result<(), Error> {
        let (client_end, request_stream) =
            fidl::endpoints::create_request_stream::<fsys::EventStreamMarker>()?;
        let mut event_names =
            vec!["running", "started", "stopped", "diagnostics_ready"].into_iter();
        let subscription = self.subscribe(&mut event_names, client_end);
        subscription.await?.map_err(|error| format_err!("Error: {:?}", error))?;
        EventStreamServer::new(sender).spawn(request_stream);
        Ok(())
    }
}

struct EventStreamServer {
    sender: ComponentEventChannel,
}

impl EventStreamServer {
    fn new(sender: ComponentEventChannel) -> Self {
        Self { sender }
    }
}

impl EventStreamServer {
    fn spawn(self, stream: fsys::EventStreamRequestStream) {
        fasync::Task::spawn(async move {
            self.handle_request_stream(stream)
                .await
                .unwrap_or_else(|e: Error| error!("failed to run event stream server: {:?}", e));
        })
        .detach();
    }

    async fn handle_request_stream(
        mut self,
        mut stream: fsys::EventStreamRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            stream.try_next().await.context("Error running event stream server")?
        {
            match request {
                fsys::EventStreamRequest::OnEvent { event, .. } => {
                    if let Ok(event) = event.try_into() {
                        self.send(event).await;
                    }
                }
            }
        }
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
        crate::events::types::*,
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_io::NodeMarker,
        fuchsia_zircon as zx,
        futures::{future::RemoteHandle, FutureExt, StreamExt},
    };

    #[fasync::run_singlethreaded(test)]
    async fn event_stream() {
        let (source_proxy, stream_receiver) = spawn_fake_event_source();
        let (sender, mut event_stream) = mpsc::channel(CHANNEL_CAPACITY);
        source_proxy.listen(sender).await.expect("failed to listen");
        let stream_server = stream_receiver.await.into_proxy().expect("get stream proxy");

        // Send a `Started` event.
        stream_server
            .on_event(fsys::Event {
                event_type: Some(fsys::EventType::Started),
                descriptor: Some(fsys::ComponentDescriptor {
                    moniker: Some("./foo:0/bar:0".to_string()),
                    component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string()),
                }),
                timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                ..fsys::Event::empty()
            })
            .expect("send started event ok");

        // Send a `Running` event.
        stream_server
            .on_event(fsys::Event {
                event_type: Some(fsys::EventType::Running),
                descriptor: Some(fsys::ComponentDescriptor {
                    moniker: Some("./foo:0/bar:0".to_string()),
                    component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string()),
                }),
                event_result: Some(fsys::EventResult::Payload(fsys::EventPayload::Running(
                    fsys::RunningPayload { started_timestamp: Some(0) },
                ))),
                timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                ..fsys::Event::empty()
            })
            .expect("send running event ok");

        // Send a `CapabilityReady` event for diagnostics.
        let (node, _) = fidl::endpoints::create_request_stream::<NodeMarker>().unwrap();
        stream_server
            .on_event(fsys::Event {
                event_type: Some(fsys::EventType::CapabilityReady),
                descriptor: Some(fsys::ComponentDescriptor {
                    moniker: Some("./foo:0/bar:0".to_string()),
                    component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string()),
                }),
                event_result: Some(fsys::EventResult::Payload(
                    fsys::EventPayload::CapabilityReady(fsys::CapabilityReadyPayload {
                        path: Some("/diagnostics".to_string()),
                        node: Some(node),
                    }),
                )),
                timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                ..fsys::Event::empty()
            })
            .expect("send diagnostics ready event ok");

        // Send a Stopped event.
        stream_server
            .on_event(fsys::Event {
                event_type: Some(fsys::EventType::Stopped),
                descriptor: Some(fsys::ComponentDescriptor {
                    moniker: Some("./foo:0/bar:0".to_string()),
                    component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string()),
                }),
                timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                ..fsys::Event::empty()
            })
            .expect("send stopped event ok");

        let expected_component_id = ComponentIdentifier::Moniker("./foo:0/bar:0".to_string());

        let shared_data = EventMetadata {
            component_id: expected_component_id.clone(),
            component_url: "fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx".to_string(),
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

        // Assert the third received event was a CapabilityReady event for diagnostics.
        let event = event_stream.next().await.unwrap();
        match event {
            ComponentEvent::DiagnosticsReady(DiagnosticsReadyEvent {
                metadata,
                directory: Some(_),
            }) => assert_eq!(metadata.component_id, expected_component_id),
            _ => assert!(false),
        }

        // Assert the fourth received event was a Stop event.
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
        let metadata_comparator = |x: &EventMetadata, y: &EventMetadata| {
            assert_eq!(x.component_id, y.component_id);
            assert_eq!(x.component_url, y.component_url);
        };

        // Need to explicitly check every case despite the logic being the same since rust
        // requires multi-case match arms to have variable bindings be the same type in every
        // case. This isn't doable in our polymorphic event enums.
        match (event1, event2) {
            (ComponentEvent::Start(x), ComponentEvent::Start(y)) => {
                metadata_comparator(&x.metadata, &y.metadata)
            }
            (ComponentEvent::Stop(x), ComponentEvent::Stop(y)) => {
                metadata_comparator(&x.metadata, &y.metadata)
            }
            (ComponentEvent::Running(x), ComponentEvent::Running(y)) => {
                metadata_comparator(&x.metadata, &y.metadata)
            }
            (ComponentEvent::DiagnosticsReady(x), ComponentEvent::DiagnosticsReady(y)) => {
                metadata_comparator(&x.metadata, &y.metadata)
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
                        assert_eq!(
                            events,
                            vec!["running", "started", "stopped", "diagnostics_ready",]
                        );
                        responder.send(&mut Ok(())).expect("responder send ok");
                        return stream;
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
