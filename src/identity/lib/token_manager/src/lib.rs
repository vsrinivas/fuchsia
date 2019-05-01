// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TokenManager manages a set of service provider credentials.
//!
//! Supported credential types include long-lived OAuth refresh tokens and short-lived OAuth
//! access tokens and Firebase tokens derived from these. Long-lived tokens are stored in a
//! database and short-lived credentials are cached where possible.
//!
//! A client of the TokenManager must provide the path for the credential database and suppliers
//! for AuthProviders and AuthenticationUiContexts.

#![deny(warnings)]
#![deny(missing_docs)]
#![feature(async_await, await_macro)]

use fidl::endpoints::ClientEnd;
use fidl_fuchsia_auth::{AuthProviderMarker, AuthenticationContextProviderProxy};
use futures::future::FutureObj;

mod auth_provider_connection;
mod error;
mod token_manager;
mod tokens;

pub use crate::auth_provider_connection::AuthProviderConnection;
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
/// `AuthProvider` interface.
pub trait AuthProviderSupplier {
    /// Asynchronously creates an `AuthProvider` for the requested `auth_provider_type` and returns
    /// the `ClientEnd` for communication with it.
    fn get<'a>(
        &'a self,
        auth_provider_type: &'a str,
    ) -> FutureObj<'a, Result<ClientEnd<AuthProviderMarker>, TokenManagerError>>;
}
