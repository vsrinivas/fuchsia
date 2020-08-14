// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The `ElementManagement` library provides utilities for Sessions to service
//! incoming [`fidl_fuchsia_session::ElementManagerRequest`]s.
//!
//! Elements are instantiated as dynamic component instances in a component collection of the
//! calling component.

use {
    async_trait::async_trait,
    fidl,
    fidl::endpoints::{DiscoverableService, Proxy, UnifiedServiceMarker},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_mem as fmem,
    fidl_fuchsia_session::{Annotation, Annotations, ElementSpec, Value},
    fidl_fuchsia_sys as fsys, fidl_fuchsia_sys2 as fsys2, fuchsia_async as fasync,
    fuchsia_component,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon as zx, realm_management,
    std::collections::HashMap,
    std::fmt,
    thiserror::Error,
};

/// Errors returned by calls to [`ElementManager`].
#[derive(Debug, Error, Clone, PartialEq)]
pub enum ElementManagerError {
    /// Returned when the element manager fails to created the component instance associated with
    /// a given element.
    #[error("Element spec for \"{}/{}\" missing url.", name, collection)]
    UrlMissing { name: String, collection: String },

    /// Returned when the element manager fails to created the component instance associated with
    /// a given element.
    #[error("Element {} not created at \"{}/{}\": {:?}", url, collection, name, err)]
    NotCreated { name: String, collection: String, url: String, err: fcomponent::Error },

    /// Returned when the element manager fails to launch a component with the fuchsia.sys
    /// given launcher. This may be due to an issue with the launcher itself (not bound?).
    #[error("Element {} not launched: {:?}", url, err_str)]
    NotLaunched { url: String, err_str: String },

    /// Returned when the element manager fails to bind to the component instance associated with
    /// a given element.
    #[error("Element {} not bound at \"{}/{}\": {:?}", url, collection, name, err)]
    NotBound { name: String, collection: String, url: String, err: fcomponent::Error },
}

impl ElementManagerError {
    pub fn url_missing(
        name: impl Into<String>,
        collection: impl Into<String>,
    ) -> ElementManagerError {
        ElementManagerError::UrlMissing { name: name.into(), collection: collection.into() }
    }

    pub fn not_created(
        name: impl Into<String>,
        collection: impl Into<String>,
        url: impl Into<String>,
        err: impl Into<fcomponent::Error>,
    ) -> ElementManagerError {
        ElementManagerError::NotCreated {
            name: name.into(),
            collection: collection.into(),
            url: url.into(),
            err: err.into(),
        }
    }

    pub fn not_launched(url: impl Into<String>, err_str: impl Into<String>) -> ElementManagerError {
        ElementManagerError::NotLaunched { url: url.into(), err_str: err_str.into() }
    }

    pub fn not_bound(
        name: impl Into<String>,
        collection: impl Into<String>,
        url: impl Into<String>,
        err: impl Into<fcomponent::Error>,
    ) -> ElementManagerError {
        ElementManagerError::NotBound {
            name: name.into(),
            collection: collection.into(),
            url: url.into(),
            err: err.into(),
        }
    }
}

/// Checks whether the component is a *.cm or not
///
/// #Parameters
/// - `component_url`: The component url.
fn is_v2_component(component_url: &str) -> bool {
    component_url.ends_with(".cm")
}

/// Manages the elements associated with a session.
#[async_trait]
pub trait ElementManager {
    /// Adds an element to the session.
    ///
    /// This method creates the component instance and binds to it, causing it to start running.
    ///
    /// # Parameters
    /// - `spec`: The description of the element to add as a child.
    /// - `child_name`: The name of the element, must be unique within a session. The name must be
    ///                 non-empty, of the form [a-z0-9-_.].
    /// - `child_collection`: The collection to add the element in, must match a collection in the
    ///                       calling component's CML file.
    ///
    /// On success, the [`Element`] is returned back to the session.
    ///
    /// # Errors
    /// If the child cannot be created or bound in the current [`fidl_fuchsia_sys2::Realm`]. In
    /// particular, it is an error to call [`launch_element`] twice with the same `child_name`.
    async fn launch_element(
        &self,
        spec: ElementSpec,
        child_name: &str,
        child_collection: &str,
    ) -> Result<Element, ElementManagerError>;
}

