// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools to support working with Fuchsia package repositories hosted on Google Cloud storage.
//!
//! See
//! - [Package](https://fuchsia.dev/fuchsia-src/concepts/packages/package?hl=en)
//! - [TUF](https://theupdateframework.io/)

use {
    crate::{
        async_spooled::AsyncSpooledTempFile,
        range::{ContentLength, Range},
        repository::{Error, RepoProvider, RepositorySpec},
        resource::Resource,
    },
    anyhow::{anyhow, Context as _},
    futures::{future::BoxFuture, AsyncRead, FutureExt as _, TryStreamExt as _},
    hyper::{header::CONTENT_LENGTH, Body, Response, StatusCode},
    std::{fmt::Debug, io, time::SystemTime},
    tuf::{
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        pouf::Pouf1,
        repository::RepositoryProvider as TufRepositoryProvider,
    },
    url::Url,
};

const X_GOOG_STORED_CONTENT_LENGTH: &str = "x-goog-stored-content-length";
const UNKNOWN_CONTENT_LEN_BUF_SIZE: usize = 8_196;

/// Helper trait that lets us mock gcs::client::Client for testing.
#[doc(hidden)]
#[async_trait::async_trait]
pub trait GcsClient {
    async fn stream(&self, bucket: &str, object: &str) -> anyhow::Result<Response<Body>>;
}

#[async_trait::async_trait]
impl GcsClient for gcs::client::Client {
    async fn stream(&self, bucket: &str, object: &str) -> anyhow::Result<Response<Body>> {
        gcs::client::Client::stream(self, bucket, object).await
    }
}

/// [GcsRepository] serves a package repository from a Google Cloud Storage bucket.
#[derive(Debug)]
pub struct GcsRepository<T = gcs::client::Client> {
    client: T,

    /// URL to the GCS bucket and object prefix that contains the metadata repository. This must
    /// have the gs:// URL scheme. The constructor will make sure this has a trailing slash so it
    /// is treated as a directory.
    metadata_repo_url: Url,

    /// URL to the GCS bucket and object prefix that contains the blobs repository. This must have
    /// the gs:// URL scheme. The constructor will make sure this has a trailing slash so it is
    /// treated as a directory.
    blob_repo_url: Url,
}

impl<T> GcsRepository<T>
where
    T: GcsClient + Debug + Send + Sync,
{
    pub fn new(
        client: T,
        mut metadata_repo_url: Url,
        mut blob_repo_url: Url,
    ) -> Result<Self, anyhow::Error> {
        if metadata_repo_url.scheme() != "gs" {
            return Err(anyhow!("unsupported scheme {}", metadata_repo_url));
        }

        if blob_repo_url.scheme() != "gs" {
            return Err(anyhow!("unsupported scheme {}", blob_repo_url));
        }

        // `Url::join()` treats urls with a trailing slash as a directory, and without as a file. In
        // the latter case, it will strip off the last segment before joining paths. Since the
        // metadata and blob url are directories, make sure they have a trailing slash.
        if !metadata_repo_url.path().ends_with('/') {
            metadata_repo_url.set_path(&format!("{}/", metadata_repo_url.path()));
        }

        if !blob_repo_url.path().ends_with('/') {
            blob_repo_url.set_path(&format!("{}/", blob_repo_url.path()));
        }

        Ok(Self { client, metadata_repo_url, blob_repo_url })
    }

    fn get<'a>(
        &'a self,
        root: &Url,
        path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        let url = root.join(path);

        async move {
            let url = url.map_err(|err| anyhow!(err))?;
            let bucket =
                url.host_str().ok_or_else(|| anyhow!("url must include a bucket: {}", url))?;
            let object = url.path();

            // FIXME(http://fxbug.dev/98991): The gcs library does not yet support range requests, so
            // always fetch the full range.
            let resp = self.client.stream(bucket, object).await?;

            match resp.status() {
                StatusCode::OK => {
                    // `Resource` requires us to know the exact length of the artifact. That's the case
                    // if we get a `Content-Length` header.
                    if let Some(content_len) = resp.headers().get(CONTENT_LENGTH) {
                        let content_len =
                            ContentLength::from_http_content_length_header(content_len)
                                .with_context(|| {
                                    format!("parsing Content-Length header: {}", url)
                                })?;

                        // Make sure we didn't try to fetch data that's out of bounds.
                        if !content_len.contains_range(range) {
                            return Err(Error::RangeNotSatisfiable);
                        }

                        let body = resp.into_body();

                        return Ok(Resource {
                            content_range: content_len.into(),
                            stream: Box::pin(
                                body.map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e)),
                            ),
                        });
                    }

                    // If we didn't get a `Content-Length`, then maybe the artifact was stored
                    // compressed, and sent to us uncompressed. When this happens, we instead get a
                    // `x-goog-stored-content-length` header.
                    //
                    // See https://cloud.google.com/storage/docs/transcoding for more details.
                    if resp.headers().contains_key(X_GOOG_STORED_CONTENT_LENGTH) {
                        return self.get_with_stored_content_len(resp.into_body(), range).await;
                    }

                    Err(Error::Other(anyhow!(
                    "response missing Content-Length or x-goog-stored-content-length headers: {}",
                    url
                )))
                }
                StatusCode::NOT_FOUND => Err(Error::NotFound),
                StatusCode::RANGE_NOT_SATISFIABLE => Err(Error::RangeNotSatisfiable),
                status => {
                    if status.is_success() {
                        Err(Error::Other(anyhow!("unexpected status code {}: {}", url, status)))
                    } else {
                        // GCS may return a more detailed error description in the body.
                        if let Ok(body) = hyper::body::to_bytes(resp.into_body()).await {
                            let body_str = String::from_utf8_lossy(&body);
                            Err(Error::Other(anyhow!(
                                "error downloading resource {}: {}\n{}",
                                url,
                                status,
                                body_str
                            )))
                        } else {
                            Err(Error::Other(anyhow!(
                                "error downloading resource {}: {}",
                                url,
                                status
                            )))
                        }
                    }
                }
            }
        }
        .boxed()
    }

    /// Handles a response that contains a `x-goog-stored-content-length` header.
    ///
    /// Since we don't know the size of the resource, we'll buffer up to a certain size, then
    /// transition over into a temporary file. Once the file has been fully downloaded, we will
    /// compute the actual length, then return it.
    async fn get_with_stored_content_len(
        &self,
        mut body: Body,
        range: Range,
    ) -> Result<Resource, Error> {
        let mut tmp = AsyncSpooledTempFile::new(UNKNOWN_CONTENT_LEN_BUF_SIZE);

        while let Some(chunk) = body.try_next().await? {
            tmp.write_all(&chunk).await.map_err(Error::Io)?;
        }

        let (len, stream) = tmp.into_stream().await.map_err(Error::Io)?;

        // Make sure we didn't try to fetch data that's out of bounds.
        let content_len = ContentLength::new(len);
        if !content_len.contains_range(range) {
            return Err(Error::RangeNotSatisfiable);
        }

        Ok(Resource { content_range: content_len.into(), stream })
    }
}

