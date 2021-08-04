// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provide Google Cloud Storage (GCS) authentication.

use {
    anyhow::{anyhow, Result},
    fuchsia_hyper::new_https_client,
    http::request,
    hyper::{Body, Method, Request},
    serde::{Deserialize, Serialize},
    serde_json,
    std::time::{Duration, Instant},
};

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

/// Insist on a margin of extra time (avoid using a token that is about to
/// expire).
const EXPIRATION_MARGIN: Duration = Duration::from_secs(60);

/// URL used for gaining a new access token.
///
/// See RefreshTokenRequest, OauthTokenResponse.
const OAUTH_REFRESH_TOKEN_ENDPOINT: &str = "https://www.googleapis.com/oauth2/v3/token";

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

/// Response body from [`OAUTH_REFRESH_TOKEN_ENDPOINT`].
#[derive(Deserialize)]
struct OauthTokenResponse {
    /// A limited time (see `expires_in`) token used in an Authorization header.
    access_token: String,

    /// This is optional per OAuth spec, so a default value is provided.
    #[serde(default = "OauthTokenResponse::default_expires_in")]
    expires_in: u64, // seconds
}

impl OauthTokenResponse {
    /// The default access token lifetime in seconds from Google OAuth servers.
    /// Used only when no "expires_in" is provided in the `OauthTokenResponse`.
    fn default_expires_in() -> u64 {
        // Default access token lifetime in seconds.
        3599
    }
}

/// User credentials for use with GCS.
pub struct TokenStore {
    /// A value provided by GCS for fetching tokens.
    client_id: &'static str,

    /// A value provided by GCS for fetching tokens. Not really a "secret" in
    /// this use. This requires that the `refresh_token` be kept secret.
    client_secret: &'static str,

    /// A long lasting secret token used to attain a new `access_token`. This
    /// value is a user secret and must not be misused (such as by logging).
    refresh_token: Option<String>,

    /// A limited time (see `access_expires`) token used in an Authorization
    /// header.
    ///
    /// Only valid if `Instant::now() < expired()`, though it's better to allow
    /// some extra time (e.g. maybe 30 seconds).
    access_token: String,

    /// Determine if a new `access_token` needed (i.e. the current token has
    /// expired).
    access_expires: Instant,
}

/// Create a time that is prior to "now" such that `Instant::now() > expired()`.
fn expired() -> Instant {
    Instant::now().checked_sub(Duration::new(1, 0)).expect("expired instant")
}

impl TokenStore {
    /// Using a refresh_token of None will only allow access to public GCS data,
    /// unless it's set later with `set_refresh_token()`.
    pub fn new(refresh_token: Option<String>) -> Self {
        Self {
            client_id: GSUTIL_CLIENT_ID,
            client_secret: GSUTIL_CLIENT_SECRET,
            refresh_token: refresh_token,
            access_token: "".to_string(),
            access_expires: expired(),
        }
    }

    /// Apply Authorization header, if available.
    ///
    /// If no access_token is set, no changes are made to the builder.
    pub async fn authenticate(&mut self, builder: request::Builder) -> Result<request::Builder> {
        match &self.get_access_token().await? {
            Some(access_token) => {
                Ok(builder.header("Authorization", format!("Bearer {}", access_token)))
            }
            None => Ok(builder),
        }
    }

    /// If `refresh_token` is set, either return the current access token or
    /// attempt to retrieve a new access token from GCS (erring if retrieval
    /// fails).
    ///
    /// If `refresh_token` is None return None.
    async fn get_access_token(&mut self) -> Result<Option<&str>> {
        match &self.refresh_token {
            Some(refresh_token) => {
                if Instant::now() + EXPIRATION_MARGIN >= self.access_expires {
                    let req_body = RefreshTokenRequest {
                        client_id: &self.client_id,
                        client_secret: &self.client_secret,
                        refresh_token: refresh_token,
                        grant_type: "refresh_token",
                    };
                    let body = serde_json::to_vec(&req_body)?;
                    let req = Request::builder()
                        .method(Method::POST)
                        .uri(OAUTH_REFRESH_TOKEN_ENDPOINT)
                        .body(Body::from(body))?;

                    let res = new_https_client().request(req).await?;

                    if res.status().is_success() {
                        let bytes = hyper::body::to_bytes(res.into_body()).await?;
                        let info: OauthTokenResponse = serde_json::from_slice(&bytes)?;
                        self.access_token = info.access_token;
                        self.access_expires = Instant::now() + Duration::from_secs(info.expires_in);
                        Ok(Some(&self.access_token))
                    } else {
                        Err(anyhow!("Failed to update GCS access token."))
                    }
                } else {
                    Ok(Some(&self.access_token))
                }
            }
            None => Ok(None),
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_expired() {
        assert!(Instant::now() > expired());
    }

    #[test]
    fn test_default_expires_in() {
        let res: OauthTokenResponse =
            serde_json::from_str(r#"{"access_token": "fake", "expires_in": 123}"#)
                .expect("token response");
        assert_eq!(res.expires_in, 123);
        let res: OauthTokenResponse =
            serde_json::from_str(r#"{"access_token": "fake"}"#).expect("token response");
        assert_eq!(res.expires_in, 3599);
    }
}
