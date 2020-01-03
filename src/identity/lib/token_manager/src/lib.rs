// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TokenManager manages a set of service provider credentials.
//!
//! Supported credential types include long-lived OAuth refresh tokens and short-lived OAuth
//! access tokens derived from these. Long-lived tokens are stored in a database and short-lived
//! credentials are cached where possible.
//!
//! A client of the TokenManager must provide the path for the credential database and suppliers
//! for AuthProviders and AuthenticationUiContexts.

#![deny(missing_docs)]

use async_trait::async_trait;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_auth::AuthenticationContextProviderProxy;
use fidl_fuchsia_identity_external::{OauthMarker, OauthOpenIdConnectMarker, OpenIdConnectMarker};

mod auth_provider_cache;
mod auth_provider_connection;
mod error;
mod fake_auth_provider_supplier;
mod token_manager;
mod tokens;

pub use crate::auth_provider_connection::{AuthProviderConnection, AuthProviderService};
pub use crate::error::{ResultExt, TokenManagerError};
pub use crate::token_manager::TokenManager;

/// The context that a particular request to the token manager should be executed in, capturing
/// information that was supplied upon creation of the channel.
pub struct TokenManagerContext {
    /// The application that this request is being sent on behalf of.
    pub application_url: String,
    /// An `AuthenticationContextProviderProxy` capable of generating new `AuthenticationUiContext`
    /// channels.
    pub auth_ui_context_provider: AuthenticationContextProviderProxy,
}

/// A type capable of supplying channels to communicate with components implementing the
/// `AuthProvider` interface..
// TODO(satsukiu): the methods this contains should probably just be generic over the
// various auth provider protocols.  This isn't done at the moment since channels need
// to be passed between account_handler and account_manager using distinct methods in
// AccountHandlerContext.
#[async_trait]
pub trait AuthProviderSupplier {
    /// Returns a `ClientEnd` for communication with a token provider for the requested
    /// `auth_provider_type` over the `Oauth` protocol.
    async fn get_oauth(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OauthMarker>, TokenManagerError>;

    /// Returns a `ClientEnd` for communication with a token provider for the requested
    /// `auth_provider_type` over the `OpenIdConnect` protocol.
    async fn get_open_id_connect(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OpenIdConnectMarker>, TokenManagerError>;

    /// Returns a `ClientEnd` for communication with a token provider for the requested
    /// `auth_provider_type` over the `OauthOpenIdConnect` protocol.
    async fn get_oauth_open_id_connect(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OauthOpenIdConnectMarker>, TokenManagerError>;
}
