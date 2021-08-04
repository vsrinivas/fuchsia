// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Download blob data from Google Cloud Storage (GCS).

use {
    crate::token_store::TokenStore,
    anyhow::Result,
    fuchsia_hyper::{new_https_client, HttpsClient},
    http::request,
    hyper::{Body, Method, Request, Response},
    once_cell::sync::OnceCell,
    regex::Regex,
    std::{
        fs,
        path::Path,
        sync::{Arc, Mutex},
    },
    url::Url,
};

/// Base URL for JSON API access.
const API_BASE: &str = "https://www.googleapis.com/storage/v1";

/// Base URL for reading (blob) objects.
const STORAGE_BASE: &str = "https://storage.googleapis.com";

/// An https client capable of fetching objects from GCS.
#[allow(dead_code)]
pub struct Client {
    /// Base client used for HTTP/S IO.
    https: HttpsClient,

    /// Base URL for JSON API access.
    api_base: Url,

    /// Base URL for reading (blob) objects.
    storage_base: Url,

    /// User credentials for use with GCS.
    token_store: Arc<Mutex<TokenStore>>,
}

impl Client {
    /// An https client that used a `token_store` to authenticate with GCS.
    ///
    /// The `token_store` may be used by multiple clients, even in separate
    /// threads. It's preferable to share a single token_store to share the
    /// access token stored therein.
    ///
    /// The new client will default to using:
    /// - api_base: https://www.googleapis.com/storage/v1
    /// - storage_base: https://storage.googleapis.com
    ///
    /// This is sufficient for downloading publicly accessible data blobs from
    /// GCS.
    pub fn new(token_store: Arc<Mutex<TokenStore>>) -> Self {
        Self::from(
            Url::parse(API_BASE).expect("parse API_BASE"),
            Url::parse(STORAGE_BASE).expect("parse STORAGE_BASE"),
            token_store,
        )
    }

    /// Create a client with custom URLs for the API and Storage.
    fn from(api_base: Url, storage_base: Url, token_store: Arc<Mutex<TokenStore>>) -> Self {
        let https = new_https_client();
        Self { https, api_base, storage_base, token_store }
    }

    /// Apply Authorization header, if available.
    ///
    /// Hands off to TokenStore::authenticate().
    async fn authenticate(&self, builder: request::Builder) -> Result<request::Builder> {
        let mut token_store = self.token_store.lock().expect("token store lock");
        Ok(token_store.authenticate(builder).await?)
    }

    /// Reads content of a stored object (blob) from GCS.
    pub async fn download(&self, bucket: &str, object: &str) -> Result<Response<Body>> {
        let url = self.storage_base.join(&format!("{}/{}", bucket, object))?;

        let req = Request::builder().method(Method::GET).uri(url.into_string());
        let req = self.authenticate(req).await?;
        let req = req.body(Body::from(""))?;

        let res = self.https.request(req).await?;
        Ok(res)
    }
}

/// Fetch an existing refresh token from a .boto (gsutil) configuration file.
///
/// TODO(fxbug.dev/82014): Using an ffx specific token will be preferred once
/// that feature is created. For the near term, an existing gsutil token is
/// workable.
///
/// Alert: The refresh token is considered a private secret for the user. Do
///        not print the token to a log or otherwise disclose it.
pub fn read_boto_refresh_token(boto_path: &Path) -> Result<Option<String>> {
    // Read the file at `boto_path` to retrieve a value from a line resembling
    // "gs_oauth2_refresh_token = <string_of_chars>".
    static GS_REFRESH_TOKEN_RE: OnceCell<Regex> = OnceCell::new();
    let re = GS_REFRESH_TOKEN_RE
        .get_or_init(|| Regex::new(r#"\n\s*gs_oauth2_refresh_token\s*=\s*(\S+)"#).expect("regex"));
    let data = fs::read_to_string(boto_path)?;
    let refresh_token = match re.captures(&data) {
        Some(found) => Some(found.get(1).expect("found at least one").as_str().to_string()),
        None => None,
    };
    Ok(refresh_token)
}

#[cfg(test)]
mod test {
    use {super::*, hyper::StatusCode};

    #[should_panic(expected = "Connection refused")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fake_download() {
        let api_base = Url::parse("http://localhost:9000").expect("api_base");
        let storage_base = Url::parse("http://localhost:9001").expect("storage_base");
        let token_store = Arc::new(Mutex::new(TokenStore::new(/*refresh_token=*/ None)));
        let client = Client::from(api_base, storage_base, token_store);
        let bucket = "fake_bucket";
        let object = "fake/object/path.txt";
        client.download(bucket, object).await.expect("client download");
    }

    // This test is marked "ignore" because it actually downloads from GCS,
    // which isn't good for a CI/GI test. It's here because it's handy to have
    // as a local developer test. Run with `fx test gcs_lib_test -- --ignored`.
    // Note: gsutil config is required.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gcs_download_public() {
        let token_store = Arc::new(Mutex::new(TokenStore::new(/*refresh_token=*/ None)));
        let client = Client::new(token_store);
        let bucket = "fuchsia";
        let object = "development/5.20210610.3.1/sdk/linux-amd64/gn.tar.gz";
        let res = client.download(bucket, object).await.expect("client download");
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
        let refresh_token = read_boto_refresh_token(&boto_path).expect("boto token");
        let token_store = Arc::new(Mutex::new(TokenStore::new(refresh_token)));
        let client = Client::new(token_store);
        let bucket = "fuchsia-sdk";
        let object = "development/8910718018192331792/images/astro-release.tgz";
        let res = client.download(bucket, object).await.expect("client download");
        assert_eq!(res.status(), StatusCode::OK);
    }
}
