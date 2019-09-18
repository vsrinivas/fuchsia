// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    fidl_fuchsia_space::{
        ErrorCode as SpaceErrorCode, ManagerMarker as SpaceManagerMarker,
        ManagerProxy as SpaceManagerProxy,
    },
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    futures::prelude::*,
    matches::assert_matches,
    std::fs::File,
    std::path::PathBuf,
    tempfile::TempDir,
};

struct Mounts {
    pkgfs_ctl: TempDir,
    pkgfs_versions: TempDir,
}

impl Mounts {
    fn new() -> Self {
        Self {
            pkgfs_ctl: tempfile::tempdir().expect("/tmp to exist"),
            pkgfs_versions: tempfile::tempdir().expect("/tmp to exist"),
        }
    }
}

struct Proxies {
    space_manager: SpaceManagerProxy,
}

struct TestEnv {
    _env: NestedEnvironment,
    mounts: Mounts,
    proxies: Proxies,
    pkg_cache: App,
}

impl TestEnv {
    fn new() -> Self {
        let mounts = Mounts::new();

        let mut fs = ServiceFs::new();

        let pkg_cache = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg-cache-integration-tests#meta/pkg-cache-without-pkgfs.cmx".to_string(),
        )
        .add_dir_to_namespace(
            "/pkgfs/ctl".to_string(),
            File::open(mounts.pkgfs_ctl.path()).expect("/pkgfs/ctl tempdir to open"),
        )
        .expect("/pksfs/ctl to mount")
        .add_dir_to_namespace(
            "/pkgfs/versions".to_string(),
            File::open(mounts.pkgfs_versions.path()).expect("/pkgfs/versions tempdir to open"),
        )
        .expect("/pkgfs/versions to mount");

        let env = fs
            .create_salted_nested_environment("pkg_cache_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let pkg_cache = pkg_cache.spawn(env.launcher()).expect("pkg_cache to launch");

        let proxies = Proxies {
            space_manager: pkg_cache
                .connect_to_service::<SpaceManagerMarker>()
                .expect("connect to space manager"),
        };

        Self { _env: env, mounts, proxies, pkg_cache: pkg_cache }
    }

    fn pkgfs_garbage_path(&self) -> PathBuf {
        self.mounts.pkgfs_ctl.path().join("garbage")
    }

    fn create_pkgfs_garbage(&self) {
        File::create(self.pkgfs_garbage_path()).expect("create garbage file");
    }

    fn pkgfs_garbage_exists(&self) -> bool {
        self.pkgfs_garbage_path().exists()
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_gc_garbage_file_deleted() {
    let env = TestEnv::new();
    env.create_pkgfs_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs_garbage_exists());
}

#[fasync::run_singlethreaded(test)]
async fn test_gc_twice_same_client() {
    let env = TestEnv::new();
    env.create_pkgfs_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs_garbage_exists());

    env.create_pkgfs_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs_garbage_exists());
}

#[fasync::run_singlethreaded(test)]
async fn test_gc_twice_different_clients() {
    let env = TestEnv::new();
    env.create_pkgfs_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs_garbage_exists());

    env.create_pkgfs_garbage();
    let second_connection =
        env.pkg_cache.connect_to_service::<SpaceManagerMarker>().expect("connect to space manager");
    let res = second_connection.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs_garbage_exists());
}

#[fasync::run_singlethreaded(test)]
async fn test_gc_error_missing_garbage_file() {
    let env = TestEnv::new();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Err(SpaceErrorCode::Internal)));
}