enum ExposedCapabilities {
    /// v1 component App
    App(fuchsia_component::client::App),
    /// v2 component exposed capabilities directory
    Directory(zx::Channel),
}

impl fmt::Debug for ExposedCapabilities {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ExposedCapabilities::App(_) => write!(f, "CFv1 App"),
            ExposedCapabilities::Directory(_) => write!(f, "CFv2 exposed capabilities Directory"),
        }
    }
}

/// Represents a component launched by an Element Manager.
///
/// The component can be either a v1 component launched by the fuchsia.sys.Launcher, or a v2
/// component launched as a child of the Element Manager's realm.
///
/// The Element can be used to connect to services exposed by the underlying v1 or v2 component.
#[derive(Debug)]
pub struct Element {
    /// CF v1 or v2 object that manages a `Directory` request channel for requesting services
    /// exposed by the component.
    exposed_capabilities: ExposedCapabilities,

    /// Element annotation key/value pairs.
    custom_annotations: HashMap<String, Value>,

    /// The component URL used to launch the component. Private but printable via "{:?}".
    url: String,

    /// v2 component child name, or empty string if not a child of the realm (such as a CFv1
    /// component). Private but printable via "{:?}"".
    name: String,

    /// v2 component child collection name or empty string if not a child of the realm (such as a
    /// CFv1 component). Private but printable via "{:?}"".
    collection: String,
}

/// A component launched in response to `ElementManager::ProposeElement()`.
///
/// A session uses `ElementManager` to launch and return the Element, and can then use the Element
/// to connect to exposed capabilities.
///
/// An Element composes either a CFv2 component (launched as a child of the `ElementManager`'s
/// realm) or a CFv1 component (launched via a fuchsia::sys::Launcher).
impl Element {
    /// Creates an Element from a `fuchsia_component::client::App`.
    ///
    /// # Parameters
    /// - `url`: The launched component URL.
    /// - `app`: The v1 component wrapped in an App, returned by the launch function.
    pub fn from_app(app: fuchsia_component::client::App, url: &str) -> Element {
        Element {
            exposed_capabilities: ExposedCapabilities::App(app),
            url: url.to_string(),
            name: "".to_string(),
            collection: "".to_string(),
            custom_annotations: HashMap::new(),
        }
    }

    /// Creates an Element from a component's exposed capabilities directory.
    ///
    /// # Parameters
    /// - `directory_channel`: A channel to the component's `Directory` of exposed capabilities.
    /// - `name`: The launched component's name.
    /// - `url`: The launched component URL.
    /// - `collection`: The launched component's collection name.
    pub fn from_directory_channel(
        directory_channel: zx::Channel,
        name: &str,
        url: &str,
        collection: &str,
    ) -> Element {
        Element {
            exposed_capabilities: ExposedCapabilities::Directory(directory_channel),
            url: url.to_string(),
            name: name.to_string(),
            collection: collection.to_string(),
            custom_annotations: HashMap::new(),
        }
    }

    // # Note
    //
    // The methods below are copied from fuchsia_component::client::App in order to offer
    // services in the same way, but from any `Element`, wrapping either a v1 `App` or a v2
    // component's `Directory` of exposed services.

    /// Returns a reference to the component's `Directory` of exposed capabilities. A session can
    /// request services, and/or other capabilities, from the Element, using this channel.
    ///
    /// # Returns
    /// A `channel` to the component's `Directory` of exposed capabilities.
    #[inline]
    pub fn directory_channel(&self) -> &zx::Channel {
        match &self.exposed_capabilities {
            ExposedCapabilities::App(app) => &app.directory_channel(),
            ExposedCapabilities::Directory(directory_channel) => &directory_channel,
        }
    }

    #[inline]
    fn service_path_prefix(&self) -> &str {
        match &self.exposed_capabilities {
            ExposedCapabilities::App(..) => "",
            ExposedCapabilities::Directory(..) => "svc/",
        }
    }

