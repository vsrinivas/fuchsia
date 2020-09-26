// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::Repository, anyhow::Error, fidl_fuchsia_pkg::LocalMirrorRequestStream,
    fuchsia_url::pkg_url::RepoUrl, pkg_local_mirror::PkgLocalMirror, tempfile::TempDir,
};

/// An implementation of fuchsia.pkg/LocalMirror useful for tests.
pub struct FakePkgLocalMirror {
    pkg_local_mirror: PkgLocalMirror,
    _dir: TempDir,
}

impl FakePkgLocalMirror {
    /// Create a `FakePkgLocalMirror` from the blobs and metadata in `repo`, serving the metadata at
    /// `url`.
    pub async fn from_repository_and_url(repo: &Repository, url: &RepoUrl) -> Self {
        let dir = tempfile::tempdir().unwrap();
        let () = repo
            .copy_local_repository_to_dir(
                &io_util::directory::open_in_namespace(
                    dir.path().to_str().unwrap(),
                    fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
                )
                .unwrap(),
                url,
            )
            .await;
        let pkg_local_mirror = PkgLocalMirror::new(
            &io_util::directory::open_in_namespace(
                dir.path().to_str().unwrap(),
                fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            )
            .unwrap(),
        )
        .await
        .unwrap();
        FakePkgLocalMirror { pkg_local_mirror, _dir: dir }
    }

    /// Handle a fuchsia.pkg/LocalMirror request stream.
    pub async fn handle_request_stream(
        &self,
        request: LocalMirrorRequestStream,
    ) -> Result<(), Error> {
        self.pkg_local_mirror.handle_request_stream(request).await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{PackageBuilder, RepositoryBuilder},
        fidl_fuchsia_pkg::LocalMirrorMarker,
        fidl_fuchsia_pkg_ext::{BlobId, RepositoryUrl},
        fuchsia_async as fasync,
        io_util::file::read,
    };

    #[fasync::run_singlethreaded(test)]
    async fn handle_request_stream() {
        let package = PackageBuilder::new("test").build().await.unwrap();
        let meta_far_merkle = package.meta_far_merkle_root().clone();
        let repo = RepositoryBuilder::new().add_package(package).build().await.unwrap();
        let url = "fuchsia-pkg://example.org".parse().unwrap();
        let mirror = FakePkgLocalMirror::from_repository_and_url(&repo, &url).await;
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<LocalMirrorMarker>().unwrap();
        let _server =
            fasync::Task::spawn(async move { mirror.handle_request_stream(stream).await });

        let (metadata, server_end) = fidl::endpoints::create_proxy().unwrap();
        let () = proxy
            .get_metadata(&mut RepositoryUrl::from(url).into(), "timestamp.json", server_end)
            .await
            .unwrap()
            .unwrap();
        let metadata = read(&metadata).await.unwrap();
        serde_json::from_slice::<serde_json::Value>(&metadata).expect("metadata is valid json");

        let (blob, server_end) = fidl::endpoints::create_proxy().unwrap();
        let () = proxy
            .get_blob(&mut BlobId::from(meta_far_merkle).into(), server_end)
            .await
            .unwrap()
            .unwrap();
        let blob = read(&blob).await.unwrap();
        fuchsia_archive::Reader::new(std::io::Cursor::new(blob)).expect("meta.far is a valid FAR");
    }
}
