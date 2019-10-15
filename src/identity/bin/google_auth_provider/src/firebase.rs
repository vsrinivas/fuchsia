// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::FIREBASE_TOKEN_URI;
use crate::error::{AuthProviderError, ResultExt};
use crate::http::{HttpRequest, HttpRequestBuilder};
use crate::openid::IdToken;

use fidl_fuchsia_auth::{AuthProviderStatus, FirebaseToken};
use hyper::StatusCode;
use log::warn;
use serde_derive::{Deserialize, Serialize};
use serde_json::{from_str, to_string};
use std::str::FromStr;
use url::{form_urlencoded, Url};

type AuthProviderResult<T> = Result<T, AuthProviderError>;

/// Representation of the JSON body for a Firebase token request.
#[derive(Serialize)]
struct FirebaseTokenRequestBody {
    #[serde(rename = "postBody")]
    post_body: String,
    #[serde(rename = "returnIdpCredential")]
    return_idp_credential: bool,
    #[serde(rename = "returnSecureToken")]
    return_secure_token: bool,
    #[serde(rename = "requestUri")]
    request_uri: String,
}

/// Successful response type for Firebase requests.
#[derive(Deserialize)]
struct FirebaseTokenResponse {
    #[serde(rename = "idToken")]
    id_token: String,
    email: String,
    #[serde(rename = "localId")]
    local_id: String,
    #[serde(rename = "expiresIn")]
    expires_in_sec: String,
}

/// Error response type for Firebase requests.
#[derive(Deserialize)]
struct FirebaseErrorResponse {
    message: String,
}

/// Constructs a Firebase token request.
pub fn build_firebase_token_request(
    id_token: IdToken,
    firebase_api_key: String,
) -> AuthProviderResult<HttpRequest> {
    let params = vec![("key", firebase_api_key.as_str())];
    let url = Url::parse_with_params(FIREBASE_TOKEN_URI.as_str(), &params)
        .auth_provider_status(AuthProviderStatus::InternalError)?;

    let token_request_body = FirebaseTokenRequestBody {
        post_body: form_urlencoded::Serializer::new(String::new())
            .append_pair("id_token", id_token.0.as_str())
            .append_pair("providerId", "google.com")
            .finish(),
        return_idp_credential: true,
        return_secure_token: true,
        request_uri: "http://localhost".to_string(),
    };

    let request_body =
        to_string(&token_request_body).auth_provider_status(AuthProviderStatus::InternalError)?;

    HttpRequestBuilder::new(url.as_str(), "POST")
        .with_header("key", firebase_api_key.as_str())
        .with_header("accept", "application/json")
        .with_header("content-type", "application/json")
        .set_body(&request_body)
        .finish()
}

/// Parses a response for a Firebase token request.
pub fn parse_firebase_token_response(
    response_body: Option<String>,
    status: StatusCode,
) -> AuthProviderResult<FirebaseToken> {
    match (response_body.as_ref(), status) {
        (Some(response), StatusCode::OK) => {
            let FirebaseTokenResponse { id_token, email, local_id, expires_in_sec } =
                from_str::<FirebaseTokenResponse>(&response)
                    .auth_provider_status(AuthProviderStatus::OauthServerError)?;
            let parsed_expiry_seconds = u64::from_str(&expires_in_sec)
                .auth_provider_status(AuthProviderStatus::OauthServerError)?;
            Ok(FirebaseToken {
                id_token,
                email: Some(email),
                local_id: Some(local_id),
                expires_in: parsed_expiry_seconds,
            })
        }
        (Some(response), status) if status.is_client_error() => {
            let FirebaseErrorResponse { message } = from_str::<FirebaseErrorResponse>(&response)
                .auth_provider_status(AuthProviderStatus::OauthServerError)?;
            warn!("Got unexpected error while retrieving Firebase token: {}", message);
            Err(AuthProviderError::new(AuthProviderStatus::OauthServerError))
        }
        _ => Err(AuthProviderError::new(AuthProviderStatus::OauthServerError)),
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_parse_firebase_token_response_success() {
        let response_body = "{\"idToken\": \"test-firebase-token\", \"localId\": \"test-id\",\
                             \"email\": \"test@example.com\", \"expiresIn\": \"3600\"}"
            .to_string();
        assert_eq!(
            parse_firebase_token_response(Some(response_body), StatusCode::OK).unwrap(),
            FirebaseToken {
                id_token: "test-firebase-token".to_string(),
                email: Some("test@example.com".to_string()),
                local_id: Some("test-id".to_string()),
                expires_in: 3600,
            }
        );
    }

    #[test]
    fn test_parse_firebase_token_response_failures() {
        // Client error
        let response_body = "{\"message\": \"invalid API key\"}".to_string();
        let result = parse_firebase_token_response(Some(response_body), StatusCode::BAD_REQUEST);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);

        // Server error
        let result = parse_firebase_token_response(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);

        // Malformed response
        let response_body = "\\malformed\\".to_string();
        let result = parse_firebase_token_response(Some(response_body), StatusCode::OK);
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::OauthServerError);
    }
}
