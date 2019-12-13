// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::collection::RealmPath,
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_inspect::TreeProxy,
    fidl_fuchsia_inspect_deprecated::InspectProxy,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_sys_internal::{
        ComponentEventListenerMarker, ComponentEventListenerRequest,
        ComponentEventListenerRequestStream, ComponentEventProviderProxy, SourceIdentity,
    },
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{channel::mpsc, stream::BoxStream, SinkExt, StreamExt, TryStreamExt},
    std::{collections::HashMap, path::PathBuf},
};

/// The capacity for bounded channels used by this implementation.
static CHANNEL_CAPACITY: usize = 1024;

type ComponentEventChannel = mpsc::Sender<ComponentEvent>;

#[derive(Debug)]
pub struct InspectReaderData {
    /// Path through the component hierarchy to the component
    /// that this data packet is about.
    pub component_hierarchy_path: PathBuf,

    /// The path from the root parent to
    /// the component generating the inspect reader
    /// data, with all non-monikers stripped, such as
    /// `r` and `c` characters denoting realm or component
    /// or realm id/component id.
    /// eg: /r/my_realm/123/c/echo.cmx/123/ becomes:
    ///     [my_realm, echo.cmx]
    pub absolute_moniker: Vec<String>,

    /// The name of the component.
    pub component_name: String,

    /// The instance ID of the component.
    pub component_id: String,

    /// Proxy to the inspect data host.
    pub data_directory_proxy: Option<DirectoryProxy>,
}

/// Represents the data associated with a component event.
#[derive(Debug)]
pub struct ComponentEventData {
    /// The name of the component.
    pub component_name: String,

    /// The instance ID of the component.
    pub component_id: String,

    /// Extra data about this event (to be stored in extra files in the archive).
    pub component_data_map: Option<HashMap<String, Data>>,

    /// The path to the component's realm.
    pub realm_path: RealmPath,
}

/// An event that occurred to a component.
#[derive(Debug)]
pub enum ComponentEvent {
    /// The component existed when the collection process started.
    Existing(ComponentEventData),

    /// We observed the component starting.
    Start(ComponentEventData),

    /// We observed the component stopping.
    Stop(ComponentEventData),

    /// We observed the creation of a new `out` directory.
    OutDirectoryAppeared(InspectReaderData),
}

/// A stream of |ComponentEvent|s
pub type ComponentEventStream = BoxStream<'static, ComponentEvent>;

/// Data associated with a component.
/// This data is stored by data collectors and passed by the collectors to processors.
#[derive(Debug)]
pub enum Data {
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

pub async fn listen(provider: ComponentEventProviderProxy) -> Result<ComponentEventStream, Error> {
    let (events_client_end, listener_request_stream) =
        fidl::endpoints::create_request_stream::<ComponentEventListenerMarker>()?;
    provider.set_listener(events_client_end)?;
    let (sender, receiver) = mpsc::channel(CHANNEL_CAPACITY);
    EventListenerServer::new(sender).spawn(listener_request_stream);
    Ok(receiver.boxed())
}

struct EventListenerServer {
    sender: ComponentEventChannel,
}

impl EventListenerServer {
    fn new(sender: ComponentEventChannel) -> Self {
        Self { sender }
    }

