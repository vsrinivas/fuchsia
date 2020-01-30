// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{PkgFs, TestEnv},
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_space::{ErrorCode, ManagerMarker},
    fuchsia_async as fasync,
    matches::assert_matches,
    std::{
        fs::{create_dir, File},
        path::PathBuf,
    },
    tempfile::TempDir,
};

struct TempDirPkgFs {
    root: TempDir,
}

impl TempDirPkgFs {
    fn new() -> Self {
        let root = tempfile::tempdir().unwrap();
        // pkg-cache needs these dirs to exist to start
        create_dir(root.path().join("ctl")).unwrap();
        create_dir(root.path().join("system")).unwrap();
        create_dir(root.path().join("versions")).unwrap();
        Self { root }
    }

    fn garbage_path(&self) -> PathBuf {
        self.root.path().join("ctl/garbage")
    }

    fn create_garbage(&self) {
        File::create(self.garbage_path()).unwrap();
    }

    fn garbage_exists(&self) -> bool {
        self.garbage_path().exists()
    }
}

impl PkgFs for TempDirPkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        Ok(fdio::transfer_fd(File::open(self.root.path()).unwrap()).unwrap().into())
    }
}

#[fasync::run_singlethreaded(test)]
async fn gc_garbage_file_deleted() {
    let env = TestEnv::new(TempDirPkgFs::new());
    env.pkgfs.create_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs.garbage_exists());
}

#[fasync::run_singlethreaded(test)]
async fn gc_twice_same_client() {
    let env = TestEnv::new(TempDirPkgFs::new());
    env.pkgfs.create_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs.garbage_exists());

    env.pkgfs.create_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs.garbage_exists());
}

#[fasync::run_singlethreaded(test)]
async fn gc_twice_different_clients() {
    let env = TestEnv::new(TempDirPkgFs::new());
    env.pkgfs.create_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs.garbage_exists());

    env.pkgfs.create_garbage();
    let second_connection =
        env.pkg_cache.connect_to_service::<ManagerMarker>().expect("connect to space manager");
    let res = second_connection.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs.garbage_exists());
}

#[fasync::run_singlethreaded(test)]
async fn gc_error_missing_garbage_file() {
    let env = TestEnv::new(TempDirPkgFs::new());

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Err(ErrorCode::Internal)));
}
