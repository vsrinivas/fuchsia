// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provide Google Cloud Storage (GCS) access.

use {
    anyhow::{bail, Result},
    async_lock::Mutex,
    fuchsia_hyper::HttpsClient,
    http::{request, StatusCode},
    hyper::{Body, Method, Request, Response},
    once_cell::sync::OnceCell,
    regex::Regex,
    serde::{Deserialize, Serialize},
    serde_json,
    std::{fs, path::Path},
    url::{form_urlencoded, Url},
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

const APPROVE_AUTH_CODE_URL: &str = "\
        https://accounts.google.com/o/oauth2/auth?\
scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcloud-platform\
&redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob\
&response_type=code\
&client_id=909320924072.apps.googleusercontent.com&access_type=offline";

/// Base URL for JSON API access.
const API_BASE: &str = "https://www.googleapis.com/storage/v1";

/// Base URL for reading (blob) objects.
const STORAGE_BASE: &str = "https://storage.googleapis.com";

/// URL used to exchange an auth_code for a refresh_token.
const EXCHANGE_AUTH_CODE_URL: &str = "https://oauth2.googleapis.com/token";

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
/// 'expires_in' is intentionally omitted.
#[derive(Deserialize)]
struct OauthTokenResponse {
    /// A limited time (see `expires_in`) token used in an Authorization header.
    access_token: String,
}

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

    /// For our use (a non-web program), always "urn:ietf:wg:oauth:2.0:oob"
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

/// Response from the `/b/<bucket>/o` object listing API.
#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct ListResponse {
    /// Continuation token; only present when there is more data.
    next_page_token: Option<String>,

    /// List of objects, sorted by name.
    #[serde(default)]
    items: Vec<ListResponseItem>,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct ListResponseItem {
    /// GCS object name.
    name: String,
}

/// User credentials for use with GCS.
///
/// Specifically to:
/// - api_base: https://www.googleapis.com/storage/v1
/// - storage_base: https://storage.googleapis.com
pub struct TokenStore {
    /// Base URL for JSON API access.
    _api_base: Url,

    /// Base URL for reading (blob) objects.
    storage_base: Url,

    /// A long lasting secret token used to attain a new `access_token`. This
    /// value is a user secret and must not be misused (such as by logging).
    refresh_token: Option<String>,

    /// A limited time token used in an Authorization header.
    ///
    /// Only valid if `Instant::now() < expired()`, though it's better to allow
    /// some extra time (e.g. maybe 30 seconds).
    access_token: Mutex<String>,
}

impl TokenStore {
    /// Only allow access to public GCS data.
    pub fn new_without_auth() -> Self {
        Self {
            _api_base: Url::parse(API_BASE).expect("parse API_BASE"),
            storage_base: Url::parse(STORAGE_BASE).expect("parse STORAGE_BASE"),
            refresh_token: None,
            access_token: Mutex::new("".to_string()),
        }
    }

    /// Allow access to public and private (auth-required) GCS data.
    ///
    /// The `refresh_token` must not be an empty string (this will generate an
    /// Err Result.
    ///
    /// The `access_token` is not required, but if it happens to be available,
    /// i.e. if a code exchange was just done, it avoids an extra round-trip to
    /// provide it here.
    pub fn new_with_auth<T>(refresh_token: String, access_token: T) -> Result<Self>
    where
        T: Into<Option<String>>,
    {
        if refresh_token.is_empty() {
            bail!("Empty refresh token passed to new_with_auth. Please report this as a bug.");
        }
        let access_token = Mutex::new(access_token.into().unwrap_or("".to_string()));
        Ok(Self {
            _api_base: Url::parse(API_BASE).expect("parse API_BASE"),
            storage_base: Url::parse(STORAGE_BASE).expect("parse STORAGE_BASE"),
            refresh_token: Some(refresh_token),
            access_token,
        })
    }

    /// Allow access to public and private GCS data.
    ///
    /// The `https_client` will be used to exchange the auth code for a refresh
    /// token.
    /// The `auth_code` must not be an empty string (this will generate an Err
    /// Result.
    pub async fn new_with_code(https_client: &HttpsClient, auth_code: &str) -> Result<Self> {
        if auth_code.is_empty() {
            bail!("Empty auth code passed to new_with_code. Please report this as a bug.");
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
        let res = https_client.request(req).await?;

        // If the response was successful, extract the new tokens.
        if res.status().is_success() {
            let bytes = hyper::body::to_bytes(res.into_body()).await?;
            let info: ExchangeAuthCodeResponse = serde_json::from_slice(&bytes)?;
            let access_token = Mutex::new(info.access_token.unwrap_or("".to_string()));
            Ok(Self {
                _api_base: Url::parse(API_BASE).expect("parse API_BASE"),
                storage_base: Url::parse(STORAGE_BASE).expect("parse STORAGE_BASE"),
                refresh_token: Some(info.refresh_token),
                access_token,
            })
        } else {
            bail!("Unable to gain new access token.");
        }
    }

    pub fn refresh_token(&self) -> Option<String> {
        self.refresh_token.to_owned()
    }

    /// Create localhost base URLs and fake credentials for testing.
    #[cfg(test)]
    fn local_fake(refresh_token: Option<String>) -> Self {
        let api_base = Url::parse("http://localhost:9000").expect("api_base");
        let storage_base = Url::parse("http://localhost:9001").expect("storage_base");
        Self {
            _api_base: api_base,
            storage_base,
            refresh_token,
            access_token: Mutex::new("".to_string()),
        }
    }

    /// Apply Authorization header, if available.
    ///
    /// If no access_token is set, no changes are made to the builder.
    async fn authenticate(&self, builder: request::Builder) -> Result<request::Builder> {
        if self.refresh_token.is_none() {
            return Ok(builder);
        }
        let access_token = self.access_token.lock().await;
        Ok(builder.header("Authorization", format!("Bearer {}", access_token)))
    }

    /// Use the refresh token to get a new access token.
    async fn refresh_access_token(&self, https_client: &HttpsClient) -> Result<()> {
        match &self.refresh_token {
            Some(refresh_token) => {
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

                let res = https_client.request(req).await?;

                if res.status().is_success() {
                    let bytes = hyper::body::to_bytes(res.into_body()).await?;
                    let info: OauthTokenResponse = serde_json::from_slice(&bytes)?;
                    let mut access_token = self.access_token.lock().await;
                    *access_token = info.access_token;
                    Ok(())
                } else {
                    bail!("Unable to gain new access token.");
                }
            }
            None => bail!("A refresh token is required to gain new access token."),
        }
    }

    /// Reads content of a stored object (blob) from GCS.
    ///
    /// A leading slash "/" on `object` will be ignored.
    pub(crate) async fn download(
        &self,
        https_client: &HttpsClient,
        bucket: &str,
        object: &str,
    ) -> Result<Response<Body>> {
        // If the bucket and object are from a gs:// URL, the object may have a
        // undesirable leading slash. Trim it if present.
        let object = if object.starts_with('/') { &object[1..] } else { object };
        let res = self.attempt_download(https_client, bucket, object).await?;
        Ok(match res.status() {
            StatusCode::FORBIDDEN | StatusCode::UNAUTHORIZED => {
                match &self.refresh_token {
                    Some(_) => {
                        // Refresh the access token and make one extra try.
                        self.refresh_access_token(&https_client).await?;
                        self.attempt_download(https_client, bucket, object).await?
                    }
                    None => {
                        // With no refresh token, there's no option to retry.
                        res
                    }
                }
            }
            _ => res,
        })
    }

    /// Make one attempt to read content of a stored object (blob) from GCS
    /// without considering redirects or retries.
    ///
    /// Callers are expected to handle errors and call attempt_download() again
    /// as desired (e.g. follow redirects).
    async fn attempt_download(
        &self,
        https_client: &HttpsClient,
        bucket: &str,
        object: &str,
    ) -> Result<Response<Body>> {
        let url = self.storage_base.join(&format!("{}/{}", bucket, object))?;
        self.send_request(https_client, url).await
    }

    /// Make one attempt to request data from GCS.
    ///
    /// Callers are expected to handle errors and call attempt_download() again
    /// as desired (e.g. follow redirects).
    async fn send_request(&self, https_client: &HttpsClient, url: Url) -> Result<Response<Body>> {
        let req = Request::builder().method(Method::GET).uri(url.into_string());
        let req = self.authenticate(req).await?;
        let req = req.body(Body::from(""))?;

        let res = https_client.request(req).await?;
        Ok(res)
    }

    /// List objects from GCS in `bucket` with matching `prefix`.
    ///
    /// A leading slash "/" on `prefix` will be ignored.
    pub async fn list(
        &self,
        https_client: &HttpsClient,
        bucket: &str,
        prefix: &str,
    ) -> Result<Vec<String>> {
        // If the bucket and prefix are from a gs:// URL, the prefix may have a
        // undesirable leading slash. Trim it if present.
        let prefix = if prefix.starts_with('/') { &prefix[1..] } else { prefix };

        Ok(match self.attempt_list(https_client, bucket, prefix).await {
            Err(e) => {
                match &self.refresh_token {
                    Some(_) => {
                        // Refresh the access token and make one extra try.
                        self.refresh_access_token(&https_client).await?;
                        self.attempt_list(https_client, bucket, prefix).await?
                    }
                    None => {
                        // With no refresh token, there's no option to retry.
                        return Err(e);
                    }
                }
            }
            Ok(value) => value,
        })
    }

    /// Make one attempt to list objects from GCS.
    async fn attempt_list(
        &self,
        https_client: &HttpsClient,
        bucket: &str,
        prefix: &str,
    ) -> Result<Vec<String>> {
        let mut base_url = Url::parse(API_BASE).expect("parse API_BASE");
        base_url.path_segments_mut().unwrap().extend(&["b", bucket, "o"]);
        base_url
            .query_pairs_mut()
            .append_pair("prefix", prefix)
            .append_pair("prettyPrint", "false")
            .append_pair("fields", "nextPageToken,items/name");
        let mut results = Vec::new();
        let mut page_token: Option<String> = None;
        loop {
            // Create a new URL for each "page" of results.
            let mut url = base_url.clone();
            if let Some(t) = page_token {
                url.query_pairs_mut().append_pair("pageToken", t.as_str());
            }
            let res = self.send_request(https_client, url).await?;
            match res.status() {
                StatusCode::FORBIDDEN | StatusCode::UNAUTHORIZED => {
                    bail!("Auth required to list {}", base_url);
                }
                StatusCode::OK => {
                    let bytes = hyper::body::to_bytes(res.into_body()).await?;
                    let info: ListResponse = serde_json::from_slice(&bytes)?;
                    results.extend(info.items.into_iter().map(|i| i.name));
                    if info.next_page_token.is_none() {
                        break;
                    }
                    page_token = info.next_page_token;
                }
                _ => {
                    bail!("Failed to list {:?} {:?}", base_url, res);
                }
            }
        }
        Ok(results)
    }
}

/// URL which guides the user to an authorization code.
///
/// Expected flow:
/// - Request that the user open a browser to this location
/// - authenticate and grant permission to this program to use GCS
/// - user, copy-pastes the auth code the web page provides back to this program
/// - create a TokenStore with TokenStore::new_with_code(pasted_string)
pub fn auth_code_url() -> String {
    APPROVE_AUTH_CODE_URL.to_string()
}

/// Fetch an existing refresh token from a .boto (gsutil) configuration file.
///
/// Tip, the `boto_path` is commonly "~/.boto". E.g.
/// ```
/// use home::home_dir;
/// let boto_path = Path::new(&home_dir().expect("home dir")).join(".boto");
/// ```
///
/// TODO(fxbug.dev/82014): Using an ffx specific token will be preferred once
/// that feature is created. For the near term, an existing gsutil token is
/// workable.
///
/// Alert: The refresh token is considered a private secret for the user. Do
///        not print the token to a log or otherwise disclose it.
pub fn read_boto_refresh_token<P: AsRef<Path>>(boto_path: P) -> Result<Option<String>> {
    // Read the file at `boto_path` to retrieve a value from a line resembling
    // "gs_oauth2_refresh_token = <string_of_chars>".
    static GS_REFRESH_TOKEN_RE: OnceCell<Regex> = OnceCell::new();
    let re = GS_REFRESH_TOKEN_RE
        .get_or_init(|| Regex::new(r#"\n\s*gs_oauth2_refresh_token\s*=\s*(\S+)"#).expect("regex"));
    let data = fs::read_to_string(boto_path.as_ref())?;
    let refresh_token = match re.captures(&data) {
        Some(found) => Some(found.get(1).expect("found at least one").as_str().to_string()),
        None => None,
    };
    Ok(refresh_token)
}

#[cfg(test)]
mod test {
    use {super::*, fuchsia_hyper::new_https_client, hyper::StatusCode};

    #[test]
    fn test_approve_auth_code_url() {
        // If the GSUTIL_CLIENT_ID is changed, the APPROVE_AUTH_CODE_URL must be
        // updated as well.
        assert_eq!(
            auth_code_url(),
            format!(
                "\
            https://accounts.google.com/o/oauth2/auth?\
            scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcloud-platform\
            &redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob\
            &response_type=code\
            &client_id={}&access_type=offline",
                GSUTIL_CLIENT_ID
            )
        );
    }

    #[should_panic(expected = "Connection refused")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fake_download() {
        let token_store = TokenStore::local_fake(/*refresh_token=*/ None);
        let bucket = "fake_bucket";
        let object = "fake/object/path.txt";
        token_store.download(&new_https_client(), bucket, object).await.expect("client download");
    }

    // This test is marked "ignore" because it actually downloads from GCS,
    // which isn't good for a CI/GI test. It's here because it's handy to have
    // as a local developer test. Run with `fx test gcs_lib_test -- --ignored`.
    // Note: gsutil config is required.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gcs_download_public() {
        let token_store = TokenStore::new_without_auth();
        let bucket = "fuchsia";
        let object = "development/5.20210610.3.1/sdk/linux-amd64/gn.tar.gz";
        let res = token_store
            .download(&new_https_client(), bucket, object)
            .await
            .expect("client download");
        assert_eq!(res.status(), StatusCode::OK);
    }

    // This test is marked "ignore" because it actually downloads from GCS,
    // which isn't good for a CI/GI test. It's here because it's handy to have
    // as a local developer test. Run with `fx test gcs_lib_test -- --ignored`.
    // Note: gsutil config is required.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gcs_download_auth() {
        use home::home_dir;
        let boto_path = Path::new(&home_dir().expect("home dir")).join(".boto");
        let refresh_token =
            read_boto_refresh_token(&boto_path).expect("boto file").expect("refresh token");
        let token_store = TokenStore::new_with_auth(refresh_token, /*access_token=*/ None)
            .expect("new with auth");
        let https = new_https_client();
        token_store.refresh_access_token(&https).await.expect("refresh_access_token");
        let bucket = "fuchsia-sdk";
        let object = "development/LATEST_LINUX";
        let res = token_store.download(&https, bucket, object).await.expect("client download");
        assert_eq!(res.status(), StatusCode::OK);
    }
}
