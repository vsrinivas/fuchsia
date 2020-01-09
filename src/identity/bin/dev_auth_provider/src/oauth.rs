// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::*;

use fidl_fuchsia_identity_external::{
    Error as ApiError, OauthAccessTokenFromOauthRefreshTokenRequest, OauthRefreshTokenRequest,
    OauthRequest, OauthRequestStream,
};
use fidl_fuchsia_identity_tokens::{OauthAccessToken, OauthRefreshToken};
use futures::future;
use futures::prelude::*;
use log::warn;
use serde_derive::{Deserialize, Serialize};

/// Data format for an access token.  In general, an access token is in an
/// opaque format that is unreadable to a client.  Here, we serialize the data as json,
/// which enables enables:
///     1. Integration tests to track what refresh tokens were used to generate a token
///     2. dev_auth_provider to return consistent information such client ids across
///     different calls when appropriate.
#[derive(PartialEq, Debug, Serialize, Deserialize)]
pub struct AccessTokenContent {
    /// The client to which the access token is issued.
    pub client_id: String,
    /// Contents of the refresh token the access token was created from.
    pub refresh_token: String,
    /// Id of the service provider account the token is issued for.
    pub account_id: String,
    /// An id that uniquely identifies this access token.
    pub id: String,
}

impl AccessTokenContent {
    /// Create a new `AccessTokenContent`.  If client_id is not specified the token
    /// will contain the client_id "none".
    pub fn new(refresh_token: String, account_id: String, client_id: Option<String>) -> Self {
        AccessTokenContent {
            client_id: client_id.unwrap_or("none".to_string()),
            refresh_token: refresh_token,
            account_id: account_id,
            id: format!("at_{}", generate_random_string()),
        }
    }
}

/// An implementation of the `Oauth` protocol for testing.
pub struct Oauth {}

impl Oauth {
    /// Handles requests received on the provided `request_stream`.
    pub async fn handle_requests_for_stream(request_stream: OauthRequestStream) {
        request_stream
            .try_for_each(|r| future::ready(Self::handle_request(r)))
            .unwrap_or_else(|e| warn!("Error running Oauth {:?}", e))
            .await;
    }

    /// Handles a single `OauthRequest`.
    fn handle_request(request: OauthRequest) -> Result<(), fidl::Error> {
        match request {
            OauthRequest::CreateRefreshToken { request, responder } => {
                responder.send(&mut Self::create_refresh_token(request))
            }
            OauthRequest::GetAccessTokenFromRefreshToken { request, responder } => {
                responder.send(&mut Self::get_access_token_from_refresh_token(request))
            }
            OauthRequest::RevokeRefreshToken { refresh_token, responder } => {
                responder.send(&mut Self::revoke_refresh_token(refresh_token))
            }
            OauthRequest::RevokeAccessToken { access_token, responder } => {
                responder.send(&mut Self::revoke_access_token(access_token))
            }
        }
    }

    fn create_refresh_token(
        request: OauthRefreshTokenRequest,
    ) -> Result<(OauthRefreshToken, OauthAccessToken), ApiError> {
        let refresh_token_content = format!("rt_{}", generate_random_string());
        let account_id = request.account_id.unwrap_or(format!(
            "{}{}",
            generate_random_string(),
            USER_PROFILE_INFO_ID_DOMAIN
        ));

        let access_token_content = serde_json::to_string(&AccessTokenContent::new(
            refresh_token_content.clone(),
            account_id.clone(),
            None,
        ))
        .map_err(|_| ApiError::Internal)?;

        Ok((
            OauthRefreshToken {
                content: Some(refresh_token_content),
                account_id: Some(account_id),
            },
            OauthAccessToken {
                content: Some(access_token_content),
                expiry_time: Some(get_token_expiry_time_nanos()),
            },
        ))
    }

    fn get_access_token_from_refresh_token(
        request: OauthAccessTokenFromOauthRefreshTokenRequest,
    ) -> Result<OauthAccessToken, ApiError> {
        let refresh_token = request.refresh_token.ok_or(ApiError::InvalidRequest)?;

        let refresh_token_content = refresh_token.content.ok_or(ApiError::InvalidRequest)?;
        let account_id = refresh_token.account_id.ok_or(ApiError::InvalidRequest)?;

        let client_id = request.client_id;

        let access_token_content = serde_json::to_string(&AccessTokenContent::new(
            refresh_token_content,
            account_id,
            client_id,
        ))
        .map_err(|_| ApiError::Internal)?;

        Ok(OauthAccessToken {
            content: Some(access_token_content),
            expiry_time: Some(get_token_expiry_time_nanos()),
        })
    }

    fn revoke_refresh_token(_refresh_token: OauthRefreshToken) -> Result<(), ApiError> {
        Ok(())
    }