#[async_trait::async_trait]
impl<T> RepoProvider for GcsRepository<T>
where
    T: GcsClient + Debug + Send + Sync + 'static,
{
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::Gcs {
            metadata_repo_url: self.metadata_repo_url.as_str().to_owned(),
            blob_repo_url: self.blob_repo_url.as_str().to_owned(),
        }
    }

    fn fetch_metadata_range<'a>(
        &'a self,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        self.get(&self.metadata_repo_url, resource_path, range)
    }

    fn fetch_blob_range<'a>(
        &'a self,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        self.get(&self.blob_repo_url, resource_path, range)
    }

    fn blob_len<'a>(&'a self, path: &str) -> BoxFuture<'a, Result<u64, anyhow::Error>> {
        // FIXME(http://fxbug.dev/98993): The gcs library does not expose a more efficient API for
        // determining the blob size.
        let fut = self.fetch_blob_range(path, Range::Full);
        async move { Ok(fut.await?.total_len()) }.boxed()
    }

    fn blob_modification_time<'a>(
        &'a self,
        _path: &str,
    ) -> BoxFuture<'a, Result<Option<SystemTime>, anyhow::Error>> {
        // FIXME(http://fxbug.dev/98993): The gcs library does not expose an API to determine the
        // blob modification time.
        async move { Ok(None) }.boxed()
    }
}

