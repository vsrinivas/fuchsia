// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::dirs_to_test,
    anyhow::{anyhow, Context as _, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{
        DirectoryProxy, NodeInfo, NodeMarker, NodeProxy, MODE_TYPE_BLOCK_DEVICE,
        MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, MODE_TYPE_SERVICE, MODE_TYPE_SOCKET, OPEN_FLAG_APPEND,
        OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_NO_REMOTE, OPEN_FLAG_POSIX,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    futures::future::Future,
    itertools::Itertools as _,
    std::{
        clone::Clone,
        collections::HashSet,
        iter::{FromIterator, IntoIterator},
    },
};

#[fuchsia::test]
async fn open() {
    for dir in dirs_to_test().await {
        open_per_package_source(dir).await
    }
}

async fn open_per_package_source(root_dir: DirectoryProxy) {
    // Testing dimensions:
    //   1. Receiver of the open call: /, meta/, subdir below meta/, subdir not below meta/
    //   2. Type of node the path points at: self, meta/, subdir below meta/, file below meta/,
    //      subdir not below meta/, file not below meta/ (not all receivers can open every type of
    //      target)
    //   3. Whether the path being opened is segmented
    // The flags and modes are handled by the helper functions.
    assert_open_root_directory(&root_dir, ".", ".").await;
    assert_open_content_directory(&root_dir, ".", "dir").await;
    assert_open_content_directory(&root_dir, ".", "dir/dir").await;
    assert_open_content_file(&root_dir, ".", "file").await;
    assert_open_content_file(&root_dir, ".", "dir/file").await;
    assert_open_meta_as_directory(&root_dir, ".", "meta").await;
    assert_open_meta_as_file(&root_dir, ".", "meta").await;
    assert_open_meta_subdirectory(&root_dir, ".", "meta/dir").await;
    assert_open_meta_file(&root_dir, ".", "meta/file").await;

    // Self-opening "meta" does not trigger the file/dir duality.
    assert_open_meta_subdirectory(&root_dir, "meta", ".").await;
    assert_open_meta_subdirectory(&root_dir, "meta", "dir").await;
    assert_open_meta_subdirectory(&root_dir, "meta", "dir/dir").await;
    assert_open_meta_file(&root_dir, "meta", "file").await;
    assert_open_meta_file(&root_dir, "meta", "dir/file").await;

    assert_open_meta_subdirectory(&root_dir, "meta/dir", ".").await;
    assert_open_meta_subdirectory(&root_dir, "meta/dir", "dir").await;
    assert_open_meta_subdirectory(&root_dir, "meta/dir", "dir/dir").await;
    assert_open_meta_file(&root_dir, "meta/dir", "file").await;
    assert_open_meta_file(&root_dir, "meta/dir", "dir/file").await;

    assert_open_content_directory(&root_dir, "dir", ".").await;
    assert_open_content_directory(&root_dir, "dir", "dir").await;
    assert_open_content_directory(&root_dir, "dir", "dir/dir").await;
    assert_open_content_file(&root_dir, "dir", "file").await;
    assert_open_content_file(&root_dir, "dir", "dir/file").await;
}

const ALL_FLAGS: [u32; 15] = [
    0,
    OPEN_RIGHT_READABLE,
    OPEN_RIGHT_WRITABLE,
    OPEN_RIGHT_ADMIN,
    OPEN_RIGHT_EXECUTABLE,
    OPEN_FLAG_CREATE,
    OPEN_FLAG_CREATE_IF_ABSENT,
    OPEN_FLAG_TRUNCATE,
    OPEN_FLAG_DIRECTORY,
    OPEN_FLAG_APPEND,
    OPEN_FLAG_NO_REMOTE,
    OPEN_FLAG_NODE_REFERENCE,
    OPEN_FLAG_DESCRIBE,
    OPEN_FLAG_POSIX,
    OPEN_FLAG_NOT_DIRECTORY,
];

const ALL_MODES: [u32; 6] = [
    0,
    MODE_TYPE_DIRECTORY,
    MODE_TYPE_BLOCK_DEVICE,
    MODE_TYPE_FILE,
    MODE_TYPE_SOCKET,
    MODE_TYPE_SERVICE,
];

async fn assert_open_root_directory(
    package_root: &DirectoryProxy,
    parent_path: &str,
    child_base_path: &str,
) {
    assert_open_success(
        package_root,
        parent_path,
        product(ALL_FLAGS, ALL_MODES),
        generate_valid_paths(child_base_path),
        verify_directory_opened,
    )
    .await
    // There is no combination of flags and modes that causes opening the root directory to fail.
}

fn product<I, J>(xs: I, ys: J) -> impl Iterator<Item = (u32, u32)>
where
    I: IntoIterator<Item = u32>,
    J: IntoIterator<Item = u32>,
    <J as IntoIterator>::IntoIter: std::clone::Clone,
{
    xs.into_iter().cartesian_product(ys)
}

async fn assert_open_success<V, Fut>(
    package_root: &DirectoryProxy,
    parent_path: &str,
    allowed_flags_and_modes: impl Iterator<Item = (u32, u32)>,
    child_paths: Vec<String>,
    verifier: V,
) where
    V: Fn(NodeProxy, u32) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let parent = io_util::directory::open_directory(package_root, parent_path, 0)
        .await
        .expect("open parent directory");
    for (flag, mode) in allowed_flags_and_modes {
        for path in &child_paths {
            let node = open_node(&parent, flag, mode, path);
            if let Err(e) = verifier(node, flag).await {
                panic!(
                    "failed to verify open. parent: {:?}, child: {:?}, flag: {:#x}, \
                       mode: {:#x}, error: {:#}",
                    parent_path, path, flag, mode, e
                );
            }
        }
    }
}