    /// Connect to a service provided by the `Element`.
    ///
    /// # Type Parameters
    /// - S: A FIDL service `Marker` type.
    ///
    /// # Returns
    /// - A service `Proxy` matching the `Marker`, or an error if the service is not available from
    /// the `Element`.
    #[inline]
    pub fn connect_to_service<S: DiscoverableService>(&self) -> Result<S::Proxy, anyhow::Error> {
        let (client_channel, server_channel) = zx::Channel::create()?;
        self.connect_to_service_with_channel::<S>(server_channel)?;
        Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
    }

    /// Connect to a FIDL Unified Service provided by the `Element`.
    ///
    /// # Type Parameters
    /// - US: A FIDL Unified Service `Marker` type.
    ///
    /// # Returns
    /// - A service `Proxy` matching the `Marker`, or an error if the service is not available from
    /// the `Element`.
    #[inline]
    pub fn connect_to_unified_service<US: UnifiedServiceMarker>(
        &self,
    ) -> Result<US::Proxy, anyhow::Error> {
        fuchsia_component::client::connect_to_unified_service_at_dir::<US>(
            &self.directory_channel(),
        )
    }

    /// Connect to a service by passing a channel for the server.
    ///
    /// # Type Parameters
    /// - S: A FIDL service `Marker` type.
    ///
    /// # Parameters
    /// - server_channel: The server-side endpoint of a channel pair, to bind to the requested
    /// service. The caller will interact with the service via the client-side endpoint.
    ///
    /// # Returns
    /// - Result::Ok or an error if the service is not available from the `Element`.
    #[inline]
    pub fn connect_to_service_with_channel<S: DiscoverableService>(
        &self,
        server_channel: zx::Channel,
    ) -> Result<(), anyhow::Error> {
        self.connect_to_named_service_with_channel(S::SERVICE_NAME, server_channel)
    }

    /// Connect to a service by name.
    ///
    /// # Parameters
    /// - service_name: A FIDL service by name.
    /// - server_channel: The server-side endpoint of a channel pair, to bind to the requested
    /// service. The caller will interact with the service via the client-side endpoint.
    ///
    /// # Returns
    /// - Result::Ok or an error if the service is not available from the `Element`.
    #[inline]
    pub fn connect_to_named_service_with_channel(
        &self,
        service_name: &str,
        server_channel: zx::Channel,
    ) -> Result<(), anyhow::Error> {
        fdio::service_connect_at(
            &self.directory_channel(),
            &(self.service_path_prefix().to_owned() + service_name),
            server_channel,
        )?;
        Ok(())
    }

    pub fn set_annotations(&mut self, annotations: Annotations) -> Result<(), anyhow::Error> {
        if let Some(mut custom_annotations) = annotations.custom_annotations {
            for annotation in custom_annotations.drain(..) {
                if annotation.value.is_none() {
                    self.custom_annotations.remove(&annotation.key);
                } else {
                    self.custom_annotations
                        .insert(annotation.key.to_string(), *annotation.value.unwrap());
                }
            }
        }
        Ok(())
    }

    pub fn get_annotations(&mut self) -> Result<Annotations, anyhow::Error> {
        let mut custom_annotations = vec![];
        for (key, value) in &self.custom_annotations {
            custom_annotations.push(Annotation {
                key: key.to_string(),
                value: Some(Box::new(match &*value {
                    Value::Text(content) => Value::Text(content.to_string()),
                    Value::Buffer(content) => {
                        let mut bytes = Vec::<u8>::with_capacity(content.size as usize);
                        let vmo = fidl::Vmo::create(content.size).unwrap();
                        content.vmo.read(&mut bytes[..], 0)?;
                        vmo.write(&bytes[..], 0)?;
                        Value::Buffer(fmem::Buffer { vmo, size: content.size })
                    }
                })),
            });
        }
        Ok(Annotations { custom_annotations: Some(custom_annotations) })
    }
}

