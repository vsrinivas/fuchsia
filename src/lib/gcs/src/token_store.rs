// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provide Google Cloud Storage (GCS) access.

use {
    crate::{auth::pkce::new_access_token, error::GcsError},
    anyhow::{bail, Context, Result},
    async_lock::Mutex,
    fuchsia_hyper::HttpsClient,
    http::{request, StatusCode},
    hyper::{Body, Method, Request, Response},
    once_cell::sync::OnceCell,
    regex::Regex,
    serde_json,
    std::{fmt, fs, path::Path, string::String},
    url::Url,
};

/// Base URL for JSON API access.
const API_BASE: &str = "https://www.googleapis.com/storage/v1";

/// Base URL for reading (blob) objects.
const STORAGE_BASE: &str = "https://storage.googleapis.com";

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
    async fn refresh_access_token(&self, _https_client: &HttpsClient) -> Result<(), GcsError> {
        tracing::debug!("refresh_access_token");
        match &self.refresh_token {
            Some(refresh_token) => {
                let new_token = new_access_token(refresh_token).await?;
                let mut access_token = self.access_token.lock().await;
                *access_token = new_token;
                Ok(())
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
        tracing::debug!("download {:?}, {:?}", bucket, object);
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
        tracing::debug!("https_client.request {:?}", url);
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
        tracing::debug!("attempt_list of gs://{}/{}", bucket, prefix);
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
                    tracing::debug!("attempt_list status {}", res.status());
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
