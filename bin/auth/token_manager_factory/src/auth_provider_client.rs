// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, ResultExt};
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{
    AuthProviderConfig, AuthProviderFactoryMarker, AuthProviderFactoryProxy, AuthProviderMarker,
    AuthProviderStatus, Status,
};
use fuchsia_app::client::{App, Launcher};
use fuchsia_zircon as zx;
use log::info;
use parking_lot::Mutex;
use std::sync::Arc;
use token_manager::{ResultExt as TokenManagerResultExt, TokenManagerError};

/// The information neccesary to retain an open connection to the `AuthProviderFactory` interface
/// of a launched component.
struct ConnectionState {
    // Note: The app must remain in scope for the connection to be retained, but never needs to be
    // read.
    _app: App,
    factory_proxy: Arc<AuthProviderFactoryProxy>,
}

/// A type capable of launching a particular component implementing the `AuthProviderFactory`
/// interface and acquiring `AuthProvider` connections from that component. Launching is
/// performed on demand.
pub struct AuthProviderClient {
    /// The URL that should be used to launch the component.
    component_url: String,
    /// Optional params to be passed to the component at launch.
    params: Option<Vec<String>>,
    /// The state needed to retain a connection, once one has been created.
    connection_state: Mutex<Option<ConnectionState>>,
}

impl AuthProviderClient {
    /// Creates a new `AuthProviderClient` from the supplied `AuthProviderConfig`.
    pub fn from_config(config: AuthProviderConfig) -> Self {
        AuthProviderClient {
            component_url: config.url,
            params: config.params,
            connection_state: Mutex::new(None),
        }
    }

    /// Returns an `AuthProviderFactoryProxy` for opening new `AuthProvider` connections. If
    /// a component has previously been launched this is used, otherwise a fresh component is
    /// launched and its proxy is stored for future use.
    fn get_factory_proxy(&self) -> Result<Arc<AuthProviderFactoryProxy>, TokenManagerError> {
        // If a factory proxy has already been created return it.
        let mut connection_state_lock = self.connection_state.lock();
        if let Some(connection_state) = &*connection_state_lock {
            return Ok(Arc::clone(&connection_state.factory_proxy));
        }

        // Launch the auth provider and connect to its factory interface.
        info!("Launching AuthProvider component: {}", self.component_url);
        let launcher = Launcher::new()
            .context("Failed to start launcher")
            .token_manager_status(Status::UnknownError)?;
        let app = launcher
            .launch(self.component_url.clone(), self.params.clone())
            .context("Failed to launch AuthProviderFactory")
            .token_manager_status(Status::AuthProviderServiceUnavailable)?;
        let factory_proxy = Arc::new(
            app.connect_to_service(AuthProviderFactoryMarker)
                .context("Failed to connect to AuthProviderFactory")
                .token_manager_status(Status::AuthProviderServiceUnavailable)?,
        );
        connection_state_lock.get_or_insert(ConnectionState {
            _app: app,
            factory_proxy: Arc::clone(&factory_proxy),
        });
        Ok(factory_proxy)
    }

    /// Returns a `ClientEnd` for communicating with the `AuthProvider`. If a component has
    /// previously been launched this is used, otherwise a fresh component is launched.
    pub async fn get(&self) -> Result<ClientEnd<AuthProviderMarker>, TokenManagerError> {
        let factory_proxy = self.get_factory_proxy()?;
        let (server_chan, client_chan) =
            zx::Channel::create().token_manager_status(Status::UnknownError)?;

        // Connect to the factory and request an auth provider.
        match await!(factory_proxy.get_auth_provider(ServerEnd::new(server_chan))) {
            Ok(AuthProviderStatus::Ok) => Ok(ClientEnd::new(client_chan)),
            Ok(status) => Err(
                TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                    .with_cause(format_err!("Error getting auth provider: {:?}", status)),
            ),
            Err(err) => Err(
                TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                    .with_cause(format_err!("GetAuthProvider method failed with {:?}", err)),
            ),
        }
    }
}
