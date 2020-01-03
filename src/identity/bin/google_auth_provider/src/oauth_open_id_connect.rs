// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::USER_INFO_URI;
use crate::error::{ResultExt, TokenProviderError};
use crate::http::{HttpClient, HttpRequest, HttpRequestBuilder};
use crate::oauth::{self, AccessToken, RefreshToken};
use crate::time::Clock;
use fidl_fuchsia_identity_external::{
    Error as ApiError, OauthOpenIdConnectRequest, OauthOpenIdConnectRequestStream,
    OpenIdTokenFromOauthRefreshTokenRequest, OpenIdUserInfoFromOauthAccessTokenRequest,
};
use fidl_fuchsia_identity_tokens::{OpenIdToken, OpenIdUserInfo};
use fuchsia_zircon::Duration;
use futures::prelude::*;
use hyper::StatusCode;
use log::warn;
use serde_derive::Deserialize;
use serde_json::from_str;
use std::marker::PhantomData;

type TokenProviderResult<T> = Result<T, TokenProviderError>;

/// An implementation of the `fuchsia.identity.external.OauthOpenIdConnect`
/// protocol that communicates with the Google identity system to perform
/// exchanges between Oauth 2.0 and OpenId Connect tokens.
pub struct OauthOpenIdConnect<HC, C>
where
    HC: HttpClient,
    C: Clock,
{
    /// A client used for making HTTP requests.
    http_client: HC,
    /// A marker denoting which clock implementation to use.
    _clock: PhantomData<C>,
}

impl<HC, C> OauthOpenIdConnect<HC, C>
where
    HC: HttpClient,
    C: Clock,
{
    /// Create a new GoogleAuthProvider.
    pub fn new(http_client: HC) -> Self {
        OauthOpenIdConnect { http_client, _clock: PhantomData }
    }

    /// Handle requests passed to the supplied stream.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: OauthOpenIdConnectRequestStream,
    ) -> Result<(), fidl::Error> {
        while let Some(request) = stream.try_next().await? {
            self.handle_request(request).await?;
        }
        Ok(())
    }

    async fn handle_request(&self, request: OauthOpenIdConnectRequest) -> Result<(), fidl::Error> {
        match request {
            OauthOpenIdConnectRequest::GetIdTokenFromRefreshToken { request, responder } => {
                let mut response =
                    self.get_id_token_from_refresh_token(request).await.map_err(|e| e.api_error);
                responder.send(&mut response).map_err(|e| {
                    warn!("Error sending response for GetIdTokenFromRefreshToken: {:?}", e);
                    e
                })
            }
            OauthOpenIdConnectRequest::GetUserInfoFromAccessToken { request, responder } => {
                let mut response =
                    self.get_user_info_from_access_token(request).await.map_err(|e| e.api_error);
                responder.send(&mut response).map_err(|e| {
                    warn!("Error sending response for GetUserInfoFromAccessToken: {:?}", e);
                    e
                })
            }
        }
    }

    async fn get_id_token_from_refresh_token(
        &self,
        request: OpenIdTokenFromOauthRefreshTokenRequest,
    ) -> TokenProviderResult<OpenIdToken> {
        let OpenIdTokenFromOauthRefreshTokenRequest { refresh_token, audiences } = request;
        let refresh_token_contents = refresh_token
            .ok_or(TokenProviderError::new(ApiError::InvalidRequest))?
            .content
            .ok_or(TokenProviderError::new(ApiError::InvalidRequest))?;
        if refresh_token_contents.is_empty() {
            return Err(TokenProviderError::new(ApiError::InvalidRequest));
        }

        let audience = match audiences {
            None => None,
            Some(mut audiences) => match audiences.len() {
                0 => None,
                1 => Some(audiences.remove(0)),
                // TODO(satsukiu): support requests for multiple audiences
                _ => return Err(TokenProviderError::new(ApiError::UnsupportedOperation)),
            },
        };

        match audience.as_ref() {
            Some(aud) if aud.is_empty() => {
                return Err(TokenProviderError::new(ApiError::InvalidRequest))
            }
            _ => (),
        }

        let request = build_id_token_request(RefreshToken(refresh_token_contents), audience)?;
        let (response_body, status) = self.http_client.request(request).await?;
        let (id_token, expires_in) = parse_id_token_response(response_body, status)?;

        let expiry_time = C::current_time() + expires_in;
        Ok(OpenIdToken { content: Some(id_token.0), expiry_time: Some(expiry_time.into_nanos()) })
    }

    async fn get_user_info_from_access_token(
        &self,
        request: OpenIdUserInfoFromOauthAccessTokenRequest,
    ) -> TokenProviderResult<OpenIdUserInfo> {
        let access_token_content = request
            .access_token
            .ok_or(TokenProviderError::new(ApiError::InvalidRequest))?
            .content
            .ok_or(TokenProviderError::new(ApiError::InvalidRequest))?;
        if access_token_content.is_empty() {
            return Err(TokenProviderError::new(ApiError::InvalidRequest));
        }

        let request = build_user_info_request(&AccessToken(access_token_content))?;
        let (response_body, status_code) = self.http_client.request(request).await?;
        let OpenIdUserInfoResponse { sub, name, email, picture, .. } =
            parse_user_info_response(response_body, status_code)?;
        Ok(OpenIdUserInfo { subject: Some(sub), name, email, picture })
    }
}

