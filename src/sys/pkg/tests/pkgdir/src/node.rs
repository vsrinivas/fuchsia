// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{dirs_to_test, Mode, PackageSource},
    anyhow::{anyhow, Context as _, Error},
    fidl::AsHandleRef,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
};

#[fuchsia::test]
async fn get_attr() {
    for source in dirs_to_test().await {
        get_attr_per_package_source(source).await
    }
}

trait U64Verifier: std::fmt::Debug {
    #[track_caller]
    fn verify(&self, num: u64);
}

impl U64Verifier for u64 {
    fn verify(&self, num: u64) {
        assert_eq!(num, *self)
    }
}

#[derive(Debug)]
struct AnyU64;
impl U64Verifier for AnyU64 {
    fn verify(&self, _num: u64) {}
}

/// pkgfs uses this timestamp when it doesn't have something else to return.
/// This value is computed via a comedy of errors in the implementation involving
/// The golang zero time.Time value, returning seconds instead of nanoseconds, and
/// integer underflow.
const PKGFS_PLACEHOLDER_TIME: u64 = 18446744011573954816;

async fn get_attr_per_package_source(source: PackageSource) {
    let root_dir = &source.dir;
    #[derive(Debug)]
    struct Args {
        open_flags: fio::OpenFlags,
        open_mode: u32,
        expected_mode: u32,
        id_verifier: Box<dyn U64Verifier>,
        expected_content_size: u64,
        expected_storage_size: u64,
        time_verifier: Box<dyn U64Verifier>,
    }

    impl Default for Args {
        fn default() -> Self {
            Self {
                open_flags: fio::OpenFlags::empty(),
                open_mode: 0,
                expected_mode: 0,
                id_verifier: Box::new(1),
                expected_content_size: 0,
                expected_storage_size: 0,
                time_verifier: Box::new(0),
            }
        }
    }

    async fn verify_get_attrs(root_dir: &fio::DirectoryProxy, path: &str, args: Args) {
        let node =
            fuchsia_fs::directory::open_node(root_dir, path, args.open_flags, args.open_mode)
                .await
                .unwrap();
        let (status, attrs) = node.get_attr().await.unwrap();
        zx::Status::ok(status).unwrap();
        assert_eq!(Mode(attrs.mode), Mode(args.expected_mode));
        args.id_verifier.verify(attrs.id);
        assert_eq!(attrs.content_size, args.expected_content_size);
        assert_eq!(attrs.storage_size, args.expected_storage_size);
        assert_eq!(attrs.link_count, 1);
        args.time_verifier.verify(attrs.creation_time);
        args.time_verifier.verify(attrs.modification_time);
    }

    verify_get_attrs(
        root_dir,
        ".",
        Args {
            open_flags: fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            expected_mode: fio::MODE_TYPE_DIRECTORY
                | if source.is_pkgdir() {
                    // "mode protection group and other bytes not set"
                    0o700
                } else {
                    0o755
                },
            time_verifier: if source.is_pkgdir() {
                // "creation and modification times unimplemented"
                Box::new(0)
            } else {
                Box::new(PKGFS_PLACEHOLDER_TIME)
            },
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "dir",
        Args {
            expected_mode: fio::MODE_TYPE_DIRECTORY
                | if source.is_pkgdir() {
                    // "mode protection group and other bytes not set"
                    0o700
                } else {
                    0o755
                },
            time_verifier: if source.is_pkgdir() {
                // "creation and modification times unimplemented"
                Box::new(0)
            } else {
                Box::new(PKGFS_PLACEHOLDER_TIME)
            },
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "file",
        Args {
            open_flags: fio::OpenFlags::RIGHT_READABLE,
            expected_mode: fio::MODE_TYPE_FILE | 0o500,
            id_verifier: Box::new(AnyU64),
            expected_content_size: 4,
            expected_storage_size: 8192,
            time_verifier: Box::new(0),
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "meta",
        Args {
            open_mode: fio::MODE_TYPE_FILE,
            expected_mode: fio::MODE_TYPE_FILE
                | if source.is_pkgdir() {
                    // "mode protection group and other bytes not set"
                    0o600
                } else {
                    0o644
                },
            expected_content_size: 64,
            expected_storage_size: 64,
            time_verifier: if source.is_pkgdir() {
                // "creation and modification times unimplemented"
                Box::new(0)
            } else {
                Box::new(PKGFS_PLACEHOLDER_TIME)
            },
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "meta",
        Args {
            open_mode: fio::MODE_TYPE_DIRECTORY,
            expected_mode: fio::MODE_TYPE_DIRECTORY
                | if source.is_pkgdir() {
                    // "mode protection group and other bytes not set"
                    0o700
                } else {
                    0o755
                },
            expected_content_size: 75,
            expected_storage_size: 75,
            time_verifier: if source.is_pkgdir() {
                // "creation and modification times unimplemented"
                Box::new(0)
            } else {
                Box::new(PKGFS_PLACEHOLDER_TIME)
            },
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "meta/dir",
        Args {
            expected_mode: fio::MODE_TYPE_DIRECTORY
                | if source.is_pkgdir() {
                    // "mode protection group and other bytes not set"
                    0o700
                } else {
                    0o755
                },
            expected_content_size: 75,
            expected_storage_size: 75,
            time_verifier: if source.is_pkgdir() {
                // "creation and modification times unimplemented"
                Box::new(0)
            } else {
                Box::new(PKGFS_PLACEHOLDER_TIME)
            },
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "meta/file",
        Args {
            expected_mode: fio::MODE_TYPE_FILE
                | if source.is_pkgdir() {
                    // "mode protection group and other bytes not set"
                    0o600
                } else {
                    0o644
                },
            expected_content_size: 9,
            expected_storage_size: 9,
            time_verifier: if source.is_pkgdir() {
                // "creation and modification times unimplemented"
                Box::new(0)
            } else {
                Box::new(PKGFS_PLACEHOLDER_TIME)
            },
            ..Default::default()
        },
    )
    .await;
}

#[fuchsia::test]
async fn close() {
    for source in dirs_to_test().await {
        close_per_package_source(source).await
    }
}

async fn close_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    async fn verify_close(root_dir: &fio::DirectoryProxy, path: &str, mode: u32) {
        let node =
            fuchsia_fs::directory::open_node(root_dir, path, fio::OpenFlags::RIGHT_READABLE, mode)
                .await
                .unwrap();

        let () = node.close().await.unwrap().map_err(zx::Status::from_raw).unwrap();

        assert_matches::assert_matches!(
            node.close().await,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::PEER_CLOSED, .. })
        );
    }

    verify_close(&root_dir, ".", fio::MODE_TYPE_DIRECTORY).await;
    verify_close(&root_dir, "dir", fio::MODE_TYPE_DIRECTORY).await;
    verify_close(&root_dir, "meta", fio::MODE_TYPE_DIRECTORY).await;
    verify_close(&root_dir, "meta/dir", fio::MODE_TYPE_DIRECTORY).await;

    verify_close(&root_dir, "file", fio::MODE_TYPE_FILE).await;
    verify_close(&root_dir, "meta/file", fio::MODE_TYPE_FILE).await;
    verify_close(&root_dir, "meta", fio::MODE_TYPE_FILE).await;
}

#[fuchsia::test]
async fn describe() {
    for source in dirs_to_test().await {
        describe_per_package_source(source).await
    }
}

async fn describe_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    assert_describe_directory(&root_dir, ".").await;

    assert_describe_directory(&root_dir, "meta").await;
    assert_describe_meta_file(&root_dir, "meta").await;

    assert_describe_directory(&root_dir, "meta/dir").await;
    assert_describe_directory(&root_dir, "dir").await;

    assert_describe_file(&root_dir, "file").await;
    assert_describe_meta_file(&root_dir, "meta/file").await;
}

async fn assert_describe_directory(package_root: &fio::DirectoryProxy, path: &str) {
    for flag in [fio::OpenFlags::empty(), fio::OpenFlags::NODE_REFERENCE] {
        let node =
            fuchsia_fs::directory::open_node(package_root, path, flag, fio::MODE_TYPE_DIRECTORY)
                .await
                .unwrap();

        if let Err(e) = verify_describe_directory_success(node).await {
            panic!("failed to verify describe. path: {:?}, error: {:#}", path, e);
        }
    }
}

async fn verify_describe_directory_success(node: fio::NodeProxy) -> Result<(), Error> {
    match node.describe().await {
        Ok(fio::NodeInfo::Directory(directory_object)) => {
            assert_eq!(directory_object, fio::DirectoryObject);
            Ok(())
        }
        Ok(other) => Err(anyhow!("wrong node type returned: {:?}", other)),
        Err(e) => Err(e).context("failed to call describe"),
    }
}

async fn assert_describe_file(package_root: &fio::DirectoryProxy, path: &str) {
    for flag in [fio::OpenFlags::RIGHT_READABLE, fio::OpenFlags::NODE_REFERENCE] {
        let node = fuchsia_fs::directory::open_node(package_root, path, flag, 0).await.unwrap();
        if let Err(e) = verify_describe_content_file(node, flag).await {
            panic!(
                "failed to verify describe. path: {:?}, flag: {:#x}, \
                    mode: {:#x}, error: {:#}",
                path, flag, 0, e
            );
        }
    }
}

async fn verify_describe_content_file(
    node: fio::NodeProxy,
    flag: fio::OpenFlags,
) -> Result<(), Error> {
    if flag.intersects(fio::OpenFlags::NODE_REFERENCE) {
        match node.describe().await {
            Ok(fio::NodeInfo::Service(fio::Service)) => Ok(()),
            Ok(other) => Err(anyhow!("wrong node type returned: {:?}", other)),
            Err(e) => Err(e).context("failed to call describe"),
        }
    } else {
        match node.describe().await {
            Ok(fio::NodeInfo::File(fio::FileObject { event: Some(event), stream: None })) => {
                match event.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST) {
                    Ok(_) => Ok(()),
                    Err(_) => Err(anyhow!("FILE_SIGNAL_READABLE not set")),
                }
            }
            Ok(other) => Err(anyhow!("wrong node type returned: {:?}", other)),
            Err(e) => Err(e).context("failed to call describe"),
        }
    }
}

async fn assert_describe_meta_file(package_root: &fio::DirectoryProxy, path: &str) {
    for flag in [fio::OpenFlags::empty(), fio::OpenFlags::NODE_REFERENCE] {
        let node = fuchsia_fs::directory::open_node(package_root, path, flag, fio::MODE_TYPE_FILE)
            .await
            .unwrap();
        if let Err(e) = verify_describe_meta_file_success(node).await {
            panic!(
                "failed to verify describe. path: {:?}, flag: {:#x}, \
                    mode: fio::MODE_TYPE_FILE, error: {:#}",
                path, flag, e
            );
        }
    }
}

async fn verify_describe_meta_file_success(node: fio::NodeProxy) -> Result<(), Error> {
    match node.describe().await {
        Ok(fio::NodeInfo::File(_)) => Ok(()),
        Ok(other) => Err(anyhow!("wrong node type returned: {:?}", other)),
        Err(e) => Err(e).context("failed to call describe"),
    }
}

#[fuchsia::test]
async fn set_flags() {
    for source in dirs_to_test().await {
        set_flags_per_package_source(source).await
    }
}

async fn set_flags_per_package_source(source: PackageSource) {
    let package_root = &source.dir;
    do_set_flags(package_root, ".", fio::MODE_TYPE_DIRECTORY, fio::OpenFlags::empty())
        .await
        .assert_not_supported();
    do_set_flags(package_root, "meta", fio::MODE_TYPE_DIRECTORY, fio::OpenFlags::empty())
        .await
        .assert_not_supported();
    do_set_flags(package_root, "meta/dir", fio::MODE_TYPE_DIRECTORY, fio::OpenFlags::empty())
        .await
        .assert_not_supported();
    do_set_flags(package_root, "dir", fio::MODE_TYPE_DIRECTORY, fio::OpenFlags::empty())
        .await
        .assert_not_supported();
    do_set_flags(package_root, "file", fio::MODE_TYPE_FILE, fio::OpenFlags::empty())
        .await
        .assert_ok();
    {
        let outcome =
            do_set_flags(package_root, "meta", fio::MODE_TYPE_FILE, fio::OpenFlags::empty()).await;
        if source.is_pkgdir() {
            // TODO(fxbug.dev/86883): should pkgdir support OPEN_FLAG_APPEND (as a no-op)?
            outcome.assert_ok();
        } else {
            outcome.assert_not_supported();
        }
    };
    {
        let outcome =
            do_set_flags(package_root, "meta", fio::MODE_TYPE_FILE, fio::OpenFlags::APPEND).await;
        if source.is_pkgdir() {
            // TODO(fxbug.dev/86883): should pkgdir support OPEN_FLAG_APPEND (as a no-op)?
            outcome.assert_ok();
        } else {
            outcome.assert_not_supported();
        }
    };
    {
        let outcome =
            do_set_flags(package_root, "meta/file", fio::MODE_TYPE_FILE, fio::OpenFlags::empty())
                .await;
        if source.is_pkgdir() {
            // TODO(fxbug.dev/86883): should pkgdir support OPEN_FLAG_APPEND (as a no-op)?
            outcome.assert_ok();
        } else {
            outcome.assert_not_supported();
        }
    };
    {
        let outcome =
            do_set_flags(package_root, "meta/file", fio::MODE_TYPE_FILE, fio::OpenFlags::APPEND)
                .await;
        if source.is_pkgdir() {
            // TODO(fxbug.dev/86883): should pkgdir support OPEN_FLAG_APPEND (as a no-op)?
            outcome.assert_ok();
        } else {
            outcome.assert_not_supported();
        }
    };
}

struct SetFlagsOutcome<'a> {
    argument: fio::OpenFlags,
    path: &'a str,
    mode: Mode,
    result: Result<Result<(), zx::Status>, fidl::Error>,
}
async fn do_set_flags<'a>(
    package_root: &fio::DirectoryProxy,
    path: &'a str,
    mode: u32,
    argument: fio::OpenFlags,
) -> SetFlagsOutcome<'a> {
    let node =
        fuchsia_fs::directory::open_node(package_root, path, fio::OpenFlags::RIGHT_READABLE, mode)
            .await
            .unwrap();

    let result = node.set_flags(argument).await.map(zx::Status::ok);
    SetFlagsOutcome { path, mode: Mode(mode), result, argument }
}