/// A [`SimpleElementManager`] creates and binds elements.
///
/// The [`SimpleElementManager`] provides no additional functionality for managing elements (e.g.,
/// tracking which elements are running, de-duplicating elements, etc.).
pub struct SimpleElementManager {
    /// The realm which this element manager uses to create components.
    realm: fsys2::RealmProxy,

    /// The ElementManager uses a fuchsia::sys::Launcher to create a component without a "*.cm" file
    /// (including *.cmx and other URLs with supported schemes, such as "https"). If a launcher is
    /// not provided during intialization, it is requested from the environment.
    sys_launcher: Result<fsys::LauncherProxy, anyhow::Error>,
}

/// An element manager that launches v1 and v2 components, returning them to the caller.
impl SimpleElementManager {
    pub fn new(realm: fsys2::RealmProxy) -> SimpleElementManager {
        SimpleElementManager { realm, sys_launcher: connect_to_service::<fsys::LauncherMarker>() }
    }

    /// Initializer used by tests, to override the default fuchsia::sys::Launcher with a mock
    /// launcher.
    pub fn new_with_sys_launcher(
        realm: fsys2::RealmProxy,
        sys_launcher: fsys::LauncherProxy,
    ) -> Self {
        SimpleElementManager { realm, sys_launcher: Ok(sys_launcher) }
    }

    /// Launches a v1 component with the specified URL.
    ///
    /// #Parameters
    /// - `child_url`: The component url of the child added to the session. This function launches
    /// components using a fuchsia::sys::Launcher. It supports all CFv1 component URL schemes,
    /// such as URLs starting with `https`, and fuchsia component URLs ending in `.cmx`. Fuchsia
    /// components ending in `.cm` should use `launch_child_component()` instead.
    ///
    /// #Returns
    /// The launched application.
    async fn launch_v1_component(&self, child_url: &str) -> Result<Element, ElementManagerError> {
        let sys_launcher = (&self.sys_launcher).as_ref().map_err(|err: &anyhow::Error| {
            ElementManagerError::not_launched(
                child_url.clone(),
                format!("Error connecting to fuchsia::sys::Launcher: {}", err.to_string()),
            )
        })?;

        let app = fuchsia_component::client::launch(&sys_launcher, child_url.to_string(), None)
            .map_err(|err: anyhow::Error| {
                ElementManagerError::not_launched(child_url.clone(), err.to_string())
            })?;
        Ok(Element::from_app(app, child_url))
    }

    /// Adds a v2 component element to the Element Manager's realm.
    ///
    /// #Parameters
    /// - `child_name`: The name of the element, must be unique within a session. The name must be
    ///                 non-empty, of the form [a-z0-9-_.].
    /// - `child_url`: The component url of the child added to the session.
    /// - `child_collection`: The collection to add the element in, must match a collection in the
    ///                       calling component's CML file.
    /// - `realm`: The `Realm` to which the child will be added.
    /// Returns:
    /// - An Element from which services can be requested (such as a View)
    async fn launch_child_component(
        &self,
        child_name: &str,
        child_url: &str,
        child_collection: &str,
        realm: &fsys2::RealmProxy,
    ) -> Result<Element, ElementManagerError> {
        fx_log_info!(
            "launch_child_component(name={}, url={}, collection={})",
            child_name,
            child_url,
            child_collection
        );
        realm_management::create_child_component(&child_name, &child_url, child_collection, &realm)
            .await
            .map_err(|err: fcomponent::Error| {
                ElementManagerError::not_created(child_name, child_collection, child_url, err)
            })?;

        let directory_channel = match realm_management::bind_child_component(
            child_name,
            child_collection,
            &realm,
        )
        .await
        {
            Ok(channel) => channel,
            Err(err) => {
                return Err(ElementManagerError::not_bound(
                    child_name,
                    child_collection,
                    child_url,
                    err,
                ))
            }
        };
        Ok(Element::from_directory_channel(
            directory_channel,
            child_name,
            child_url,
            child_collection,
        ))
    }
}

