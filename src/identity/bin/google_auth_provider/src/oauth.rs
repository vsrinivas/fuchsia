// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains methods for creating OAuth requests and interpreting
//! responses.

use crate::constants::{
    FUCHSIA_CLIENT_ID, OAUTH_REVOCATION_URI, OAUTH_TOKEN_EXCHANGE_URI, REDIRECT_URI,
};
use crate::error::{ResultExt, TokenProviderError};
use crate::http::{HttpRequest, HttpRequestBuilder};

use failure::format_err;
use fidl_fuchsia_identity_external::Error as ApiError;
use fuchsia_zircon::Duration;
use hyper::StatusCode;
use log::warn;
use serde_derive::Deserialize;
use serde_json::from_str;
use std::borrow::Cow;
use std::collections::HashMap;
use url::{form_urlencoded, Url};

type TokenProviderResult<T> = Result<T, TokenProviderError>;
#[derive(Debug, PartialEq)]
pub struct AuthCode(pub String);
#[derive(Debug, PartialEq)]
pub struct RefreshToken(pub String);
#[derive(Debug, PartialEq)]
pub struct AccessToken(pub String);

/// Response type for Oauth access token requests with a refresh token.
#[derive(Debug, Deserialize)]
struct AccessTokenResponseWithRefreshToken {
    pub access_token: String,
    pub refresh_token: String,
}

/// Response type for Oauth access token requests where a refresh token is not expected.
#[derive(Debug, Deserialize)]
struct AccessTokenResponseWithoutRefreshToken {
    pub access_token: String,
    pub expires_in: u32,
}

/// Error response type for Oauth requests.
#[derive(Debug, Deserialize)]
struct OAuthErrorResponse {
    pub error: String,
    pub error_description: Option<String>,
}

/// Construct an Oauth access token request using an authorization code.
pub fn build_request_with_auth_code(auth_code: AuthCode) -> TokenProviderResult<HttpRequest> {
    let request_body = form_urlencoded::Serializer::new(String::new())
        .append_pair("code", auth_code.0.as_str())
        .append_pair("redirect_uri", REDIRECT_URI.as_str())
        .append_pair("client_id", FUCHSIA_CLIENT_ID)
        .append_pair("grant_type", "authorization_code")
        .finish();

    HttpRequestBuilder::new(OAUTH_TOKEN_EXCHANGE_URI.as_str(), "POST")
        .with_header("content-type", "application/x-www-form-urlencoded")
        .set_body(&request_body)
        .finish()
}

/// Construct an Oauth access token request using a refresh token grant.  If
/// `client_id` is not given the Fuchsia client id is used.
pub fn build_request_with_refresh_token(
    refresh_token: RefreshToken,
    scopes: Vec<String>,
    client_id: Option<String>,
) -> TokenProviderResult<HttpRequest> {
    let request_body = form_urlencoded::Serializer::new(String::new())
        .append_pair("refresh_token", refresh_token.0.as_str())
        .append_pair("client_id", client_id.as_ref().map_or(FUCHSIA_CLIENT_ID, String::as_str))
        .append_pair("grant_type", "refresh_token")
        .append_pair("scope", &scopes.join(" "))
        .finish();

    HttpRequestBuilder::new(OAUTH_TOKEN_EXCHANGE_URI.as_str(), "POST")
        .with_header("content-type", "application/x-www-form-urlencoded")
        .set_body(&request_body)
        .finish()
}

/// Construct an Oauth token revocation request.  `credential` may be either
/// an access token or refresh token.
pub fn build_revocation_request(credential: String) -> TokenProviderResult<HttpRequest> {
    let request_body =
        form_urlencoded::Serializer::new(String::new()).append_pair("token", &credential).finish();

    HttpRequestBuilder::new(OAUTH_REVOCATION_URI.as_str(), "POST")
        .with_header("content-type", "application/x-www-form-urlencoded")
        .set_body(&request_body)
        .finish()
}