async fn assert_open_content_directory(
    package_root: &DirectoryProxy,
    parent_path: &str,
    child_base_path: &str,
) {
    let success_flags = [
        0,
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_ADMIN,
        OPEN_RIGHT_EXECUTABLE,
        OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NO_REMOTE,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
        OPEN_FLAG_NOT_DIRECTORY,
    ];
    let fail_flags = subtract(ALL_FLAGS, success_flags);

    assert_open_success(
        package_root,
        parent_path,
        product(success_flags, ALL_MODES),
        generate_valid_paths(child_base_path),
        verify_directory_opened,
    )
    .await;

    assert_open_flag_and_mode_failure(
        package_root,
        parent_path,
        product(fail_flags, ALL_MODES),
        generate_valid_paths(child_base_path),
        verify_open_failed,
    )
    .await;
}

fn subtract<I, J>(minuend: I, subtrahend: J) -> HashSet<u32>
where
    I: IntoIterator<Item = u32>,
    J: IntoIterator<Item = u32>,
{
    let mut minuend = HashSet::from_iter(minuend);
    for i in subtrahend {
        minuend.remove(&i);
    }
    minuend
}

async fn assert_open_flag_and_mode_failure<V, Fut>(
    package_root: &DirectoryProxy,
    parent_path: &str,
    disallowed_flags_and_modes: impl Iterator<Item = (u32, u32)>,
    child_paths: Vec<String>,
    verifier: V,
) where
    V: Fn(NodeProxy) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let parent = io_util::directory::open_directory(package_root, parent_path, 0)
        .await
        .expect("open parent directory");
    for (flag, mode) in disallowed_flags_and_modes {
        for path in &child_paths {
            let node = open_node(&parent, flag, mode, path);
            if let Err(e) = verifier(node).await {
                panic!(
                    "failed to verify open failed. parent: {:?}, child: {:?}, flag: {:#x}, \
                       mode: {:#x}, error: {:#}",
                    parent_path, path, flag, mode, e
                );
            }
        }
    }
}

async fn assert_open_content_file(
    package_root: &DirectoryProxy,
    parent_path: &str,
    child_base_path: &str,
) {
    let success_flags = [
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_READABLE | OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_RIGHT_READABLE | OPEN_FLAG_NO_REMOTE,
        OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX,
        OPEN_RIGHT_READABLE | OPEN_FLAG_NOT_DIRECTORY,
        OPEN_RIGHT_EXECUTABLE,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_NO_REMOTE,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_POSIX,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_NOT_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE,
    ];
    let success_modes =
        [0, MODE_TYPE_BLOCK_DEVICE, MODE_TYPE_FILE, MODE_TYPE_SOCKET, MODE_TYPE_SERVICE];
    let success_flags_and_modes = product(success_flags, success_modes);

    let fail_flags_and_modes = product(
        [
            0,
            OPEN_RIGHT_WRITABLE,
            OPEN_RIGHT_ADMIN,
            OPEN_FLAG_CREATE,
            OPEN_FLAG_CREATE_IF_ABSENT,
            OPEN_FLAG_TRUNCATE,
            OPEN_FLAG_DIRECTORY,
            OPEN_FLAG_APPEND,
            OPEN_FLAG_NO_REMOTE,
            OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_POSIX,
            OPEN_FLAG_NOT_DIRECTORY,
        ],
        success_modes,
    )
    .chain(product(success_flags, [MODE_TYPE_DIRECTORY]));

    assert_open_success(
        package_root,
        parent_path,
        success_flags_and_modes,
        generate_valid_paths(child_base_path),
        verify_content_file_opened,
    )
    .await;

    assert_open_flag_and_mode_failure(
        package_root,
        parent_path,
        fail_flags_and_modes,
        generate_valid_paths(child_base_path),
        verify_open_failed,
    )
    .await;
}

