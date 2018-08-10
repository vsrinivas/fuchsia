// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::zx;
use async;
use component::client::{App, Launcher};
use failure::{Error, ResultExt};
use fidl::endpoints2::ServerEnd;
use fidl_fuchsia_auth::{AuthProviderConfig, AuthProviderFactoryMarker, AuthProviderFactoryProxy,
                        AuthProviderProxy, AuthProviderStatus};
use futures::future::{ready as fready, FutureObj};
use futures::prelude::*;
use std::boxed::Box;
use std::sync::{Arc, RwLock};

/// A simple wrapper to set an optional value after instantiation. The value
/// cannot be unset once set.
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

/// An object capable of launching a particular AuthProvider and acquiring
/// a proxy to communicate with it.
///
/// The URL and configuration parameters for this provider are supplied at
/// construction but initialization of the interface is delayed until the first
/// call to get_proxy().
pub struct AuthProviderClient {
    /// The URL that should be used to launch the AuthProvider.
    url: String,
    /// Optional params to be passed to the AuthProvider at launch.
    params: Option<Vec<String>>,
    /// A wrapper containing the proxy object and associated objects for the AuthProvider,
    /// once it has been created.
    deferred_state: Arc<RwLock<DeferredOption<ConnectionState>>>,
}

impl AuthProviderClient {
    /// Creates a new AuthProviderClient from the supplied AuthProviderConfig.
    pub fn from_config(config: AuthProviderConfig) -> Self {
        AuthProviderClient {
            url: config.url,
            params: config.params,
            deferred_state: Arc::new(RwLock::new(DeferredOption::new())),
        }
    }

    /// Gets a proxy object for actions on the AuthProvider. If a proxy has
    /// been created previously this is returned, otherwise a new proxy is
    /// created by launching a factory interface and then acquiring a
    /// provider interface from this factory.
    pub fn get_proxy(
        &self,
    ) -> FutureObj<'static, Result<Arc<AuthProviderProxy>, Error>> {
        // First check if a proxy has already been created, and if so return
        // immediately.
        if let Some(client_state) = self.deferred_state.read().unwrap().get() {
            return FutureObj::new(Box::new(fready(Ok(client_state.provider_proxy.clone()))));
        }

        // Launch the factory for the auth provider and prepare a channel to
        // communicate.
        info!("Launching factory for AuthProvider: {}", self.url);
        let (app, factory_proxy) = future_try!(self.launch_factory());
        let (server_chan, client_chan) = future_try!(zx::Channel::create());

        // Create a new reference to the DeferredOption that can live beyond our self
        // reference.
        let deferred_state = self.deferred_state.clone();
        FutureObj::new(Box::new(
            factory_proxy
                .get_auth_provider(ServerEnd::new(server_chan))
                .map_err(|err| format_err!("GetAuthProvider method failed with {:?}", err))
                .and_then(move |status| {
                    match status {
                        AuthProviderStatus::Ok => {
                            let client_async = async::Channel::from_channel(client_chan).unwrap();
                            let provider_proxy = Arc::new(AuthProviderProxy::new(client_async));
                            deferred_state.write().unwrap().set(ConnectionState {
                                _app: app,
                                provider_proxy: provider_proxy.clone(),
                            });
                            fready(Ok(provider_proxy))
                        }
                        _ => fready(Err(format_err!("Error getting auth provider: {:?}", status))),
                    }
                }),
        ))
    }

    /// Launches a new instance of the associated AuthProvider and connects to
    /// the factory interface.
    fn launch_factory(&self) -> Result<(App, AuthProviderFactoryProxy), Error> {
        let launcher = Launcher::new().context("Failed to start launcher")?;
        let app = launcher
            .launch(self.url.clone(), self.params.clone())
            .context("Failed to launch AuthProviderFactory")?;
        let factory_proxy = app
            .connect_to_service(AuthProviderFactoryMarker)
            .context("Failed to connect to AuthProviderFactory")?;
        Ok((app, factory_proxy))
    }
}