impl SetFlagsOutcome<'_> {
    fn error_context(&self) -> String {
        format!("path: {:?} as {:?}", self.path, self.mode)
    }

    #[track_caller]
    fn assert_not_supported(&self) {
        match &self.result {
            Ok(Err(zx::Status::NOT_SUPPORTED)) => {}
            Ok(Err(e)) => panic!(
                "set_flags({:?}): wrong error status: {} on {}",
                self.argument,
                e,
                self.error_context()
            ),
            Err(e) => panic!(
                "failed to call set_flags({:?}): {:?} on {}",
                self.argument,
                e,
                self.error_context()
            ),
            Ok(Ok(())) => panic!(
                "set_flags({:?}) suceeded unexpectedly on {}",
                self.argument,
                self.error_context()
            ),
        };
    }

    #[track_caller]
    fn assert_ok(&self) {
        match &self.result {
            Ok(Ok(())) => {}
            e => {
                panic!("set_flags({:?}) failed: {:?} on {}", self.argument, e, self.error_context())
            }
        }
    }
}

#[fuchsia::test]
async fn set_attr() {
    for source in dirs_to_test().await {
        set_attr_per_package_source(source).await
    }
}

async fn set_attr_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    assert_set_attr(&root_dir, ".", fio::MODE_TYPE_DIRECTORY).await;
    assert_set_attr(&root_dir, "meta", fio::MODE_TYPE_DIRECTORY).await;
    assert_set_attr(&root_dir, "meta/dir", fio::MODE_TYPE_DIRECTORY).await;
    assert_set_attr(&root_dir, "dir", fio::MODE_TYPE_DIRECTORY).await;
    assert_set_attr(&root_dir, "meta", fio::MODE_TYPE_FILE).await;
    assert_set_attr(&root_dir, "meta/file", fio::MODE_TYPE_FILE).await;
}

