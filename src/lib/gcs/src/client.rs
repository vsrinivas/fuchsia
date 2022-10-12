// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Download blob data from Google Cloud Storage (GCS).

use {
    crate::token_store::TokenStore,
    anyhow::{bail, Context, Result},
    fuchsia_hyper::{new_https_client, HttpsClient},
    hyper::{body::HttpBody as _, header::CONTENT_LENGTH, Body, Response, StatusCode},
    std::{
        fs::{create_dir_all, File},
        io::Write,
        path::Path,
        sync::Arc,
    },
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
///     ClientFactory::new_with_auth(auth).expect(...)
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

/// A snapshot of the progress.
#[derive(Clone, Debug, PartialEq)]
pub struct ProgressState<'a> {
    /// Current URL.
    pub url: &'a str,

    /// The current step of the progress.
    pub at: u64,

    /// The total steps.
    pub of: u64,
}
/// This types promote self-documenting code.
pub type OverallProgress<'a> = ProgressState<'a>;
pub type DirectoryProgress<'a> = ProgressState<'a>;
pub type FileProgress<'a> = ProgressState<'a>;

/// Allow the user an opportunity to cancel an operation.
#[derive(Debug, PartialEq)]
pub enum ProgressResponse {
    /// Keep going.
    Continue,

    /// The user (or high level code) asks that the operation halt. This isn't
    /// an error in itself. Simply stop (e.g. break from iteration) and return.
    Cancel,
}

pub type ProgressResult<E = anyhow::Error> = std::result::Result<ProgressResponse, E>;

/// Throttle an action to N times per second.
pub struct Throttle {
    prior_update: std::time::Instant,
    interval: std::time::Duration,
}

impl Throttle {
    /// `is_ready()` will return true after each `interval` amount of time.
    pub fn from_duration(interval: std::time::Duration) -> Self {
        let prior_update = std::time::Instant::now();
        let prior_update = prior_update.checked_sub(interval).expect("reasonable interval");
        // A std::time::Instant cannot be created at zero or epoch start, so
        // the interval is deducted twice to create an instant that will trigger
        // on the first call to is_ready().
        // See Rust issue std::time::Instant #40910.
        let prior_update = prior_update.checked_sub(interval).expect("reasonable interval");
        Throttle { prior_update, interval }
    }

    /// Determine whether enough time has elapsed.
    ///
    /// Returns true at most N times per second, where N is the value passed
    /// into `new()`.
    pub fn is_ready(&mut self) -> bool {
        let now = std::time::Instant::now();
        if now.duration_since(self.prior_update) >= self.interval {
            self.prior_update = now;
            return true;
        }
        return false;
    }
}

