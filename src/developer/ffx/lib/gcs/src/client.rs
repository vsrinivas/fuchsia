// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Download blob data from Google Cloud Storage (GCS).
//! Note: This currently only support public/anonymous downloads until OAuth2
//!       features are added.

use {
    anyhow::Result,
    fuchsia_hyper::{new_https_client, HttpsClient},
    hyper::{Body, Method, Request, Response},
    url::Url,
};

/// Base URL for JSON API access.
const API_BASE: &str = "https://www.googleapis.com/storage/v1";

/// Base URL for reading (blob) objects.
const STORAGE_BASE: &str = "https://storage.googleapis.com";

/// An https client capable of fetching objects from GCS.
#[allow(dead_code)]
pub struct Client {
    https: HttpsClient,
    api_base: Url,
    storage_base: Url,
}

impl Client {
    /// The new client will default to using:
    /// - api_base: https://www.googleapis.com/storage/v1
    /// - storage_base: https://storage.googleapis.com
    /// - anonymous authentication (i.e. no authentication)
    ///
    /// This is sufficient for downloading publicly accessible data blobs from
    /// GCS.
    pub fn new() -> Self {
        let https = new_https_client();
        let api_base = Url::parse(API_BASE).expect("parse API_BASE");
        let storage_base = Url::parse(STORAGE_BASE).expect("parse STORAGE_BASE");
        Self { https, api_base, storage_base }
    }

    /// Create a client with custom URLs for the API and Storage.
    #[cfg(test)]
    fn from(api_base: Url, storage_base: Url) -> Self {
        let https = new_https_client();
        Self { https, api_base, storage_base }
    }

    /// Reads content of a stored object (blob) from GCS.
    pub async fn download(&self, bucket: &str, object: &str) -> Result<Response<Body>> {
        let url = self.storage_base.join(&format!("{}/{}", bucket, object))?;

        let req = Request::builder().method(Method::GET).uri(url.into_string());
        let req = req.body(Body::from(""))?;

        let res = self.https.request(req).await?;
        Ok(res)
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        hyper::{body::HttpBody, StatusCode},
        std::io::{self, Write},
    };

    #[should_panic(expected = "Connection refused")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fake_download() {
        let api_base = Url::parse("http://localhost:9000").expect("api_base");
        let storage_base = Url::parse("http://localhost:9001").expect("storage_base");
        let client = Client::from(api_base, storage_base);
        let bucket = "fake_bucket";
        let object = "fake/object/path.txt";
        client.download(bucket, object).await.expect("client download");
    }

    // This test is marked "ignore" because it actually downloads from GCS,
    // which isn't good for a CI/GI test. It's here because it's handy to have
    // as a local developer test. Run with `fx test gcs_lib_test -- --ignored`.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gcs_download() {
        let client = Client::new();
        let bucket = "fuchsia";
        let object = "development/5.20210610.3.1/sdk/linux-amd64/gn.tar.gz";
        let mut res = client.download(bucket, object).await.expect("client download");
        assert_eq!(res.status(), StatusCode::OK);
        if res.status() == StatusCode::OK {
            let stdout = io::stdout();
            let mut handle = stdout.lock();
            while let Some(next) = res.data().await {
                let chunk = next.expect("next chunk");
                handle.write_all(&chunk).expect("write chunk");
            }
        }
    }
}