// TODO(satsukiu): the following methods and structs don't need to be public once
// they aren't used from the auth_provider mod.

#[derive(Debug, PartialEq)]
pub struct IdToken(pub String);

/// Response type for OpenID user info requests.
#[derive(Debug, Deserialize, PartialEq)]
pub struct OpenIdUserInfoResponse {
    pub sub: String,
    pub name: Option<String>,
    pub email: Option<String>,
    pub profile: Option<String>,
    pub picture: Option<String>,
}

/// Response type for an OpenID ID token request.
#[derive(Debug, Deserialize)]
struct OpenIdTokenResponse {
    pub id_token: String,
    pub expires_in: u32,
}

/// Error response for OpenID requests.
#[derive(Debug, Deserialize)]
struct OpenIdErrorResponse {
    pub error: String,
}

/// Construct an `HttpRequest` for an OpenID user info request.
pub fn build_user_info_request(access_token: &AccessToken) -> TokenProviderResult<HttpRequest> {
    HttpRequestBuilder::new(USER_INFO_URI.as_str(), "GET")
        .with_header("Authorization", format!("Bearer {}", access_token.0))
        .finish()
}

/// Construct an `HttpRequest` to request an OpenID ID token.
pub fn build_id_token_request(
    refresh_token: RefreshToken,
    audience: Option<String>,
) -> TokenProviderResult<HttpRequest> {
    // OpenID standard dictates that id_token is returned as part of an Oauth
    // access token response.  Thus, the request is really just an Oauth request.
    oauth::build_request_with_refresh_token(refresh_token, vec![], audience)
}

/// Parse an OpenID user info response.
pub fn parse_user_info_response(
    response_body: Option<String>,
    status_code: StatusCode,
) -> TokenProviderResult<OpenIdUserInfoResponse> {
    match (response_body.as_ref(), status_code) {
        (Some(response), StatusCode::OK) => {
            serde_json::from_str::<OpenIdUserInfoResponse>(&response)
                .token_provider_error(ApiError::Server)
        }
        (Some(response), status) if status.is_client_error() => {
            let error_response = from_str::<OpenIdErrorResponse>(&response)
                .token_provider_error(ApiError::Server)?;
            warn!("Got unexpected error code for OpenId user info: {}", error_response.error);
            Err(TokenProviderError::new(ApiError::Server))
        }
        _ => Err(TokenProviderError::new(ApiError::Server)),
    }
}

