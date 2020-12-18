// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{PkgFs, TestEnv},
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_paver as paver,
    fidl_fuchsia_space::{ErrorCode, ManagerMarker},
    fuchsia_async::{self as fasync, OnSignals},
    fuchsia_zircon as zx,
    matches::assert_matches,
    mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
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
    let env = TestEnv::builder().pkgfs(TempDirPkgFs::new()).build().await;
    env.pkgfs.create_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs.garbage_exists());
}

#[fasync::run_singlethreaded(test)]
async fn gc_twice_same_client() {
    let env = TestEnv::builder().pkgfs(TempDirPkgFs::new()).build().await;
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
    let env = TestEnv::builder().pkgfs(TempDirPkgFs::new()).build().await;
    env.pkgfs.create_garbage();

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs.garbage_exists());

    env.pkgfs.create_garbage();
    let second_connection = env
        .apps
        .pkg_cache
        .connect_to_protocol_at_exposed_dir::<ManagerMarker>()
        .expect("connect to space manager");
    let res = second_connection.gc().await;

    assert_matches!(res, Ok(Ok(())));
    assert!(!env.pkgfs.garbage_exists());
}

#[fasync::run_singlethreaded(test)]
async fn gc_error_missing_garbage_file() {
    let env = TestEnv::builder().pkgfs(TempDirPkgFs::new()).build().await;

    let res = env.proxies.space_manager.gc().await;

    assert_matches!(res, Ok(Err(ErrorCode::Internal)));
}

#[fasync::run_singlethreaded(test)]
async fn gc_error_pending_commit() {
    let (throttle_hook, throttler) = mphooks::throttle();

    let env = TestEnv::builder()
        .pkgfs(TempDirPkgFs::new())
        .paver_service_builder(
            MockPaverServiceBuilder::new()
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending)))
                .insert_hook(throttle_hook),
        )
        .build()
        .await;
    env.pkgfs.create_garbage();

    // Allow the paver to emit enough events to unblock the CommitStatusProvider FIDL server, but
    // few enough to guarantee the commit is still pending.
    let () = throttler.emit_next_paver_events(&[
        PaverEvent::QueryCurrentConfiguration,
        PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A },
    ]);
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Err(ErrorCode::PendingCommit)));

    // When the commit completes, GC should unblock as well.
    let () = throttler.emit_next_paver_events(&[
        PaverEvent::SetConfigurationHealthy { configuration: paver::Configuration::A },
        PaverEvent::SetConfigurationUnbootable { configuration: paver::Configuration::B },
        PaverEvent::BootManagerFlush,
    ]);
    let event_pair =
        env.proxies.commit_status_provider.is_current_system_committed().await.unwrap();
    assert_eq!(OnSignals::new(&event_pair, zx::Signals::USER_0).await, Ok(zx::Signals::USER_0));
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
}
