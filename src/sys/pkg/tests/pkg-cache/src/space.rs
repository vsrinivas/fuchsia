// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{PkgFs, TestEnv},
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_paver as paver,
    fidl_fuchsia_space::{ErrorCode, ManagerMarker},
    fuchsia_async::{self as fasync, OnSignals},
    fuchsia_merkle::Hash,
    fuchsia_pkg::{MetaContents, PackagePath},
    fuchsia_zircon as zx,
    maplit::{btreemap, hashmap},
    matches::assert_matches,
    mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
    std::{
        fs::{create_dir, create_dir_all, File},
        io::Write as _,
        path::PathBuf,
    },
    system_image::StaticPackages,
    tempfile::TempDir,
};

struct TempDirPkgFs {
    root: TempDir,
}

impl TempDirPkgFs {
    fn new() -> Self {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let fake_package_hash: Hash =
            "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![(
            PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
            fake_package_hash,
        )]);
        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                btreemap! {
                    "some-blob".to_string() =>
                        "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap()
                }
            ).unwrap(),
            fake_package_hash.clone() => MetaContents::from_map(
                btreemap! {
                    "other-blob".to_string() =>
                        "3333333333333333333333333333333333333333333333333333333333333333".parse().unwrap()
                }
            ).unwrap()
        };
        let root = tempfile::tempdir().unwrap();

        create_dir(root.path().join("ctl")).unwrap();

        create_dir(root.path().join("system")).unwrap();
        File::create(root.path().join("system/meta"))
            .unwrap()
            .write_all(system_image_hash.to_string().as_bytes())
            .unwrap();
        create_dir(root.path().join("system/data")).unwrap();
        static_packages
            .serialize(File::create(root.path().join("system/data/static_packages")).unwrap())
            .unwrap();

        create_dir(root.path().join("versions")).unwrap();
        for (hash, contents) in versions_contents.iter() {
            let meta_path = root.path().join(format!("versions/{}/meta", hash));
            create_dir_all(&meta_path).unwrap();
            contents.serialize(&mut File::create(meta_path.join("contents")).unwrap()).unwrap();
        }

        create_dir(root.path().join("blobfs")).unwrap();

        Self { root }
    }

    fn garbage_path(&self) -> PathBuf {
        self.root.path().join("ctl/do-not-use-this-garbage")
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

    fn blobfs_root_proxy(&self) -> Result<DirectoryProxy, Error> {
        let dir_handle: ClientEnd<DirectoryMarker> =
            fdio::transfer_fd(File::open(self.root.path().join("blobfs")).unwrap()).unwrap().into();
        Ok(dir_handle.into_proxy().unwrap())
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
        .realm_instance
        .root
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
                .insert_hook(throttle_hook)
                .insert_hook(mphooks::config_status(|_| Ok(paver::ConfigurationStatus::Pending))),
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
