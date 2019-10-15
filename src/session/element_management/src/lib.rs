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
    fidl_fuchsia_sys2 as fsys, realm_management,
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
        &self,
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
    realm: fsys::RealmProxy
}

impl SimpleElementManager {
    pub fn new(realm: fsys::RealmProxy) -> SimpleElementManager {
        SimpleElementManager { realm }
    }
}

#[async_trait]
impl ElementManager for SimpleElementManager {
    async fn add_element(
        &self,
        spec: ElementSpec,
        child_name: &str,
        child_collection: &str,
    ) -> Result<(), ElementManagerError> {
        let child_url = spec
            .component_url
            .ok_or_else(|| ElementManagerError::url_missing(child_name, child_collection))?;

        realm_management::create_child(&child_name, &child_url, child_collection, &self.realm)
            .await
            .map_err(|err: fsys::Error| {
                ElementManagerError::not_created(child_name, child_collection, &child_url, err)
            })?;

        realm_management::bind_child(&child_name, child_collection, &self.realm).await.map_err(
            |err: fsys::Error| {
                ElementManagerError::not_bound(child_name, child_collection, &child_url, err)
            },
        )?;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{ElementManager, ElementManagerError, SimpleElementManager},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_session::ElementSpec,
        fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
        futures::prelude::*,
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

    /// Tests that adding an element successfully returns [`Ok`].
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
        let element_manager = SimpleElementManager::new(realm);

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
        let element_manager = SimpleElementManager::new(realm);

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
        let element_manager = SimpleElementManager::new(realm);

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
        let element_manager = SimpleElementManager::new(realm);

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
        let element_manager = SimpleElementManager::new(realm);

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
