// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_identity_external::{
    Error as ApiError, OpenIdConnectRequest, OpenIdConnectRequestStream,
};
use fidl_fuchsia_identity_tokens::OpenIdToken;
use futures::prelude::*;
use log::warn;

/// An implementation of the `OpenIdConnect` protocol for testing.
pub struct OpenIdConnect {}

impl OpenIdConnect {
    /// Handles requests passed to the supplied stream.
    pub async fn handle_requests_for_stream(stream: OpenIdConnectRequestStream) {
        stream
            .try_for_each(|r| future::ready(Self::handle_request(r)))
            .unwrap_or_else(|e| warn!("Error running OpenIdConnect {:?}", e))
            .await;
    }

    fn handle_request(request: OpenIdConnectRequest) -> Result<(), fidl::Error> {
        let OpenIdConnectRequest::RevokeIdToken { id_token, responder } = request;
        responder.send(&mut Self::revoke_id_token(id_token))
    }

    fn revoke_id_token(_id_token: OpenIdToken) -> Result<(), ApiError> {
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_identity_external::OpenIdConnectMarker;
    use fuchsia_async as fasync;
    use futures::future::join;

    #[fasync::run_until_stalled(test)]
    async fn revoke_id_token() {
        let (proxy, request_stream) = create_proxy_and_stream::<OpenIdConnectMarker>().unwrap();
        let server_fut = OpenIdConnect::handle_requests_for_stream(request_stream);

        let test_fut = async move {
            assert!(proxy
                .revoke_id_token(OpenIdToken {
                    content: Some("id-token".to_string()),
                    expiry_time: None,
                })
                .await
                .unwrap()
                .is_ok());
        };

        let (_, _) = join(test_fut, server_fut).await;
    }
}
