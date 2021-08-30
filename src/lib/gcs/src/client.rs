// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Download blob data from Google Cloud Storage (GCS).

use {
    crate::token_store::TokenStore,
    anyhow::{bail, Result},
    fuchsia_hyper::{new_https_client, HttpsClient},
    hyper::{body::HttpBody as _, Body, Response, StatusCode},
    std::{fs::File, io::Write, path::Path, sync::Arc},
};

/// Create clients with credentials for use with GCS.
///
/// Avoid more than one GCS ClientFactory with the *same auth* at a time. One way
/// to accomplish this is with a static once cell.
/// ```
/// use once_cell::sync::OnceCell;
/// static GCS_CLIENT_FACTORY: OnceCell<ClientFactory> = OnceCell::new();
/// let client_factory = GCS_CLIENT_FACTORY.get_or_init(|| {
///     let auth = [...];
///     ClientFactory::new_with_auth(auth)
/// });
/// ```
/// If more than one GCS ClientFactory with auth is active at the same time, the
/// creation of access tokens may create unnecessary network traffic (spamming)
/// or contention.
///
/// Note: A ClientFactory using `TokenStore::new_without_auth()` doesn't have
/// issues with more than one instance since there are no tokens to update.
///
/// The ClientFactory is thread/async safe to encourage creating a single,
/// shared instance.
pub struct ClientFactory {
    token_store: Arc<TokenStore>,
}

impl ClientFactory {
    /// Create a ClientFactory. Avoid creating more than one (see above).
    pub fn new(token_store: TokenStore) -> Self {
        let token_store = Arc::new(token_store);
        Self { token_store }
    }

    /// Create a new https client with shared access to the GCS credentials.
    ///
    /// Multiple clients may be created to perform downloads in parallel.
    pub fn create_client(&self) -> Client {
        Client::from_token_store(self.token_store.clone())
    }
}

/// An https client capable of fetching objects from GCS.
#[allow(dead_code)]
pub struct Client {
    /// Base client used for HTTP/S IO.
    https: HttpsClient,
    token_store: Arc<TokenStore>,
}

impl Client {
    /// An https client that used a `token_store` to authenticate with GCS.
    ///
    /// The `token_store` may be used by multiple clients, even in separate
    /// threads. It's preferable to share a single token_store to share the
    /// access token stored therein.
    ///
    /// This is sufficient for downloading publicly accessible data blobs from
    /// GCS.
    ///
    /// Intentionally not public. Use ClientFactory::new_client() instead.
    fn from_token_store(token_store: Arc<TokenStore>) -> Self {
        Self { https: new_https_client(), token_store }
    }

    /// Save content of a stored object (blob) from GCS at location `output`.
    ///
    /// Wraps call to `self.write` which wraps `self.stream()`.
    pub async fn fetch<P: AsRef<Path>>(&self, bucket: &str, object: &str, output: P) -> Result<()> {
        let mut file = File::create(output.as_ref())?;
        self.write(bucket, object, &mut file).await
    }

    /// Reads content of a stored object (blob) from GCS.
    pub async fn stream(&self, bucket: &str, object: &str) -> Result<Response<Body>> {
        self.token_store.download(&self.https, bucket, object).await
    }

    /// Write content of a stored object (blob) from GCS to writer.
    ///
    /// Wraps call to `self.stream`.
    pub async fn write<W: Write>(&self, bucket: &str, object: &str, writer: &mut W) -> Result<()> {
        let mut res = self.stream(bucket, object).await?;
        if res.status() == StatusCode::OK {
            while let Some(next) = res.data().await {
                let chunk = next?;
                writer.write_all(&chunk)?;
            }
            return Ok(());
        }
        bail!("Failed to fetch file, result status: {:?}", res.status());
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::token_store::read_boto_refresh_token,
        std::{fs::read_to_string, path::Path},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_client_factory_no_auth() {
        let client_factory = ClientFactory::new(TokenStore::new_without_auth());
        let client = client_factory.create_client();
        let res =
            client.stream("for_testing_does_not_exist", "face_test_object").await.expect("stream");
        assert_eq!(res.status(), 404);
    }

    /// This test relies on a local file which is not present on test bots, so
    /// it is marked "ignore".
    /// This can be run with `fx test gcs_lib_test -- --ignored`.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_client_factory_with_auth() {
        // Set up authorized client.
        use home::home_dir;
        let boto_path = Path::new(&home_dir().expect("home dir")).join(".boto");
        let refresh =
            read_boto_refresh_token(&boto_path).expect("boto file").expect("refresh token");
        let auth = TokenStore::new_with_auth(refresh, /*access_token=*/ None);
        let client_factory = ClientFactory::new(auth);
        let client = client_factory.create_client();

        // Try downloading something that doesn't exist.
        let res =
            client.stream("for_testing_does_not_exist", "face_test_object").await.expect("stream");
        assert_eq!(res.status(), 404);

        // Fetch something that does exist.
        let out_dir = tempfile::tempdir().unwrap();
        let out_file = out_dir.path().join("downloaded");
        client.fetch("fuchsia-sdk", "development/LATEST_LINUX", &out_file).await.expect("fetch");
        assert!(out_file.exists());
        let fetched = read_to_string(out_file).expect("read out_file");
        assert!(!fetched.is_empty());

        // Write the same data.
        let mut written = Vec::new();
        client.write("fuchsia-sdk", "development/LATEST_LINUX", &mut written).await.expect("write");
        // The data is expected to be small (less than a KiB). For a non-test
        // keeping the whole file in memory may be impractical.
        let written = String::from_utf8(written).expect("streamed string");
        assert!(!written.is_empty());

        // Compare the fetched and written data.
        assert_eq!(fetched, written);

        // Stream the same data.
        let res = client.stream("fuchsia-sdk", "development/LATEST_LINUX").await.expect("stream");
        assert_eq!(res.status(), 200);
        // The data is expected to be small (less than a KiB). For a non-test
        // keeping the whole file in memory may be impractical.
        let streamed_bytes = hyper::body::to_bytes(res.into_body()).await.expect("streamed bytes");
        let streamed = String::from_utf8(streamed_bytes.to_vec()).expect("streamed string");

        // Compare the fetched and streamed data.
        assert_eq!(fetched, streamed);
    }
}
