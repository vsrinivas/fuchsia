// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Download blob data from Google Cloud Storage (GCS).

use {
    crate::token_store::{read_boto_refresh_token, TokenStore},
    anyhow::Result,
    fuchsia_hyper::{new_https_client, HttpsClient},
    hyper::{Body, Response},
    std::path::Path,
    std::sync::Arc,
};

/// Create clients with credentials for use with GCS.
///
/// It's advisable to only have one GCS ClientFactory at a time. One way to
/// accomplish this is with a static once cell.
/// ```
/// use once_cell::sync::OnceCell;
/// static GCS_CLIENT_FACTORY: OnceCell<ClientFactory> = OnceCell::new();
/// let client_factory = GCS_CLIENT_FACTORY.get_or_init(|| ClientFactory::new());
/// ```
/// If more than one GCS ClientFactory is active at the same time, the creation
/// of access tokens may create unnecessary network traffic (spamming) or
/// contention.
///
/// The ClientFactory is thread/async safe to encourage creating a single,
/// shared instance.
pub struct ClientFactory {
    token_store: Arc<TokenStore>,
}

impl ClientFactory {
    /// Create a ClientFactory. Avoid creating more than one (see above).
    pub fn new() -> Self {
        /// TODO(fxbug.dev/82014): Don't use gsutil data.
        use home::home_dir;
        let boto_path = Path::new(&home_dir().expect("home dir")).join(".boto");
        let refresh = read_boto_refresh_token(&boto_path).expect("boto token");
        let token_store = Arc::new(TokenStore::new(refresh));
        Self { token_store }
    }

    /// Create a new https client with shared access to the GCS credentials.
    pub fn new_client(&self) -> Client {
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

    /// Reads content of a stored object (blob) from GCS.
    pub async fn download(&self, bucket: &str, object: &str) -> Result<Response<Body>> {
        self.token_store.download(&self.https, bucket, object).await
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_client_factory() {
        let client_factory = ClientFactory::new();
        let client = client_factory.new_client();
        let res = client
            .download("for_testing_does_not_exist", "face_test_object")
            .await
            .expect("download");
        assert_eq!(res.status(), 404);
    }
}
