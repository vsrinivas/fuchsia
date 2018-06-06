// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use fidl::Error;
use fidl::endpoints2::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AuthProviderConfig, AuthenticationContextProviderMarker,
                        TokenManagerMarker, TokenManagerRequest};
use futures::future::{self, FutureResult};
use futures::prelude::*;
use std::sync::Arc;

/// An object capable of creating authentication tokens for a user across a
/// range of services as represented by AuthProviderConfigs. Uses the supplied
/// AuthenticationContextProvier to render UI where necessary.
pub struct TokenManager {
    user_id: String,
    // TODO(jsankey): Add additional state.
}

impl TokenManager {
    /// Creates a new TokenManager to handle requests for the specified user
    /// over the supplied channel.
    pub fn spawn(
        user_id: String,
        _application_url: String,
        _auth_provider_configs: Vec<AuthProviderConfig>,
        _auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
        server_end: ServerEnd<TokenManagerMarker>,
    ) {
        let manager = Arc::new(TokenManager { user_id });

        match server_end.into_stream() {
            Err(err) => {
                warn!("Error creating TokenManager request stream {:?}", err);
            }
            Ok(request_stream) => async::spawn(
                request_stream
                    .for_each(move |req| manager.handle_request(req))
                    .map(|_| ())
                    .recover(|err| warn!("Error running TokenManager {:?}", err)),
            ),
        };
    }

    /// Handles a single request to the TokenManager.
    fn handle_request(&self, req: TokenManagerRequest) -> FutureResult<(), Error> {
        match req {
            // TODO(jsankey): Implment the actual functionality of a TokenManager.
            _ => {
                info!(
                    "Received not yet implemented token manager request for user {}",
                    self.user_id
                );
                future::ok::<(), Error>(())
            }
        }
    }
}
