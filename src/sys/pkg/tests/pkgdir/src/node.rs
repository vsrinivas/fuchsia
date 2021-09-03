// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{dirs_to_test, PackageSource},
    anyhow::{anyhow, Context as _, Error},
    fidl::AsHandleRef,
    fidl_fuchsia_io::{
        DirectoryProxy, FileObject, NodeAttributes, NodeInfo, NodeProxy, Service,
        MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, OPEN_FLAG_NODE_REFERENCE, OPEN_RIGHT_READABLE,
    },
    fuchsia_zircon as zx,
};

#[fuchsia::test]
async fn get_attr() {
    for source in dirs_to_test().await {
        get_attr_per_package_source(source).await
    }
}

trait U64Verifier {
    fn verify(&self, num: u64);
}

impl U64Verifier for u64 {
    fn verify(&self, num: u64) {
        assert_eq!(num, *self)
    }
}

struct AnyU64;
impl U64Verifier for AnyU64 {
    fn verify(&self, _num: u64) {}
}

struct PositiveU64;
impl U64Verifier for PositiveU64 {
    fn verify(&self, num: u64) {
        assert!(num > 0);
    }
}

async fn get_attr_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    struct Args {
        open_flags: u32,
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
                open_flags: 0,
                open_mode: 0,
                expected_mode: 0,
                id_verifier: Box::new(1),
                expected_content_size: 0,
                expected_storage_size: 0,
                time_verifier: Box::new(PositiveU64),
            }
        }
    }

    async fn verify_get_attrs(root_dir: &DirectoryProxy, path: &str, args: Args) {
        let node = io_util::directory::open_node(root_dir, path, args.open_flags, args.open_mode)
            .await
            .unwrap();
        let (status, attrs) = node.get_attr().await.unwrap();
        zx::Status::ok(status).unwrap();
        assert_eq!(attrs.mode, args.expected_mode);
        args.id_verifier.verify(attrs.id);
        assert_eq!(attrs.content_size, args.expected_content_size);
        assert_eq!(attrs.storage_size, args.expected_storage_size);
        assert_eq!(attrs.link_count, 1);
        args.time_verifier.verify(attrs.creation_time);
        args.time_verifier.verify(attrs.modification_time);
    }

    verify_get_attrs(
        &root_dir,
        ".",
        Args { expected_mode: MODE_TYPE_DIRECTORY | 0o755, ..Default::default() },
    )
    .await;
    verify_get_attrs(
        &root_dir,
        "dir",
        Args { expected_mode: MODE_TYPE_DIRECTORY | 0o755, ..Default::default() },
    )
    .await;
    verify_get_attrs(
        &root_dir,
        "file",
        Args {
            open_flags: OPEN_RIGHT_READABLE,
            expected_mode: MODE_TYPE_FILE | 0o500,
            id_verifier: Box::new(AnyU64),
            expected_content_size: 4,
            expected_storage_size: 8192,
            time_verifier: Box::new(0),
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        &root_dir,
        "meta",
        Args {
            open_mode: MODE_TYPE_FILE,
            expected_mode: MODE_TYPE_FILE | 0o644,
            expected_content_size: 64,
            expected_storage_size: 64,
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        &root_dir,
        "meta",
        Args {
            open_mode: MODE_TYPE_DIRECTORY,
            expected_mode: MODE_TYPE_DIRECTORY | 0o755,
            expected_content_size: 69,
            expected_storage_size: 69,
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        &root_dir,
        "meta/dir",
        Args {
            expected_mode: MODE_TYPE_DIRECTORY | 0o755,
            expected_content_size: 69,
            expected_storage_size: 69,
            ..Default::default()
        },
    )
    .await;
    verify_get_attrs(
        &root_dir,
        "meta/file",
        Args {
            expected_mode: MODE_TYPE_FILE | 0o644,
            expected_content_size: 9,
            expected_storage_size: 9,
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
    async fn verify_close(root_dir: &DirectoryProxy, path: &str, mode: u32) {
        let node =
            io_util::directory::open_node(root_dir, path, OPEN_RIGHT_READABLE, mode).await.unwrap();

        let status = node.close().await.unwrap();
        let () = zx::Status::ok(status).unwrap();

        matches::assert_matches!(
            node.close().await,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::PEER_CLOSED, .. })
        );
    }

    verify_close(&root_dir, ".", MODE_TYPE_DIRECTORY).await;
    verify_close(&root_dir, "dir", MODE_TYPE_DIRECTORY).await;
    verify_close(&root_dir, "meta", MODE_TYPE_DIRECTORY).await;
    verify_close(&root_dir, "meta/dir", MODE_TYPE_DIRECTORY).await;

    verify_close(&root_dir, "file", MODE_TYPE_FILE).await;
    verify_close(&root_dir, "meta/file", MODE_TYPE_FILE).await;
    verify_close(&root_dir, "meta", MODE_TYPE_FILE).await;
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

async fn assert_describe_directory(package_root: &DirectoryProxy, path: &str) {
    for flag in [0, OPEN_FLAG_NODE_REFERENCE] {
        let node = io_util::directory::open_node(package_root, path, flag, MODE_TYPE_DIRECTORY)
            .await
            .unwrap();

        if let Err(e) = verify_describe_directory_success(node).await {
            panic!("failed to verify describe. path: {:?}, error: {:#}", path, e);
        }
    }
}

async fn verify_describe_directory_success(node: NodeProxy) -> Result<(), Error> {
    match node.describe().await {
        Ok(NodeInfo::Directory(directory_object)) => {
            assert_eq!(directory_object, fidl_fuchsia_io::DirectoryObject);
            Ok(())
        }
        Ok(other) => return Err(anyhow!("wrong node type returned: {:?}", other)),
        Err(e) => return Err(e).context("failed to call describe"),
    }
}

async fn assert_describe_file(package_root: &DirectoryProxy, path: &str) {
    for flag in [OPEN_RIGHT_READABLE, OPEN_FLAG_NODE_REFERENCE] {
        let node = io_util::directory::open_node(package_root, path, flag, 0).await.unwrap();
        if let Err(e) = verify_describe_content_file(node, flag).await {
            panic!(
                "failed to verify describe. path: {:?}, flag: {:#x}, \
                    mode: {:#x}, error: {:#}",
                path, flag, 0, e
            );
        }
    }
}

async fn verify_describe_content_file(node: NodeProxy, flag: u32) -> Result<(), Error> {
    if flag & OPEN_FLAG_NODE_REFERENCE != 0 {
        match node.describe().await {
            Ok(NodeInfo::Service(Service {})) => return Ok(()),
            Ok(other) => return Err(anyhow!("wrong node type returned: {:?}", other)),
            Err(e) => return Err(e).context("failed to call describe"),
        }
    } else {
        match node.describe().await {
            Ok(NodeInfo::File(FileObject { event: Some(event), stream: None })) => {
                match event.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST) {
                    Ok(_) => return Ok(()),
                    Err(_) => return Err(anyhow!("FILE_SIGNAL_READABLE not set")),
                }
            }
            Ok(other) => return Err(anyhow!("wrong node type returned: {:?}", other)),
            Err(e) => return Err(e).context("failed to call describe"),
        }
    }
}

async fn assert_describe_meta_file(package_root: &DirectoryProxy, path: &str) {
    for flag in [0, OPEN_FLAG_NODE_REFERENCE] {
        let node =
            io_util::directory::open_node(package_root, path, flag, MODE_TYPE_FILE).await.unwrap();
        if let Err(e) = verify_describe_meta_file_success(node).await {
            panic!(
                "failed to verify describe. path: {:?}, flag: {:#x}, \
                    mode: MODE_TYPE_FILE, error: {:#}",
                path, flag, e
            );
        }
    }
}

async fn verify_describe_meta_file_success(node: NodeProxy) -> Result<(), Error> {
    match node.describe().await {
        Ok(NodeInfo::File(_)) => return Ok(()),
        Ok(other) => return Err(anyhow!("wrong node type returned: {:?}", other)),
        Err(e) => return Err(e).context("failed to call describe"),
    }
}

#[fuchsia::test]
async fn node_set_flags() {
    for source in dirs_to_test().await {
        node_set_flags_per_package_source(source).await
    }
}

async fn node_set_flags_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    assert_node_set_flags(&root_dir, ".", MODE_TYPE_DIRECTORY).await;
    assert_node_set_flags(&root_dir, "meta", MODE_TYPE_DIRECTORY).await;
    assert_node_set_flags(&root_dir, "meta/dir", MODE_TYPE_DIRECTORY).await;
    assert_node_set_flags(&root_dir, "dir", MODE_TYPE_DIRECTORY).await;
    assert_node_set_flags(&root_dir, "meta", MODE_TYPE_FILE).await;
    assert_node_set_flags(&root_dir, "meta/file", MODE_TYPE_FILE).await;
}

async fn assert_node_set_flags(package_root: &DirectoryProxy, path: &str, mode: u32) {
    let node =
        io_util::directory::open_node(package_root, path, OPEN_RIGHT_READABLE, mode).await.unwrap();

    if let Err(e) = verify_node_set_flag_success(node).await {
        panic!("node_set_flags failed. path: {:?}, error: {:#}", path, e);
    }
}

async fn verify_node_set_flag_success(node: NodeProxy) -> Result<(), Error> {
    match node.node_set_flags(0).await {
        Ok(status) => {
            if zx::Status::from_raw(status) == zx::Status::NOT_SUPPORTED {
                return Ok(());
            }
            return Err(anyhow!("wrong status returned: {:?}", zx::Status::from_raw(status)));
        }
        Err(e) => return Err(e).context("failed to call node_set_flags"),
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
    assert_set_attr(&root_dir, ".", MODE_TYPE_DIRECTORY).await;
    assert_set_attr(&root_dir, "meta", MODE_TYPE_DIRECTORY).await;
    assert_set_attr(&root_dir, "meta/dir", MODE_TYPE_DIRECTORY).await;
    assert_set_attr(&root_dir, "dir", MODE_TYPE_DIRECTORY).await;
    assert_set_attr(&root_dir, "meta", MODE_TYPE_FILE).await;
    assert_set_attr(&root_dir, "meta/file", MODE_TYPE_FILE).await;
}

async fn assert_set_attr(package_root: &DirectoryProxy, path: &str, mode: u32) {
    let node = io_util::directory::open_node(package_root, path, 0, mode).await.unwrap();

    if let Err(e) = verify_set_attr(node).await {
        panic!("set_attr failed. path: {:?}, error: {:#}", path, e);
    }
}

async fn verify_set_attr(node: NodeProxy) -> Result<(), Error> {
    let mut node_attr = NodeAttributes {
        mode: 0,
        id: 0,
        content_size: 0,
        storage_size: 0,
        link_count: 0,
        creation_time: 0,
        modification_time: 0,
    };
    match node.set_attr(0, &mut node_attr).await {
        Ok(status) => {
            if zx::Status::from_raw(status) == zx::Status::NOT_SUPPORTED {
                return Ok(());
            }
            return Err(anyhow!("wrong status returned: {:?}", zx::Status::from_raw(status)));
        }
        Err(e) => return Err(e).context("failed to call set_attr"),
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
    assert_sync(&root_dir, ".", MODE_TYPE_DIRECTORY).await;
    assert_sync(&root_dir, "meta", MODE_TYPE_DIRECTORY).await;
    assert_sync(&root_dir, "meta/dir", MODE_TYPE_DIRECTORY).await;
    assert_sync(&root_dir, "dir", MODE_TYPE_DIRECTORY).await;
    assert_sync(&root_dir, "meta", MODE_TYPE_FILE).await;
    assert_sync(&root_dir, "meta/file", MODE_TYPE_FILE).await;
}

async fn assert_sync(package_root: &DirectoryProxy, path: &str, mode: u32) {
    let node = io_util::directory::open_node(package_root, path, 0, mode).await.unwrap();

    if let Err(e) = verify_sync(node).await {
        panic!("sync failed. path: {:?}, error: {:#}", path, e);
    }
}

async fn verify_sync(node: NodeProxy) -> Result<(), Error> {
    match node.sync().await {
        Ok(status) => {
            if zx::Status::from_raw(status) == zx::Status::NOT_SUPPORTED {
                return Ok(());
            }
            return Err(anyhow!("wrong status returned: {:?}", zx::Status::from_raw(status)));
        }
        Err(e) => return Err(e).context("failed to call sync"),
    }
}