/// Parse an OpenID ID token response.
pub fn parse_id_token_response(
    response_body: Option<String>,
    status_code: StatusCode,
) -> TokenProviderResult<(IdToken, Duration)> {
    match (response_body.as_ref(), status_code) {
        (Some(response), StatusCode::OK) => {
            let OpenIdTokenResponse { id_token, expires_in } =
                serde_json::from_str::<OpenIdTokenResponse>(&response)
                    .token_provider_error(ApiError::Server)?;
            Ok((IdToken(id_token), Duration::from_seconds(expires_in as i64)))
        }
        (Some(response), status) if status.is_client_error() => {
            let error_response = from_str::<OpenIdErrorResponse>(&response)
                .token_provider_error(ApiError::Server)?;
            warn!("Got unexpected error code while retrieving ID token: {}", error_response.error);
            Err(TokenProviderError::new(ApiError::Server))
        }
        _ => Err(TokenProviderError::new(ApiError::Server)),
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::http::mock::TestHttpClient;
    use crate::time::mock::{FixedClock, TEST_CURRENT_TIME};
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_identity_external::{OauthOpenIdConnectMarker, OauthOpenIdConnectProxy};
    use fidl_fuchsia_identity_tokens::{OauthAccessToken, OauthRefreshToken};
    use fuchsia_async as fasync;
    use fuchsia_zircon::Duration;
    use futures::future::join;
    use hyper::StatusCode;

    async fn run_proxy_test<Fn, Fut>(
        test_object: OauthOpenIdConnect<TestHttpClient, FixedClock>,
        test_fn: Fn,
    ) where
        Fn: FnOnce(OauthOpenIdConnectProxy) -> Fut,
        Fut: Future<Output = Result<(), fidl::Error>>,
    {
        let (proxy, stream) = create_proxy_and_stream::<OauthOpenIdConnectMarker>().unwrap();
        let server_fut = test_object.handle_requests_from_stream(stream);
        let test_fut = test_fn(proxy);
        let (test_res, server_res) = join(test_fut, server_fut).await;
        assert!(test_res.is_ok());
        assert!(server_res.is_ok());
    }

    fn create_refresh_token(content: &str) -> OauthRefreshToken {
        OauthRefreshToken { content: Some(content.to_string()), account_id: None }
    }

    fn create_access_token(content: &str) -> OauthAccessToken {
        OauthAccessToken { content: Some(content.to_string()), expiry_time: None }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_id_token_success() {
        let http_result = "{\"id_token\": \"test-id-token\", \"expires_in\": 3600}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::OK);
        let test_object = OauthOpenIdConnect::new(mock_http);

        run_proxy_test(test_object, |proxy| async move {
            let id_token = proxy
                .get_id_token_from_refresh_token(OpenIdTokenFromOauthRefreshTokenRequest {
                    refresh_token: Some(create_refresh_token("refresh_token")),
                    audiences: None,
                })
                .await?
                .unwrap();

            assert_eq!(id_token.content, Some("test-id-token".to_string()),);
            assert_eq!(
                id_token.expiry_time.unwrap(),
                (TEST_CURRENT_TIME.clone() + Duration::from_seconds(3600)).into_nanos()
            );
            Ok(())
        })
        .await;

        // An empty vector for audiences is also accepted as default audience.
        let http_result = "{\"id_token\": \"test-id-token\", \"expires_in\": 3600}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::OK);
        let test_object = OauthOpenIdConnect::new(mock_http);

        run_proxy_test(test_object, |proxy| async move {
            let id_token = proxy
                .get_id_token_from_refresh_token(OpenIdTokenFromOauthRefreshTokenRequest {
                    refresh_token: Some(create_refresh_token("refresh_token")),
                    audiences: Some(vec![]),
                })
                .await?
                .unwrap();

            assert_eq!(id_token.content, Some("test-id-token".to_string()),);
            assert_eq!(
                id_token.expiry_time.unwrap(),
                (TEST_CURRENT_TIME.clone() + Duration::from_seconds(3600)).into_nanos()
            );
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_id_token_failures() {
        // Invalid request
        let mock_http = TestHttpClient::with_error(ApiError::Internal);
        let test_object = OauthOpenIdConnect::new(mock_http);
        run_proxy_test(test_object, |proxy| async move {
            let request_result = proxy
                .get_id_token_from_refresh_token(OpenIdTokenFromOauthRefreshTokenRequest {
                    refresh_token: Some(create_refresh_token("refresh_token")),
                    audiences: Some(vec!["".to_string()]),
                })
                .await?;
            assert_eq!(request_result, Err(ApiError::InvalidRequest));
            Ok(())
        })
        .await;

        // Error response
        let http_result = "{\"error\": \"invalid_client\"}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::BAD_REQUEST);
        let test_object = OauthOpenIdConnect::new(mock_http);
        run_proxy_test(test_object, |proxy| async move {
            let request_result = proxy
                .get_id_token_from_refresh_token(OpenIdTokenFromOauthRefreshTokenRequest {
                    refresh_token: Some(create_refresh_token("refresh_token")),
                    audiences: None,
                })
                .await?;
            assert_eq!(request_result, Err(ApiError::Server));
            Ok(())
        })
        .await;

        // Network error
        let mock_http = TestHttpClient::with_error(ApiError::Network);
        let test_object = OauthOpenIdConnect::new(mock_http);
        run_proxy_test(test_object, |proxy| async move {
            let request_result = proxy
                .get_id_token_from_refresh_token(OpenIdTokenFromOauthRefreshTokenRequest {
                    refresh_token: Some(create_refresh_token("refresh_token")),
                    audiences: None,
                })
                .await?;
            assert_eq!(request_result, Err(ApiError::Network));
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_user_info_success() {
        let http_response =
            "{\"sub\": \"test-id\", \"name\": \"Bill\", \"profile\": \"profile-url\", \
             \"picture\": \"picture-url\", \"email\": \"bill@test.com\"}";
        let mock_http = TestHttpClient::with_response(Some(http_response), StatusCode::OK);
        let test_object = OauthOpenIdConnect::new(mock_http);
        run_proxy_test(test_object, |proxy| async move {
            let user_info = proxy
                .get_user_info_from_access_token(OpenIdUserInfoFromOauthAccessTokenRequest {
                    access_token: Some(create_access_token("access_token")),
                })
                .await?
                .unwrap();
            assert_eq!(
                user_info,
                OpenIdUserInfo {
                    subject: Some("test-id".to_string()),
                    name: Some("Bill".to_string()),
                    email: Some("bill@test.com".to_string()),
                    picture: Some("picture-url".to_string())
                }
            );
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_user_info_failures() {
        // Invalid request
        let mock_http = TestHttpClient::with_error(ApiError::Internal);
        let test_object = OauthOpenIdConnect::new(mock_http);
        run_proxy_test(test_object, |proxy| async move {
            let request_result = proxy
                .get_user_info_from_access_token(OpenIdUserInfoFromOauthAccessTokenRequest {
                    access_token: Some(create_access_token("")),
                })
                .await?;
            assert_eq!(request_result, Err(ApiError::InvalidRequest));
            Ok(())
        })
        .await;

        // Error response
        let mock_http = TestHttpClient::with_response(None, StatusCode::INTERNAL_SERVER_ERROR);
        let test_object = OauthOpenIdConnect::new(mock_http);
        run_proxy_test(test_object, |proxy| async move {
            let request_result = proxy
                .get_user_info_from_access_token(OpenIdUserInfoFromOauthAccessTokenRequest {
                    access_token: Some(create_access_token("access_token")),
                })
                .await?;
            assert_eq!(request_result, Err(ApiError::Server));
            Ok(())
        })
        .await;

        // Network error
        let mock_http = TestHttpClient::with_error(ApiError::Network);
        let test_object = OauthOpenIdConnect::new(mock_http);
        run_proxy_test(test_object, |proxy| async move {
            let request_result = proxy
                .get_user_info_from_access_token(OpenIdUserInfoFromOauthAccessTokenRequest {
                    access_token: Some(create_access_token("access_token")),
                })
                .await?;
            assert_eq!(request_result, Err(ApiError::Network));
            Ok(())
        })
        .await;
    }

    #[test]
    fn test_parse_user_info_success() {
        // All optional arguments returned
        let http_result = String::from(
            "{\"sub\": \"id-123\", \"name\": \"Amanda\", \"profile\": \"profile-url\", \
             \"picture\": \"picture-url\", \"email\": \"id-123@example.com\"}",
        );
        let user_info_response =
            parse_user_info_response(Some(http_result), StatusCode::OK).unwrap();
        assert_eq!(
            user_info_response,
            OpenIdUserInfoResponse {
                sub: String::from("id-123"),
                name: Some(String::from("Amanda")),
                email: Some(String::from("id-123@example.com")),
                profile: Some(String::from("profile-url")),
                picture: Some(String::from("picture-url")),
            }
        );

        // Only ID provided
        let http_result = String::from("{\"sub\": \"id-321\"}");
        let user_info_response =
            parse_user_info_response(Some(http_result), StatusCode::OK).unwrap();
        assert_eq!(
            user_info_response,
            OpenIdUserInfoResponse {
                sub: String::from("id-321"),
                name: None,
                email: None,
                profile: None,
                picture: None,
            }
        );
    }

    #[test]
    fn test_parse_user_info_failures() {
        // Bad token case
        let invalid_http_result = String::from("{\"error\": \"invalid_token\"}");
        let result = parse_user_info_response(Some(invalid_http_result), StatusCode::UNAUTHORIZED);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Server error case
        let result = parse_user_info_response(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Malformed response case
        let invalid_http_result = String::from("\\\\malformed\\\\");
        let result = parse_user_info_response(Some(invalid_http_result), StatusCode::OK);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);
    }

    #[test]
    fn test_parse_id_token_response_success() {
        let http_result = "{\"id_token\": \"test-id-token\", \"expires_in\": 3600}".to_string();
        let (id_token, expires_in) =
            parse_id_token_response(Some(http_result), StatusCode::OK).unwrap();
        assert_eq!(id_token, IdToken("test-id-token".to_string()));
        assert_eq!(expires_in, Duration::from_seconds(3600));
    }

    #[test]
    fn test_parse_id_token_response_failures() {
        // Bad token case
        let invalid_http_result = "{\"error\": \"invalid_token\"}".to_string();
        let result = parse_id_token_response(Some(invalid_http_result), StatusCode::UNAUTHORIZED);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Server error case
        let result = parse_id_token_response(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Malformed response case
        let invalid_http_result = "\\\\malformed\\\\".to_string();
        let result = parse_id_token_response(Some(invalid_http_result), StatusCode::OK);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);
    }
}
