// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth_provider::GoogleAuthProvider;
use fidl::Error;
use fidl_fuchsia_auth::{
    AuthProviderFactoryRequest, AuthProviderFactoryRequestStream, AuthProviderStatus,
};
use fuchsia_async as fasync;

use futures::prelude::*;
use log::error;
use std::sync::Arc;

/// Implementation of the `AuthProviderFactory` FIDL protocol capable of
/// returning `AuthProvider` channels for a `GoogleAuthProvider`.
pub struct GoogleAuthProviderFactory {
    /// The GoogleAuthProvider vended through `GetAuthProvider` requests.
    google_auth_provider: Arc<GoogleAuthProvider>,
}

impl GoogleAuthProviderFactory {
    /// Create a new `GoogleAuthProviderFactory`
    pub fn new() -> Self {
        GoogleAuthProviderFactory { google_auth_provider: Arc::new(GoogleAuthProvider::new()) }
    }

    /// Handle requests passed to the supplied stream.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AuthProviderFactoryRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = await!(stream.try_next())? {
            await!(self.handle_request(request))?;
        }
        Ok(())
    }

    /// Handle `AuthProviderFactoryRequest`.
    /// Spawn a new `AuthProvider` on every `GetAuthProvider` request
    /// to the `AuthProviderFactory` interface.
    async fn handle_request(&self, req: AuthProviderFactoryRequest) -> Result<(), Error> {
        let AuthProviderFactoryRequest::GetAuthProvider { auth_provider: server_end, responder } =
            req;
        let request_stream = server_end.into_stream()?;
        let auth_provider = Arc::clone(&self.google_auth_provider);
        fasync::spawn(async move {
            await!(auth_provider.handle_requests_from_stream(request_stream))
                .unwrap_or_else(|e| error!("Error handling AuthProvider channel {:?}", e));
        });
        responder.send(AuthProviderStatus::Ok)
    }
}