async fn assert_set_attr(package_root: &fio::DirectoryProxy, path: &str, mode: u32) {
    let node = fuchsia_fs::directory::open_node(package_root, path, fio::OpenFlags::empty(), mode)
        .await
        .unwrap();

    if let Err(e) = verify_set_attr(node).await {
        panic!("set_attr failed. path: {:?}, error: {:#}", path, e);
    }
}

async fn verify_set_attr(node: fio::NodeProxy) -> Result<(), Error> {
    let mut node_attr = fio::NodeAttributes {
        mode: 0,
        id: 0,
        content_size: 0,
        storage_size: 0,
        link_count: 0,
        creation_time: 0,
        modification_time: 0,
    };
    match node.set_attr(fio::NodeAttributeFlags::empty(), &mut node_attr).await {
        Ok(status) => {
            if matches!(
                zx::Status::from_raw(status),
                zx::Status::NOT_SUPPORTED | zx::Status::BAD_HANDLE
            ) {
                Ok(())
            } else {
                Err(anyhow!("wrong status returned: {:?}", zx::Status::from_raw(status)))
            }
        }
        Err(e) => Err(e).context("failed to call set_attr"),
    }
}

#[fuchsia::test]
async fn sync() {
    for source in dirs_to_test().await {
        sync_per_package_source(source).await
    }
}