    fn spawn(self, stream: ComponentEventListenerRequestStream) {
        fasync::spawn(async move {
            self.handle_request_stream(stream)
                .await
                .unwrap_or_else(|e: Error| eprintln!("error running tree server: {:?}", e));
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

    async fn handle_on_start(&mut self, component: SourceIdentity) -> Result<(), Error> {
        if !(component.component_name.is_some() && component.instance_id.is_some()) {
            return Ok(());
        }
        self.send_event(ComponentEvent::Start(ComponentEventData {
            component_name: component.component_name.unwrap(),
            component_id: component.instance_id.unwrap(),
            component_data_map: None,
            realm_path: RealmPath(vec![]),
        }))
        .await
    }

    async fn handle_on_stop(&mut self, component: SourceIdentity) -> Result<(), Error> {
        if !(component.component_name.is_some() && component.instance_id.is_some()) {
            return Ok(());
        }
        self.send_event(ComponentEvent::Stop(ComponentEventData {
            component_name: component.component_name.unwrap(),
            component_id: component.instance_id.unwrap(),
            component_data_map: None,
            realm_path: RealmPath(vec![]),
        }))
        .await
    }

    async fn handle_on_directory_ready(
        &mut self,
        component: SourceIdentity,
        directory: fidl::endpoints::ClientEnd<DirectoryMarker>,
    ) -> Result<(), Error> {
        if !(component.component_name.is_some()
            && component.instance_id.is_some()
            && component.moniker.is_some())
        {
            return Ok(());
        }
        let component_hierarchy_path = PathBuf::from(format!(
            "{}/{}/out/diagnostics/",
            component.moniker.clone().unwrap().join("/"),
            component.instance_id.clone().unwrap()
        ));
        self.send_event(ComponentEvent::OutDirectoryAppeared(InspectReaderData {
            component_hierarchy_path,
            absolute_moniker: component.moniker.unwrap(),
            component_name: component.component_name.unwrap(),
            component_id: component.instance_id.unwrap(),
            data_directory_proxy: directory.into_proxy().ok(),
        }))
        .await
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
        futures::{channel::oneshot, TryStreamExt},
    };

    #[derive(Clone)]
    struct ClonableSourceIdentity {
        moniker: Vec<String>,
        component_name: String,
        instance_id: String,
    }

    impl Into<SourceIdentity> for ClonableSourceIdentity {
        fn into(self) -> SourceIdentity {
            SourceIdentity {
                moniker: Some(self.moniker),
                component_url: None,
                component_name: Some(self.component_name),
                instance_id: Some(self.instance_id),
            }
        }
    }

    impl Into<ComponentEventData> for ClonableSourceIdentity {
        fn into(self) -> ComponentEventData {
            ComponentEventData {
                component_name: self.component_name,
                component_id: self.instance_id,
                component_data_map: None,
                realm_path: RealmPath(vec![]),
            }
        }
    }

    impl PartialEq for ComponentEvent {
        fn eq(&self, other: &Self) -> bool {
            match (self, other) {
                (ComponentEvent::Existing(a), ComponentEvent::Existing(b)) => {
                    return a == b;
                }
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
            self.realm_path == other.realm_path
                && self.component_name == other.component_name
                && self.component_id == other.component_id
        }
    }

    impl PartialEq for InspectReaderData {
        fn eq(&self, other: &Self) -> bool {
            let InspectReaderData {
                component_hierarchy_path,
                absolute_moniker,
                component_name,
                component_id,
                data_directory_proxy: _,
            } = self;
            let InspectReaderData {
                component_hierarchy_path: heirarchy_path,
                component_name: name,
                absolute_moniker: moniker,
                component_id: id,
                data_directory_proxy: _,
            } = other;
            component_hierarchy_path == heirarchy_path
                && component_name == name
                && component_id == id
                && absolute_moniker == moniker
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn component_event_stream() {
        let (provider_proxy, listener_receiver) = spawn_fake_component_event_provider();
        let mut event_stream = listen(provider_proxy).await.expect("failed to listen");
        let listener = listener_receiver
            .await
            .expect("failed to receive listener")
            .into_proxy()
            .expect("failed to get listener proxy");

        let identity: ClonableSourceIdentity = ClonableSourceIdentity {
            moniker: vec!["root".to_string(), "a".to_string(), "test.cmx".to_string()],
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
            ComponentEvent::OutDirectoryAppeared(data) => {
                assert_eq!(
                    data.component_hierarchy_path.to_string_lossy().to_string(),
                    "root/a/test.cmx/12345/out/diagnostics/"
                );
                assert_eq!(data.absolute_moniker, identity.moniker);
                assert_eq!(data.component_name, identity.component_name);
                assert_eq!(data.component_id, identity.instance_id);
                assert!(data.data_directory_proxy.is_some());
            }
            _ => assert!(false),
        }

        let event = event_stream.next().await.unwrap();
        assert_eq!(event, ComponentEvent::Stop(identity.clone().into()));
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
