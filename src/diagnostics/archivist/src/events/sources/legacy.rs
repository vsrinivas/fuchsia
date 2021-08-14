// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::{error::EventError, types::*},
    async_trait::async_trait,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys_internal::{
        ComponentEventListenerMarker, ComponentEventListenerRequest,
        ComponentEventListenerRequestStream, ComponentEventProviderProxy, SourceIdentity,
    },
    fuchsia_async as fasync,
    futures::{channel::mpsc, SinkExt, TryStreamExt},
    std::{convert::TryInto, ops::Deref},
    tracing::error,
};

pub struct ComponentEventProvider {
    proxy: ComponentEventProviderProxy,
    task: Option<fasync::Task<()>>,
}

impl From<ComponentEventProviderProxy> for ComponentEventProvider {
    fn from(proxy: ComponentEventProviderProxy) -> Self {
        Self { proxy, task: None }
    }
}

impl Deref for ComponentEventProvider {
    type Target = ComponentEventProviderProxy;

    fn deref(&self) -> &Self::Target {
        &self.proxy
    }
}

#[async_trait]
impl EventSource for ComponentEventProvider {
    /// Subscribe to component lifecycle events.
    /// |node| is the node where stats about events seen will be recorded.
    async fn listen(&mut self, sender: mpsc::Sender<ComponentEvent>) -> Result<(), EventError> {
        let (events_client_end, listener_request_stream) =
            fidl::endpoints::create_request_stream::<ComponentEventListenerMarker>()?;
        self.set_listener(events_client_end)
            .map_err(|e| EventError::Fidl("set component event provider listener", e))?;
        self.task = Some(EventListenerServer::new(sender).spawn(listener_request_stream));
        Ok(())
    }
}

struct EventListenerServer {
    sender: mpsc::Sender<ComponentEvent>,
}

impl EventListenerServer {
    fn new(sender: ComponentEventChannel) -> Self {
        Self { sender }
    }

    fn spawn(self, stream: ComponentEventListenerRequestStream) -> fasync::Task<()> {
        fasync::Task::spawn(async move {
            self.handle_request_stream(stream)
                .await
                .unwrap_or_else(|e| error!(?e, "failed to run v1 events processing server"));
        })
    }

    async fn handle_request_stream(
        mut self,
        mut stream: ComponentEventListenerRequestStream,
    ) -> Result<(), EventError> {
        while let Some(request) = stream
            .try_next()
            .await
            .map_err(|e| EventError::Fidl("ComponentEventListener stream", e))?
        {
            match request {
                ComponentEventListenerRequest::OnStart { component, .. } => {
                    self.handle_on_start(component).await?;
                }
                ComponentEventListenerRequest::OnDiagnosticsDirReady {
                    component,
                    directory,
                    ..
                } => {
                    self.handle_on_directory_ready(component, directory).await?;
                }
                ComponentEventListenerRequest::OnStop { component, .. } => {
                    self.handle_on_stop(component).await?;
                }
            }
        }
        Ok(())
    }

    async fn handle_on_start(&mut self, component: SourceIdentity) -> Result<(), EventError> {
        let metadata: EventMetadata = component.try_into()?;

        let start_event = StartEvent { metadata };

        self.send_event(ComponentEvent::Start(start_event)).await;

        Ok(())
    }

    async fn handle_on_stop(&mut self, component: SourceIdentity) -> Result<(), EventError> {
        let metadata: EventMetadata = component.try_into()?;

        let stop_event = StopEvent { metadata };

        self.send_event(ComponentEvent::Stop(stop_event)).await;
        Ok(())
    }

    async fn handle_on_directory_ready(
        &mut self,
        component: SourceIdentity,
        directory: fidl::endpoints::ClientEnd<DirectoryMarker>,
    ) -> Result<(), EventError> {
        let metadata: EventMetadata = component.try_into()?;

        let diagnostics_ready_event_data =
            DiagnosticsReadyEvent { metadata, directory: directory.into_proxy().ok() };

        self.send_event(ComponentEvent::DiagnosticsReady(diagnostics_ready_event_data)).await;

        Ok(())
    }

