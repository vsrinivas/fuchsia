// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools to download a Fuchsia package from from a TUF repository.
//! See
//! - [Package](https://fuchsia.dev/fuchsia-src/concepts/packages/package?hl=en)
//! - [TUF](https://theupdateframework.io/)

use {
    crate::{
        range::{ContentRange, Range},
        repository::{Error, RepoProvider},
        resource::Resource,
    },
    anyhow::{anyhow, Context as _, Result},
    fidl_fuchsia_developer_ffx_ext::RepositorySpec,
    futures::{future::BoxFuture, AsyncRead, FutureExt as _, TryStreamExt as _},
    hyper::{
        client::{connect::Connect, Client},
        header::{CONTENT_LENGTH, CONTENT_RANGE, RANGE},
        Body, Method, Request, StatusCode, Uri,
    },
    std::{fmt::Debug, time::SystemTime},
    tuf::{
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        pouf::Pouf1,
        repository::{
            HttpRepository as TufHttpRepository, HttpRepositoryBuilder as TufHttpRepositoryBuilder,
            RepositoryProvider as TufRepositoryProvider,
        },
    },
    url::Url,
};

#[derive(Debug)]
pub struct HttpRepository<C>
where
    C: Connect + Send + Sync + 'static,
{
    client: Client<C, Body>,
    tuf_repo: TufHttpRepository<C, Pouf1>,
    metadata_repo_url: Url,
    blob_repo_url: Url,
}

impl<C> HttpRepository<C>
where
    C: Connect + Clone + Debug + Send + Sync + 'static,
{
    pub fn new(
        client: Client<C, Body>,
        mut metadata_repo_url: Url,
        mut blob_repo_url: Url,
    ) -> Self {
        // `URL.join` treats urls with a trailing slash as a directory, and without as a file.
        // In the latter case, it will strip off the last segment before joining paths. Since the
        // metadata and blob url are directories, make sure they have a trailing slash.
        if !metadata_repo_url.path().ends_with('/') {
            metadata_repo_url.set_path(&format!("{}/", metadata_repo_url.path()));
        }

        if !blob_repo_url.path().ends_with('/') {
            blob_repo_url.set_path(&format!("{}/", blob_repo_url.path()));
        }

        let tuf_repo =
            TufHttpRepositoryBuilder::<_, Pouf1>::new(metadata_repo_url.clone(), client.clone())
                .build();

        Self { client, tuf_repo, metadata_repo_url, blob_repo_url }
    }
}

impl<C> HttpRepository<C>
where
    C: Connect + Clone + Send + Sync + 'static,
{
    fn fetch_from<'a>(
        &'a self,
        root: &Url,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        let full_url = root.join(resource_path);

        async move {
            let full_url = full_url.map_err(|e| anyhow!(e))?;
            let uri = full_url.as_str().parse::<Uri>().map_err(|e| anyhow!(e))?;

            let mut builder = Request::builder().method(Method::GET).uri(uri);

            // Add a 'Range' header if we're requesting a subset of the file.
            if let Some(http_range) = range.to_http_request_header() {
                builder = builder.header(RANGE, http_range);
            }

            let request = builder.body(Body::empty()).context("creating http request")?;

            let resp = self
                .client
                .request(request)
                .await
                .context(format!("fetching resource {}", full_url.as_str()))?;

            // Check if the response was successful, or propagate the error.
            //
            // Note that according to [RFC-7233], it's possible for a 'Range' request from the
            // client to get a `200 OK` response and the full resource in return. Furthermore, the
            // server is allowed to return a `206 Partial Content` and a different subset
            // `Content-Range` that was requested by the `Range` request. The RFC mentions that a
            // server may do this to coalesce overlapping ranges, or separated by a gap. So we'll
            // parse these headers and pass along the response range to the caller.
            //
            // [RFC-7233]: https://datatracker.ietf.org/doc/html/rfc7233#section-4.1
            let content_range = match resp.status() {
                StatusCode::OK => {
                    // The package resolver currently requires a 'Content-Length' header, so error out
                    // if one wasn't provided.
                    let content_length = resp.headers().get(CONTENT_LENGTH).ok_or_else(|| {
                        Error::Other(anyhow!("response missing Content-Length header"))
                    })?;

                    ContentRange::from_http_content_length_header(content_length)
                        .context("parsing Content-Length header")?
                }
                StatusCode::PARTIAL_CONTENT => {
                    // According to [RFC-7233], a Partial Content status must come with a
                    // 'Content-Range' header.
                    //
                    // [RFC-7233]: https://datatracker.ietf.org/doc/html/rfc7233#section-4.1
                    let content_range = resp.headers().get(CONTENT_RANGE).ok_or_else(|| {
                        Error::Other(anyhow!(
                            "received Partial Content status, but missing Content-Range header"
                        ))
                    })?;

                    ContentRange::from_http_content_range_header(content_range)
                        .context("parsing Content-Range header")?
                }
                StatusCode::NOT_FOUND => {
                    return Err(Error::NotFound);
                }
                StatusCode::RANGE_NOT_SATISFIABLE => {
                    return Err(Error::RangeNotSatisfiable);
                }
                status => {
                    if status.is_success() {
                        return Err(Error::Other(anyhow!("unexpected status code: {}", status)));
                    } else {
                        return Err(Error::Other(anyhow!(
                            "error downloading resource: {}",
                            status
                        )));
                    }
                }
            };

            let body = resp.into_body();

            Ok(Resource {
                content_range,
                stream: Box::pin(
                    body.map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e)),
                ),
            })
        }
        .boxed()
    }
}

