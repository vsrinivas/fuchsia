// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provide OAuth2 support for Google Cloud Storage (GCS) access.
//!
//! There are two main APIs here:
//! - new_refresh_token() gets a long-lived, storable (to disk) token than can
//!                       be used to create new access tokens.
//! - new_access_token() accepts a refresh token and returns a reusable though
//!                      short-lived token that can be used to access data.

use {
    crate::error::GcsError,
    anyhow::{Context, Result},
    hyper::{Body, Method, Request /*Response*/},
    serde::{Deserialize, Serialize},
    serde_json,
    std::{
        io::{self, BufRead, BufReader, Read, Write},
        string::String,
    },
    url::form_urlencoded,
};

/// URL used for gaining a new access token.
///
/// See RefreshTokenRequest, OauthTokenResponse.
const OAUTH_REFRESH_TOKEN_ENDPOINT: &str = "https://www.googleapis.com/oauth2/v3/token";

/// For a web site, a client secret is kept locked away in a secure server. This
/// is not a web site and the value is needed, so a non-secret "secret" is used.
///
/// These values (and the quote following) are taken form
/// https://chromium.googlesource.com/chromium/tools/depot_tools.git/+/c6a2ee693093926868170f678d8d290bf0de0c15/third_party/gsutil/oauth2_plugin/oauth2_helper.py
/// and
/// https://chromium.googlesource.com/catapult/+/2c541cdf008959bc9813c641cc4ecd0194979486/third_party/gsutil/gslib/utils/system_util.py#177
///
/// "Google OAuth2 clients always have a secret, even if the client is an
/// installed application/utility such as gsutil.  Of course, in such cases the
/// "secret" is actually publicly known; security depends entirely on the
/// secrecy of refresh tokens, which effectively become bearer tokens."
const GSUTIL_CLIENT_ID: &str = "909320924072.apps.googleusercontent.com";
const GSUTIL_CLIENT_SECRET: &str = "p3RlpR10xMFh9ZXBS/ZNLYUu";

const APPROVE_AUTH_CODE_URL: &str = "\
        https://accounts.google.com/o/oauth2/auth?\
scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcloud-platform\
&redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob\
&response_type=code\
&access_type=offline\
&client_id=";

/// URL used to exchange an auth_code for a refresh_token.
const EXCHANGE_AUTH_CODE_URL: &str = "https://oauth2.googleapis.com/token";

/// POST body to [`EXCHANGE_AUTH_CODE_URL`].
#[derive(Serialize)]
struct ExchangeAuthCodeRequest<'a> {
    /// A value provided by GCS for fetching tokens.
    client_id: &'a str,

    /// A (normally secret) value provided by GCS for fetching tokens.
    client_secret: &'a str,

    /// A short lived authorization code used to attain an initial
    /// `refresh_token` and `access_token`.
    code: &'a str,

    /// Will be "authorization_code" for a authorization code.
    grant_type: &'a str,

    /// A local loopback uri to receive the response (with the auth code).
    redirect_uri: &'a str,
}

/// Response body from [`EXCHANGE_AUTH_CODE_URL`].
/// 'expires_in' is intentionally omitted.
#[derive(Deserialize)]
struct ExchangeAuthCodeResponse {
    /// A limited time (see `expires_in`) token used in an Authorization header.
    access_token: Option<String>,

    /// A long lasting secret token. This value is a user secret and must not be
    /// misused (such as by logging). Suitable for storing in a local file and
    /// reusing later.
    refresh_token: String,
}

/// Response body from [`OAUTH_REFRESH_TOKEN_ENDPOINT`].
/// 'expires_in' is intentionally omitted.
#[derive(Deserialize)]
struct OauthTokenResponse {
    /// A limited time (see `expires_in`) token used in an Authorization header.
    access_token: String,
}

/// POST body to [`OAUTH_REFRESH_TOKEN_ENDPOINT`].
#[derive(Serialize)]
struct RefreshTokenRequest<'a> {
    /// A value provided by GCS for fetching tokens.
    client_id: &'a str,

    /// A (normally secret) value provided by GCS for fetching tokens.
    client_secret: &'a str,

    /// A long lasting secret token used to attain a new `access_token`.
    refresh_token: &'a str,

    /// Will be "refresh_token" for a refresh token.
    grant_type: &'a str,
}

/// Performs steps to get a refresh token from scratch.
///
/// This may involve user interaction such as opening a browser window..
pub async fn new_refresh_token() -> Result<String> {
    let auth_code = get_auth_code().context("getting auth code")?;
    let (refresh_token, _) = auth_code_to_refresh(&auth_code).await.context("get refresh token")?;
    Ok(refresh_token)
}

