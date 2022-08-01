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
        range::{ContentRange, Range},
        repository::{Error, RepositoryBackend, RepositorySpec},
        resource::Resource,
    },
    anyhow::{anyhow, Context as _},
    futures::{future::BoxFuture, AsyncRead, FutureExt as _, TryStreamExt as _},
    hyper::{header::CONTENT_LENGTH, Body, Response, StatusCode},
    std::{fmt::Debug, io, time::SystemTime},
    tuf::{
        interchange::Json,
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        repository::RepositoryProvider,
    },
    url::Url,
};

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
#[derive(Clone, Debug)]
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
    T: GcsClient + Debug,
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

    async fn get(&self, root: &Url, path: &str, range: Range) -> Result<Resource, Error> {
        let url = root.join(path).map_err(|err| anyhow!(err))?;
        let bucket = url.host_str().ok_or_else(|| anyhow!("url must include a bucket: {}", url))?;
        let object = url.path();

        // FIXME(http://fxbug.dev/98991): The gcs library does not yet support range requests, so
        // always fetch the full range.
        let resp = self.client.stream(bucket, object).await?;

        let content_range = match resp.status() {
            StatusCode::OK => {
                // The package resolver currently requires a 'Content-Length' header, so error out
                // if one wasn't provided.
                let content_length = resp.headers().get(CONTENT_LENGTH).ok_or_else(|| {
                    Error::Other(anyhow!("response missing Content-Length header: {}", url))
                })?;

                ContentRange::from_http_content_length_header(content_length)
                    .with_context(|| format!("parsing Content-Length header: {}", url))?
            }
            StatusCode::NOT_FOUND => {
                return Err(Error::NotFound);
            }
            StatusCode::RANGE_NOT_SATISFIABLE => {
                return Err(Error::RangeNotSatisfiable);
            }
            status => {
                if status.is_success() {
                    return Err(Error::Other(anyhow!(
                        "unexpected status code {}: {}",
                        url,
                        status
                    )));
                } else {
                    // GCS may return a more detailed error description in the body.
                    if let Ok(body) = hyper::body::to_bytes(resp.into_body()).await {
                        let body_str = String::from_utf8_lossy(&body);
                        return Err(Error::Other(anyhow!(
                            "error downloading resource {}: {}\n{}",
                            url,
                            status,
                            body_str
                        )));
                    } else {
                        return Err(Error::Other(anyhow!(
                            "error downloading resource {}: {}",
                            url,
                            status
                        )));
                    }
                }
            }
        };

        // Since we fetched the full length, validate that the range request is inbounds, or error
        // out.
        match range {
            Range::Full => {}
            Range::From { first_byte_pos } => {
                if first_byte_pos >= content_range.total_len() {
                    return Err(Error::RangeNotSatisfiable);
                }
            }
            Range::Inclusive { first_byte_pos, last_byte_pos } => {
                if first_byte_pos > last_byte_pos
                    || first_byte_pos >= content_range.total_len()
                    || last_byte_pos >= content_range.total_len()
                {
                    return Err(Error::RangeNotSatisfiable);
                }
            }
            Range::Suffix { len } => {
                if len > content_range.total_len() {
                    return Err(Error::RangeNotSatisfiable);
                }
            }
        }

        let body = resp.into_body();

        Ok(Resource {
            content_range,
            stream: Box::pin(body.map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))),
        })
    }
}

