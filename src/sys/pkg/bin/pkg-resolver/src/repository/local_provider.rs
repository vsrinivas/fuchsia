// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::LocalMirrorProxy,
    fidl_fuchsia_pkg_ext::RepositoryUrl,
    fuchsia_fs::file::AsyncReader,
    fuchsia_zircon::Status,
    futures::{future::BoxFuture, prelude::*},
    tuf::{
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        pouf::Pouf1,
        repository::RepositoryProvider,
    },
};

pub struct LocalMirrorRepositoryProvider {
    proxy: LocalMirrorProxy,
    url: fuchsia_url::RepositoryUrl,
}

impl LocalMirrorRepositoryProvider {
    pub fn new(proxy: LocalMirrorProxy, url: fuchsia_url::RepositoryUrl) -> Self {
        Self { proxy, url }
    }
}

fn make_opaque_error(e: Error) -> tuf::Error {
    tuf::Error::Opaque(format!("{:#}", e))
}

impl RepositoryProvider<Pouf1> for LocalMirrorRepositoryProvider {
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let meta_path = meta_path.clone();
        let path = meta_path.components::<Pouf1>(version).join("/");
        async move {
            let (local, remote) = fidl::endpoints::create_endpoints::<fio::FileMarker>()
                .context("creating file proxy")
                .map_err(make_opaque_error)?;
            self.proxy
                .get_metadata(&mut RepositoryUrl::from(self.url.clone()).into(), &path, remote)
                .await
                .context("sending get_metadata")
                .map_err(make_opaque_error)?
                .map_err(|_| tuf::Error::MetadataNotFound { path: meta_path.clone(), version })?;

            // Wait for OnOpen so that we know that the open actually succeeded.
            let file_proxy =
                local.into_proxy().context("creating FileProxy").map_err(make_opaque_error)?;
            let mut stream = file_proxy.take_event_stream();

            let event = if let Some(event) = stream.next().await {
                event
            } else {
                return Err(tuf::Error::Opaque(
                    "Expected OnOpen, but did not get one.".to_string(),
                ));
            };
            let status = match event {
                Ok(fio::FileEvent::OnOpen_ { s, .. }) => Status::ok(s),
                Ok(fio::FileEvent::OnRepresentation { .. }) => Ok(()),
                Err(e) => return Err(make_opaque_error(anyhow!(e).context("waiting for OnOpen"))),
            };

            match status {
                Ok(()) => {}
                Err(Status::NOT_FOUND) => {
                    return Err(tuf::Error::MetadataNotFound { path: meta_path, version })
                }
                Err(e) => return Err(tuf::Error::Opaque(format!("open failed: {:?}", e))),
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
        _target_path: &TargetPath,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
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

            let url: fuchsia_url::RepositoryUrl = "fuchsia-pkg://example.com".parse().unwrap();
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
            .fetch_metadata(&MetadataPath::root(), MetadataVersion::None)
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
            .fetch_metadata(&MetadataPath::root(), MetadataVersion::Number(1))
            .await
            .expect("fetch_metadata succeeds");

        let mut data = Vec::new();
        result.read_to_end(&mut data).await.expect("Read succeeds");
        assert!(!data.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fetch_metadata_v4_fails() {
        let env = TestEnv::new().await;
        let result =
            env.provider.fetch_metadata(&MetadataPath::root(), MetadataVersion::Number(4)).await;

        assert!(result.is_err());
    }
}