async fn sync_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    assert_sync(&root_dir, ".", fio::MODE_TYPE_DIRECTORY).await;
    assert_sync(&root_dir, "meta", fio::MODE_TYPE_DIRECTORY).await;
    assert_sync(&root_dir, "meta/dir", fio::MODE_TYPE_DIRECTORY).await;
    assert_sync(&root_dir, "dir", fio::MODE_TYPE_DIRECTORY).await;
    assert_sync(&root_dir, "meta", fio::MODE_TYPE_FILE).await;
    assert_sync(&root_dir, "meta/file", fio::MODE_TYPE_FILE).await;
}

async fn assert_sync(package_root: &fio::DirectoryProxy, path: &str, mode: u32) {
    let node = fuchsia_fs::directory::open_node(package_root, path, fio::OpenFlags::empty(), mode)
        .await
        .unwrap();

    if let Err(e) = verify_sync(node).await {
        panic!("sync failed. path: {:?}, error: {:#}", path, e);
    }
}

async fn verify_sync(node: fio::NodeProxy) -> Result<(), Error> {
    let result = node.sync().await.context("failed to call sync")?;
    let result = result.map_err(zx::Status::from_raw);
    if result == Err(zx::Status::NOT_SUPPORTED) {
        Ok(())
    } else {
        Err(anyhow!("wrong status returned: {:?}", result))
    }
}
