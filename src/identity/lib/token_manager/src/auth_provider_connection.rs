// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{ResultExt as TokenManagerResultExt, TokenManagerError};
use failure::{format_err, ResultExt};
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AuthProviderConfig, AuthProviderMarker, Status};
use fuchsia_component::client::{launch, launcher, App};
use fuchsia_zircon as zx;
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

    /// Connects the supplied `ServerEnd` to the `AuthProvider`. If a component has previously been
    /// launched this is used, otherwise a fresh component is launched.
    pub async fn connect(
        &self,
        server_end: ServerEnd<AuthProviderMarker>,
    ) -> Result<(), TokenManagerError> {
        let app = self.get_app()?;
        app.pass_to_service::<AuthProviderMarker>(server_end.into_channel()).map_err(|err| {
            TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                .with_cause(format_err!("GetAuthProvider method failed with {:?}", err))
        })
    }

    /// Returns a `ClientEnd` for communicating with the `AuthProvider`. If a component has
    /// previously been launched this is used, otherwise a fresh component is launched.
    pub async fn get(&self) -> Result<ClientEnd<AuthProviderMarker>, TokenManagerError> {
        let (server_chan, client_chan) =
            zx::Channel::create().token_manager_status(Status::UnknownError)?;
        self.connect(ServerEnd::new(server_chan)).await?;
        Ok(ClientEnd::new(client_chan))
    }
}
