// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{ResultExt as TokenManagerResultExt, TokenManagerError};
use failure::{format_err, ResultExt};
use fidl::endpoints::{create_endpoints, ClientEnd, DiscoverableService, ServerEnd};
use fidl_fuchsia_auth::AuthProviderMarker;
use fidl_fuchsia_auth::{AuthProviderConfig, Status};
use fidl_fuchsia_identity_external::{OauthMarker, OauthOpenIdConnectMarker, OpenIdConnectMarker};
use fuchsia_component::client::{launch, launcher, App};
use log::info;
use parking_lot::Mutex;
use std::sync::Arc;

/// A type capable of launching and connecting to a component that implements the
/// `AuthProvider` protocol. Launching is performed on demand.
///
/// Note: This type is not used by TokenManager directly, but is helpful for clients needing to
/// implement the `AuthProviderSupplier` trait.
pub struct AuthProviderConnection {
    /// The URL that should be used to launch the component.
    component_url: String,
    /// Optional params to be passed to the component at launch.
    params: Option<Vec<String>>,
    /// The state needed to retain a connection, once one has been created.
    connection_state: Mutex<Option<Arc<App>>>,
}

impl AuthProviderConnection {
    /// Creates a new `AuthProviderConnection` from the supplied `AuthProviderConfig`.
    pub fn from_config(config: AuthProviderConfig) -> Self {
        AuthProviderConnection {
            component_url: config.url,
            params: config.params,
            connection_state: Mutex::new(None),
        }
    }

    /// Creates a new `AuthProviderClient` from the supplied `AuthProviderConfig` reference.
    pub fn from_config_ref(config: &AuthProviderConfig) -> Self {
        AuthProviderConnection {
            component_url: config.url.clone(),
            params: config.params.clone(),
            connection_state: Mutex::new(None),
        }
    }

    /// Returns the component_url supplied at construction
    pub fn component_url(&self) -> &str {
        &self.component_url
    }

    /// Returns an `App` for opening new `AuthProvider` connections. If a
    /// component has previously been launched this is used, otherwise a fresh
    /// component is launched and its proxy is stored for future use.
    fn get_app(&self) -> Result<Arc<App>, TokenManagerError> {
        // If a factory proxy has already been created return it.
        let mut connection_state_lock = self.connection_state.lock();
        if let Some(connection_state) = &*connection_state_lock {
            return Ok(Arc::clone(&connection_state));
        }

        // Launch the auth provider and connect to its factory interface.
        info!("Launching AuthProvider component: {}", self.component_url);
        let launcher = launcher()
            .context("Failed to start launcher")
            .token_manager_status(Status::UnknownError)?;
        let app = launch(&launcher, self.component_url.clone(), self.params.clone())
            .context("Failed to launch AuthProvider")
            .token_manager_status(Status::AuthProviderServiceUnavailable)?;
        let app_arc = Arc::new(app);
        connection_state_lock.get_or_insert(Arc::clone(&app_arc));
        Ok(app_arc)
    }

    /// Connects the supplied channel to the `AuthProvider`. If a component has previously been
    /// launched this is used, otherwise a fresh component is launched.
    pub fn connect<S>(&self, server_end: ServerEnd<S>) -> Result<(), TokenManagerError>
    where
        S: AuthProviderService,
    {
        let app = self.get_app()?;
        app.pass_to_service::<S>(server_end.into_channel()).map_err(|err| {
            TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                .with_cause(format_err!("GetAuthProvider method failed with {:?}", err))
        })
    }

    /// Returns a `ClientEnd` for communicating with the `AuthProvider`. If a component has
    /// previously been launched this is used, otherwise a fresh component is launched.
    pub fn get<S>(&self) -> Result<ClientEnd<S>, TokenManagerError>
    where
        S: AuthProviderService,
    {
        let (client_end, server_end) =
            create_endpoints().token_manager_status(Status::UnknownError)?;
        self.connect(server_end)?;
        Ok(client_end)
    }
}

/// A marker trait for identifying services that may be provided by an auth provider.
pub trait AuthProviderService: DiscoverableService {}

impl AuthProviderService for AuthProviderMarker {}
impl AuthProviderService for OauthMarker {}
impl AuthProviderService for OpenIdConnectMarker {}
impl AuthProviderService for OauthOpenIdConnectMarker {}
