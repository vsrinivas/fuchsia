// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provide Google Cloud Storage (GCS) access.

use {
    anyhow::{bail, Context, Result},
    async_lock::Mutex,
    fuchsia_hyper::HttpsClient,
    http::{request, StatusCode},
    hyper::{Body, Method, Request, Response},
    once_cell::sync::OnceCell,
    regex::Regex,
    serde::{Deserialize, Serialize},
    serde_json,
    std::{
        fmt, fs,
        io::{self, BufRead, BufReader, Read, Write},
        path::Path,
        string::String,
    },
    thiserror::Error,
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
&access_type=offline\
&client_id=";

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

/// Errors specific to this lib, some may be addressed by the caller.
#[derive(Error, Debug)]
pub enum GcsError {
    /// The refresh token is no longer valid (e.g. expired or revoked).
    /// Do something similar to `gsutil config` or new_refresh_token() to
    /// generate a new refresh token and try again.
    #[error("GCS refresh token is invalid. Please generate a new GCS OAUTH2 refresh token.")]
    NeedNewRefreshToken,

    /// The access token is no longer valid (e.g. expired or revoked). Access
    /// tokens are short lived and don't require user interaction to renew.
    /// Refresh the access token and try the action again.
    #[error("GCS access token is invalid. This should be handled. Report as a bug.")]
    NeedNewAccessToken,

    /// GCS returned an empty response for the request.
    #[error("The requested GCS bucket + path contains no data (not found): {0}, {1}.")]
    NotFound(String, String),

    /// The authorization code is empty string (or None). Likely a mistake in
    /// the calling application (e.g. calling new_with_code() with an empty
    /// string).
    #[error("Empty authorization code passed to GCS. Report as a bug.")]
    MissingAuthCode,

    /// Likely a mistake in the calling application.
    #[error("A GCS refresh token is required to gain a new access token. Report as a bug.")]
    MissingRefreshToken,

    /// May be a network issue. Consider informing the user and offer to retry.
    #[error(
        "Unable to refresh GCS access token: HTTP {0}. Check network connection and try again."
    )]
    RefreshAccessError(http::StatusCode),

    /// May be a network issue. Consider informing the user and offer to retry.
    #[error("GCS HttpResponseError: {0}. Check network connection and try again.")]
    HttpResponseError(http::StatusCode),

    /// May be a network issue. Consider informing the user and offer to retry.
    #[error("GCS HttpError: {0}. Check network connection and try again.")]
    HttpError(#[from] http::Error),

    /// May be a network issue. Consider informing the user and offer to retry.
    #[error("GCS HyperError: {0}. Check network connection and try again.")]
    HyperError(#[from] hyper::Error),

    /// Possible in-transit corruption, but more likely the GCS protocol
    /// changed. May need to update GCS lib.
    #[error("GCS SerdeError: {0}. Report as a bug.")]
    SerdeError(#[from] serde_json::Error),
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
#[derive(Debug, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct ListResponse {
    /// Continuation token; only present when there is more data.
    next_page_token: Option<String>,

    /// List of objects, sorted by name.
    #[serde(default)]
    items: Vec<ListResponseItem>,
}

#[derive(Debug, serde::Deserialize)]
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
    api_base: Url,

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
            api_base: Url::parse(API_BASE).expect("parse API_BASE"),
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
    pub fn new_with_auth<T>(refresh_token: String, access_token: T) -> Result<Self, GcsError>
    where
        T: Into<Option<String>>,
    {
        if refresh_token.is_empty() {
            return Err(GcsError::MissingRefreshToken);
        }
        let access_token = Mutex::new(access_token.into().unwrap_or("".to_string()));
        Ok(Self {
            api_base: Url::parse(API_BASE).expect("parse API_BASE"),
            storage_base: Url::parse(STORAGE_BASE).expect("parse STORAGE_BASE"),
            refresh_token: Some(refresh_token),
            access_token,
        })
    }

    pub fn refresh_token(&self) -> Option<String> {
        self.refresh_token.to_owned()
    }

    /// Create localhost base URLs and fake credentials for testing.
    #[cfg(test)]
    fn local_fake(refresh_token: Option<String>) -> Self {
        let api_base = Url::parse("http://localhost:9000").expect("api_base");
        let storage_base = Url::parse("http://localhost:9001").expect("storage_base");
        Self { api_base, storage_base, refresh_token, access_token: Mutex::new("".to_string()) }
    }

    /// Apply Authorization header, if available.
    ///
    /// If no access_token is set, no changes are made to the builder.
    async fn authorize(&self, builder: request::Builder) -> Result<request::Builder> {
        if self.refresh_token.is_none() {
            return Ok(builder);
        }
        let access_token = self.access_token.lock().await;
        Ok(builder.header("Authorization", format!("Bearer {}", access_token)))
    }

    /// Use the refresh token to get a new access token.
    async fn refresh_access_token(&self, https_client: &HttpsClient) -> Result<(), GcsError> {
        tracing::trace!("refresh_access_token");
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
                    match res.status() {
                        StatusCode::BAD_REQUEST => return Err(GcsError::NeedNewRefreshToken),
                        _ => return Err(GcsError::HttpResponseError(res.status())),
                    }
                }
            }
            None => return Err(GcsError::MissingRefreshToken),
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
        tracing::trace!("download {:?}, {:?}", bucket, object);
        // If the bucket and object are from a gs:// URL, the object may have a
        // undesirable leading slash. Trim it if present.
        let object = if object.starts_with('/') { &object[1..] } else { object };

        let res = self
            .attempt_download(https_client, bucket, object)
            .await
            .context("attempt_download")?;
        Ok(match res.status() {
            StatusCode::FORBIDDEN | StatusCode::UNAUTHORIZED => {
                match &self.refresh_token {
                    Some(_) => {
                        // Refresh the access token and make one extra try.
                        self.refresh_access_token(&https_client)
                            .await
                            .context("refresh_access_token")?;
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
        tracing::trace!("https_client.request {:?}", url);
        let req = Request::builder().method(Method::GET).uri(url.into_string());
        let req = self.authorize(req).await?;
        let req = req.body(Body::from(""))?;

        let res = https_client.request(req).await?;
        Ok(res)
    }

    /// Determine whether a gs url points to either a file or directory.
    ///
    /// A leading slash "/" on `prefix` will be ignored.
    ///
    /// Ok(false) will be returned instead of GcsError::NotFound.
    pub async fn exists(
        &self,
        https_client: &HttpsClient,
        bucket: &str,
        prefix: &str,
    ) -> Result<bool> {
        tracing::debug!("testing existence of gs://{}/{}", bucket, prefix);
        // Note: gs 'stat' will not match a directory.  So, gs 'list' is used to
        // determine existence. The number of items in a directory may be
        // enormous, so the results are limited to one item.
        match self.list(https_client, bucket, prefix, /*limit=*/ Some(1)).await {
            Ok(list) => {
                assert!(list.len() <= 1, "exists returned {} items.", list.len());
                Ok(!list.is_empty())
            }
            Err(e) => match e.downcast_ref::<GcsError>() {
                Some(GcsError::NotFound(_, _)) => Ok(false),
                Some(_) | None => Err(e),
            },
        }
    }

    /// List objects from GCS in `bucket` with matching `prefix`.
    ///
    /// A leading slash "/" on `prefix` will be ignored.
    pub async fn list(
        &self,
        https_client: &HttpsClient,
        bucket: &str,
        prefix: &str,
        limit: Option<u32>,
    ) -> Result<Vec<String>> {
        tracing::debug!("list objects at gs://{}/{}", bucket, prefix);
        Ok(match self.attempt_list(https_client, bucket, prefix, limit).await {
            Err(e) => {
                match e.downcast_ref::<GcsError>() {
                    Some(GcsError::NeedNewAccessToken) => {
                        match &self.refresh_token {
                            Some(_) => {
                                // Refresh the access token and make one extra try.
                                self.refresh_access_token(&https_client)
                                    .await
                                    .context("refreshing access token")?;
                                self.attempt_list(https_client, bucket, prefix, limit)
                                    .await
                                    .context("attempting list")?
                            }
                            None => {
                                // With no refresh token, there's no option to retry.
                                bail!(
                                    "Access to gs://{}/{} requires authorization. Error {:?}",
                                    bucket,
                                    prefix,
                                    e
                                );
                            }
                        }
                    }
                    Some(_) => return Err(e),
                    None => bail!("Unable to list GCS data. Error {:?}", e),
                }
            }
            Ok(value) => value,
        })
    }

    /// Make one attempt to list objects from GCS.
    ///
    /// If `limit` is given, at most N results will be returned. If `limit` is
    /// None then all matching values will be returned.
    async fn attempt_list(
        &self,
        https_client: &HttpsClient,
        bucket: &str,
        prefix: &str,
        limit: Option<u32>,
    ) -> Result<Vec<String>> {
        tracing::trace!("attempt_list of gs://{}/{}", bucket, prefix);
        // If the bucket and prefix are from a gs:// URL, the prefix may have a
        // undesirable leading slash. Trim it if present.
        let prefix = if prefix.starts_with('/') { &prefix[1..] } else { prefix };

        let mut base_url = self.api_base.to_owned();
        base_url.path_segments_mut().unwrap().extend(&["b", bucket, "o"]);
        base_url
            .query_pairs_mut()
            .append_pair("prefix", prefix)
            .append_pair("prettyPrint", "false")
            .append_pair("fields", "nextPageToken,items/name");
        if let Some(limit) = limit {
            base_url.query_pairs_mut().append_pair("maxResults", &limit.to_string());
        }
        let mut results = Vec::new();
        let mut page_token: Option<String> = None;
        loop {
            // Create a new URL for each "page" of results.
            let mut url = base_url.clone();
            if let Some(t) = page_token {
                url.query_pairs_mut().append_pair("pageToken", t.as_str());
            }
            let res = self.send_request(https_client, url).await.context("sending request")?;
            match res.status() {
                StatusCode::FORBIDDEN | StatusCode::UNAUTHORIZED => {
                    bail!(GcsError::NeedNewAccessToken);
                }
                StatusCode::OK => {
                    let bytes = hyper::body::to_bytes(res.into_body())
                        .await
                        .context("hyper::body::to_bytes")?;
                    let info: ListResponse =
                        serde_json::from_slice(&bytes).context("serde_json::from_slice")?;
                    results.extend(info.items.into_iter().map(|i| i.name));
                    if info.next_page_token.is_none() {
                        break;
                    }
                    if let Some(limit) = limit {
                        if results.len() >= limit as usize {
                            break;
                        }
                    }
                    page_token = info.next_page_token;
                }
                _ => {
                    bail!("Failed to list {:?} {:?}", base_url, res);
                }
            }
        }
        if results.is_empty() {
            bail!(GcsError::NotFound(bucket.to_string(), prefix.to_string()));
        }
        Ok(results)
    }
}

impl fmt::Debug for TokenStore {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("TokenStore")
            .field("api_base", &self.api_base)
            .field("storage_base", &self.storage_base)
            .field("refresh_token", &self.refresh_token.as_ref().map(|_| ".."))
            .field("access_token", &"..")
            .finish()
    }
}

/// Performs steps to get a refresh token from scratch.
///
/// This may involve user interaction such as opening a browser window..
pub async fn new_refresh_token() -> Result<String> {
    let auth_code = get_auth_code().context("getting auth code")?;
    let (refresh_token, _) = auth_code_to_refresh(&auth_code).await.context("get refresh token")?;
    Ok(refresh_token)
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
pub fn get_auth_code_with<W, R>(writer: &mut W, reader: &mut R) -> Result<String>
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
    let data = fs::read_to_string(boto_path.as_ref()).context("read_to_string boto_path")?;
    let refresh_token = match re.captures(&data) {
        Some(found) => Some(found.get(1).expect("found at least one").as_str().to_string()),
        None => None,
    };
    Ok(refresh_token)
}

/// Overwrite the 'gs_oauth2_refresh_token' in the file at `boto_path`.
///
/// TODO(fxbug.dev/82014): Using an ffx specific token will be preferred once
/// that feature is created. For the near term, an existing gsutil token is
/// workable.
///
/// Alert: The refresh token is considered a private secret for the user. Do
///        not print the token to a log or otherwise disclose it.
pub fn write_boto_refresh_token<P: AsRef<Path>>(boto_path: P, token: &str) -> Result<()> {
    use std::{fs::set_permissions, os::unix::fs::PermissionsExt};
    let boto_path = boto_path.as_ref();
    let data = if !boto_path.is_file() {
        fs::File::create(boto_path).context("Create .boto file")?;
        const USER_READ_WRITE: u32 = 0o600;
        let permissions = std::fs::Permissions::from_mode(USER_READ_WRITE);
        set_permissions(&boto_path, permissions).context("Boto set permissions")?;
        format!(
            "# This file was created by the Fuchsia GCS lib.\
            \n[GSUtil]\
            \ngs_oauth2_refresh_token = {}\
            \ndefault_project_id =\
            \n",
            token
        )
    } else {
        static GS_UPDATE_REFRESH_TOKEN_RE: OnceCell<Regex> = OnceCell::new();
        let re = GS_UPDATE_REFRESH_TOKEN_RE.get_or_init(|| {
            Regex::new(r#"(\n\s*gs_oauth2_refresh_token\s*=\s*)\S*"#).expect("regex")
        });
        let data = fs::read_to_string(boto_path).context("read boto refresh")?;
        re.replace(&data, format!("${{1}}{}", token).as_str()).to_string()
    };
    fs::write(boto_path, data).context("Writing .boto file")?;
    Ok(())
}

#[cfg(test)]
mod test {
    use {super::*, fuchsia_hyper::new_https_client, hyper::StatusCode, tempfile};

    #[test]
    fn test_get_auth_code_with() {
        let mut output: Vec<u8> = Vec::new();
        let mut input = "fake_auth_code".as_bytes();
        let auth_code = get_auth_code_with(&mut output, &mut input).expect("auth code");
        assert_eq!(auth_code, "fake_auth_code");
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gcs_read_write_refresh_token() {
        let boto_temp = tempfile::TempDir::new().expect("temp dir");
        let boto_path = boto_temp.path().join(".boto");
        assert!(!boto_path.is_file());
        // Test with no .boto file.
        write_boto_refresh_token(&boto_path, "first-token").expect("write token");
        assert!(boto_path.is_file());
        let refresh_token =
            read_boto_refresh_token(&boto_path).expect("boto").expect("first token");
        assert_eq!(refresh_token, "first-token");
        // Test updating existing .boto file.
        write_boto_refresh_token(&boto_path, "second-token").expect("write token");
        let refresh_token =
            read_boto_refresh_token(&boto_path).expect("boto").expect("second token");
        assert_eq!(refresh_token, "second-token");
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

    // This test is marked "ignore" because it actually downloads from GCS,
    // which isn't good for a CI/GI test. It's here because it's handy to have
    // as a local developer test. Run with `fx test gcs_lib_test -- --ignored`.
    // Note: gsutil config is required.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gcs_exits() {
        use home::home_dir;
        let boto_path = Path::new(&home_dir().expect("home dir")).join(".boto");
        let refresh_token =
            read_boto_refresh_token(&boto_path).expect("boto file").expect("refresh token");
        let token_store = TokenStore::new_with_auth(refresh_token, /*access_token=*/ None)
            .expect("new with auth");
        let https = new_https_client();
        token_store.refresh_access_token(&https).await.expect("refresh_access_token");
        let bucket = "fuchsia-sdk";
        let object = "development/LATEST_";
        assert!(token_store.exists(&https, bucket, object).await.expect("exists"));
        let object = "development";
        assert!(token_store.exists(&https, bucket, object).await.expect("exists"));
        let object = "development_not_found";
        assert!(!token_store.exists(&https, bucket, object).await.expect("exists"));
    }
}
