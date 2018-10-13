// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_auth::{AuthProviderConfig, AuthProviderFactoryMarker, AuthProviderProxy,
                        AuthProviderStatus};
use fuchsia_app::client::{App, Launcher};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::prelude::*;
use log::info;
use std::sync::{Arc, RwLock};

/// A simple wrapper to set an optional value after instantiation. The value cannot be unset once
/// set.
struct DeferredOption<T> {
    value: Option<T>,
}

impl<T> DeferredOption<T> {
    pub fn new() -> DeferredOption<T> {
        DeferredOption { value: None }
    }

    pub fn get(&self) -> Option<&T> {
        self.value.as_ref()
    }

    pub fn set(&mut self, value: T) {
        self.value = Some(value);
    }
}

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
    // Most operations on an AuthProviderClient operate asynchronously. Define internal state
    // in an Arc for ease of decoupling lifetimes.
    inner: Arc<Inner>,
}

/// The Arc-wrapped internal state of an AuthProviderClient.
struct Inner {
    /// The URL that should be used to launch the AuthProvider.
    url: String,
    /// Optional params to be passed to the AuthProvider at launch.
    params: Option<Vec<String>>,
    /// A wrapper containing the proxy object and associated objects for the AuthProvider, once it
    /// has been created.
    deferred_state: RwLock<DeferredOption<ConnectionState>>,
}

impl AuthProviderClient {
    /// Creates a new AuthProviderClient from the supplied AuthProviderConfig.
    pub fn from_config(config: AuthProviderConfig) -> Self {
        AuthProviderClient {
            inner: Arc::new(Inner {
                url: config.url,
                params: config.params,
                deferred_state: RwLock::new(DeferredOption::new()),
            }),
        }
    }

    /// Gets a proxy object for actions on the AuthProvider. If a proxy has been created previously
    /// this is returned, otherwise a new proxy is created by launching a factory interface and
    /// then acquiring a provider interface from this factory.
    pub fn get_proxy(
        &self,
    ) -> impl Future<Output = Result<Arc<AuthProviderProxy>, Error>> + 'static {
        // Extract the state we need from self for both eventual outcomes so that we don't bind
        // the lifetime of the future to the lifetime of the supplied self reference.
        let inner = self.inner.clone();

        async move {
            // If a proxy has already been created return it.
            if let Some(client_state) = inner.deferred_state.read().unwrap().get() {
                return Ok(client_state.provider_proxy.clone());
            }

            // Launch the factory for the auth provider and prepare a channel to
            // communicate.
            info!("Launching factory for AuthProvider: {}", inner.url);
            let launcher = Launcher::new().context("Failed to start launcher")?;
            let app = launcher
                .launch(inner.url.clone(), inner.params.clone())
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
                    inner.deferred_state.write().unwrap().set(ConnectionState {
                        _app: app,
                        provider_proxy: provider_proxy.clone(),
                    });
                    Ok(provider_proxy)
                }
                Ok(status) => Err(format_err!("Error getting auth provider: {:?}", status)),
                Err(err) => Err(format_err!("GetAuthProvider method failed with {:?}", err)),
            }
        }
    }
}
