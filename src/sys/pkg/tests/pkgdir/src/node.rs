// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{dirs_to_test, Mode, PackageSource},
    anyhow::{anyhow, Context as _, Error},
    fidl::{endpoints::Proxy as _, AsHandleRef as _},
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
            expected_mode: fio::MODE_TYPE_DIRECTORY | 0o700,
            time_verifier: Box::new(0),
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "dir",
        Args {
            expected_mode: fio::MODE_TYPE_DIRECTORY | 0o700,
            time_verifier: Box::new(0),
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
            expected_mode: fio::MODE_TYPE_FILE | 0o600,
            expected_content_size: 64,
            expected_storage_size: 64,
            time_verifier: Box::new(0),
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "meta",
        Args {
            open_mode: fio::MODE_TYPE_DIRECTORY,
            expected_mode: fio::MODE_TYPE_DIRECTORY | 0o700,
            expected_content_size: 75,
            expected_storage_size: 75,
            time_verifier: Box::new(0),
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "meta/dir",
        Args {
            expected_mode: fio::MODE_TYPE_DIRECTORY | 0o700,
            expected_content_size: 75,
            expected_storage_size: 75,
            time_verifier: Box::new(0),
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        root_dir,
        "meta/file",
        Args {
            expected_mode: fio::MODE_TYPE_FILE | 0o600,
            expected_content_size: 9,
            expected_storage_size: 9,
            time_verifier: Box::new(0),
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
    let protocol = node.query().await.context("failed to call query")?;
    if protocol == fio::DIRECTORY_PROTOCOL_NAME.as_bytes() {
        Ok(())
    } else {
        Err(anyhow!("wrong protocol returned: {:?}", std::str::from_utf8(&protocol)))
    }
}

async fn assert_describe_file(package_root: &fio::DirectoryProxy, path: &str) {
    for flag in [fio::OpenFlags::RIGHT_READABLE, fio::OpenFlags::NODE_REFERENCE] {
        let node = fuchsia_fs::directory::open_node(package_root, path, flag, 0).await.unwrap();
        if let Err(e) = verify_describe_content_file(node, flag).await {
            panic!(
                "failed to verify describe. path: {:?}, flag: {:?}, \
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
    let protocol = node.query().await.context("failed to call query")?;
    if flag.intersects(fio::OpenFlags::NODE_REFERENCE) {
        if protocol == fio::NODE_PROTOCOL_NAME.as_bytes() {
            Ok(())
        } else {
            Err(anyhow!("wrong protocol returned: {:?}", std::str::from_utf8(&protocol)))
        }
    } else {
        if protocol == fio::FILE_PROTOCOL_NAME.as_bytes() {
            let fio::FileInfo { observer, .. } = fio::FileProxy::new(node.into_channel().unwrap())
                .describe2()
                .await
                .context("failed to call describe")?;
            match observer {
                Some(observer) => {
                    let _: zx::Signals = observer
                        .wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST)
                        .context("FILE_SIGNAL_READABLE not set")?;
                    Ok(())
                }
                None => Err(anyhow!("expected observer to be set")),
            }
        } else {
            Err(anyhow!("wrong protocol returned: {:?}", std::str::from_utf8(&protocol)))
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
                "failed to verify describe. path: {:?}, flag: {:?}, \
                    mode: fio::MODE_TYPE_FILE, error: {:#}",
                path, flag, e
            );
        }
    }
}

async fn verify_describe_meta_file_success(node: fio::NodeProxy) -> Result<(), Error> {
    let protocol = node.query().await.context("failed to call describe")?;
    if protocol == fio::FILE_PROTOCOL_NAME.as_bytes() {
        Ok(())
    } else {
        Err(anyhow!("wrong protocol returned: {:?}", std::str::from_utf8(&protocol)))
    }
}

#[fuchsia::test]
async fn get_flags() {
    for source in dirs_to_test().await {
        get_flags_per_package_source(source).await
    }
}

async fn get_flags_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    assert_get_flags_root_dir(&root_dir, ".").await;
    assert_get_flags_content_dir(&root_dir, "dir").await;
    assert_get_flags_content_file(&root_dir, "file").await;
    assert_get_flags_meta(&root_dir, "meta").await;
    assert_get_flags_meta_file(&root_dir, "meta/file").await;
    assert_get_flags_meta_dir(&root_dir, "meta/dir").await;
}

/// Opens a file and verifies the result of GetFlags().
async fn assert_get_flags(root_dir: &fio::DirectoryProxy, path: &str, open_flags: fio::OpenFlags) {
    let node = fuchsia_fs::directory::open_node(root_dir, path, open_flags, 0).await.unwrap();

    // The flags returned by GetFlags() do NOT always match the flags the node is opened with
    // because GetFlags only returns those flags that are meaningful after the Open call (instead
    // of only during).
    // C++ VFS https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/storage/vfs/cpp/file_connection.cc;l=124;drc=6865fdce358ab86d9d2deae5d4693786fbe88d45
    // Rust VFS https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/storage/vfs/rust/src/common.rs;l=21;drc=8cea889022e03a335c73905f5b0aa80937165c03
    let mask = fio::OpenFlags::APPEND
        | fio::OpenFlags::NODE_REFERENCE
        | fio::OpenFlags::RIGHT_READABLE
        | fio::OpenFlags::RIGHT_WRITABLE
        | fio::OpenFlags::RIGHT_EXECUTABLE;
    let expected_flags = open_flags & (mask);

    // Verify GetFlags() produces the expected result.
    let (status, flags) = node.get_flags().await.unwrap();
    let () = zx::Status::ok(status).unwrap();
    assert_eq!(flags, expected_flags);
}

async fn assert_get_flags_root_dir(root_dir: &fio::DirectoryProxy, path: &str) {
    for open_flag in [
        fio::OpenFlags::empty(),
        fio::OpenFlags::RIGHT_READABLE,
        fio::OpenFlags::DIRECTORY,
        fio::OpenFlags::NODE_REFERENCE,
        fio::OpenFlags::DESCRIBE,
    ] {
        assert_get_flags(root_dir, path, open_flag).await;
    }
}

async fn assert_get_flags_content_dir(root_dir: &fio::DirectoryProxy, path: &str) {
    for open_flag in [
        fio::OpenFlags::empty(),
        fio::OpenFlags::RIGHT_READABLE,
        fio::OpenFlags::DIRECTORY,
        fio::OpenFlags::NODE_REFERENCE,
        fio::OpenFlags::DESCRIBE,
    ] {
        assert_get_flags(root_dir, path, open_flag).await;
    }
}

async fn assert_get_flags_content_file(root_dir: &fio::DirectoryProxy, path: &str) {
    for open_flag in [
        fio::OpenFlags::RIGHT_READABLE,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::POSIX_EXECUTABLE,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::NOT_DIRECTORY,
        fio::OpenFlags::RIGHT_EXECUTABLE,
        fio::OpenFlags::RIGHT_EXECUTABLE | fio::OpenFlags::DESCRIBE,
        fio::OpenFlags::RIGHT_EXECUTABLE | fio::OpenFlags::POSIX_EXECUTABLE,
        fio::OpenFlags::RIGHT_EXECUTABLE | fio::OpenFlags::NOT_DIRECTORY,
    ] {
        assert_get_flags(root_dir, path, open_flag).await;
    }
}

async fn assert_get_flags_meta(root_dir: &fio::DirectoryProxy, path: &str) {
    for open_flag in [
        fio::OpenFlags::empty(),
        fio::OpenFlags::RIGHT_READABLE,
        fio::OpenFlags::DIRECTORY,
        fio::OpenFlags::NODE_REFERENCE,
        fio::OpenFlags::DESCRIBE,
        fio::OpenFlags::POSIX_EXECUTABLE,
        fio::OpenFlags::NOT_DIRECTORY,
    ] {
        assert_get_flags(root_dir, path, open_flag).await;
    }
}

async fn assert_get_flags_meta_file(root_dir: &fio::DirectoryProxy, path: &str) {
    for open_flag in [
        fio::OpenFlags::empty(),
        fio::OpenFlags::RIGHT_READABLE,
        fio::OpenFlags::NODE_REFERENCE,
        fio::OpenFlags::DESCRIBE,
        fio::OpenFlags::POSIX_EXECUTABLE,
        fio::OpenFlags::NOT_DIRECTORY,
    ] {
        assert_get_flags(root_dir, path, open_flag).await;
    }
}

async fn assert_get_flags_meta_dir(root_dir: &fio::DirectoryProxy, path: &str) {
    for open_flag in [
        fio::OpenFlags::empty(),
        fio::OpenFlags::RIGHT_READABLE,
        fio::OpenFlags::NODE_REFERENCE,
        fio::OpenFlags::DESCRIBE,
        fio::OpenFlags::POSIX_EXECUTABLE,
        fio::OpenFlags::DIRECTORY,
    ] {
        assert_get_flags(root_dir, path, open_flag).await;
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
    do_set_flags(package_root, "meta", fio::MODE_TYPE_FILE, fio::OpenFlags::empty())
        .await
        .assert_ok();
    do_set_flags(package_root, "meta", fio::MODE_TYPE_FILE, fio::OpenFlags::APPEND)
        .await
        .assert_ok();
    do_set_flags(package_root, "meta/file", fio::MODE_TYPE_FILE, fio::OpenFlags::empty())
        .await
        .assert_ok();
    do_set_flags(package_root, "meta/file", fio::MODE_TYPE_FILE, fio::OpenFlags::APPEND)
        .await
        .assert_ok();
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