/// Use the 'refresh_token' to request an access token.
///
/// Access tokens are short-lived. Unlike a refresh token, there's little value
/// in storing an access token to disk for later use, though it may be used many
/// times before needing to get a new access_token.
pub async fn new_access_token(refresh_token: &str) -> Result<String, GcsError> {
    tracing::trace!("new_access_token");
    let req_body = RefreshTokenRequest {
        client_id: GSUTIL_CLIENT_ID,
        client_secret: GSUTIL_CLIENT_SECRET,
        refresh_token: refresh_token,
        grant_type: "refresh_token",
    };
    let body = serde_json::to_vec(&req_body)?;
    let req = Request::builder()
        .method(Method::POST)
        .uri(OAUTH_REFRESH_TOKEN_ENDPOINT)
        .body(Body::from(body))?;

    let https_client = fuchsia_hyper::new_https_client();
    let res = https_client.request(req).await?;

    if res.status().is_success() {
        let bytes = hyper::body::to_bytes(res.into_body()).await?;
        let info: OauthTokenResponse = serde_json::from_slice(&bytes)?;
        Ok(info.access_token)
    } else {
        match res.status() {
            http::StatusCode::BAD_REQUEST => return Err(GcsError::NeedNewRefreshToken),
            _ => return Err(GcsError::HttpResponseError(res.status())),
        }
    }
}

/// Convert an authorization code to a refresh token.
///
/// The `auth_code` must not be an empty string (this will generate an Err
/// Result.
async fn auth_code_to_refresh(auth_code: &str) -> Result<(String, Option<String>), GcsError> {
    tracing::trace!("auth_code_to_refresh");

    if auth_code.is_empty() {
        return Err(GcsError::MissingAuthCode);
    }
    // Add POST parameters to exchange the auth_code for a refresh_token
    // and possibly an access_token.
    let body = form_urlencoded::Serializer::new(String::new())
        .append_pair("code", auth_code)
        .append_pair("redirect_uri", "urn:ietf:wg:oauth:2.0:oob")
        .append_pair("client_id", GSUTIL_CLIENT_ID)
        .append_pair("client_secret", GSUTIL_CLIENT_SECRET)
        .append_pair("grant_type", "authorization_code")
        .finish();
    // Build the request and send it.
    let req = Request::builder()
        .method(Method::POST)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .uri(EXCHANGE_AUTH_CODE_URL)
        .body(Body::from(body))?;

    let https_client = fuchsia_hyper::new_https_client();
    let res = https_client.request(req).await?;

    if !res.status().is_success() {
        return Err(GcsError::RefreshAccessError(res.status()));
    }

    // Extract the new tokens.
    let bytes = hyper::body::to_bytes(res.into_body()).await?;
    let info: ExchangeAuthCodeResponse = serde_json::from_slice(&bytes)?;
    let refresh_token = info.refresh_token;
    let access_token = info.access_token;
    Ok((refresh_token.to_string(), access_token))
}

/// Ask the user to visit a URL and copy-paste the auth code provided.
///
/// A helper wrapper around get_auth_code_with() using stdin/stdout.
fn get_auth_code() -> Result<String> {
    let stdout = io::stdout();
    let mut output = stdout.lock();
    let stdin = io::stdin();
    let mut input = stdin.lock();
    get_auth_code_with(&mut output, &mut input)
}

/// Ask the user to visit a URL and copy-paste the authorization code provided.
///
/// Consider using `get_auth_code()` for operation with stdin/stdout.
///
/// For a GUI, consider creating a separate (custom) function to ask the user to
/// follow the web flow to get a authorization code (tip: use `auth_code_url()`
/// to get the URL).
fn get_auth_code_with<W, R>(writer: &mut W, reader: &mut R) -> Result<String>
where
    W: Write,
    R: Read,
{
    writeln!(
        writer,
        "Please visit this site. Proceed through the web flow to allow access \
        and copy the authentication code:\
        \n\n{}{}\n\nPaste the code (from the web page) here\
        \nand press return: ",
        APPROVE_AUTH_CODE_URL, GSUTIL_CLIENT_ID,
    )?;
    writer.flush().expect("flush auth code prompt");
    let mut auth_code = String::new();
    let mut buf_reader = BufReader::new(reader);
    buf_reader.read_line(&mut auth_code).expect("Need an auth_code.");
    Ok(auth_code.trim().to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_auth_code_with() {
        let mut output: Vec<u8> = Vec::new();
        let mut input = "fake_auth_code".as_bytes();
        let auth_code = get_auth_code_with(&mut output, &mut input).expect("auth code");
        assert_eq!(auth_code, "fake_auth_code");
    }
}
