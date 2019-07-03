// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains methods for creating OAuth requests and interpreting
//! responses.

use crate::constants::{FUCHSIA_CLIENT_ID, OAUTH_URI, REDIRECT_URI};
use crate::error::{AuthProviderError, ResultExt};
use crate::http::{HttpRequest, HttpRequestBuilder};

use fidl_fuchsia_auth::AuthProviderStatus;
use hyper::StatusCode;
use log::warn;
use serde_derive::Deserialize;
use serde_json::from_str;
use url::form_urlencoded;

type AuthProviderResult<T> = Result<T, AuthProviderError>;

/// Response type for Oauth access token requests.
#[derive(Debug, Deserialize)]
struct AccessTokenResponse {
    pub access_token: String,
    pub refresh_token: String,
}

/// Error response type for Oauth requests.
#[derive(Debug, Deserialize)]
struct OAuthErrorResponse {
    pub error: String,
    pub error_description: Option<String>,
}

/// Construct an Oauth access token request using an authorization code.
pub fn build_request_with_auth_code(auth_code: String) -> AuthProviderResult<HttpRequest> {
    let request_body = form_urlencoded::Serializer::new(String::new())
        .append_pair("code", auth_code.as_str())
        .append_pair("redirect_uri", REDIRECT_URI.as_str())
        .append_pair("client_id", FUCHSIA_CLIENT_ID)
        .append_pair("grant_type", "authorization_code")
        .finish();

    HttpRequestBuilder::new(OAUTH_URI.as_str(), "POST")
        .with_header("content-type", "application/x-www-form-urlencoded")
        .set_body(&request_body)
        .finish()
}

/// Parses a response for an OAuth access token request when both a refresh token
/// and access token are expected in the response.
pub fn parse_response_with_refresh_token(
    response_body: Option<String>,
    status: StatusCode,
) -> AuthProviderResult<(String, String)> {
    match (response_body.as_ref(), status) {
        (Some(response), StatusCode::OK) => {
            let response = from_str::<AccessTokenResponse>(&response)
                .auth_provider_status(AuthProviderStatus::OauthServerError)?;
            Ok((response.refresh_token, response.access_token))
        }
        (Some(response), status) if status.is_client_error() => {
            let error_response = from_str::<OAuthErrorResponse>(&response)
                .auth_provider_status(AuthProviderStatus::OauthServerError)?;
            let status = match error_response.error.as_str() {
                "invalid_grant" => AuthProviderStatus::ReauthRequired,
                error_code => {
                    warn!("Got unexpected error code during auth code exchange: {}", error_code);
                    AuthProviderStatus::OauthServerError
                }
            };
            Err(AuthProviderError::new(status))
        }
        _ => Err(AuthProviderError::new(AuthProviderStatus::OauthServerError)),
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_parse_response_success() {
        let response_body = Some(
            "{\"refresh_token\": \"test-refresh-token\", \"access_token\": \"test-access-token\"}"
                .to_string(),
        );
        assert_eq!(
            ("test-refresh-token".to_string(), "test-access-token".to_string()),
            parse_response_with_refresh_token(response_body, StatusCode::OK).unwrap()
        )
    }

    #[test]
    fn test_parse_response_failures() {
        // Expired auth token
        let response =
            "{\"error\": \"invalid_grant\", \"error_description\": \"ouch\"}".to_string();
        let result = parse_response_with_refresh_token(Some(response), StatusCode::BAD_REQUEST);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::ReauthRequired);

        // Server side error
        let result = parse_response_with_refresh_token(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);

        // Invalid client error
        let response = "{\"error\": \"invalid_client\"}".to_string();
        let result = parse_response_with_refresh_token(Some(response), StatusCode::UNAUTHORIZED);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);

        // Malformed response
        let response = "{\"a malformed response\"}".to_string();
        let result = parse_response_with_refresh_token(Some(response), StatusCode::OK);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);
    }
}