    fn revoke_access_token(_access_token: OauthAccessToken) -> Result<(), ApiError> {
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_identity_external::{OauthMarker, OauthProxy};
    use fuchsia_async as fasync;
    use futures::future::join;

    async fn run_proxy_test<F, Fut>(test_fn: F)
    where
        F: FnOnce(OauthProxy) -> Fut,
        Fut: Future<Output = Result<(), anyhow::Error>>,
    {
        let (proxy, stream) = create_proxy_and_stream::<OauthMarker>().unwrap();
        let server_fut = Oauth::handle_requests_for_stream(stream);
        let (test_result, _) = join(test_fn(proxy), server_fut).await;
        assert!(test_result.is_ok());
    }

    #[test]
    fn test_access_token_content_round_trip() {
        // Specified client id case
        let content = AccessTokenContent::new(
            "test-refresh-token".to_string(),
            "account_id@example.com".to_string(),
            Some("test-client-id".to_string()),
        );

        let serialized = serde_json::to_string(&content).unwrap();
        let result_content = serde_json::from_str::<AccessTokenContent>(&serialized).unwrap();
        assert_eq!(content, result_content);

        // Unspecified client id case
        let content = AccessTokenContent::new(
            "test-refresh-token".to_string(),
            "account_id@example.com".to_string(),
            None,
        );

        let serialized = serde_json::to_string(&content).unwrap();
        let result_content = serde_json::from_str::<AccessTokenContent>(&serialized).unwrap();
        assert_eq!(content, result_content);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_create_refresh_token() {
        run_proxy_test(|proxy| {
            async move {
                // Account ID given case
                let request = OauthRefreshTokenRequest {
                    account_id: Some("test-account".to_string()),
                    ui_context: None,
                };
                let (refresh_token, access_token) =
                    proxy.create_refresh_token(request).await?.unwrap();

                assert_eq!(refresh_token.account_id.unwrap(), "test-account".to_string());
                assert!(refresh_token.content.unwrap().contains("rt_"));

                assert!(access_token.content.unwrap().contains("at_"));
                assert!(access_token.expiry_time.is_some());

                // Account ID unspecified case
                let request = OauthRefreshTokenRequest { account_id: None, ui_context: None };
                let (refresh_token, access_token) =
                    proxy.create_refresh_token(request).await?.unwrap();

                assert!(refresh_token.account_id.unwrap().contains(USER_PROFILE_INFO_ID_DOMAIN));
                assert!(refresh_token.content.unwrap().contains("rt_"));

                assert!(access_token.content.unwrap().contains("at_"));
                assert!(access_token.expiry_time.is_some());
                Ok(())
            }
        })
        .await
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_access_token_from_refresh_token() {
        run_proxy_test(|proxy| {
            async move {
                // client ID given case
                let refresh_token_content = format!("rt_{}", generate_random_string());
                let client_id = generate_random_string();
                let request = OauthAccessTokenFromOauthRefreshTokenRequest {
                    refresh_token: Some(OauthRefreshToken {
                        content: Some(refresh_token_content.clone()),
                        account_id: Some("account-id".to_string()),
                    }),
                    client_id: Some(client_id.clone()),
                    scopes: None,
                };

                let access_token =
                    proxy.get_access_token_from_refresh_token(request).await?.unwrap();
                let access_token_serialized = access_token.content.unwrap();
                let access_token_content =
                    serde_json::from_str::<AccessTokenContent>(&access_token_serialized).unwrap();
                assert_eq!(access_token_content.client_id, client_id);
                assert_eq!(access_token_content.refresh_token, refresh_token_content.clone());
                assert_eq!(access_token_content.account_id, "account-id".to_string());
                assert!(access_token_content.id.contains("at_"));

                // client ID unspecified case
                let refresh_token_content = format!("rt_{}", generate_random_string());
                let request = OauthAccessTokenFromOauthRefreshTokenRequest {
                    refresh_token: Some(OauthRefreshToken {
                        content: Some(refresh_token_content.clone()),
                        account_id: Some("account-id".to_string()),
                    }),
                    client_id: None,
                    scopes: None,
                };

                let access_token =
                    proxy.get_access_token_from_refresh_token(request).await?.unwrap();
                let access_token_serialized = access_token.content.unwrap();
                let access_token_content =
                    serde_json::from_str::<AccessTokenContent>(&access_token_serialized).unwrap();
                assert_eq!(access_token_content.client_id, "none".to_string());
                assert_eq!(access_token_content.refresh_token, refresh_token_content.clone());
                assert_eq!(access_token_content.account_id, "account-id".to_string());
                assert!(access_token_content.id.contains("at_"));
                Ok(())
            }
        })
        .await
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_refresh_token() {
        run_proxy_test(|proxy| async move {
            let refresh_token = OauthRefreshToken {
                content: Some("refresh-token".to_string()),
                account_id: Some("account-id".to_string()),
            };
            assert!(proxy.revoke_refresh_token(refresh_token).await?.is_ok());
            Ok(())
        })
        .await
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_access_token() {
        run_proxy_test(|proxy| async move {
            let access_token =
                OauthAccessToken { content: Some("access-token".to_string()), expiry_time: None };
            assert!(proxy.revoke_access_token(access_token).await?.is_ok());
            Ok(())
        })
        .await
    }
}
