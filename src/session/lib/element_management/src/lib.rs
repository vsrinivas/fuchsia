// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The `ElementManagement` library provides utilities for Sessions to service
//! incoming [`fidl_fuchsia_session::ElementManagerRequest`]s.
//!
//! Elements are instantiated as dynamic component instances in a component collection of the
//! calling component.

use {
    async_trait::async_trait, failure::Fail, fidl_fuchsia_session::ElementSpec,
    fidl_fuchsia_sys::LauncherProxy, fidl_fuchsia_sys2 as fsys, fuchsia_component,
    realm_management,
};

/// Errors returned by calls to [`ElementManager`].
#[derive(Debug, Fail, Clone, PartialEq)]
pub enum ElementManagerError {
    /// Returned when the element manager fails to created the component instance associated with
    /// a given element.
    #[fail(display = "Element spec for \"{}/{}\" missing url.", name, collection)]
    UrlMissing { name: String, collection: String },

    /// Returned when the element manager fails to created the component instance associated with
    /// a given element.
    #[fail(display = "Element {} not created at \"{}/{}\": {:?}", url, collection, name, err)]
    NotCreated { name: String, collection: String, url: String, err: fsys::Error },

    /// Returned when the element manager fails to bind to the component instance associated with
    /// a given element.
    #[fail(display = "Element {} not bound at \"{}/{}\": {:?}", url, collection, name, err)]
    NotBound { name: String, collection: String, url: String, err: fsys::Error },
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
        err: impl Into<fsys::Error>,
    ) -> ElementManagerError {
        ElementManagerError::NotCreated {
            name: name.into(),
            collection: collection.into(),
            url: url.into(),
            err: err.into(),
        }
    }

    pub fn not_bound(
        name: impl Into<String>,
        collection: impl Into<String>,
        url: impl Into<String>,
        err: impl Into<fsys::Error>,
    ) -> ElementManagerError {
        ElementManagerError::NotBound {
            name: name.into(),
            collection: collection.into(),
            url: url.into(),
            err: err.into(),
        }
    }
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
    /// # Errors
    /// If the child cannot be created or bound in the current [`fidl_fuchsia_sys2::Realm`]. In
    /// particular, it is an error to call [`add_element`] twice with the same `child_name`.
    async fn add_element(
        &mut self,
        spec: ElementSpec,
        child_name: &str,
        child_collection: &str,
    ) -> Result<(), ElementManagerError>;
}

/// A [`SimpleElementManager`] creates and binds elements.
///
/// The [`SimpleElementManager`] provides no additional functionality for managing elements (e.g.,
/// tracking which elements are running, de-duplicating elements, etc.).
pub struct SimpleElementManager {
    /// The realm which this element manager uses to create components.
    realm: fsys::RealmProxy,

    /// The launcher which this element manager uses to create a component with a *.cmx file.
    launcher: Option<LauncherProxy>,

    /// Represents the launched cmx applications. Needs to stay alive to keep the service running.
    apps: Vec<fuchsia_component::client::App>,
}

impl SimpleElementManager {
    pub fn new(realm: fsys::RealmProxy, launcher: Option<LauncherProxy>) -> SimpleElementManager {
        SimpleElementManager { realm, launcher, apps: vec![] }
    }
}

/// Checks whether the component is a *.cmx or not
///
/// #Parameters
/// - `component_url`: The component url.
fn is_cmx(component_url: &str) -> bool {
    component_url.ends_with(".cmx")
}

/// Launches a component ending in *.cmx in the session.
///
/// #Parameters
/// - `child_url`: The component url of the child added to the session.
/// - `launcher`: The launcher which will create the component in the session.
///
/// #Returns
/// The launched application.
fn add_cmx_element(
    child_url: &str,
    launcher: &LauncherProxy,
) -> Result<fuchsia_component::client::App, failure::Error> {
    fuchsia_component::client::launch(launcher, child_url.to_string(), None)
}

