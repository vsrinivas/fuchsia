// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The `element_management` library provides utilities for sessions to service
//! incoming [`fidl_fuchsia_element::ManagerRequest`]s.
//!
//! Elements are instantiated as dynamic component instances in a component collection of the
//! calling component.

use {
    async_trait::async_trait,
    fidl,
    fidl::endpoints::{DiscoverableProtocolMarker, Proxy, ServiceMarker},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_element as felement,
    fidl_fuchsia_sys as fsys, fidl_fuchsia_sys2 as fsys2, fuchsia_async as fasync,
    fuchsia_component,
    fuchsia_syslog::fx_log_err,
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    rand::{distributions::Alphanumeric, thread_rng, Rng},
    realm_management,
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

    /// Returned when the element manager fails to create an element with additional services for
    /// the given ServiceList. This may be because:
    ///   - `host_directory` is not set
    ///   - `provider` is set
    #[error("Element spec for \"{}/{}\" contains an invalid ServiceList", name, collection)]
    InvalidServiceList { name: String, collection: String },

    /// Returned when the element manager fails to create an element that uses a CFv2 component
    /// because the spec provides a ServiceList with additional services.
    ///
    /// `Spec.additional_services` is only supported for CFv1 components.
    #[error(
        "Element spec for \"{}/{}\" provides additional_services for a CFv2 component",
        name,
        collection
    )]
    AdditionalServicesNotSupported { name: String, collection: String },

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

    pub fn invalid_service_list(
        name: impl Into<String>,
        collection: impl Into<String>,
    ) -> ElementManagerError {
        ElementManagerError::InvalidServiceList { name: name.into(), collection: collection.into() }
    }

    pub fn additional_services_not_supported(
        name: impl Into<String>,
        collection: impl Into<String>,
    ) -> ElementManagerError {
        ElementManagerError::AdditionalServicesNotSupported {
            name: name.into(),
            collection: collection.into(),
        }
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
/// # Parameters
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
    ///
    /// On success, the [`Element`] is returned back to the session.
    ///
    /// # Errors
    /// If the child cannot be created or bound in the current [`fidl_fuchsia_sys2::Realm`]. In
    /// particular, it is an error to call [`launch_element`] twice with the same `child_name`.
    async fn launch_element(
        &self,
        spec: felement::Spec,
        child_name: &str,
    ) -> Result<Element, ElementManagerError>;

    /// Handles requests to the [`Manager`] protocol.
    ///
    /// # Parameters
    /// `stream`: The stream that receives [`Manager`] requests.
    ///
    /// # Returns
    /// `Ok` if the request stream has been successfully handled once the client closes
    /// its connection. `Error` if a FIDL IO error was encountered.
    async fn handle_requests(
        &mut self,
        mut stream: felement::ManagerRequestStream,
    ) -> Result<(), fidl::Error>;
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
    /// - P: A FIDL service `Marker` type.
    ///
    /// # Returns
    /// - A service `Proxy` matching the `Marker`, or an error if the service is not available from
    /// the `Element`.
    #[inline]
    pub fn connect_to_service<P: DiscoverableProtocolMarker>(
        &self,
    ) -> Result<P::Proxy, anyhow::Error> {
        let (client_channel, server_channel) = zx::Channel::create()?;
        self.connect_to_service_with_channel::<P>(server_channel)?;
        Ok(P::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
    }

    /// Connect to a FIDL service provided by the `Element`.
    ///
    /// # Type Parameters
    /// - S: A FIDL service `Marker` type.
    ///
    /// # Returns
    /// - A service `Proxy` matching the `Marker`, or an error if the service is not available from
    /// the `Element`.
    #[inline]
    pub fn connect_to_unified_service<S: ServiceMarker>(&self) -> Result<S::Proxy, anyhow::Error> {
        fuchsia_component::client::connect_to_service_at_dir::<S>(&self.directory_channel())
    }

    /// Connect to a service by passing a channel for the server.
    ///
    /// # Type Parameters
    /// - P: A FIDL service `Marker` type.
    ///
    /// # Parameters
    /// - server_channel: The server-side endpoint of a channel pair, to bind to the requested
    /// service. The caller will interact with the service via the client-side endpoint.
    ///
    /// # Returns
    /// - Result::Ok or an error if the service is not available from the `Element`.
    #[inline]
    pub fn connect_to_service_with_channel<P: DiscoverableProtocolMarker>(
        &self,
        server_channel: zx::Channel,
    ) -> Result<(), anyhow::Error> {
        self.connect_to_named_service_with_channel(P::PROTOCOL_NAME, server_channel)
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
}

/// A list of services passed to an Element created from a CFv1 component.
///
/// This is a subset of fuchsia::sys::ServiceList that only supports a host
/// directory, not a ServiceProvider.
struct AdditionalServices {
    /// List of service names.
    pub names: Vec<String>,

    /// A channel to the directory hosting the services in `names`.
    pub host_directory: zx::Channel,
}

/// A [`SimpleElementManager`] creates and binds elements.
///
/// The [`SimpleElementManager`] provides no additional functionality for managing elements (e.g.,
/// tracking which elements are running, de-duplicating elements, etc.).
pub struct SimpleElementManager {
    /// The realm which this element manager uses to create components.
    realm: fsys2::RealmProxy,

    /// The collection in which elements will be launched.
    ///
    /// This is only used for elements that have a CFv2 (*.cm) component URL, and has no meaning
    /// for CFv1 elementes.
    ///
    /// The component that is running the `SimpleElementManager` must have a collection
    /// with the same name in its CML file.
    collection: String,

    /// A proxy to the `fuchsia::sys::Launcher` protocol used to create CFv1 components, or an error
    /// that represents a failure to connect to the protocol.
    ///
    /// If a launcher is not provided during intialization, it is requested from the environment.
    sys_launcher: Result<fsys::LauncherProxy, anyhow::Error>,

    /// Elements that were launched with no `Controller` provided by the client.
    ///
    /// Elements are added to this list to ensure they stay running when the
    /// client disconnects from `Manager`.
    uncontrolled_elements: Vec<Element>,
}

/// An element manager that launches v1 and v2 components, returning them to the caller.
impl SimpleElementManager {
    pub fn new(realm: fsys2::RealmProxy, collection: &str) -> SimpleElementManager {
        SimpleElementManager {
            realm,
            collection: collection.to_string(),
            sys_launcher: fuchsia_component::client::connect_to_protocol::<fsys::LauncherMarker>(),
            uncontrolled_elements: vec![],
        }
    }

    /// Initializer used by tests, to override the default fuchsia::sys::Launcher with a mock
    /// launcher.
    pub fn new_with_sys_launcher(
        realm: fsys2::RealmProxy,
        collection: &str,
        sys_launcher: fsys::LauncherProxy,
    ) -> Self {
        SimpleElementManager {
            realm,
            collection: collection.to_string(),
            sys_launcher: Ok(sys_launcher),
            uncontrolled_elements: vec![],
        }
    }

    /// Launches a CFv1 component as an element.
    ///
    /// # Parameters
    /// - `child_url`: The component url of the child added to the session. This function launches
    ///                components using a fuchsia::sys::Launcher. It supports all CFv1 component
    ///                URL schemes, such as URLs starting with `https`, and Fuchsia component URLs
    ///                ending in `.cmx`. Fuchsia components ending in `.cm` should use
    ///                `launch_child_component()` instead.
    /// - `additional_services`: Additional services to add the new component's namespace under /svc,
    ///                          in addition to those coming from the environment.
    ///
    /// # Returns
    /// An Element backed by the CFv1 component.
    async fn launch_v1_element(
        &self,
        child_url: &str,
        additional_services: Option<AdditionalServices>,
    ) -> Result<Element, ElementManagerError> {
        let sys_launcher = (&self.sys_launcher).as_ref().map_err(|err: &anyhow::Error| {
            ElementManagerError::not_launched(
                child_url.clone(),
                format!("Error connecting to fuchsia::sys::Launcher: {}", err.to_string()),
            )
        })?;

        let mut launch_options = fuchsia_component::client::LaunchOptions::new();
        if let Some(services) = additional_services {
            launch_options.set_additional_services(services.names, services.host_directory);
        }

        let app = fuchsia_component::client::launch_with_options(
            &sys_launcher,
            child_url.to_string(),
            None,
            launch_options,
        )
        .map_err(|err: anyhow::Error| {
            ElementManagerError::not_launched(child_url.clone(), err.to_string())
        })?;

        Ok(Element::from_app(app, child_url))
    }

    /// Launches a CFv2 component as an element.
    ///
    /// The component is created as a child in the Element Manager's realm.
    ///
    /// # Parameters
    /// - `child_name`: The name of the element, must be unique within a session. The name must be
    ///                 non-empty, of the form [a-z0-9-_.].
    /// - `child_url`: The component url of the child added to the session.
    ///
    /// # Returns
    /// An Element backed by the CFv2 component.
    async fn launch_v2_element(
        &self,
        child_name: &str,
        child_url: &str,
    ) -> Result<Element, ElementManagerError> {
        fx_log_info!(
            "launch_v2_element(name={}, url={}, collection={})",
            child_name,
            child_url,
            self.collection
        );
        realm_management::create_child_component(
            &child_name,
            &child_url,
            &self.collection,
            &self.realm,
        )
        .await
        .map_err(|err: fcomponent::Error| {
            ElementManagerError::not_created(child_name, &self.collection, child_url, err)
        })?;

        let directory_channel =
            match realm_management::bind_child_component(child_name, &self.collection, &self.realm)
                .await
            {
                Ok(channel) => channel,
                Err(err) => {
                    return Err(ElementManagerError::not_bound(
                        child_name,
                        self.collection.clone(),
                        child_url,
                        err,
                    ))
                }
            };
        Ok(Element::from_directory_channel(
            directory_channel,
            child_name,
            child_url,
            &self.collection,
        ))
    }
}

/// Handles Controller protocol requests.
///
/// # Parameters
/// - `stream`: the input channel that receives [`Controller`] requests.
/// - `element`: the [`Element`] that is being controlled.
///
/// # Returns
/// () when there are no more valid requests.
async fn handle_controller_requests(
    mut stream: felement::ControllerRequestStream,
    _element: Element,
) {
    while let Ok(Some(request)) = stream.try_next().await {
        match request {
            felement::ControllerRequest::UpdateAnnotations {
                annotations_to_set: _,
                annotations_to_delete: _,
                responder,
            } => {
                fx_log_err!(
                    "TODO(fxbug.dev/65759): Controller.UpdateAnnotations is not implemented",
                );
                responder.control_handle().shutdown_with_epitaph(zx::Status::UNAVAILABLE);
            }
            felement::ControllerRequest::GetAnnotations { responder } => {
                fx_log_err!("TODO(fxbug.dev/65759): Controller.GetAnnotations is not implemented");
                responder.control_handle().shutdown_with_epitaph(zx::Status::UNAVAILABLE);
            }
        }
    }
}

#[async_trait]
impl ElementManager for SimpleElementManager {
    async fn launch_element(
        &self,
        spec: felement::Spec,
        child_name: &str,
    ) -> Result<Element, ElementManagerError> {
        let child_url = spec
            .component_url
            .ok_or_else(|| ElementManagerError::url_missing(child_name, &self.collection))?;

        let additional_services =
            spec.additional_services.map_or(Ok(None), |services| match services {
                fsys::ServiceList {
                    names,
                    host_directory: Some(service_host_directory),
                    provider: None,
                } => Ok(Some(AdditionalServices { names, host_directory: service_host_directory })),
                _ => Err(ElementManagerError::invalid_service_list(child_name, &self.collection)),
            })?;

        let element = if is_v2_component(&child_url) {
            // `additional_services` is only supported for CFv1 components.
            if additional_services.is_some() {
                return Err(ElementManagerError::additional_services_not_supported(
                    child_name,
                    &self.collection,
                ));
            }

            self.launch_v2_element(&child_name, &child_url).await?
        } else {
            self.launch_v1_element(&child_url, additional_services).await?
        };

        Ok(element)
    }

    async fn handle_requests(
        &mut self,
        mut stream: felement::ManagerRequestStream,
    ) -> Result<(), fidl::Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                felement::ManagerRequest::ProposeElement { spec, controller, responder } => {
                    let mut child_name: String =
                        thread_rng().sample_iter(&Alphanumeric).take(16).collect();
                    child_name.make_ascii_lowercase();

                    let mut result = match self.launch_element(spec, &child_name).await {
                        Ok(element) => {
                            match controller {
                                Some(controller) => match controller.into_stream() {
                                    Ok(stream) => {
                                        fasync::Task::spawn(handle_controller_requests(
                                            stream, element,
                                        ))
                                        .detach();
                                        Ok(())
                                    }
                                    Err(err) => {
                                        fx_log_err!(
                                            "Failed to convert Controller request to stream: {:?}",
                                            err
                                        );
                                        Err(felement::ProposeElementError::InvalidArgs)
                                    }
                                },
                                // If the element proposer did not provide a controller, add the
                                // element to a vector to keep it alive:
                                None => {
                                    self.uncontrolled_elements.push(element);
                                    Ok(())
                                }
                            }
                        }
                        Err(err) => {
                            fx_log_err!("Failed to launch element: {:?}", err);

                            match err {
                                ElementManagerError::UrlMissing { .. } => {
                                    Err(felement::ProposeElementError::NotFound)
                                }
                                ElementManagerError::InvalidServiceList { .. }
                                | ElementManagerError::AdditionalServicesNotSupported { .. }
                                | ElementManagerError::NotCreated { .. }
                                | ElementManagerError::NotBound { .. }
                                | ElementManagerError::NotLaunched { .. } => {
                                    Err(felement::ProposeElementError::InvalidArgs)
                                }
                            }
                        }
                    };

                    let _ = responder.send(&mut result);
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{ElementManager, ElementManagerError, SimpleElementManager},
        fidl::endpoints::{spawn_stream_handler, ServerEnd},
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_element as felement,
        fidl_fuchsia_io as fio, fidl_fuchsia_sys as fsys, fidl_fuchsia_sys2 as fsys2,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::{channel::mpsc::channel, prelude::*},
        lazy_static::lazy_static,
        test_util::Counter,
    };

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

    /// Tests that launching a component with a cmx file successfully returns an Element
    /// with outgoing directory routing appropriate for v1 components.
    #[fasync::run_until_stalled(test)]
    async fn launch_v1_element_success() {
        lazy_static! {
            static ref CREATE_COMPONENT_CALL_COUNT: Counter = Counter::new(0);
        }

        const ELEMENT_COUNT: usize = 1;

        let component_url = "test_url.cmx";
        let child_name = "child";
        let child_collection = "elements";

        let realm = spawn_stream_handler(move |_realm_request| async move {
            panic!("Realm should not receive any requests as it's only used for v2 components")
        })
        .unwrap();

        let (directory_open_sender, directory_open_receiver) = channel::<String>(1);
        let directory_request_handler = move |directory_request| match directory_request {
            fio::DirectoryRequest::Open { path: capability_path, .. } => {
                let mut result_sender = directory_open_sender.clone();
                fasync::Task::spawn(async move {
                    let _ = result_sender.send(capability_path).await;
                })
                .detach()
            }
            _ => panic!("Directory handler received an unexpected request"),
        };

        let (create_component_sender, create_component_receiver) = channel::<()>(ELEMENT_COUNT);
        let launcher = spawn_stream_handler(move |launcher_request| {
            let directory_request_handler = directory_request_handler.clone();
            let mut result_sender = create_component_sender.clone();
            async move {
                match launcher_request {
                    fsys::LauncherRequest::CreateComponent {
                        launch_info: fsys::LaunchInfo { url, directory_request, .. },
                        ..
                    } => {
                        assert_eq!(url, component_url);
                        spawn_directory_server(
                            ServerEnd::<fio::DirectoryMarker>::new(directory_request.unwrap())
                                .into_stream()
                                .unwrap(),
                            directory_request_handler,
                        );
                        fasync::Task::spawn(async move {
                            let _ = result_sender.send(()).await;
                            CREATE_COMPONENT_CALL_COUNT.inc();
                        })
                        .detach()
                    }
                }
            }
        })
        .unwrap();

        let element_manager =
            SimpleElementManager::new_with_sys_launcher(realm, child_collection, launcher);
        let result = element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    ..felement::Spec::EMPTY
                },
                child_name,
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

    /// Tests that launching multiple v1 components (without "*.cm") successfully returns [`Ok`].
    #[fasync::run_until_stalled(test)]
    async fn launch_multiple_v1_element_success() {
        lazy_static! {
            static ref CREATE_COMPONENT_CALL_COUNT: Counter = Counter::new(0);
        }

        const ELEMENT_COUNT: usize = 2;

        let (sender, receiver) = channel::<()>(ELEMENT_COUNT);

        let a_component_url = "a_url.cmx";
        let a_child_name = "a_child";

        let b_component_url = "https://google.com";
        let b_child_name = "b_child";

        let child_collection = "elements";

        let realm = spawn_stream_handler(move |_realm_request| async move {
            panic!("Realm should not receive any requests as it's only used for v2 components")
        })
        .unwrap();

        let launcher = spawn_stream_handler(move |launcher_request| {
            let mut result_sender = sender.clone();
            async move {
                match launcher_request {
                    fsys::LauncherRequest::CreateComponent {
                        launch_info: fsys::LaunchInfo { .. },
                        ..
                    } => fasync::Task::spawn(async move {
                        let _ = result_sender.send(()).await.expect("Could not create component.");
                        CREATE_COMPONENT_CALL_COUNT.inc();
                    })
                    .detach(),
                }
            }
        })
        .unwrap();

        let element_manager =
            SimpleElementManager::new_with_sys_launcher(realm, child_collection, launcher);
        assert!(element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(a_component_url.to_string()),
                    ..felement::Spec::EMPTY
                },
                a_child_name,
            )
            .await
            .is_ok());
        assert!(element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(b_component_url.to_string()),
                    ..felement::Spec::EMPTY
                },
                b_child_name,
            )
            .await
            .is_ok());

        // Verify that the CreateComponent was actually called.
        receiver.into_future().await;
        assert_eq!(CREATE_COMPONENT_CALL_COUNT.get(), ELEMENT_COUNT);
    }

    /// Tests that launching an element with a *.cmx URL and `additional_services` ServiceList
    /// passes the services to the component Launcher.
    #[fasync::run_until_stalled(test)]
    async fn launch_v1_element_with_additional_services() {
        lazy_static! {
            static ref CREATE_COMPONENT_CALL_COUNT: Counter = Counter::new(0);
        }

        let component_url = "test_url.cmx";
        let child_name = "child";
        let child_collection = "elements";

        const ELEMENT_COUNT: usize = 1;

        let realm = spawn_stream_handler(move |_realm_request| async move {
            panic!("Realm should not receive any requests as it's only used for v2 components")
        })
        .unwrap();

        // Spawn a directory server from which the element can connect to services.
        let (directory_open_sender, directory_open_receiver) = channel::<String>(1);
        let directory_request_handler = move |directory_request| match directory_request {
            fio::DirectoryRequest::Open { path: capability_path, .. } => {
                let mut result_sender = directory_open_sender.clone();
                fasync::Task::spawn(async move {
                    let _ = result_sender.send(capability_path).await;
                })
                .detach()
            }
            _ => panic!("Directory handler received an unexpected request"),
        };

        let (dir_client, dir_server) = fidl::Channel::create().unwrap();
        spawn_directory_server(
            ServerEnd::<fio::DirectoryMarker>::new(dir_server).into_stream().unwrap(),
            directory_request_handler.clone(),
        );

        // The element receives the client side handle to the directory
        // through `additional_services.host_directory`.
        let service_name = "myService";
        let additional_services = fsys::ServiceList {
            names: vec![service_name.to_string()],
            host_directory: Some(dir_client),
            provider: None,
        };

        let (create_component_sender, create_component_receiver) = channel::<()>(ELEMENT_COUNT);
        let launcher = spawn_stream_handler(move |launcher_request| {
            let mut result_sender = create_component_sender.clone();
            async move {
                match launcher_request {
                    fsys::LauncherRequest::CreateComponent {
                        launch_info: fsys::LaunchInfo { url, additional_services, .. },
                        ..
                    } => {
                        assert_eq!(url, component_url);

                        assert!(additional_services.is_some());
                        let services = additional_services.unwrap();
                        assert!(services.host_directory.is_some());
                        let host_directory = services.host_directory.unwrap();

                        assert_eq!(vec![service_name.to_string()], services.names);

                        // Connect to the service hosted in `additional_services.host_directory`.
                        let (_client_channel, server_channel) = zx::Channel::create().unwrap();
                        fdio::service_connect_at(&host_directory, service_name, server_channel)
                            .expect("could not connect to service");

                        fasync::Task::spawn(async move {
                            let _ = result_sender.send(()).await;
                            CREATE_COMPONENT_CALL_COUNT.inc();
                        })
                        .detach()
                    }
                }
            }
        })
        .unwrap();

        let element_manager =
            SimpleElementManager::new_with_sys_launcher(realm, child_collection, launcher);
        let result = element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    additional_services: Some(additional_services),
                    ..felement::Spec::EMPTY
                },
                child_name,
            )
            .await;
        let element = result.unwrap();
        assert!(format!("{:?}", element).contains(component_url));

        // Verify that the CreateComponent was actually called.
        create_component_receiver.into_future().await;
        assert_eq!(CREATE_COMPONENT_CALL_COUNT.get(), ELEMENT_COUNT);

        // Verify that the element opened a path in `host_directory` that matches the
        // service name as a result of connecting to the service.
        let open_paths = directory_open_receiver.take(1).collect::<Vec<_>>().await;
        assert_eq!(vec![service_name], open_paths);
    }

    /// Tests that launching a *.cm element successfully returns an Element with
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
            _ => panic!("Directory handler received an unexpected request"),
        };

        let realm = spawn_stream_handler(move |realm_request| {
            let directory_request_handler = directory_request_handler.clone();
            async move {
                match realm_request {
                    fsys2::RealmRequest::CreateChild { collection, decl, args: _, responder } => {
                        assert_eq!(decl.url.unwrap(), component_url);
                        assert_eq!(decl.name.unwrap(), child_name);
                        assert_eq!(&collection.name, child_collection);

                        let _ = responder.send(&mut Ok(()));
                    }
                    fsys2::RealmRequest::BindChild { child, exposed_dir, responder } => {
                        assert_eq!(child.collection, Some(child_collection.to_string()));
                        spawn_directory_server(
                            exposed_dir.into_stream().unwrap(),
                            directory_request_handler,
                        );
                        let _ = responder.send(&mut Ok(()));
                    }
                    _ => panic!("Realm handler received an unexpected request"),
                }
            }
        })
        .unwrap();

        let launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("Launcher should not receive any requests as it's only used for v1 components")
        })
        .unwrap();

        let element_manager =
            SimpleElementManager::new_with_sys_launcher(realm, child_collection, launcher);

        let result = element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    ..felement::Spec::EMPTY
                },
                child_name,
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

        let realm = spawn_stream_handler(move |realm_request| async move {
            match realm_request {
                fsys2::RealmRequest::CreateChild { collection, decl, args: _, responder } => {
                    assert_eq!(decl.url.unwrap(), component_url);
                    assert_eq!(decl.name.unwrap(), child_name);
                    assert_eq!(&collection.name, child_collection);

                    let _ = responder.send(&mut Ok(()));
                }
                fsys2::RealmRequest::BindChild { child, exposed_dir: _, responder } => {
                    assert_eq!(child.collection, Some(child_collection.to_string()));

                    let _ = responder.send(&mut Ok(()));
                }
                _ => panic!("Realm handler received an unexpected request"),
            }
        })
        .unwrap();

        let launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("Launcher should not receive any requests as it's only used for v1 components")
        })
        .unwrap();

        let element_manager =
            SimpleElementManager::new_with_sys_launcher(realm, child_collection, launcher);

        assert!(element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    ..felement::Spec::EMPTY
                },
                child_name,
            )
            .await
            .is_ok());
    }

    /// Tests that launching an element with no URL returns the appropriate error.
    #[fasync::run_until_stalled(test)]
    async fn launch_element_no_url() {
        let realm = spawn_stream_handler(move |_realm_request| async move {
            panic!("Realm should not receive any requests as there is no component to launch")
        })
        .unwrap();

        let launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("Launcher should not receive any requests as there is no component to launch")
        })
        .unwrap();

        let element_manager = SimpleElementManager::new_with_sys_launcher(realm, "", launcher);

        let result = element_manager
            .launch_element(felement::Spec { component_url: None, ..felement::Spec::EMPTY }, "")
            .await;
        assert!(result.is_err());
        assert_eq!(result.err().unwrap(), ElementManagerError::url_missing("", ""));
    }

    /// Tests that launching a CFv1 element with a ServiceList that specifies a `provider`
    /// returns `ElementManagerError::InvalidServiceList`.
    #[fasync::run_until_stalled(test)]
    async fn launch_v1_element_with_service_list_provider() {
        let component_url = "fuchsia-pkg://fuchsia.com/simple_element#meta/simple_element.cmx";

        let (channel, _) = fidl::Channel::create().unwrap();
        let provider = fidl::endpoints::ClientEnd::<fsys::ServiceProviderMarker>::new(channel);

        // This ServiceList is invalid because it specifies a `provider` that is not None.
        let additional_services = fsys::ServiceList {
            names: vec!["fuchsia.service.Foo".to_string()],
            host_directory: None,
            provider: Some(provider),
        };

        let launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("Launcher should not receive any requests as the spec is invalid")
        })
        .unwrap();

        let realm = spawn_stream_handler(move |_realm_request| async move {
            panic!("Realm should not receive any requests since the child won't be created")
        })
        .unwrap();

        let element_manager = SimpleElementManager::new_with_sys_launcher(realm, "", launcher);

        let result = element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    additional_services: Some(additional_services),
                    ..felement::Spec::EMPTY
                },
                "",
            )
            .await;
        assert!(result.is_err());
        assert_eq!(result.err().unwrap(), ElementManagerError::invalid_service_list("", ""));
    }

    /// Tests that launching a *.cm element with a spec that contains `additional_services`
    /// returns `ProposeElementError::AdditionalServicesNotSupported`
    #[fasync::run_until_stalled(test)]
    async fn launch_v2_element_with_additional_services() {
        // This is a CFv2 component URL. `additional_services` is only supported for v1 components.
        let component_url = "fuchsia-pkg://fuchsia.com/simple_element#meta/simple_element.cm";

        let (channel, _) = fidl::Channel::create().unwrap();
        let additional_services = fsys::ServiceList {
            names: vec!["fuchsia.service.Foo".to_string()],
            host_directory: Some(channel),
            provider: None,
        };

        let launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("Launcher should not receive any requests as it's only used for v1 components")
        })
        .unwrap();

        let realm = spawn_stream_handler(move |_realm_request| async move {
            panic!("Realm should not receive any requests since the child won't be created")
        })
        .unwrap();
        let element_manager = SimpleElementManager::new_with_sys_launcher(realm, "", launcher);

        let result = element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    additional_services: Some(additional_services),
                    ..felement::Spec::EMPTY
                },
                "",
            )
            .await;
        assert!(result.is_err());
        assert_eq!(
            result.err().unwrap(),
            ElementManagerError::additional_services_not_supported("", "")
        );
    }

    /// Tests that launching an element which is not successfully created in the realm returns an
    /// appropriate error.
    #[fasync::run_until_stalled(test)]
    async fn launch_element_create_error_internal() {
        let component_url = "fuchsia-pkg://fuchsia.com/simple_element#meta/simple_element.cm";

        let launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("Launcher should not receive any requests as it's only used for v1 components")
        })
        .unwrap();

        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_stream_handler(move |realm_request| async move {
            match realm_request {
                fsys2::RealmRequest::CreateChild { collection: _, decl: _, args: _, responder } => {
                    let _ = responder.send(&mut Err(fcomponent::Error::Internal));
                }
                _ => panic!("Realm handler received an unexpected request"),
            }
        })
        .unwrap();
        let element_manager = SimpleElementManager::new_with_sys_launcher(realm, "", launcher);

        let result = element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    ..felement::Spec::EMPTY
                },
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

        let launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("Launcher should not receive any requests as it's only used for v1 components")
        })
        .unwrap();

        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_stream_handler(move |realm_request| async move {
            match realm_request {
                fsys2::RealmRequest::CreateChild { collection: _, decl: _, args: _, responder } => {
                    let _ = responder.send(&mut Err(fcomponent::Error::ResourceUnavailable));
                }
                _ => panic!("Realm handler received an unexpected request"),
            }
        })
        .unwrap();
        let element_manager = SimpleElementManager::new_with_sys_launcher(realm, "", launcher);

        let result = element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    ..felement::Spec::EMPTY
                },
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

        let launcher = spawn_stream_handler(move |_launcher_request| async move {
            panic!("Launcher should not receive any requests as it's only used for v1 components")
        })
        .unwrap();

        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_stream_handler(move |realm_request| async move {
            match realm_request {
                fsys2::RealmRequest::CreateChild { collection: _, decl: _, args: _, responder } => {
                    let _ = responder.send(&mut Ok(()));
                }
                fsys2::RealmRequest::BindChild { child: _, exposed_dir: _, responder } => {
                    let _ = responder.send(&mut Err(fcomponent::Error::InstanceCannotStart));
                }
                _ => panic!("Realm handler received an unexpected request"),
            }
        })
        .unwrap();
        let element_manager = SimpleElementManager::new_with_sys_launcher(realm, "", launcher);

        let result = element_manager
            .launch_element(
                felement::Spec {
                    component_url: Some(component_url.to_string()),
                    ..felement::Spec::EMPTY
                },
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
