// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;
use failure::format_err;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_auth::{AuthProviderConfig, AuthProviderMarker, Status};
use fidl_fuchsia_identity_external::{OauthMarker, OauthOpenIdConnectMarker, OpenIdConnectMarker};
use std::collections::HashMap;
use std::sync::Arc;
use token_manager::{AuthProviderConnection, AuthProviderService, TokenManagerError};

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

    fn get<S>(&self, auth_provider_type: &str) -> Result<ClientEnd<S>, TokenManagerError>
    where
        S: AuthProviderService,
    {
        let auth_provider_connection: &AuthProviderConnection =
            match self.auth_provider_connections.get(auth_provider_type) {
                Some(apc) => apc,
                None => {
                    let err = TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                        .with_cause(format_err!("Unknown auth provider {}", auth_provider_type));
                    return Err(err);
                }
            };
        auth_provider_connection.get()
    }
}

#[async_trait]
impl token_manager::AuthProviderSupplier for AuthProviderSupplier {
    async fn get_auth_provider(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<AuthProviderMarker>, TokenManagerError> {
        self.get(auth_provider_type)
    }

    async fn get_oauth(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OauthMarker>, TokenManagerError> {
        self.get(auth_provider_type)
    }

    async fn get_open_id_connect(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OpenIdConnectMarker>, TokenManagerError> {
        self.get(auth_provider_type)
    }

    async fn get_oauth_open_id_connect(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OauthOpenIdConnectMarker>, TokenManagerError> {
        self.get(auth_provider_type)
    }
}
