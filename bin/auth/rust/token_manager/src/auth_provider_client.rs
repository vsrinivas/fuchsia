// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_auth::{
    AuthProviderConfig, AuthProviderFactoryMarker, AuthProviderProxy, AuthProviderStatus,
};
use fuchsia_app::client::{App, Launcher};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use log::info;
use parking_lot::RwLock;
use std::sync::Arc;

/// The information neccesary to retain an open connection to an Auth Provider.
struct ConnectionState {
    // Note: The app need to remain in scope for the connection to be used,
    // but never needs to be read.
    _app: App,
    provider_proxy: Arc<AuthProviderProxy>,
}

/// An object capable of launching a particular AuthProvider and acquiring a proxy to communicate
/// with it.
///
/// The URL and configuration parameters for this provider are supplied at construction but
/// initialization of the interface is delayed until the first call to get_proxy().
pub struct AuthProviderClient {
    /// The URL that should be used to launch the AuthProvider.
    url: String,
    /// Optional params to be passed to the AuthProvider at launch.
    params: Option<Vec<String>>,
    /// A wrapper containing the proxy object and associated objects for the AuthProvider, once it
    /// has been created.
    deferred_state: RwLock<Option<ConnectionState>>,
}

impl AuthProviderClient {
    /// Creates a new AuthProviderClient from the supplied AuthProviderConfig.
    pub fn from_config(config: AuthProviderConfig) -> Self {
        AuthProviderClient {
            url: config.url,
            params: config.params,
            deferred_state: RwLock::new(None),
        }
    }

    /// Gets a proxy object for actions on the AuthProvider. If a proxy has been created previously
    /// this is returned, otherwise a new proxy is created by launching a factory interface and
    /// then acquiring a provider interface from this factory.
    pub async fn get_proxy(&self) -> Result<Arc<AuthProviderProxy>, Error> {
        // If a proxy has already been created return it.
        if let Some(client_state) = &*self.deferred_state.read() {
            return Ok(Arc::clone(&client_state.provider_proxy));
        }

        // Launch the factory for the auth provider and prepare a channel to communicate.
        info!("Launching factory for AuthProvider: {}", self.url);
        let launcher = Launcher::new().context("Failed to start launcher")?;
        let app = launcher
            .launch(self.url.clone(), self.params.clone())
            .context("Failed to launch AuthProviderFactory")?;
        let factory_proxy = app
            .connect_to_service(AuthProviderFactoryMarker)
            .context("Failed to connect to AuthProviderFactory")?;
        let (server_chan, client_chan) = zx::Channel::create()?;

        // Connect to the factory and request an auth provider.
        match await!(factory_proxy.get_auth_provider(ServerEnd::new(server_chan))) {
            Ok(AuthProviderStatus::Ok) => {
                let client_async = fasync::Channel::from_channel(client_chan).unwrap();
                let provider_proxy = Arc::new(AuthProviderProxy::new(client_async));
                self.deferred_state.write().get_or_insert(ConnectionState {
                    _app: app,
                    provider_proxy: Arc::clone(&provider_proxy),
                });
                Ok(provider_proxy)
            }
            Ok(status) => Err(format_err!("Error getting auth provider: {:?}", status)),
            Err(err) => Err(format_err!("GetAuthProvider method failed with {:?}", err)),
        }
    }
}
