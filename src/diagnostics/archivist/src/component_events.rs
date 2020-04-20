// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_inspect::TreeProxy,
    fidl_fuchsia_inspect_deprecated::InspectProxy,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_sys_internal::{
        ComponentEventListenerMarker, ComponentEventListenerRequest,
        ComponentEventListenerRequestStream, ComponentEventProviderProxy, SourceIdentity,
    },
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode},
    fuchsia_zircon as zx,
    futures::{channel::mpsc, stream::BoxStream, SinkExt, StreamExt, TryStreamExt},
    log::error,
    std::{
        collections::HashMap,
        convert::{TryFrom, TryInto},
        ops::{Deref, DerefMut},
    },
};

/// A realm path is a vector of realm names.
#[derive(Clone, Eq, PartialEq, Debug)]
pub struct RealmPath(pub Vec<String>);

impl Deref for RealmPath {
    type Target = Vec<String>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for RealmPath {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl From<Vec<String>> for RealmPath {
    fn from(v: Vec<String>) -> Self {
        RealmPath(v)
    }
}

impl Into<String> for RealmPath {
    fn into(self) -> String {
        self.0.join("/").to_string()
    }
}

#[derive(Debug)]
pub struct InspectReaderData {
    /// The identifier of this component
    pub component_id: ComponentIdentifier,

    /// Proxy to the inspect data host.
    pub data_directory_proxy: Option<DirectoryProxy>,
}

/// Represents the ID of a component.
#[derive(Debug, Clone, PartialEq)]
pub enum ComponentIdentifier {
    Legacy(LegacyIdentifier),
}

impl ComponentIdentifier {
    /// Returns the relative moniker to be used for selectors.
    /// For legacy components (v1), this is the relative moniker with respect to the root realm.
    pub fn relative_moniker_for_selectors(&self) -> Vec<String> {
        match self {
            Self::Legacy(identifier) => {
                let mut moniker = identifier.realm_path.clone();
                moniker.push(identifier.component_name.clone());
                moniker.0
            }
        }
    }

    pub fn component_name(&self) -> String {
        match self {
            Self::Legacy(identifier) => identifier.component_name.clone(),
        }
    }

    pub fn instance_id(&self) -> String {
        match self {
            Self::Legacy(identifier) => identifier.instance_id.clone(),
        }
    }
}

impl TryFrom<SourceIdentity> for ComponentIdentifier {
    type Error = anyhow::Error;

    fn try_from(component: SourceIdentity) -> Result<Self, Self::Error> {
        if component.component_name.is_some()
            && component.instance_id.is_some()
            && component.realm_path.is_some()
        {
            Ok(ComponentIdentifier::Legacy(LegacyIdentifier {
                component_name: component.component_name.unwrap(),
                instance_id: component.instance_id.unwrap(),
                realm_path: RealmPath(component.realm_path.unwrap()),
            }))
        } else {
            Err(format_err!("Missing fields in SourceIdentity"))
        }
    }
}

impl ToString for ComponentIdentifier {
    fn to_string(&self) -> String {
        match self {
            Self::Legacy(identifier) => format!(
                "{}/{}:{}",
                identifier.realm_path.join("/"),
                identifier.component_name,
                identifier.instance_id
            ),
        }
    }
}

/// The ID of a component as used in components V1.
#[derive(Debug, Clone, PartialEq)]
pub struct LegacyIdentifier {
    /// The name of the component.
    pub component_name: String,

    /// The instance ID of the component.
    pub instance_id: String,

    /// The path to the component's realm.
    pub realm_path: RealmPath,
}

/// Represents the data associated with a component event.
#[derive(Debug)]
pub struct ComponentEventData {
    pub component_id: ComponentIdentifier,

    /// Extra data about this event (to be stored in extra files in the archive).
    pub component_data_map: Option<HashMap<String, InspectData>>,
}

/// The capacity for bounded channels used by this implementation.
pub static CHANNEL_CAPACITY: usize = 1024;

pub type ComponentEventChannel = mpsc::Sender<ComponentEvent>;

/// A stream of |ComponentEvent|s
pub type ComponentEventStream = BoxStream<'static, ComponentEvent>;

/// An event that occurred to a component.
#[derive(Debug)]
pub enum ComponentEvent {
    /// We observed the component starting.
    Start(ComponentEventData),

    /// We observed the component stopping.
    Stop(ComponentEventData),

    /// We observed the creation of a new `out` directory.
    OutDirectoryAppeared(InspectReaderData),
}

/// Data associated with a component.
/// This data is stored by data collectors and passed by the collectors to processors.
#[derive(Debug)]
pub enum InspectData {
    /// Empty data, for testing.
    Empty,

    /// A VMO containing data associated with the event.
    Vmo(zx::Vmo),

    /// A file containing data associated with the event.
    ///
    /// Because we can't synchronously retrieve file contents like we can for VMOs, this holds
    /// the full file contents. Future changes should make streaming ingestion feasible.
    File(Vec<u8>),

    /// A connection to a Tree service and a handle to the root hierarchy VMO. This VMO is what a
    /// root.inspect file would contain and the result of calling Tree#GetContent. We hold to it
    /// so that we can use it when the component is removed, at which point calling the Tree
    /// service is not an option.
    Tree(TreeProxy, Option<zx::Vmo>),

    /// A connection to the deprecated Inspect service.
    DeprecatedFidl(InspectProxy),
}

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

    impl PartialEq for ComponentEvent {
        fn eq(&self, other: &Self) -> bool {
            match (self, other) {
                (ComponentEvent::Start(a), ComponentEvent::Start(b)) => {
                    return a == b;
                }
                (ComponentEvent::Stop(a), ComponentEvent::Stop(b)) => {
                    return a == b;
                }
                (
                    ComponentEvent::OutDirectoryAppeared(a),
                    ComponentEvent::OutDirectoryAppeared(b),
                ) => {
                    return a == b;
                }
                _ => false,
            }
        }
    }

    impl PartialEq for ComponentEventData {
        /// Check ComponentEventData for equality.
        ///
        /// We implement this manually so that we can avoid requiring equality comparison on
        /// `component_data_map`.
        fn eq(&self, other: &Self) -> bool {
            self.component_id == other.component_id
        }
    }

    impl PartialEq for InspectReaderData {
        fn eq(&self, other: &Self) -> bool {
            self.component_id == other.component_id
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