/// An https client capable of fetching objects from GCS.
#[derive(Clone, Debug)]
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

    /// Similar to path::exists(), return true if the blob is available.
    pub async fn exists(&self, bucket: &str, prefix: &str) -> Result<bool> {
        self.token_store.exists(&self.https, bucket, prefix).await
    }

    /// Save content of matching objects (blob) from GCS to local location
    /// `output_dir`.
    pub async fn fetch_all<P, F>(
        &self,
        bucket: &str,
        prefix: &str,
        output_dir: P,
        progress: &F,
    ) -> Result<()>
    where
        P: AsRef<Path>,
        F: Fn(ProgressState<'_>, ProgressState<'_>) -> ProgressResult,
    {
        let objects = self
            .token_store
            .list(&self.https, bucket, prefix, /*limit=*/ None)
            .await
            .context("listing with token store")?;
        let output_dir = output_dir.as_ref();
        let mut count = 0;
        let total = objects.len() as u64;
        for object in objects {
            if let Some(relative_path) = object.strip_prefix(prefix) {
                let start = std::time::Instant::now();
                // Strip leading slash, if present.
                let relative_path = if relative_path.starts_with("/") {
                    &relative_path[1..]
                } else {
                    relative_path
                };
                let output_path = if relative_path.is_empty() {
                    // The `relative_path` is empty with then specified prefix
                    // is a file.
                    output_dir.join(Path::new(prefix).file_name().expect("Prefix file name."))
                } else {
                    output_dir.join(relative_path)
                };

                if let Some(parent) = output_path.parent() {
                    create_dir_all(&parent)
                        .with_context(|| format!("creating dir all for {:?}", parent))?;
                }
                let mut file = File::create(&output_path).context("create file")?;
                let url = format!("gs://{}/", bucket);
                count += 1;
                let dir_progress = ProgressState { url: &url, at: count, of: total };
                self.write(bucket, &object, &mut file, &|file_progress| {
                    assert!(
                        file_progress.at <= file_progress.of,
                        "At {} of {}",
                        file_progress.at,
                        file_progress.of
                    );
                    progress(dir_progress.clone(), file_progress)
                })
                .await
                .context("write object")?;
                use std::io::{Seek, SeekFrom};
                let file_size = file.seek(SeekFrom::End(0)).context("getting file size")?;
                tracing::debug!(
                    "Wrote gs://{}/{} to {:?}, {} bytes in {} seconds.",
                    bucket,
                    object,
                    output_path,
                    file_size,
                    start.elapsed().as_secs_f32()
                );
            }
        }
        Ok(())
    }

    /// Save content of a stored object (blob) from GCS at location `output`.
    ///
    /// Wraps call to `self.write` which wraps `self.stream()`.
    pub async fn fetch_with_progress<P, F>(
        &self,
        bucket: &str,
        object: &str,
        output: P,
        progress: &F,
    ) -> ProgressResult
    where
        P: AsRef<Path>,
        F: Fn(ProgressState<'_>) -> ProgressResult,
    {
        let mut file = File::create(output.as_ref())?;
        self.write(bucket, object, &mut file, progress).await
    }

    /// As `fetch_with_progress()` without a progress callback.
    pub async fn fetch_without_progress<P>(
        &self,
        bucket: &str,
        object: &str,
        output: P,
    ) -> Result<()>
    where
        P: AsRef<Path>,
    {
        let mut file = File::create(output.as_ref())?;
        self.write(bucket, object, &mut file, &mut |_| Ok(ProgressResponse::Continue)).await?;
        Ok(())
    }

    /// Reads content of a stored object (blob) from GCS.
    pub async fn stream(&self, bucket: &str, object: &str) -> Result<Response<Body>> {
        self.token_store.download(&self.https, bucket, object).await
    }

    /// Write content of a stored object (blob) from GCS to writer.
    ///
    /// Wraps call to `self.stream`.
    pub async fn write<W, F>(
        &self,
        bucket: &str,
        object: &str,
        writer: &mut W,
        progress: &F,
    ) -> ProgressResult
    where
        W: Write + Sync,
        F: Fn(ProgressState<'_>) -> ProgressResult,
    {
        let mut res = self.stream(bucket, object).await?;
        if res.status() == StatusCode::OK {
            let mut at: u64 = 0;
            let length = if res.headers().contains_key(CONTENT_LENGTH) {
                res.headers()
                    .get(CONTENT_LENGTH)
                    .context("getting content length")?
                    .to_str()?
                    .parse::<u64>()
                    .context("parsing content length")?
            } else if res.headers().contains_key("x-goog-stored-content-length") {
                // The size of gzipped files is a guess.
                res.headers()["x-goog-stored-content-length"]
                    .to_str()
                    .context("getting content length")?
                    .parse::<u64>()
                    .context("parsing content length")?
                    * 3
            } else {
                println!("missing content-length in {}: res.headers() {:?}", object, res.headers());
                bail!("missing content-length in header");
            };
            let mut of = length;
            // Throttle the progress UI updates to avoid burning CPU on changes
            // the user will have trouble seeing anyway. Without throttling,
            // around 20% of the execution time can be spent updating the
            // progress UI. The throttle makes the overhead negligible.
            let mut throttle = Throttle::from_duration(std::time::Duration::from_millis(500));
            while let Some(next) = res.data().await {
                let chunk = next.context("next chunk")?;
                writer.write_all(&chunk).context("write chunk")?;
                at += chunk.len() as u64;
                if at > of {
                    of = at;
                }
                if throttle.is_ready() {
                    match progress(ProgressState { url: object, at, of })
                        .context("rendering progress")?
                    {
                        ProgressResponse::Cancel => break,
                        _ => (),
                    }
                }
            }
            return Ok(ProgressResponse::Continue);
        }
        bail!("Failed to fetch file, result status: {:?}", res.status());
    }

    /// List objects in `bucket` with matching `prefix`.
    pub async fn list(&self, bucket: &str, prefix: &str) -> Result<Vec<String>> {
        self.token_store.list(&self.https, bucket, prefix, /*limit=*/ None).await
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
        let auth =
            TokenStore::new_with_auth(refresh, /*access_token=*/ None).expect("new with auth");
        let client_factory = ClientFactory::new(auth);
        let client = client_factory.create_client();

        // Try downloading something that doesn't exist.
        let res =
            client.stream("for_testing_does_not_exist", "face_test_object").await.expect("stream");
        assert_eq!(res.status(), 404);

        // Fetch something that does exist.
        let out_dir = tempfile::tempdir().unwrap();
        let out_file = out_dir.path().join("downloaded");
        client
            .fetch_without_progress("fuchsia-sdk", "development/LATEST_LINUX", &out_file)
            .await
            .expect("fetch");
        assert!(out_file.exists());
        let fetched = read_to_string(out_file).expect("read out_file");
        assert!(!fetched.is_empty());

        // Write the same data.
        let mut written = Vec::new();
        client
            .write("fuchsia-sdk", "development/LATEST_LINUX", &mut written, &mut |_| {
                Ok(ProgressResponse::Continue)
            })
            .await
            .expect("write");
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