    async fn send_event(&mut self, event: ComponentEvent) {
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
        crate::{container::ComponentIdentity, events::sources::core::tests::*},
        fidl_fuchsia_io::DirectoryMarker,
        fidl_fuchsia_sys_internal::{
            ComponentEventProviderMarker, ComponentEventProviderRequest, SourceIdentity,
        },
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::{channel::oneshot, StreamExt, TryStreamExt},
        lazy_static::lazy_static,
    };

    lazy_static! {
        static ref MOCK_URL: String = "NO-OP URL".to_string();
    }

    #[derive(Clone)]
    struct ClonableSourceIdentity {
        realm_path: Vec<String>,
        component_name: String,
        instance_id: String,
    }

    impl Into<SourceIdentity> for ClonableSourceIdentity {
        fn into(self) -> SourceIdentity {
            SourceIdentity {
                realm_path: Some(self.realm_path),
                component_url: Some(MOCK_URL.clone()),
                component_name: Some(self.component_name),
                instance_id: Some(self.instance_id),
                ..SourceIdentity::EMPTY
            }
        }
    }

    impl Into<EventMetadata> for ClonableSourceIdentity {
        fn into(self) -> EventMetadata {
            let mut moniker = self.realm_path;
            moniker.push(self.component_name);
            EventMetadata {
                identity: ComponentIdentity::from_identifier_and_url(
                    &ComponentIdentifier::Legacy {
                        moniker: moniker.into(),
                        instance_id: self.instance_id,
                    },
                    &*MOCK_URL,
                ),
                timestamp: zx::Time::from_nanos(0),
            }
        }
    }

    #[fuchsia::test]
    async fn component_event_stream() {
        let (mut provider_proxy, listener_receiver) = spawn_fake_component_event_provider();
        let (sender, receiver) = mpsc::channel(CHANNEL_CAPACITY);
        provider_proxy.listen(sender).await.expect("failed to listen");
        let mut event_stream = receiver.boxed();
        let listener = listener_receiver
            .await
            .expect("failed to receive listener")
            .into_proxy()
            .expect("failed to get listener proxy");

        let identity: ClonableSourceIdentity = ClonableSourceIdentity {
            realm_path: vec!["root".to_string(), "a".to_string()],
            component_name: "test.cmx".to_string(),
            instance_id: "12345".to_string(),
        };
        listener.on_start(identity.clone().into()).expect("failed to send event 1");
        let (dir, _) = fidl::endpoints::create_request_stream::<DirectoryMarker>().unwrap();
        listener
            .on_diagnostics_dir_ready(identity.clone().into(), dir)
            .expect("failed to send event 2");
        listener.on_stop(identity.clone().into()).expect("failed to send event 3");

        let event = event_stream.next().await.unwrap();
        let expected_event =
            ComponentEvent::Start(StartEvent { metadata: identity.clone().into() });
        compare_events_ignore_timestamp_and_payload(&event, &expected_event);

        let event = event_stream.next().await.unwrap();
        match event {
            ComponentEvent::DiagnosticsReady(DiagnosticsReadyEvent {
                metadata: EventMetadata { identity: observed_identity, timestamp: _ },
                directory: Some(_),
            }) => {
                assert_eq!(
                    observed_identity.rendered_moniker,
                    format!(
                        "{}/{}:{}",
                        identity.realm_path.join("/"),
                        &identity.component_name,
                        &identity.instance_id
                    )
                );
            }
            other => panic!("unexpected event: {:?}", other),
        }

        let event = event_stream.next().await.unwrap();
        compare_events_ignore_timestamp_and_payload(
            &event,
            &ComponentEvent::Stop(StopEvent { metadata: identity.clone().into() }),
        );
    }

    fn spawn_fake_component_event_provider() -> (
        ComponentEventProvider,
        oneshot::Receiver<fidl::endpoints::ClientEnd<ComponentEventListenerMarker>>,
    ) {
        let (provider, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ComponentEventProviderMarker>().unwrap();
        let (sender, receiver) = oneshot::channel();
        fasync::Task::local(async move {
            if let Some(request) =
                request_stream.try_next().await.expect("error running fake provider")
            {
                match request {
                    ComponentEventProviderRequest::SetListener { listener, .. } => {
                        sender.send(listener).expect("failed to send listener");
                    }
                }
            }
        })
        .detach();
        (provider.into(), receiver)
    }
}