#[async_trait]
impl ElementManager for SimpleElementManager {
    async fn launch_element(
        &self,
        spec: ElementSpec,
        child_name: &str,
        child_collection: &str,
    ) -> Result<Element, ElementManagerError> {
        let child_url = spec
            .component_url
            .ok_or_else(|| ElementManagerError::url_missing(child_name, child_collection))?;

        let mut element = if is_v2_component(&child_url) {
            self.launch_child_component(&child_name, &child_url, child_collection, &self.realm)
                .await?
        } else {
            self.launch_v1_component(&child_url).await?
        };
        if spec.annotations.is_some() {
            element.set_annotations(spec.annotations.unwrap()).map_err(|err: anyhow::Error| {
                ElementManagerError::not_launched(child_url.clone(), err.to_string())
            })?;
        }

        Ok(element)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{ElementManager, ElementManagerError, SimpleElementManager},
        fidl::encoding::Decodable,
        fidl::endpoints::{create_proxy_and_stream, ServerEnd},
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio,
        fidl_fuchsia_session::ElementSpec,
        fidl_fuchsia_sys as fsys, fidl_fuchsia_sys2 as fsys2, fuchsia_async as fasync,
        fuchsia_zircon as zx,
        futures::{channel::mpsc::channel, prelude::*},
        lazy_static::lazy_static,
        test_util::Counter,
    };

    /// Spawns a local `fidl_fuchsia_sys2::Realm` server, and returns a proxy to the spawned server.
    /// The provided `request_handler` is notified when an incoming request is received.
    ///
    /// # Parameters
    /// - `request_handler`: A function which is called with incoming requests to the spawned
    ///                      `Realm` server.
    /// # Returns
    /// A `RealmProxy` to the spawned server.
    fn spawn_realm_server<F: 'static>(request_handler: F) -> fsys2::RealmProxy
    where
        F: Fn(fsys2::RealmRequest) + Send,
    {
        let (realm_proxy, mut realm_server) = create_proxy_and_stream::<fsys2::RealmMarker>()
            .expect("Failed to create realm proxy and server.");

        fasync::Task::spawn(async move {
            while let Some(realm_request) = realm_server.try_next().await.unwrap() {
                request_handler(realm_request);
            }
        })
        .detach();

        realm_proxy
    }

