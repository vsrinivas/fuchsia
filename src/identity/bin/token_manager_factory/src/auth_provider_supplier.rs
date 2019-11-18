// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::format_err;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_auth::{AuthProviderConfig, AuthProviderMarker, Status};
use futures::future::{ready as fready, FutureObj};
use std::collections::HashMap;
use std::sync::Arc;
use token_manager::{AuthProviderConnection, TokenManagerError};

/// A type capable of launching and connecting to components that implement the
/// `AuthProvider` protocol. Launching is performed on demand and components are
/// reused across calls.
#[derive(Clone)]
pub struct AuthProviderSupplier {
    /// A map from auth_provider_type to a `AuthProviderConnection` used to launch the associated
    /// component.
    auth_provider_connections: Arc<HashMap<String, AuthProviderConnection>>,
}

impl AuthProviderSupplier {
    /// Creates a new `AuthProviderSupplier` from the supplied vector of `AuthProviderConfig`
    /// objects.
    pub fn new(auth_provider_configs: Vec<AuthProviderConfig>) -> Self {
        AuthProviderSupplier {
            auth_provider_connections: Arc::new(
                auth_provider_configs
                    .into_iter()
                    .map(|apc| {
                        (apc.auth_provider_type.clone(), AuthProviderConnection::from_config(apc))
                    })
                    .collect(),
            ),
        }
    }
}

impl token_manager::AuthProviderSupplier for AuthProviderSupplier {
    /// Asynchronously creates an `AuthProvider` for the requested `auth_provider_type` and
    /// returns the `ClientEnd` for communication with it.
    fn get<'a>(
        &'a self,
        auth_provider_type: &'a str,
    ) -> FutureObj<'a, Result<ClientEnd<AuthProviderMarker>, TokenManagerError>> {
        let auth_provider_connection: &AuthProviderConnection =
            match self.auth_provider_connections.get(auth_provider_type) {
                Some(apc) => apc,
                None => {
                    let err = TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                        .with_cause(format_err!("Unknown auth provider {}", auth_provider_type));
                    return FutureObj::new(Box::new(fready(Err(err))));
                }
            };
        FutureObj::new(Box::new(auth_provider_connection.get()))
    }
}
