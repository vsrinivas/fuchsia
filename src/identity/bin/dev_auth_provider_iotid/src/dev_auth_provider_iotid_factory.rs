// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::dev_auth_provider_iotid::AuthProvider;
use fidl::Error;
use fidl_fuchsia_auth::{AuthProviderFactoryRequest, AuthProviderFactoryRequestStream,
                        AuthProviderStatus};
use fuchsia_async as fasync;
use futures::prelude::*;
use log::{info, warn};

/// The AuthProviderFactory struct is holding implementation of
/// the `AuthProviderFactory` fidl interface.
pub struct AuthProviderFactory;

impl AuthProviderFactory {
    /// Spawn a new task of handling request from the `AuthProviderFactoryRequestStream`
    /// by calling the `handle_request` method.
    pub fn spawn(stream: AuthProviderFactoryRequestStream) {
        fasync::spawn(
            stream
                .try_for_each(|r| future::ready(Self::handle_request(r)))
                .unwrap_or_else(|e| warn!("Error running AuthProviderFactory {:?}", e)),
        )
    }

    /// Handle `AuthProviderFactoryRequest`.
    /// Spawn a new `AuthProvider` on every `GetAuthProvider` request
    /// to the `AuthProviderFactory` interface.
    fn handle_request(req: AuthProviderFactoryRequest) -> Result<(), Error> {
        // Using let binding here as GetAuthProvider is currently the only method.
        let AuthProviderFactoryRequest::GetAuthProvider {
            auth_provider: server_end,
            responder,
        } = req;
        info!("Creating auth provider");
        AuthProvider::spawn(server_end);
        responder.send(AuthProviderStatus::Ok)
    }
}
