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
        fasync::spawn(async move {
            self.handle_request_stream(stream)
                .await
                .unwrap_or_else(|e: Error| error!("failed to run event stream server: {:?}", e));
        });
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
mod tests {
    use {
        super::*,
        crate::events::types::*,
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_io::NodeMarker,
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
                target_moniker: Some("./foo:0/bar:0".to_string()),
                ..fsys::Event::empty()
            })
            .expect("send started event ok");

        // Send a `Running` event.
        stream_server
            .on_event(fsys::Event {
                event_type: Some(fsys::EventType::Running),
                target_moniker: Some("./foo:0/bar:0".to_string()),
                ..fsys::Event::empty()
            })
            .expect("send running event ok");

        // Send a `CapabilityReady` event for diagnostics.
        let (node, _) = fidl::endpoints::create_request_stream::<NodeMarker>().unwrap();
        stream_server
            .on_event(fsys::Event {
                event_type: Some(fsys::EventType::CapabilityReady),
                target_moniker: Some("./foo:0/bar:0".to_string()),
                event_result: Some(fsys::EventResult::Payload(
                    fsys::EventPayload::CapabilityReady(fsys::CapabilityReadyPayload {
                        path: Some("/diagnostics".to_string()),
                        node: Some(node),
                    }),
                )),
                ..fsys::Event::empty()
            })
            .expect("send diagnostics ready event ok");

        // Send a Stopped event.
        stream_server
            .on_event(fsys::Event {
                event_type: Some(fsys::EventType::Stopped),
                target_moniker: Some("./foo:0/bar:0".to_string()),
                ..fsys::Event::empty()
            })
            .expect("send stopped event ok");

        let expected_component_id = ComponentIdentifier::Moniker("./foo:0/bar:0".to_string());

        // Assert the first received event was a Start event.
        let event = event_stream.next().await.unwrap();
        assert_eq!(
            event,
            ComponentEvent::Start(ComponentEventData {
                component_id: expected_component_id.clone(),
                component_data_map: None
            })
        );

        // Assert the second received event was a Running event.
        let event = event_stream.next().await.unwrap();
        assert_eq!(
            event,
            ComponentEvent::Start(ComponentEventData {
                component_id: expected_component_id.clone(),
                component_data_map: None
            })
        );

        // Assert the third received event was a CapabilityReady event for diagnostics.
        let event = event_stream.next().await.unwrap();
        match event {
            ComponentEvent::DiagnosticsReady(InspectReaderData {
                component_id,
                data_directory_proxy: Some(_),
            }) => assert_eq!(component_id, expected_component_id),
            _ => assert!(false),
        }

        // Assert the fourth received event was a Stop event.
        let event = event_stream.next().await.unwrap();
        assert_eq!(
            event,
            ComponentEvent::Stop(ComponentEventData {
                component_id: expected_component_id.clone(),
                component_data_map: None
            })
        );
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
        fasync::spawn(f);
        (source, stream_client_end_fut)
    }
}
