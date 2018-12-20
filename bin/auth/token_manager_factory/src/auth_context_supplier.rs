// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_auth::{
    AuthenticationContextProviderMarker, AuthenticationContextProviderProxy,
    AuthenticationUiContextMarker, Status,
};
use token_manager::{ResultExt, TokenManagerError};

/// A type capable of creating new `AuthenticationUiContext` objects using an
/// `AuthenticationContextProvider`.
pub struct AuthContextSupplier {
    /// The `AuthenticationContextProviderProxy` used to request new UI contexts.
    provider_proxy: AuthenticationContextProviderProxy,
}

impl AuthContextSupplier {
    /// Creates a new `AuthContextSupplier` using the provided
    /// `ClientEnd<AuthenticationContextProviderMarker>`.
    pub fn from_client_end(
        client_end: ClientEnd<AuthenticationContextProviderMarker>,
    ) -> Result<Self, Error> {
        Ok(AuthContextSupplier {
            provider_proxy: client_end.into_proxy()?,
        })
    }
}

impl token_manager::AuthContextSupplier for AuthContextSupplier {
    /// Creates a new `AuthenticationUiContext` and returns the `ClientEnd` for communicating with
    /// it.
    fn get(&self) -> Result<ClientEnd<AuthenticationUiContextMarker>, TokenManagerError> {
        let (client_end, server_end) =
            create_endpoints().token_manager_status(Status::UnknownError)?;

        self.provider_proxy
            .get_authentication_ui_context(server_end)
            .map(|_| client_end)
            .token_manager_status(Status::InvalidAuthContext)
    }
}
