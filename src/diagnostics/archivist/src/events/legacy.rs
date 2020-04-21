// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::types::*,
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys_internal::{
        ComponentEventListenerMarker, ComponentEventListenerRequest,
        ComponentEventListenerRequestStream, ComponentEventProviderProxy, SourceIdentity,
    },
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode},
    futures::{channel::mpsc, SinkExt, StreamExt, TryStreamExt},
    log::error,
    std::convert::TryInto,
};

/// Subscribe to component lifecycle events.
/// |node| is the node where stats about events seen will be recorded.
pub async fn listen(
    provider: ComponentEventProviderProxy,
    node: inspect::Node,
) -> Result<ComponentEventStream, Error> {
    let (events_client_end, listener_request_stream) =
        fidl::endpoints::create_request_stream::<ComponentEventListenerMarker>()?;
    provider.set_listener(events_client_end)?;
    let (sender, receiver) = mpsc::channel(CHANNEL_CAPACITY);
    EventListenerServer::new(sender, node).spawn(listener_request_stream);
    Ok(receiver.boxed())
}

struct EventListenerServer {
    sender: ComponentEventChannel,

    // Inspect stats
    _node: inspect::Node,
    components_started: inspect::UintProperty,
    components_stopped: inspect::UintProperty,
    diagnostics_directories_seen: inspect::UintProperty,
    component_log_node: BoundedListNode,
}

impl EventListenerServer {
    fn new(sender: ComponentEventChannel, node: inspect::Node) -> Self {
        let components_started = node.create_uint("components_started", 0);
        let components_stopped = node.create_uint("components_stopped", 0);
        let diagnostics_directories_seen = node.create_uint("diagnostics_directories_seen", 0);
        let component_log_node = BoundedListNode::new(node.create_child("recent_events"), 50);
        Self {
            sender,
            _node: node,
            components_started,
            components_stopped,
            diagnostics_directories_seen,
            component_log_node,
        }
    }

    fn spawn(self, stream: ComponentEventListenerRequestStream) {
        fasync::spawn(async move {
            self.handle_request_stream(stream)
                .await
                .unwrap_or_else(|e: Error| error!("failed to run tree server: {:?}", e));
        });
    }

    async fn handle_request_stream(
        mut self,
        mut stream: ComponentEventListenerRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            stream.try_next().await.context("Error running component event listener server")?
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

    fn log_inspect(&mut self, event_name: &str, identifier: &ComponentIdentifier) {
        inspect_log!(self.component_log_node,
            event: event_name,
            moniker: identifier.to_string(),
        );
    }

    async fn handle_on_start(&mut self, component: SourceIdentity) -> Result<(), Error> {
        if let Ok(component_id) = component.try_into() {
            self.components_started.add(1);
            self.log_inspect("START", &component_id);
            self.send_event(ComponentEvent::Start(ComponentEventData {
                component_id,
                component_data_map: None,
            }))
            .await?;
        }
        Ok(())
    }

    async fn handle_on_stop(&mut self, component: SourceIdentity) -> Result<(), Error> {
        if let Ok(component_id) = component.try_into() {
            self.components_stopped.add(1);
            self.log_inspect("STOP", &component_id);
            self.send_event(ComponentEvent::Stop(ComponentEventData {
                component_id,
                component_data_map: None,
            }))
            .await?;
        }
        Ok(())
    }

    async fn handle_on_directory_ready(
        &mut self,
        component: SourceIdentity,
        directory: fidl::endpoints::ClientEnd<DirectoryMarker>,
    ) -> Result<(), Error> {
        if let Ok(component_id) = component.try_into() {
            self.diagnostics_directories_seen.add(1);
            self.log_inspect("DIAGNOSTICS_DIR_READY", &component_id);
            self.send_event(ComponentEvent::OutDirectoryAppeared(InspectReaderData {
                component_id,
                data_directory_proxy: directory.into_proxy().ok(),
            }))
            .await?;
        }
        Ok(())
    }

    async fn send_event(&mut self, event: ComponentEvent) -> Result<(), Error> {
        self.sender.send(event).await.map_err(|e| format_err!("Failed to send: {:?}", e))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_io::DirectoryMarker,
        fidl_fuchsia_sys_internal::{
            ComponentEventProviderMarker, ComponentEventProviderRequest, SourceIdentity,
        },
        fuchsia_async as fasync,
        fuchsia_inspect::assert_inspect_tree,
        futures::{channel::oneshot, TryStreamExt},
    };

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
                component_url: None,
                component_name: Some(self.component_name),
                instance_id: Some(self.instance_id),
            }
        }
    }

    impl Into<ComponentEventData> for ClonableSourceIdentity {
        fn into(self) -> ComponentEventData {
            ComponentEventData {
                component_id: ComponentIdentifier::Legacy(LegacyIdentifier {
                    component_name: self.component_name,
                    instance_id: self.instance_id,
                    realm_path: RealmPath(self.realm_path),
                }),
                component_data_map: None,
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn component_event_stream() {
        let (provider_proxy, listener_receiver) = spawn_fake_component_event_provider();
        let inspector = inspect::Inspector::new();
        let mut event_stream = listen(provider_proxy, inspector.root().create_child("events"))
            .await
            .expect("failed to listen");
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
        assert_eq!(event, ComponentEvent::Start(identity.clone().into()));

        let event = event_stream.next().await.unwrap();
        match event {
            ComponentEvent::OutDirectoryAppeared(InspectReaderData {
                component_id: ComponentIdentifier::Legacy(identifier),
                data_directory_proxy: Some(_),
            }) => {
                assert_eq!(identifier.realm_path, RealmPath(identity.realm_path.clone()));
                assert_eq!(identifier.component_name, identity.component_name);
                assert_eq!(identifier.instance_id, identity.instance_id);
            }
            _ => assert!(false),
        }

        let event = event_stream.next().await.unwrap();
        assert_eq!(event, ComponentEvent::Stop(identity.clone().into()));

        assert_inspect_tree!(inspector, root: {
            events: {
                components_started: 1u64,
                components_stopped: 1u64,
                diagnostics_directories_seen: 1u64,
                recent_events: {
                    "0": {
                        "@time": inspect::testing::AnyProperty,
                        event: "START",
                        moniker: "root/a/test.cmx:12345"
                    },
                    "1": {
                        "@time": inspect::testing::AnyProperty,
                        event: "DIAGNOSTICS_DIR_READY",
                        moniker: "root/a/test.cmx:12345"
                    },
                    "2": {
                        "@time": inspect::testing::AnyProperty,
                        event: "STOP",
                        moniker: "root/a/test.cmx:12345"
                    }
                }
            }
        });
    }

    fn spawn_fake_component_event_provider() -> (
        ComponentEventProviderProxy,
        oneshot::Receiver<fidl::endpoints::ClientEnd<ComponentEventListenerMarker>>,
    ) {
        let (provider, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ComponentEventProviderMarker>().unwrap();
        let (sender, receiver) = oneshot::channel();
        fasync::spawn_local(async move {
            if let Some(request) =
                request_stream.try_next().await.expect("error running fake provider")
            {
                match request {
                    ComponentEventProviderRequest::SetListener { listener, .. } => {
                        sender.send(listener).expect("failed to send listener");
                    }
                }
            }
        });
        (provider, receiver)
    }
}