/// Parses a response for an OAuth access token request when both a refresh token
/// and access token are expected in the response.
pub fn parse_response_with_refresh_token(
    response_body: Option<String>,
    status: StatusCode,
) -> TokenProviderResult<(RefreshToken, AccessToken)> {
    match (response_body.as_ref(), status) {
        (Some(response), StatusCode::OK) => {
            let response = from_str::<AccessTokenResponseWithRefreshToken>(&response)
                .token_provider_error(ApiError::Server)?;
            Ok((RefreshToken(response.refresh_token), AccessToken(response.access_token)))
        }
        (Some(response), status) if status.is_client_error() => {
            let error_response =
                from_str::<OAuthErrorResponse>(&response).token_provider_error(ApiError::Server)?;
            let error = match error_response.error.as_str() {
                "invalid_grant" => ApiError::InvalidToken,
                error_code => {
                    warn!("Got unexpected error code during auth code exchange: {}", error_code);
                    ApiError::Server
                }
            };
            Err(TokenProviderError::new(error))
        }
        _ => Err(TokenProviderError::new(ApiError::Server)),
    }
}

/// Parses a response for an OAuth access token request when a refresh token is
/// not expected.  Returns an access token and the lifetime of the token.
pub fn parse_response_without_refresh_token(
    response_body: Option<String>,
    status: StatusCode,
) -> TokenProviderResult<(AccessToken, Duration)> {
    match (response_body.as_ref(), status) {
        (Some(response), StatusCode::OK) => {
            let response = from_str::<AccessTokenResponseWithoutRefreshToken>(&response)
                .token_provider_error(ApiError::Server)?;
            Ok((
                AccessToken(response.access_token),
                Duration::from_seconds(response.expires_in as i64),
            ))
        }
        (Some(response), status) if status.is_client_error() => {
            let response =
                from_str::<OAuthErrorResponse>(&response).token_provider_error(ApiError::Server)?;
            let error = match response.error.as_str() {
                "invalid_grant" => ApiError::InvalidToken,
                error_code => {
                    warn!("Got unexpected error code from access token request: {}", error_code);
                    ApiError::Server
                }
            };
            Err(TokenProviderError::new(error))
        }
        _ => Err(TokenProviderError::new(ApiError::Server)),
    }
}

/// Parses a response for an Oauth revocation request.
pub fn parse_revocation_response(
    response_body: Option<String>,
    status: StatusCode,
) -> TokenProviderResult<()> {
    match (response_body.as_ref(), status) {
        (_, StatusCode::OK) => Ok(()),
        (Some(response), status) if status.is_client_error() => {
            let response =
                from_str::<OAuthErrorResponse>(&response).token_provider_error(ApiError::Server)?;
            warn!("Got unexpected error code during token revocation: {}", response.error);
            Err(TokenProviderError::new(ApiError::Server))
        }
        _ => Err(TokenProviderError::new(ApiError::Server)),
    }
}

/// Parses an auth code out of a redirect URL reached through an OAuth
/// authorization flow.
pub fn parse_auth_code_from_redirect(url: Url) -> TokenProviderResult<AuthCode> {
    if (url.scheme(), url.domain(), url.path())
        != (REDIRECT_URI.scheme(), REDIRECT_URI.domain(), REDIRECT_URI.path())
    {
        return Err(TokenProviderError::new(ApiError::Internal)
            .with_cause(format_err!("Redirected to unexpected URL")));
    }

    let params = url.query_pairs().collect::<HashMap<Cow<'_, str>, Cow<'_, str>>>();

    if let Some(auth_code) = params.get("code") {
        Ok(AuthCode(auth_code.as_ref().to_string()))
    } else if let Some(error_code) = params.get("error") {
        let error = match error_code.as_ref() {
            "access_denied" => ApiError::Aborted,
            "server_error" => ApiError::Server,
            "temporarily_unavailable" => ApiError::Server,
            _ => ApiError::Unknown,
        };
        Err(TokenProviderError::new(error))
    } else {
        Err(TokenProviderError::new(ApiError::Unknown)
            .with_cause(format_err!("Authorize redirect contained neither code nor error")))
    }
}

#[cfg(test)]
mod test {
    use super::*;

    fn url_with_query(url_base: &Url, query: &str) -> Url {
        let mut url = url_base.clone();
        url.set_query(Some(query));
        url
    }