#[async_trait::async_trait]
impl<T> RepositoryBackend for GcsRepository<T>
where
    T: GcsClient + Clone + Debug + Send + Sync + 'static,
{
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::Gcs {
            metadata_repo_url: self.metadata_repo_url.as_str().to_owned(),
            blob_repo_url: self.blob_repo_url.as_str().to_owned(),
        }
    }

    async fn fetch_metadata(&self, resource_path: &str, range: Range) -> Result<Resource, Error> {
        self.get(&self.metadata_repo_url, &resource_path, range).await
    }

    async fn fetch_blob(&self, resource_path: &str, range: Range) -> Result<Resource, Error> {
        self.get(&self.blob_repo_url, &resource_path, range).await
    }

    fn get_tuf_repo(
        &self,
    ) -> Result<Box<(dyn RepositoryProvider<Json> + Send + Sync + 'static)>, Error> {
        let repo = GcsRepository::clone(&self);
        Ok(Box::new(TufGcsRepository { repo }))
    }

    async fn blob_len(&self, path: &str) -> Result<u64, anyhow::Error> {
        // FIXME(http://fxbug.dev/98993): The gcs library does not expose a more efficient API for
        // determining the blob size.
        Ok(self.fetch_blob(path, Range::Full).await?.total_len())
    }

    async fn blob_modification_time(
        &self,
        _path: &str,
    ) -> Result<Option<SystemTime>, anyhow::Error> {
        // FIXME(http://fxbug.dev/98993): The gcs library does not expose an API to determine the
        // blob modification time.
        Ok(None)
    }
}

struct TufGcsRepository<T> {
    repo: GcsRepository<T>,
}

impl<T> RepositoryProvider<Json> for TufGcsRepository<T>
where
    T: GcsClient + Debug + Clone + Send + Sync + 'static,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin + 'a>, tuf::Error>> {
        let meta_path = meta_path.clone();
        let path = meta_path.components::<Json>(version).join("/");
        let repo = self.repo.clone();

        async move {
            let resp = RepositoryBackend::fetch_metadata(&repo, &path, Range::Full).await.map_err(
                |err| match err {
                    Error::Tuf(err) => err,
                    Error::NotFound => tuf::Error::MetadataNotFound { path: meta_path, version },
                    err => tuf::Error::Opaque(err.to_string()),
                },
            )?;

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
        let repo = self.repo.clone();

        async move {
            let resp = RepositoryBackend::fetch_metadata(&repo, &path, Range::Full).await.map_err(
                |err| match err {
                    Error::Tuf(err) => err,
                    Error::NotFound => tuf::Error::TargetNotFound(target_path),
                    err => tuf::Error::Opaque(err.to_string()),
                },
            )?;

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
                file_system::CHUNK_SIZE,
                repo_tests::{self, TestEnv as _},
                FileSystemRepository,
            },
            test_utils::make_repository,
        },
        assert_matches::assert_matches,
        camino::{Utf8Path, Utf8PathBuf},
        std::{fs::File, io::Write as _},
        url::Url,
    };

    #[derive(Debug, Clone)]
    struct MockGcsClient {
        repo: FileSystemRepository,
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
                "my-tuf-repo" => self.repo.fetch_metadata(object, Range::Full).await,
                "my-blob-repo" => self.repo.fetch_blob(object, Range::Full).await,
                _ => panic!("unknown bucket {:?}", bucket),
            };

            match res {
                Ok(resource) => {
                    // We don't support range requests.
                    assert_eq!(resource.content_range.to_http_content_range_header(), None);

                    Ok(Response::builder()
                        .status(StatusCode::OK)
                        .header("Content-Length", resource.content_len())
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
            let tmp = tempfile::tempdir().unwrap();
            let dir = Utf8Path::from_path(tmp.path()).unwrap();

            // Create a repository and serve it with the server.
            let metadata_repo_path = dir.join("tuf");
            let blob_repo_path = dir.join("blobs");
            std::fs::create_dir(&metadata_repo_path).unwrap();
            std::fs::create_dir(&blob_repo_path).unwrap();

            make_repository(blob_repo_path.as_std_path(), blob_repo_path.as_std_path()).await;
            let remote_repo =
                FileSystemRepository::new(metadata_repo_path.clone(), blob_repo_path.clone());

            let tuf_url = "gs://my-tuf-repo/";
            let blob_url = "gs://my-blob-repo/";

            let repo = GcsRepository::new(
                MockGcsClient { repo: remote_repo },
                Url::parse(&tuf_url).unwrap(),
                Url::parse(&blob_url).unwrap(),
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

        fn repo(&self) -> &dyn RepositoryBackend {
            &self.repo
        }
    }

    repo_tests::repo_test_suite! {
        env = TestEnv::new().await;
        chunk_size = CHUNK_SIZE;
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