impl<T> TufRepositoryProvider<Pouf1> for GcsRepository<T>
where
    T: GcsClient + Debug + Send + Sync + 'static,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>, tuf::Error>> {
        let meta_path = meta_path.clone();
        let path = meta_path.components::<Pouf1>(version).join("/");

        async move {
            let resp =
                self.fetch_metadata_range(&path, Range::Full).await.map_err(|err| match err {
                    Error::Tuf(err) => err,
                    Error::NotFound => tuf::Error::MetadataNotFound { path: meta_path, version },
                    err => tuf::Error::Opaque(err.to_string()),
                })?;

            let reader = resp
                .stream
                .map_err(|err| io::Error::new(io::ErrorKind::Other, err))
                .into_async_read();

            let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(reader);
            Ok(reader)
        }
        .boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>, tuf::Error>> {
        let target_path = target_path.clone();
        let path = target_path.components().join("/");

        async move {
            let resp =
                self.fetch_metadata_range(&path, Range::Full).await.map_err(|err| match err {
                    Error::Tuf(err) => err,
                    Error::NotFound => tuf::Error::TargetNotFound(target_path),
                    err => tuf::Error::Opaque(err.to_string()),
                })?;

            let reader = resp
                .stream
                .map_err(|err| io::Error::new(io::ErrorKind::Other, err))
                .into_async_read();

            let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(reader);
            Ok(reader)
        }
        .boxed()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            repository::{
                repo_tests::{self, TestEnv as _},
                FileSystemRepository,
            },
            test_utils::make_repo_dir,
            util::CHUNK_SIZE,
        },
        assert_matches::assert_matches,
        camino::{Utf8Path, Utf8PathBuf},
        std::{fs::File, io::Write as _},
        url::Url,
    };

    #[derive(Debug)]
    struct MockGcsClient {
        repo: FileSystemRepository,
        content_length_header: &'static str,
    }

    #[async_trait::async_trait]
    impl GcsClient for MockGcsClient {
        async fn stream(&self, bucket: &str, mut object: &str) -> anyhow::Result<Response<Body>> {
            // The gcs library allows for leading slashes, but FileSystemRepository does not, so
            // remove it.
            if let Some(o) = object.strip_prefix('/') {
                object = o;
            }

            let res = match bucket {
                "my-tuf-repo" => self.repo.fetch_metadata_range(object, Range::Full).await,
                "my-blob-repo" => self.repo.fetch_blob_range(object, Range::Full).await,
                _ => panic!("unknown bucket {:?}", bucket),
            };

            match res {
                Ok(resource) => {
                    // We don't support range requests.
                    assert_eq!(resource.content_range.to_http_content_range_header(), None);

                    Ok(Response::builder()
                        .status(StatusCode::OK)
                        .header(self.content_length_header, resource.content_len())
                        .body(Body::wrap_stream(resource.stream))
                        .unwrap())
                }
                Err(Error::NotFound) => Ok(Response::builder()
                    .status(StatusCode::NOT_FOUND)
                    .body(Body::empty())
                    .unwrap()),
                res => panic!("unexpected result {:#?}", res),
            }
        }
    }

    struct TestEnv {
        _tmp: tempfile::TempDir,
        metadata_repo_path: Utf8PathBuf,
        blob_repo_path: Utf8PathBuf,
        repo: GcsRepository<MockGcsClient>,
    }

    impl TestEnv {
        async fn new() -> Self {
            Self::with_content_length_header("Content-Length").await
        }

        async fn with_content_length_header(content_length_header: &'static str) -> Self {
            let tmp = tempfile::tempdir().unwrap();
            let dir = Utf8Path::from_path(tmp.path()).unwrap();

            // Create a repository and serve it with the server.
            let metadata_repo_path = dir.join("tuf");
            let blob_repo_path = dir.join("blobs");
            std::fs::create_dir(&metadata_repo_path).unwrap();
            std::fs::create_dir(&blob_repo_path).unwrap();

            make_repo_dir(blob_repo_path.as_std_path(), blob_repo_path.as_std_path()).await;
            let remote_repo =
                FileSystemRepository::new(metadata_repo_path.clone(), blob_repo_path.clone());

            let tuf_url = "gs://my-tuf-repo/";
            let blob_url = "gs://my-blob-repo/";

            let repo = GcsRepository::new(
                MockGcsClient { repo: remote_repo, content_length_header },
                Url::parse(tuf_url).unwrap(),
                Url::parse(blob_url).unwrap(),
            )
            .unwrap();

            TestEnv { _tmp: tmp, metadata_repo_path, blob_repo_path, repo }
        }
    }

    #[async_trait::async_trait]
    impl repo_tests::TestEnv for TestEnv {
        fn supports_range(&self) -> bool {
            false
        }

        fn write_metadata(&self, path: &str, bytes: &[u8]) {
            let file_path = self.metadata_repo_path.join(path);
            let mut f = File::create(file_path).unwrap();
            f.write_all(bytes).unwrap();
        }

        fn write_blob(&self, path: &str, bytes: &[u8]) {
            let file_path = self.blob_repo_path.join(path);
            let mut f = File::create(file_path).unwrap();
            f.write_all(bytes).unwrap();
        }

        fn repo(&self) -> &dyn RepoProvider {
            &self.repo
        }
    }

    mod content_length {
        use super::*;

        repo_tests::repo_test_suite! {
            env = TestEnv::new().await;
            chunk_size = CHUNK_SIZE;
        }
    }

    mod goog_stored_content_length {
        use super::*;

        repo_tests::repo_test_suite! {
            env = TestEnv::with_content_length_header(X_GOOG_STORED_CONTENT_LENGTH).await;
            chunk_size = UNKNOWN_CONTENT_LEN_BUF_SIZE;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_blob_modification_time() {
        let env = TestEnv::new().await;

        std::fs::write(env.blob_repo_path.join("empty-blob"), b"").unwrap();

        // We don't support modification time.
        assert_matches!(env.repo.blob_modification_time("empty-blob").await, Ok(None));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch() {
        let env = TestEnv::new().await;

        // We don't support watch.
        assert_matches!(env.repo.supports_watch(), false);
        assert!(env.repo.watch().is_err());
    }
}