async fn assert_open_meta_as_directory(
    package_root: &DirectoryProxy,
    parent_path: &str,
    child_base_path: &str,
) {
    let mut base_success_flags = vec![
        0,
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_ADMIN,
        OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NO_REMOTE,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
        OPEN_FLAG_NOT_DIRECTORY,
    ];
    // pkgfs allows meta (file or directory) to be opened EXECUTABLE if opened directly from the
    // package root, but not if "re-opened" from itself, i.e. opening "." from the meta dir.
    if parent_path == "." {
        base_success_flags.push(OPEN_RIGHT_EXECUTABLE);
    }

    // To open "meta" as a directory:
    //  1. mode cannot be MODE_TYPE_FILE
    //  2. and at least one of the following must be true:
    //    a. MODE_TYPE_DIRECTORY is set
    //    b. OPEN_FLAG_DIRECTORY is set
    //    c. OPEN_FLAG_NODE_REFERENCE is set
    let success_flags_and_modes = product(base_success_flags.clone(), [MODE_TYPE_DIRECTORY])
        .chain(product(
            base_success_flags.clone().into_iter().map(|f| f | OPEN_FLAG_DIRECTORY),
            [0, MODE_TYPE_DIRECTORY, MODE_TYPE_BLOCK_DEVICE, MODE_TYPE_SOCKET, MODE_TYPE_SERVICE],
        ))
        .chain(product(
            base_success_flags.clone().into_iter().map(|f| f | OPEN_FLAG_NODE_REFERENCE),
            [0, MODE_TYPE_DIRECTORY, MODE_TYPE_BLOCK_DEVICE, MODE_TYPE_SOCKET, MODE_TYPE_SERVICE],
        ));

    let fail_flags = subtract(ALL_FLAGS, base_success_flags);
    let fail_flags_and_modes = product(fail_flags, ALL_MODES);

    assert_open_success(
        package_root,
        parent_path,
        success_flags_and_modes,
        generate_valid_paths(child_base_path),
        verify_directory_opened,
    )
    .await;

    assert_open_flag_and_mode_failure(
        package_root,
        parent_path,
        fail_flags_and_modes,
        generate_valid_paths(child_base_path),
        verify_open_failed,
    )
    .await
}

async fn assert_open_meta_as_file(
    package_root: &DirectoryProxy,
    parent_path: &str,
    child_base_path: &str,
) {
    // To open "meta" as a file at least one of the following must be true:
    //  1. mode is MODE_TYPE_FILE
    //  2. none of the following are true:
    //    a. MODE_TYPE_DIRECTORY is set
    //    b. OPEN_FLAG_DIRECTORY is set
    //    c. OPEN_FLAG_NODE_REFERENCE is set
    let success_flags_and_modes = product(
        [
            0,
            OPEN_RIGHT_READABLE,
            OPEN_RIGHT_ADMIN,
            OPEN_RIGHT_EXECUTABLE,
            OPEN_FLAG_CREATE_IF_ABSENT,
            OPEN_FLAG_DIRECTORY,
            OPEN_FLAG_NO_REMOTE,
            OPEN_FLAG_NODE_REFERENCE,
            OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_POSIX,
            OPEN_FLAG_NOT_DIRECTORY,
        ],
        [MODE_TYPE_FILE],
    )
    .chain(product(
        [
            OPEN_RIGHT_READABLE,
            OPEN_RIGHT_ADMIN,
            OPEN_RIGHT_EXECUTABLE,
            OPEN_FLAG_CREATE_IF_ABSENT,
            OPEN_FLAG_NO_REMOTE,
            OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_POSIX,
            OPEN_FLAG_NOT_DIRECTORY,
        ],
        [0, MODE_TYPE_BLOCK_DEVICE, MODE_TYPE_SOCKET, MODE_TYPE_SERVICE],
    ));

    assert_open_success(
        package_root,
        parent_path,
        success_flags_and_modes,
        generate_valid_paths(child_base_path),
        verify_meta_as_file_opened,
    )
    .await

    // flag and mode failures are checked by assert_open_meta_as_directory
}

