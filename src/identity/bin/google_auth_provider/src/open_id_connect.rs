// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_identity_external::{
    Error as ApiError, OpenIdConnectRequest, OpenIdConnectRequestStream,
};
use fidl_fuchsia_identity_tokens::OpenIdToken;
use futures::prelude::*;

/// An implementation of the `OpenIdConnect` protocol for operations with id tokens
/// issued by Google.
pub struct OpenIdConnect {}

impl OpenIdConnect {
    pub fn new() -> Self {
        OpenIdConnect {}
    }

    /// Handles requests passed to the supplied stream.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: OpenIdConnectRequestStream,
    ) -> Result<(), fidl::Error> {
        while let Some(request) = stream.try_next().await? {
            let OpenIdConnectRequest::RevokeIdToken { id_token, responder } = request;
            responder.send(&mut self.revoke_id_token(id_token))?;
        }
        Ok(())
    }

    fn revoke_id_token(&self, _id_token: OpenIdToken) -> Result<(), ApiError> {
        // Google does not support revoking ID tokens.
        Err(ApiError::UnsupportedOperation)
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
        let open_id_connect = OpenIdConnect::new();
        let server_fut = open_id_connect.handle_requests_from_stream(request_stream);

        let test_fut = async move {
            assert_eq!(
                proxy
                    .revoke_id_token(OpenIdToken {
                        content: Some("id-token".to_string()),
                        expiry_time: None,
                    })
                    .await
                    .unwrap()
                    .unwrap_err(),
                ApiError::UnsupportedOperation
            );
        };

        let (_, server_res) = join(test_fut, server_fut).await;
        assert!(server_res.is_ok());
    }
}