    fn spawn_directory_server<F: 'static>(
        mut directory_server: fio::DirectoryRequestStream,
        request_handler: F,
    ) where
        F: Fn(fio::DirectoryRequest) + Send,
    {
        fasync::Task::spawn(async move {
            while let Some(directory_request) = directory_server.try_next().await.unwrap() {
                request_handler(directory_request);
            }
        })
        .detach();
    }

    /// Spawns a local `fidl_fuchsia_sys::Launcher` server, and returns a proxy to the spawned
    /// server. The provided `request_handler` is notified when an incoming request is received.
    ///
    /// # Parameters
    /// - `request_handler`: A function which is called with incoming requests to the spawned
    ///                      `Launcher` server.
    /// # Returns
    /// A `LauncherProxy` to the spawned server.
    fn spawn_launcher_server<F: 'static>(request_handler: F) -> fsys::LauncherProxy
    where
        F: Fn(fsys::LauncherRequest) + Send,
    {
        let (launcher_proxy, mut launcher_server) =
            create_proxy_and_stream::<fsys::LauncherMarker>()
                .expect("Failed to create launcher proxy and server.");

        fasync::Task::spawn(async move {
            while let Some(launcher_request) = launcher_server.try_next().await.unwrap() {
                request_handler(launcher_request);
            }
        })
        .detach();

        launcher_proxy
    }

    /// Tests that adding a component with a cmx file successfully returns an Element
    /// with outgoing directory routing appropriate for v1 components.
    #[fasync::run_until_stalled(test)]
    async fn add_v1_element_success() {
        lazy_static! {
            static ref CREATE_COMPONENT_CALL_COUNT: Counter = Counter::new(0);
        }

        const ELEMENT_COUNT: usize = 1;

        let component_url = "test_url.cmx";
        let child_name = "child";
        let child_collection = "elements";

        let realm = spawn_realm_server(move |realm_request| match realm_request {
            _ => {
                // CFv1 elements do not use the realm so fail the test if it is requested.
                assert!(false);
            }
        });

        let (directory_open_sender, directory_open_receiver) = channel::<String>(1);
        let directory_request_handler = move |directory_request| match directory_request {
            fio::DirectoryRequest::Open { path: capability_path, .. } => {
                let mut result_sender = directory_open_sender.clone();
                fasync::Task::spawn(async move {
                    let _ = result_sender.send(capability_path).await;
                })
                .detach()
            }
            _ => {
                assert!(false);
            }
        };

        let (create_component_sender, create_component_receiver) = channel::<()>(ELEMENT_COUNT);
        let launcher = spawn_launcher_server(move |launcher_request| match launcher_request {
            fsys::LauncherRequest::CreateComponent {
                launch_info: fsys::LaunchInfo { url, directory_request, .. },
                ..
            } => {
                assert_eq!(url, component_url);
                let mut result_sender = create_component_sender.clone();
                spawn_directory_server(
                    ServerEnd::<fio::DirectoryMarker>::new(directory_request.unwrap())
                        .into_stream()
                        .unwrap(),
                    directory_request_handler.clone(),
                );
                fasync::Task::spawn(async move {
                    let _ = result_sender.send(()).await;
                    CREATE_COMPONENT_CALL_COUNT.inc();
                })
                .detach()
            }
        });

        let element_manager = SimpleElementManager::new_with_sys_launcher(realm, launcher);
        let result = element_manager
            .launch_element(
                ElementSpec {
                    component_url: Some(component_url.to_string()),
                    ..ElementSpec::new_empty()
                },
                child_name,
                child_collection,
            )
            .await;
        let element = result.unwrap();
        assert!(format!("{:?}", element).contains(component_url));

        // Verify that the CreateComponent was actually called.
        create_component_receiver.into_future().await;
        assert_eq!(CREATE_COMPONENT_CALL_COUNT.get(), ELEMENT_COUNT);

        // Now use the element api to open a service in the element's outgoing dir. Verify
        // that the directory channel received the request with the correct path.
        let (_client_channel, server_channel) = zx::Channel::create().unwrap();
        let _ = element.connect_to_named_service_with_channel("myService", server_channel);
        let open_paths = directory_open_receiver.take(1).collect::<Vec<_>>().await;
        // While a standard v1 component publishes its services to "svc/...", LaunchInfo.directory_request,
        // which is mocked in the test setup, points to the "svc" subdirectory in production. This is why
        // the expected path does not contain the "svc/" prefix.
        assert_eq!(vec!["myService"], open_paths);
    }

    /// Tests that adding multiple components without "*.cm" successfully returns [`Ok`].
    #[fasync::run_until_stalled(test)]
    async fn add_multiple_v1_element_success() {
        lazy_static! {
            static ref CREATE_COMPONENT_CALL_COUNT: Counter = Counter::new(0);
        }

        const ELEMENT_COUNT: usize = 2;

        let (sender, receiver) = channel::<()>(ELEMENT_COUNT);

        let a_component_url = "a_url.cmx";
        let a_child_name = "a_child";
        let a_child_collection = "elements";

        let b_component_url = "https://google.com";
        let b_child_name = "b_child";
        let b_child_collection = "elements";

        let realm = spawn_realm_server(move |realm_request| match realm_request {
            _ => {
                // CFv1 elements do not use the realm so fail the test if it is requested.
                assert!(false);
            }
        });

        let launcher = spawn_launcher_server(move |launcher_request| match launcher_request {
            fsys::LauncherRequest::CreateComponent {
                launch_info: fsys::LaunchInfo { .. }, ..
            } => {
                let mut result_sender = sender.clone();
                fasync::Task::spawn(async move {
                    let _ = result_sender.send(()).await.expect("Could not create component.");
                    CREATE_COMPONENT_CALL_COUNT.inc();
                })
                .detach()
            }
        });

        let element_manager = SimpleElementManager::new_with_sys_launcher(realm, launcher);
        assert!(element_manager
            .launch_element(
                ElementSpec {
                    component_url: Some(a_component_url.to_string()),
                    ..ElementSpec::new_empty()
                },
                a_child_name,
                a_child_collection,
            )
            .await
            .is_ok());
        assert!(element_manager
            .launch_element(
                ElementSpec {
                    component_url: Some(b_component_url.to_string()),
                    ..ElementSpec::new_empty()
                },
                b_child_name,
                b_child_collection,
            )
            .await
            .is_ok());

        // Verify that the CreateComponent was actually called.
        receiver.into_future().await;
        assert_eq!(CREATE_COMPONENT_CALL_COUNT.get(), ELEMENT_COUNT);
    }

    /// Tests that adding a *.cm element successfully returns an Element with
    /// outgoing directory routing appropriate for v2 components.
    #[fasync::run_until_stalled(test)]
    async fn launch_v2_element_success() {
        let component_url = "fuchsia-pkg://fuchsia.com/simple_element#meta/simple_element.cm";
        let child_name = "child";
        let child_collection = "elements";

        let (directory_open_sender, directory_open_receiver) = channel::<String>(1);
        let directory_request_handler = move |directory_request| match directory_request {
            fio::DirectoryRequest::Open { path: capability_path, .. } => {
                let mut result_sender = directory_open_sender.clone();
                fasync::Task::spawn(async move {
                    let _ = result_sender.send(capability_path).await;
                })
                .detach()
            }
            _ => {
                assert!(false);
            }
        };

        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys2::RealmRequest::CreateChild { collection, decl, responder } => {
                assert_eq!(decl.url.unwrap(), component_url);
                assert_eq!(decl.name.unwrap(), child_name);
                assert_eq!(&collection.name, child_collection);

                let _ = responder.send(&mut Ok(()));
            }
            fsys2::RealmRequest::BindChild { child, exposed_dir, responder } => {
                assert_eq!(child.collection, Some(child_collection.to_string()));
                spawn_directory_server(
                    exposed_dir.into_stream().unwrap(),
                    directory_request_handler.clone(),
                );
                let _ = responder.send(&mut Ok(()));
            }
            _ => {
                assert!(false);
            }
        });
        let element_manager = SimpleElementManager::new(realm);
        let result = element_manager
            .launch_element(
                ElementSpec {
                    component_url: Some(component_url.to_string()),
                    ..ElementSpec::new_empty()
                },
                child_name,
                child_collection,
            )
            .await;
        let element = result.unwrap();

        // Now use the element api to open a service in the element's outgoing dir. Verify
        // that the directory channel received the request with the correct path.
        let (_client_channel, server_channel) = zx::Channel::create().unwrap();
        let _ = element.connect_to_named_service_with_channel("myService", server_channel);
        let open_paths = directory_open_receiver.take(1).collect::<Vec<_>>().await;
        assert_eq!(vec!["svc/myService"], open_paths);
    }

    /// Tests that adding a .cm element does not use fuchsia.sys.Launcher.
    #[fasync::run_until_stalled(test)]
    async fn launch_element_success_not_use_launcher() {
        let component_url = "fuchsia-pkg://fuchsia.com/simple_element#meta/simple_element.cm";
        let child_name = "child";
        let child_collection = "elements";

        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys2::RealmRequest::CreateChild { collection, decl, responder } => {
                assert_eq!(decl.url.unwrap(), component_url);
                assert_eq!(decl.name.unwrap(), child_name);
                assert_eq!(&collection.name, child_collection);

                let _ = responder.send(&mut Ok(()));
            }
            fsys2::RealmRequest::BindChild { child, exposed_dir: _, responder } => {
                assert_eq!(child.collection, Some(child_collection.to_string()));

                let _ = responder.send(&mut Ok(()));
            }
            _ => {
                assert!(false);
            }
        });
        let launcher = spawn_launcher_server(move |launcher_request| match launcher_request {
            // Fail if any call to the launcher is made.
            _ => {
                assert!(false);
            }
        });
        let element_manager = SimpleElementManager::new_with_sys_launcher(realm, launcher);
        assert!(element_manager
            .launch_element(
                ElementSpec {
                    component_url: Some(component_url.to_string()),
                    ..ElementSpec::new_empty()
                },
                child_name,
                child_collection,
            )
            .await
            .is_ok());
    }

    /// Tests that adding an element with no URL returns the appropriate error.
    #[fasync::run_until_stalled(test)]
    async fn launch_element_no_url() {
        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_realm_server(move |realm_request| match realm_request {
            _ => {
                assert!(false);
            }
        });
        let element_manager = SimpleElementManager::new(realm);

        let result = element_manager
            .launch_element(ElementSpec { component_url: None, ..ElementSpec::new_empty() }, "", "")
            .await;
        assert!(result.is_err());
        assert_eq!(result.err().unwrap(), ElementManagerError::url_missing("", ""));
    }

    /// Tests that adding an element which is not successfully created in the realm returns an
    /// appropriate error.
    #[fasync::run_until_stalled(test)]
    async fn launch_element_create_error_internal() {
        let component_url = "fuchsia-pkg://fuchsia.com/simple_element#meta/simple_element.cm";

        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys2::RealmRequest::CreateChild { collection: _, decl: _, responder } => {
                let _ = responder.send(&mut Err(fcomponent::Error::Internal));
            }
            _ => {
                assert!(false);
            }
        });
        let element_manager = SimpleElementManager::new(realm);

        let result = element_manager
            .launch_element(
                ElementSpec {
                    component_url: Some(component_url.to_string()),
                    ..ElementSpec::new_empty()
                },
                "",
                "",
            )
            .await;
        assert!(result.is_err());
        assert_eq!(
            result.err().unwrap(),
            ElementManagerError::not_created("", "", component_url, fcomponent::Error::Internal)
        );
    }

    /// Tests that adding an element which is not successfully created in the realm returns an
    /// appropriate error.
    #[fasync::run_until_stalled(test)]
    async fn launch_element_create_error_no_space() {
        let component_url = "fuchsia-pkg://fuchsia.com/simple_element#meta/simple_element.cm";

        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys2::RealmRequest::CreateChild { collection: _, decl: _, responder } => {
                let _ = responder.send(&mut Err(fcomponent::Error::ResourceUnavailable));
            }
            _ => {
                assert!(false);
            }
        });
        let element_manager = SimpleElementManager::new(realm);

        let result = element_manager
            .launch_element(
                ElementSpec {
                    component_url: Some(component_url.to_string()),
                    ..ElementSpec::new_empty()
                },
                "",
                "",
            )
            .await;
        assert!(result.is_err());
        assert_eq!(
            result.err().unwrap(),
            ElementManagerError::not_created(
                "",
                "",
                component_url,
                fcomponent::Error::ResourceUnavailable
            )
        );
    }

    /// Tests that adding an element which is not successfully bound in the realm returns an
    /// appropriate error.
    #[fasync::run_until_stalled(test)]
    async fn launch_element_bind_error() {
        let component_url = "fuchsia-pkg://fuchsia.com/simple_element#meta/simple_element.cm";

        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys2::RealmRequest::CreateChild { collection: _, decl: _, responder } => {
                let _ = responder.send(&mut Ok(()));
            }
            fsys2::RealmRequest::BindChild { child: _, exposed_dir: _, responder } => {
                let _ = responder.send(&mut Err(fcomponent::Error::InstanceCannotStart));
            }
            _ => {
                assert!(false);
            }
        });
        let element_manager = SimpleElementManager::new(realm);

        let result = element_manager
            .launch_element(
                ElementSpec {
                    component_url: Some(component_url.to_string()),
                    ..ElementSpec::new_empty()
                },
                "",
                "",
            )
            .await;
        assert!(result.is_err());
        assert_eq!(
            result.err().unwrap(),
            ElementManagerError::not_bound(
                "",
                "",
                component_url,
                fcomponent::Error::InstanceCannotStart
            )
        );
    }
}