/// Adds a v2 component element to the session.
///
/// #Parameters
/// - `child_name`: The name of the element, must be unique within a session. The name must be
///                 non-empty, of the form [a-z0-9-_.].
/// - `child_url`: The component url of the child added to the session.
/// - `child_collection`: The collection to add the element in, must match a collection in the
///                       calling component's CML file.
/// - `realm`: The `Realm` to which the child will be added.
pub async fn add_cml_element(
    child_name: &str,
    child_url: &str,
    child_collection: &str,
    realm: &fsys::RealmProxy,
) -> Result<(), ElementManagerError> {
    realm_management::create_child_component(&child_name, &child_url, child_collection, &realm)
        .await
        .map_err(|err: fsys::Error| {
            ElementManagerError::not_created(child_name, child_collection, child_url, err)
        })?;

    realm_management::bind_child_component(child_name, child_collection, &realm).await.map_err(
        |err: fsys::Error| {
            ElementManagerError::not_bound(child_name, child_collection, child_url, err)
        },
    )?;
    Ok(())
}

#[async_trait]
impl ElementManager for SimpleElementManager {
    async fn add_element(
        &mut self,
        spec: ElementSpec,
        child_name: &str,
        child_collection: &str,
    ) -> Result<(), ElementManagerError> {
        let child_url = spec
            .component_url
            .ok_or_else(|| ElementManagerError::url_missing(child_name, child_collection))?;

        if is_cmx(&child_url) {
            let app = add_cmx_element(
                &child_url,
                self.launcher.as_ref().ok_or_else(|| {
                    ElementManagerError::not_bound(
                        child_name,
                        child_collection,
                        child_url.clone(),
                        fsys::Error::Internal,
                    )
                })?,
            )
            .unwrap();
            self.apps.push(app);
        } else {
            add_cml_element(&child_name, &child_url, child_collection, &self.realm).await?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{ElementManager, ElementManagerError, SimpleElementManager},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_session::ElementSpec,
        fidl_fuchsia_sys::{LaunchInfo, LauncherMarker, LauncherProxy, LauncherRequest},
        fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
        futures::{channel::mpsc::channel, prelude::*},
    };

    /// Spawns a local `fidl_fuchsia_sys2::Realm` server, and returns a proxy to the spawned server.
    /// The provided `request_handler` is notified when an incoming request is received.
    ///
    /// # Parameters
    /// - `request_handler`: A function which is called with incoming requests to the spawned
    ///                      `Realm` server.
    /// # Returns
    /// A `RealmProxy` to the spawned server.
    fn spawn_realm_server<F: 'static>(request_handler: F) -> fsys::RealmProxy
    where
        F: Fn(fsys::RealmRequest) + Send,
    {
        let (realm_proxy, mut realm_server) = create_proxy_and_stream::<fsys::RealmMarker>()
            .expect("Failed to create realm proxy and server.");

        fasync::spawn(async move {
            while let Some(realm_request) = realm_server.try_next().await.unwrap() {
                request_handler(realm_request);
            }
        });

        realm_proxy
    }

    /// Spawns a local `fidl_fuchsia_sys::Launcher` server, and returns a proxy to the spawned server.
    /// The provided `request_handler` is notified when an incoming request is received.
    ///
    /// # Parameters
    /// - `request_handler`: A function which is called with incoming requests to the spawned
    ///                      `Launcher` server.
    /// # Returns
    /// A `LauncherProxy` to the spawned server.
    fn spawn_launcher_server<F: 'static>(request_handler: F) -> LauncherProxy
    where
        F: Fn(LauncherRequest) + Send,
    {
        let (launcher_proxy, mut launcher_server) = create_proxy_and_stream::<LauncherMarker>()
            .expect("Failed to create launcher proxy and server.");

        fasync::spawn(async move {
            while let Some(launcher_request) = launcher_server.try_next().await.unwrap() {
                request_handler(launcher_request);
            }
        });

        launcher_proxy
    }

    /// Tests that adding a component with a cmx file successfully returns [`Ok`].
    #[fasync::run_singlethreaded(test)]
    async fn add_v1_element_success() {
        let (sender, receiver) = channel::<()>(1);

        let component_url = "test_url.cmx";
        let child_name = "child";
        let child_collection = "elements";
        let realm = spawn_realm_server(move |realm_request| match realm_request {
            _ => {
                // CFv1 elements do not use the realm so fail the test if it is requested.
                assert!(false);
            }
        });

        let launcher = spawn_launcher_server(move |launcher_request| match launcher_request {
            LauncherRequest::CreateComponent { launch_info: LaunchInfo { url, .. }, .. } => {
                assert_eq!(url, component_url);
                let mut result_sender = sender.clone();
                fasync::spawn(async move {
                    let _ = result_sender.send(()).await.expect("Could not create component.");
                })
            }
        });

        let mut element_manager = SimpleElementManager::new(realm, Some(launcher));
        assert!(element_manager
            .add_element(
                ElementSpec { component_url: Some(component_url.to_string()) },
                child_name,
                child_collection,
            )
            .await
            .is_ok());

        // Verify that the CreateComponent was actually called.
        receiver.into_future().await;
        assert_eq!(element_manager.apps.len(), 1);
    }

    /// Tests that adding multiple cmx components successfully returns [`Ok`] and the components are properly stored in `apps`.
    #[fasync::run_singlethreaded(test)]
    async fn add_multiple_v1_element_success() {
        let (sender, receiver) = channel::<()>(1);

        let a_component_url = "a_url.cmx";
        let a_child_name = "a_child";
        let a_child_collection = "elements";

        let b_component_url = "b_url.cmx";
        let b_child_name = "b_child";
        let b_child_collection = "elements";

        let realm = spawn_realm_server(move |realm_request| match realm_request {
            _ => {
                // CFv1 elements do not use the realm so fail the test if it is requested.
                assert!(false);
            }
        });

        let launcher = spawn_launcher_server(move |launcher_request| match launcher_request {
            LauncherRequest::CreateComponent { launch_info: LaunchInfo { .. }, .. } => {
                let mut result_sender = sender.clone();
                fasync::spawn(async move {
                    let _ = result_sender.send(()).await.expect("Could not create component.");
                })
            }
        });

        let mut element_manager = SimpleElementManager::new(realm, Some(launcher));
        assert!(element_manager
            .add_element(
                ElementSpec { component_url: Some(a_component_url.to_string()) },
                a_child_name,
                a_child_collection,
            )
            .await
            .is_ok());
        assert!(element_manager
            .add_element(
                ElementSpec { component_url: Some(b_component_url.to_string()) },
                b_child_name,
                b_child_collection,
            )
            .await
            .is_ok());

        assert_eq!(element_manager.apps.len(), 2);
        // Verify that the CreateComponent was actually called.
        receiver.into_future().await;
    }

    /// Tests that adding a *.cm element successfully returns [`Ok`].
    #[fasync::run_singlethreaded(test)]
    async fn add_element_success() {
        let component_url = "test_url";
        let child_name = "child";
        let child_collection = "elements";

        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::CreateChild { collection, decl, responder } => {
                assert_eq!(decl.url.unwrap(), component_url);
                assert_eq!(decl.name.unwrap(), child_name);
                assert_eq!(&collection.name, child_collection);

                let _ = responder.send(&mut Ok(()));
            }
            fsys::RealmRequest::BindChild { child, exposed_dir: _, responder } => {
                assert_eq!(child.collection, Some(child_collection.to_string()));

                let _ = responder.send(&mut Ok(()));
            }
            _ => {
                assert!(false);
            }
        });
        let mut element_manager = SimpleElementManager::new(realm, None);
        assert!(element_manager
            .add_element(
                ElementSpec { component_url: Some(component_url.to_string()) },
                child_name,
                child_collection,
            )
            .await
            .is_ok());
    }

    /// Tests that adding an element does not use the launcher.
    #[fasync::run_singlethreaded(test)]
    async fn add_element_success_not_use_launcher() {
        let component_url = "test_url";
        let child_name = "child";
        let child_collection = "elements";

        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::CreateChild { collection, decl, responder } => {
                assert_eq!(decl.url.unwrap(), component_url);
                assert_eq!(decl.name.unwrap(), child_name);
                assert_eq!(&collection.name, child_collection);

                let _ = responder.send(&mut Ok(()));
            }
            fsys::RealmRequest::BindChild { child, exposed_dir: _, responder } => {
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
        let mut element_manager = SimpleElementManager::new(realm, Some(launcher));
        assert!(element_manager
            .add_element(
                ElementSpec { component_url: Some(component_url.to_string()) },
                child_name,
                child_collection,
            )
            .await
            .is_ok());
    }

    /// Tests that adding an element with no URL returns the appropriate error.
    #[fasync::run_singlethreaded(test)]
    async fn add_element_no_url() {
        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_realm_server(move |realm_request| match realm_request {
            _ => {
                assert!(false);
            }
        });
        let mut element_manager = SimpleElementManager::new(realm, None);

        assert_eq!(
            element_manager.add_element(ElementSpec { component_url: None }, "", "").await,
            Err(ElementManagerError::url_missing("", ""))
        );
    }

    /// Tests that adding an element which is not successfully created in the realm returns an
    /// appropriate error.
    #[fasync::run_singlethreaded(test)]
    async fn add_element_create_error_internal() {
        let component_url = "test_url";

        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::CreateChild { collection: _, decl: _, responder } => {
                let _ = responder.send(&mut Err(fsys::Error::Internal));
            }
            _ => {
                assert!(false);
            }
        });
        let mut element_manager = SimpleElementManager::new(realm, None);

        assert_eq!(
            element_manager
                .add_element(
                    ElementSpec { component_url: Some(component_url.to_string()) },
                    "",
                    "",
                )
                .await,
            Err(ElementManagerError::not_created("", "", component_url, fsys::Error::Internal))
        );
    }

    /// Tests that adding an element which is not successfully created in the realm returns an
    /// appropriate error.
    #[fasync::run_singlethreaded(test)]
    async fn add_element_create_error_no_space() {
        let component_url = "test_url";

        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::CreateChild { collection: _, decl: _, responder } => {
                let _ = responder.send(&mut Err(fsys::Error::NoSpace));
            }
            _ => {
                assert!(false);
            }
        });
        let mut element_manager = SimpleElementManager::new(realm, None);

        assert_eq!(
            element_manager
                .add_element(
                    ElementSpec { component_url: Some(component_url.to_string()) },
                    "",
                    "",
                )
                .await,
            Err(ElementManagerError::not_created("", "", component_url, fsys::Error::NoSpace))
        );
    }

    /// Tests that adding an element which is not successfully bound in the realm returns an
    /// appropriate error.
    #[fasync::run_singlethreaded(test)]
    async fn add_element_bind_error() {
        let component_url = "test_url";

        // The following match errors if it sees a bind request: since the child was not created
        // successfully the bind should not be called.
        let realm = spawn_realm_server(move |realm_request| match realm_request {
            fsys::RealmRequest::CreateChild { collection: _, decl: _, responder } => {
                let _ = responder.send(&mut Ok(()));
            }
            fsys::RealmRequest::BindChild { child: _, exposed_dir: _, responder } => {
                let _ = responder.send(&mut Err(fsys::Error::InstanceCannotStart));
            }
            _ => {
                assert!(false);
            }
        });
        let mut element_manager = SimpleElementManager::new(realm, None);

        assert_eq!(
            element_manager
                .add_element(
                    ElementSpec { component_url: Some(component_url.to_string()) },
                    "",
                    "",
                )
                .await,
            Err(ElementManagerError::not_bound(
                "",
                "",
                component_url,
                fsys::Error::InstanceCannotStart
            ))
        );
    }
}
