// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains methods for creating OpenID requests and interpreting
//! responses.

use crate::constants::USER_INFO_URI;
use crate::error::{AuthProviderError, ResultExt};
use crate::http::{HttpRequest, HttpRequestBuilder};
use crate::oauth::{self, AccessToken, RefreshToken};

use fidl_fuchsia_auth::{AuthProviderStatus, UserProfileInfo};
use hyper::StatusCode;
use log::warn;
use serde_derive::Deserialize;
use serde_json::from_str;

type AuthProviderResult<T> = Result<T, AuthProviderError>;
#[derive(Debug, PartialEq)]
pub struct IdToken(pub String);

/// Response type for OpenID user info requests.
#[derive(Debug, Deserialize)]
struct OpenIdUserInfoResponse {
    pub sub: String,
    pub name: Option<String>,
    pub profile: Option<String>,
    pub picture: Option<String>,
}

/// Response type for an OpenID ID token request.
#[derive(Debug, Deserialize)]
struct OpenIdTokenResponse {
    pub id_token: String,
    pub expires_in: u64,
}

/// Error response for OpenID requests.
#[derive(Debug, Deserialize)]
struct OpenIdErrorResponse {
    pub error: String,
}

/// Construct an `HttpRequest` for an OpenID user info request.
pub fn build_user_info_request(access_token: AccessToken) -> AuthProviderResult<HttpRequest> {
    HttpRequestBuilder::new(USER_INFO_URI.as_str(), "GET")
        .with_header("Authorization", format!("Bearer {}", access_token.0))
        .finish()
}

/// Construct an `HttpRequest` to request an OpenID ID token.
pub fn build_id_token_request(
    refresh_token: RefreshToken,
    audience: Option<String>,
) -> AuthProviderResult<HttpRequest> {
    // OpenID standard dictates that id_token is returned as part of an Oauth
    // access token response.  Thus, the request is really just an Oauth request.
    oauth::build_request_with_refresh_token(refresh_token, vec![], audience)
}

/// Parse an OpenID user info response.
pub fn parse_user_info_response(
    response_body: Option<String>,
    status_code: StatusCode,
) -> AuthProviderResult<UserProfileInfo> {
    match (response_body.as_ref(), status_code) {
        (Some(response), StatusCode::OK) => {
            let OpenIdUserInfoResponse { sub, name, profile, picture } =
                serde_json::from_str::<OpenIdUserInfoResponse>(&response)
                    .auth_provider_status(AuthProviderStatus::OauthServerError)?;
            Ok(UserProfileInfo { id: sub, display_name: name, url: profile, image_url: picture })
        }
        (Some(response), status) if status.is_client_error() => {
            let error_response = from_str::<OpenIdErrorResponse>(&response)
                .auth_provider_status(AuthProviderStatus::OauthServerError)?;
            warn!("Got unexpected error code for OpenId user info: {}", error_response.error);
            Err(AuthProviderError::new(AuthProviderStatus::OauthServerError))
        }
        _ => Err(AuthProviderError::new(AuthProviderStatus::OauthServerError)),
    }
}

/// Parse an OpenID ID token response.
pub fn parse_id_token_response(
    response_body: Option<String>,
    status_code: StatusCode,
) -> AuthProviderResult<(IdToken, u64)> {
    match (response_body.as_ref(), status_code) {
        (Some(response), StatusCode::OK) => {
            let OpenIdTokenResponse { id_token, expires_in } =
                serde_json::from_str::<OpenIdTokenResponse>(&response)
                    .auth_provider_status(AuthProviderStatus::OauthServerError)?;
            Ok((IdToken(id_token), expires_in))
        }
        (Some(response), status) if status.is_client_error() => {
            let error_response = from_str::<OpenIdErrorResponse>(&response)
                .auth_provider_status(AuthProviderStatus::OauthServerError)?;
            warn!("Got unexpected error code while retrieving ID token: {}", error_response.error);
            Err(AuthProviderError::new(AuthProviderStatus::OauthServerError))
        }
        _ => Err(AuthProviderError::new(AuthProviderStatus::OauthServerError)),
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_parse_user_info_success() {
        // All optional arguments returned
        let http_result = String::from(
            "{\"sub\": \"id-123\", \"name\": \"Amanda\", \"profile\": \"profile-url\", \
             \"picture\": \"picture-url\"}",
        );
        let user_profile_info =
            parse_user_info_response(Some(http_result), StatusCode::OK).unwrap();
        assert_eq!(
            user_profile_info,
            UserProfileInfo {
                id: String::from("id-123"),
                display_name: Some(String::from("Amanda")),
                url: Some(String::from("profile-url")),
                image_url: Some(String::from("picture-url")),
            }
        );

        // Only ID provided
        let http_result = String::from("{\"sub\": \"id-321\"}");
        let user_profile_info =
            parse_user_info_response(Some(http_result), StatusCode::OK).unwrap();
        assert_eq!(
            user_profile_info,
            UserProfileInfo {
                id: String::from("id-321"),
                display_name: None,
                url: None,
                image_url: None,
            }
        );
    }

    #[test]
    fn test_parse_user_info_failures() {
        // Bad token case
        let invalid_http_result = String::from("{\"error\": \"invalid_token\"}");
        let result = parse_user_info_response(Some(invalid_http_result), StatusCode::UNAUTHORIZED);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);

        // Server error case
        let result = parse_user_info_response(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);

        // Malformed response case
        let invalid_http_result = String::from("\\\\malformed\\\\");
        let result = parse_user_info_response(Some(invalid_http_result), StatusCode::OK);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);
    }

    #[test]
    fn test_parse_id_token_response_success() {
        let http_result = "{\"id_token\": \"test-id-token\", \"expires_in\": 3600}".to_string();
        let (id_token, expires_in) =
            parse_id_token_response(Some(http_result), StatusCode::OK).unwrap();
        assert_eq!(id_token, IdToken("test-id-token".to_string()));
        assert_eq!(expires_in, 3600);
    }

    #[test]
    fn test_parse_id_token_response_failures() {
        // Bad token case
        let invalid_http_result = "{\"error\": \"invalid_token\"}".to_string();
        let result = parse_id_token_response(Some(invalid_http_result), StatusCode::UNAUTHORIZED);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);

        // Server error case
        let result = parse_id_token_response(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);

        // Malformed response case
        let invalid_http_result = "\\\\malformed\\\\".to_string();
        let result = parse_id_token_response(Some(invalid_http_result), StatusCode::OK);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);
    }
}