impl<C> RepoProvider for HttpRepository<C>
where
    C: Connect + Clone + Debug + Send + Sync + 'static,
{
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::Http {
            metadata_repo_url: self.metadata_repo_url.as_str().to_owned(),
            blob_repo_url: self.blob_repo_url.as_str().to_owned(),
        }
    }

    fn fetch_metadata_range<'a>(
        &'a self,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        self.fetch_from(&self.metadata_repo_url, resource_path, range)
    }

    fn fetch_blob_range<'a>(
        &'a self,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        self.fetch_from(&self.blob_repo_url, resource_path, range)
    }

    fn blob_len<'a>(&'a self, path: &str) -> BoxFuture<'a, Result<u64>> {
        // FIXME(http://fxbug.dev/98376): It may be more efficient to try to make a HEAD request and
        // see if that includes the content length before falling back to us requesting the blob and
        // dropping the stream.
        let fut = self.fetch_blob_range(path, Range::Full);
        async move { Ok(fut.await?.total_len()) }.boxed()
    }

    fn blob_modification_time<'a>(
        &'a self,
        _path: &str,
    ) -> BoxFuture<'a, Result<Option<SystemTime>>> {
        async move { Ok(None) }.boxed()
    }
}

impl<C> TufRepositoryProvider<Pouf1> for HttpRepository<C>
where
    C: Connect + Clone + Debug + Send + Sync + 'static,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        self.tuf_repo.fetch_metadata(meta_path, version)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        self.tuf_repo.fetch_target(target_path)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            manager::RepositoryManager,
            repo_client::RepoClient,
            repository::repo_tests::{self, TestEnv as _},
            server::RepositoryServer,
            test_utils::make_pm_repository,
            util::CHUNK_SIZE,
        },
        assert_matches::assert_matches,
        camino::Utf8Path,
        fuchsia_async as fasync,
        fuchsia_hyper::{new_client, HyperConnector},
        std::{fs::File, io::Write, net::Ipv4Addr, sync::Arc},
    };

    struct TestEnv {
        tmp: tempfile::TempDir,
        repo: HttpRepository<HyperConnector>,
        server: RepositoryServer,
        task: fasync::Task<()>,
    }

    impl TestEnv {
        async fn new() -> Self {
            let tmp = tempfile::tempdir().unwrap();
            let dir = Utf8Path::from_path(tmp.path()).unwrap();

            // Create a repository and serve it with the server.
            let remote_backend = Box::new(make_pm_repository(&dir).await) as Box<_>;
            let remote_repo = RepoClient::from_trusted_remote(remote_backend).await.unwrap();

            let manager = RepositoryManager::new();
            manager.add("tuf", remote_repo);

            let addr = (Ipv4Addr::LOCALHOST, 0).into();
            let (server_fut, _, server) =
                RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

            // Run the server in the background.
            let task = fasync::Task::local(server_fut);

            let tuf_url = server.local_url() + "/tuf";
            let blob_url = server.local_url() + "/tuf/blobs";

            let repo = HttpRepository::new(
                new_client(),
                Url::parse(&tuf_url).unwrap(),
                Url::parse(&blob_url).unwrap(),
            );

            TestEnv { tmp, repo, server, task }
        }
    }

    #[async_trait::async_trait]
    impl repo_tests::TestEnv for TestEnv {
        fn supports_range(&self) -> bool {
            true
        }

        fn repo(&self) -> &dyn RepoProvider {
            &self.repo
        }

        fn write_metadata(&self, path: &str, bytes: &[u8]) {
            let file_path = self.tmp.path().join("repository").join(path);
            let mut f = File::create(file_path).unwrap();
            f.write_all(bytes).unwrap();
        }

        fn write_blob(&self, path: &str, bytes: &[u8]) {
            let file_path = self.tmp.path().join("repository").join("blobs").join(path);
            let mut f = File::create(file_path).unwrap();
            f.write_all(bytes).unwrap();
        }

        async fn stop(self) {
            let TestEnv { tmp: _tmp, repo, server, task } = self;
            server.stop();

            // Explicitly drop the repo so we close all our client connections before we wait for
            // the server to shut down.
            drop(repo);

            task.await;
        }
    }

    repo_tests::repo_test_suite! {
        env = TestEnv::new().await;
        chunk_size = CHUNK_SIZE;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_blob_modification_time() {
        let env = TestEnv::new().await;

        std::fs::write(env.tmp.path().join("repository").join("blobs").join("empty-blob"), b"")
            .unwrap();

        // We don't support modification time.
        assert_matches!(env.repo.blob_modification_time("empty-blob").await, Ok(None));

        env.stop().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch() {
        let env = TestEnv::new().await;

        // We don't support watch.
        assert_matches!(env.repo.supports_watch(), false);
        assert!(env.repo.watch().is_err());

        env.stop().await;
    }
}
