// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_io::{FileEvent, FileMarker},
    fidl_fuchsia_pkg::LocalMirrorProxy,
    fidl_fuchsia_pkg_ext::RepositoryUrl,
    fuchsia_url::pkg_url::RepoUrl,
    fuchsia_zircon::Status,
    futures::{future::BoxFuture, prelude::*},
    io_util::file::AsyncReader,
    tuf::{
        crypto::{HashAlgorithm, HashValue},
        interchange::Json,
        metadata::{MetadataPath, MetadataVersion, TargetDescription, TargetPath},
        repository::RepositoryProvider,
    },
};

pub struct LocalMirrorRepositoryProvider {
    proxy: LocalMirrorProxy,
    url: RepoUrl,
}

impl LocalMirrorRepositoryProvider {
    #[cfg(test)] // TODO(fxb/59827) use this.
    pub fn new(proxy: LocalMirrorProxy, url: RepoUrl) -> Self {
        Self { proxy, url }
    }
}

fn make_opaque_error(e: Error) -> tuf::Error {
    tuf::Error::Opaque(format!("{:#}", e))
}

impl RepositoryProvider<Json> for LocalMirrorRepositoryProvider {
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        _max_length: Option<usize>,
        _hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin>>> {
        let path = meta_path.components::<Json>(&version).join("/");
        async move {
            let (local, remote) = fidl::endpoints::create_endpoints::<FileMarker>()
                .context("creating file proxy")
                .map_err(make_opaque_error)?;
            self.proxy
                .get_metadata(&mut RepositoryUrl::from(self.url.clone()).into(), &path, remote)
                .await
                .context("sending get_metadata")
                .map_err(make_opaque_error)?
                .map_err(|_| tuf::Error::NotFound)?;

            // Wait for OnOpen so that we know that the open actually succeeded.
            let file_proxy =
                local.into_proxy().context("creating FileProxy").map_err(make_opaque_error)?;
            let mut stream = file_proxy.take_event_stream();

            let mut status = None;
            while let Some(event) = stream.next().await {
                match event {
                    Ok(FileEvent::OnOpen_ { s, .. }) => {
                        status = Some(Status::ok(s));
                        break;
                    }
                    Err(e) => {
                        return Err(make_opaque_error(anyhow!(e).context("waiting for OnOpen")))
                    }
                }
            }

            match status {
                Some(Ok(())) => {}
                Some(Err(Status::NOT_FOUND)) => return Err(tuf::Error::NotFound),
                Some(Err(e)) => return Err(tuf::Error::Opaque(format!("open failed: {:?}", e))),
                None => {
                    return Err(tuf::Error::Opaque(format!(
                        "Expected OnOpen, but did not get one."
                    )))
                }
            }

            // Drop the stream so that AsyncReader has sole ownership of the proxy.
            std::mem::drop(stream);

            let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(
                AsyncReader::from_proxy(file_proxy)
                    .context("creating AsyncReader for file")
                    .map_err(make_opaque_error)?,
            );

            Ok(reader)
        }
        .boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        _target_path: &'a TargetPath,
        _target_description: &'a TargetDescription,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin>>> {
        future::ready(Err(tuf::Error::Opaque(
            "fetch_target is not supported for LocalMirrorRepositoryProvider".to_string(),
        )))
        .boxed()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_pkg::LocalMirrorMarker,
        fuchsia_async as fasync,
        fuchsia_pkg_testing::{FakePkgLocalMirror, PackageBuilder, Repository, RepositoryBuilder},
        tuf::metadata::Role,
    };

    struct TestEnv {
        _repo: Repository,
        _task: fasync::Task<()>,
        provider: LocalMirrorRepositoryProvider,
    }

    const EMPTY_REPO_PATH: &str = "/pkg/empty-repo";
    impl TestEnv {
        async fn new() -> Self {
            let pkg = PackageBuilder::new("test")
                .add_resource_at("file.txt", "hi there".as_bytes())
                .build()
                .await
                .unwrap();
            let repo = RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
                .add_package(pkg)
                .build()
                .await
                .unwrap();

            let url: RepoUrl = "fuchsia-pkg://example.com".parse().unwrap();
            let mirror = FakePkgLocalMirror::from_repository_and_url(&repo, &url).await;
            let (proxy, stream) =
                fidl::endpoints::create_proxy_and_stream::<LocalMirrorMarker>().unwrap();

            let task = fasync::Task::spawn(async move {
                mirror.handle_request_stream(stream).await.expect("handle_request_stream ok");
            });

            let provider = LocalMirrorRepositoryProvider::new(proxy, url);

            TestEnv { _repo: repo, _task: task, provider }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fetch_metadata_latest_succeeds() {
        let env = TestEnv::new().await;
        let mut result = env
            .provider
            .fetch_metadata(
                &MetadataPath::from_role(&Role::Root),
                &MetadataVersion::None,
                None,
                None,
            )
            .await
            .expect("fetch_metadata succeeds");

        let mut data = Vec::new();
        result.read_to_end(&mut data).await.expect("Read succeeds");
        assert!(!data.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fetch_metadata_v1_succeeds() {
        let env = TestEnv::new().await;
        let mut result = env
            .provider
            .fetch_metadata(
                &MetadataPath::from_role(&Role::Root),
                &MetadataVersion::Number(1),
                None,
                None,
            )
            .await
            .expect("fetch_metadata succeeds");

        let mut data = Vec::new();
        result.read_to_end(&mut data).await.expect("Read succeeds");
        assert!(!data.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fetch_metadata_v4_fails() {
        let env = TestEnv::new().await;
        let result = env
            .provider
            .fetch_metadata(
                &MetadataPath::from_role(&Role::Root),
                &MetadataVersion::Number(4),
                None,
                None,
            )
            .await;

        assert!(result.is_err());
    }
}