    #[test]
    fn test_parse_response_with_refresh_token_success() {
        let response_body = Some(
            "{\"refresh_token\": \"test-refresh-token\", \"access_token\": \"test-access-token\"}"
                .to_string(),
        );
        assert_eq!(
            (
                RefreshToken("test-refresh-token".to_string()),
                AccessToken("test-access-token".to_string())
            ),
            parse_response_with_refresh_token(response_body, StatusCode::OK).unwrap()
        )
    }

    #[test]
    fn test_parse_response_with_refresh_token_failures() {
        // Expired auth token
        let response =
            "{\"error\": \"invalid_grant\", \"error_description\": \"ouch\"}".to_string();
        let result = parse_response_with_refresh_token(Some(response), StatusCode::BAD_REQUEST);
        assert_eq!(result.unwrap_err().api_error, ApiError::InvalidToken);

        // Server side error
        let result = parse_response_with_refresh_token(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Invalid client error
        let response = "{\"error\": \"invalid_client\"}".to_string();
        let result = parse_response_with_refresh_token(Some(response), StatusCode::UNAUTHORIZED);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Malformed response
        let response = "{\"a malformed response\"}".to_string();
        let result = parse_response_with_refresh_token(Some(response), StatusCode::OK);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);
    }

    #[test]
    fn test_parse_response_without_refresh_token_success() {
        let response_body =
            Some("{\"access_token\": \"test-access-token\", \"expires_in\": 3600}".to_string());
        assert_eq!(
            (AccessToken("test-access-token".to_string()), Duration::from_seconds(3600)),
            parse_response_without_refresh_token(response_body, StatusCode::OK).unwrap()
        )
    }

    #[test]
    fn test_parse_response_without_refresh_token_failures() {
        // Expired auth token
        let response =
            "{\"error\": \"invalid_grant\", \"error_description\": \"expired\"}".to_string();
        let result = parse_response_without_refresh_token(Some(response), StatusCode::BAD_REQUEST);
        assert_eq!(result.unwrap_err().api_error, ApiError::InvalidToken);

        // Server side error
        let result = parse_response_without_refresh_token(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Invalid client error
        let response = "{\"error\": \"invalid_client\"}".to_string();
        let result = parse_response_without_refresh_token(Some(response), StatusCode::UNAUTHORIZED);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Malformed response
        let response = "{\"a malformed response\"}".to_string();
        let result = parse_response_without_refresh_token(Some(response), StatusCode::OK);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);
    }

    #[test]
    fn test_parse_revocation_response_success() {
        assert!(parse_revocation_response(None, StatusCode::OK).is_ok());
    }

    #[test]
    fn test_parse_revocation_response_failures() {
        // Server side error
        let result = parse_revocation_response(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Malformed response
        let response = "bad response".to_string();
        let result = parse_revocation_response(Some(response), StatusCode::BAD_REQUEST);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);
    }

    #[test]
    fn test_auth_code_from_redirect() {
        // Success case
        let success_url = url_with_query(&REDIRECT_URI, "code=test-auth-code");
        assert_eq!(
            AuthCode("test-auth-code".to_string()),
            parse_auth_code_from_redirect(success_url).unwrap()
        );

        // Access denied case
        let canceled_url = url_with_query(&REDIRECT_URI, "error=access_denied");
        assert_eq!(
            ApiError::Aborted,
            parse_auth_code_from_redirect(canceled_url).unwrap_err().api_error
        );

        // Unexpected redirect
        let error_url = Url::parse("ftp://incorrect/some-page").unwrap();
        assert_eq!(
            ApiError::Internal,
            parse_auth_code_from_redirect(error_url).unwrap_err().api_error
        );

        // Unknown error
        let invalid_url = url_with_query(&REDIRECT_URI, "error=invalid_request");
        assert_eq!(
            ApiError::Unknown,
            parse_auth_code_from_redirect(invalid_url).unwrap_err().api_error
        );

        // No code or error in url.
        assert_eq!(
            ApiError::Unknown,
            parse_auth_code_from_redirect(REDIRECT_URI.clone()).unwrap_err().api_error
        );
    }
}