async fn assert_open_meta_subdirectory(
    package_root: &DirectoryProxy,
    parent_path: &str,
    child_base_path: &str,
) {
    let success_flags = [
        0,
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_ADMIN,
        OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NO_REMOTE,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
        OPEN_FLAG_NOT_DIRECTORY,
    ];

    let fail_flags = subtract(ALL_FLAGS, success_flags);

    assert_open_success(
        package_root,
        parent_path,
        product(success_flags, ALL_MODES),
        generate_valid_paths(child_base_path),
        verify_directory_opened,
    )
    .await;

    assert_open_flag_and_mode_failure(
        package_root,
        parent_path,
        product(fail_flags, ALL_MODES),
        generate_valid_paths(child_base_path),
        verify_open_failed,
    )
    .await
}

async fn assert_open_meta_file(
    package_root: &DirectoryProxy,
    parent_path: &str,
    child_base_path: &str,
) {
    let success_flags = [
        0,
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_ADMIN,
        OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NO_REMOTE,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
        OPEN_FLAG_NOT_DIRECTORY,
    ];
    let fail_flags = subtract(ALL_FLAGS, success_flags);

    assert_open_success(
        package_root,
        parent_path,
        product(success_flags, ALL_MODES),
        generate_valid_paths(child_base_path),
        verify_meta_as_file_opened,
    )
    .await;

    assert_open_flag_and_mode_failure(
        package_root,
        parent_path,
        product(fail_flags, ALL_MODES),
        generate_valid_paths(child_base_path),
        verify_open_failed,
    )
    .await;
}

fn open_node(parent: &DirectoryProxy, flags: u32, mode: u32, path: &str) -> NodeProxy {
    let (node, server_end) = create_proxy::<NodeMarker>().expect("create_proxy");
    parent.open(flags, mode, path, server_end).expect("open node");
    node
}

fn generate_valid_paths(base: &str) -> Vec<String> {
    let mut paths = vec![
        base.to_string(),
        format!("./{}", base),
        format!("{}/", base),
        format!("{}/.", base),
        format!("/{}", base), // pkg-dir should reject this
    ];
    // pkg-dir should reject these
    if base.contains("/") {
        paths.push(base.replace("/", "//"));
        paths.push(base.replace("/", "/to-be-removed/../"));
        paths.push(base.replace("/", "/./"));
    }
    paths
}

async fn verify_directory_opened(node: NodeProxy, _flag: u32) -> Result<(), Error> {
    match node.describe().await {
        Ok(NodeInfo::Directory(directory_object)) => {
            assert_eq!(directory_object, fidl_fuchsia_io::DirectoryObject);
            Ok(())
        }
        Ok(other) => Err(anyhow!("wrong node type returned: {:?}", other)),
        Err(e) => Err(e).context("failed to call describe"),
    }
}

async fn verify_content_file_opened(node: NodeProxy, flag: u32) -> Result<(), Error> {
    if flag & OPEN_FLAG_NODE_REFERENCE != 0 {
        match node.describe().await {
            Ok(NodeInfo::Service(_)) => Ok(()),
            Ok(other) => Err(anyhow!("wrong node type returned: {:?}", other)),
            Err(e) => Err(e).context("failed to call describe"),
        }
    } else {
        match node.describe().await {
            Ok(NodeInfo::File(_)) => Ok(()),
            Ok(other) => Err(anyhow!("wrong node type returned: {:?}", other)),
            Err(e) => Err(e).context("failed to call describe"),
        }
    }
}

async fn verify_meta_as_file_opened(node: NodeProxy, _flag: u32) -> Result<(), Error> {
    match node.describe().await {
        Ok(NodeInfo::File(_)) => Ok(()),
        Ok(other) => Err(anyhow!("wrong node type returned: {:?}", other)),
        Err(e) => Err(e).context("failed to call describe"),
    }
}

async fn verify_open_failed(node: NodeProxy) -> Result<(), Error> {
    match node.describe().await {
        Ok(node_info) => Err(anyhow!("node should be closed: {:?}", node_info)),
        Err(fidl::Error::ClientChannelClosed { status, service_name: _ })
            if status == zx::Status::PEER_CLOSED =>
        {
            Ok(())
        }
        Err(e) => Err(e).context("failed with unexpected error"),
    }
}
