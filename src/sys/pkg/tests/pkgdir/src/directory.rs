// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{dirs_to_test, repeat_by_n},
    anyhow::{anyhow, Context as _, Error},
    fidl::{endpoints::create_proxy, AsHandleRef},
    fidl_fuchsia_io::{
        DirectoryObject, DirectoryProxy, FileObject, NodeEvent, NodeInfo, NodeMarker, NodeProxy,
        Service, CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_BLOCK_DEVICE, MODE_TYPE_DIRECTORY,
        MODE_TYPE_FILE, MODE_TYPE_SERVICE, MODE_TYPE_SOCKET, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_NO_REMOTE, OPEN_FLAG_POSIX,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    files_async::{DirEntry, DirentKind},
    fuchsia_zircon as zx,
    futures::{future::Future, StreamExt},
    io_util::directory::open_directory,
    itertools::Itertools as _,
    std::{
        clone::Clone,
        collections::HashSet,
        convert::TryInto,
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

async fn verify_directory_opened(node: NodeProxy, flag: u32) -> Result<(), Error> {
    match node.describe().await {
        Ok(NodeInfo::Directory(directory_object)) => {
            assert_eq!(directory_object, fidl_fuchsia_io::DirectoryObject);
            ()
        }
        Ok(other) => return Err(anyhow!("wrong node type returned: {:?}", other)),
        Err(e) => return Err(e).context("failed to call describe"),
    }

    if flag & OPEN_FLAG_DESCRIBE != 0 {
        match node.take_event_stream().next().await {
            Some(Ok(NodeEvent::OnOpen_ { s, info: Some(boxed) })) => {
                assert_eq!(zx::Status::from_raw(s), zx::Status::OK);
                assert_eq!(*boxed, NodeInfo::Directory(DirectoryObject {}));
                return Ok(());
            }
            Some(Ok(other)) => return Err(anyhow!("wrong node type returned: {:?}", other)),
            Some(Err(e)) => return Err(e).context("failed to call onopen"),
            None => return Err(anyhow!("no events!")),
        }
    };
    Ok(())
}

async fn verify_content_file_opened(node: NodeProxy, flag: u32) -> Result<(), Error> {
    if flag & OPEN_FLAG_NODE_REFERENCE != 0 {
        match node.describe().await {
            Ok(NodeInfo::Service(_)) => (),
            Ok(other) => return Err(anyhow!("wrong node type returned: {:?}", other)),
            Err(e) => return Err(e).context("failed to call describe"),
        }
    } else {
        match node.describe().await {
            Ok(NodeInfo::File(_)) => (),
            Ok(other) => return Err(anyhow!("wrong node type returned: {:?}", other)),
            Err(e) => return Err(e).context("failed to call describe"),
        }
    }
    if flag & OPEN_FLAG_DESCRIBE != 0 {
        if flag & OPEN_FLAG_NODE_REFERENCE != 0 {
            match node.take_event_stream().next().await {
                Some(Ok(NodeEvent::OnOpen_ { s, info: Some(boxed) })) => {
                    assert_eq!(zx::Status::from_raw(s), zx::Status::OK);
                    assert_eq!(*boxed, NodeInfo::Service(Service));
                    return Ok(());
                }
                Some(Ok(other)) => {
                    return Err(anyhow!("wrong node type returned: {:?}", other))
                }
                Some(Err(e)) => return Err(e).context("failed to call onopen"),
                None => return Err(anyhow!("no events!")),
            };
        } else {
            match node.take_event_stream().next().await {
                Some(Ok(NodeEvent::OnOpen_ { s, info: Some(boxed) })) => {
                    assert_eq!(zx::Status::from_raw(s), zx::Status::OK);
                    match *boxed {
                        NodeInfo::File(FileObject { event: Some(event), stream: None }) => {
                            match event.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST) {
                                Ok(_) => return Ok(()),
                                Err(_) => return Err(anyhow!("FILE_SIGNAL_READABLE not set")),
                            }
                        }
                        _ => return Err(anyhow!("expected FileObject")),
                    };
                }
                Some(Ok(other)) => {
                    return Err(anyhow!("wrong node type returned: {:?}", other))
                }
                Some(Err(e)) => return Err(e).context("failed to call onopen"),
                None => return Err(anyhow!("no events!")),
            }
        }
    }
    Ok(())
}

async fn verify_meta_as_file_opened(node: NodeProxy, flag: u32) -> Result<(), Error> {
    match node.describe().await {
        Ok(NodeInfo::File(_)) => (),
        Ok(other) => return Err(anyhow!("wrong node type returned: {:?}", other)),
        Err(e) => return Err(e).context("failed to call describe"),
    }

    if flag & OPEN_FLAG_DESCRIBE != 0 {
        match node.take_event_stream().next().await {
            Some(Ok(NodeEvent::OnOpen_ { s, info: Some(boxed) })) => {
                assert_eq!(zx::Status::from_raw(s), zx::Status::OK);
                match *boxed {
                    NodeInfo::File(_) => return Ok(()),
                    _ => return Err(anyhow!("wrong NodeInfo returned")),
                }
            }
            Some(Ok(other)) => return Err(anyhow!("wrong node type returned: {:?}", other)),
            Some(Err(e)) => return Err(e).context("failed to call onopen"),
            None => return Err(anyhow!("no events!")),
        }
    }
    Ok(())
}

async fn verify_open_failed(node: NodeProxy) -> Result<(), Error> {
    match node.describe().await {
        Ok(node_info) => Err(anyhow!("node should be closed: {:?}", node_info)),
        Err(fidl::Error::ClientChannelClosed { status, protocol_name: _ })
            if status == zx::Status::PEER_CLOSED =>
        {
            Ok(())
        }
        Err(e) => Err(e).context("failed with unexpected error"),
    }
}

// TODO(fxbug.dev/81447) enhance Clone tests.
#[fuchsia::test]
async fn clone() {
    for dir in dirs_to_test().await {
        clone_per_package_source(dir).await
    }
}

async fn clone_per_package_source(root_dir: DirectoryProxy) {
    for flag in [
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
        OPEN_RIGHT_ADMIN,
        OPEN_RIGHT_EXECUTABLE,
        OPEN_FLAG_APPEND,
        OPEN_FLAG_NO_REMOTE,
        OPEN_FLAG_DESCRIBE,
        CLONE_FLAG_SAME_RIGHTS,
    ] {
        assert_clone_directory_overflow(
            &root_dir,
            ".",
            flag,
            vec![
                DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
                DirEntry {
                    name: "dir_overflow_readdirents".to_string(),
                    kind: DirentKind::Directory,
                },
                DirEntry { name: "exceeds_max_buf".to_string(), kind: DirentKind::File },
                DirEntry { name: "file".to_string(), kind: DirentKind::File },
                DirEntry { name: "meta".to_string(), kind: DirentKind::Directory },
            ],
        )
        .await;
        assert_clone_directory_no_overflow(
            &root_dir,
            "dir",
            flag,
            vec![
                DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
                DirEntry { name: "file".to_string(), kind: DirentKind::File },
            ],
        )
        .await;
        assert_clone_directory_overflow(
            &root_dir,
            "meta",
            flag,
            vec![
                DirEntry { name: "contents".to_string(), kind: DirentKind::File },
                DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
                DirEntry {
                    name: "dir_overflow_readdirents".to_string(),
                    kind: DirentKind::Directory,
                },
                DirEntry { name: "exceeds_max_buf".to_string(), kind: DirentKind::File },
                DirEntry { name: "file".to_string(), kind: DirentKind::File },
                DirEntry { name: "package".to_string(), kind: DirentKind::File },
            ],
        )
        .await;
        assert_clone_directory_no_overflow(
            &root_dir,
            "meta/dir",
            flag,
            vec![
                DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
                DirEntry { name: "file".to_string(), kind: DirentKind::File },
            ],
        )
        .await;
    }
}

async fn assert_clone_directory_no_overflow(
    package_root: &DirectoryProxy,
    path: &str,
    flags: u32,
    expected_dirents: Vec<DirEntry>,
) {
    let parent = open_directory(package_root, path, 0).await.expect("open parent directory");
    let (clone, server_end) =
        create_proxy::<fidl_fuchsia_io::DirectoryMarker>().expect("create_proxy");

    let node_request = fidl::endpoints::ServerEnd::new(server_end.into_channel());
    parent.clone(flags, node_request).expect("cloned node");

    assert_read_dirents_no_overflow(&clone, expected_dirents).await;
}

async fn assert_clone_directory_overflow(
    package_root: &DirectoryProxy,
    path: &str,
    flags: u32,
    expected_dirents: Vec<DirEntry>,
) {
    let parent = open_directory(package_root, path, 0).await.expect("open parent directory");
    let (clone, server_end) =
        create_proxy::<fidl_fuchsia_io::DirectoryMarker>().expect("create_proxy");

    let node_request = fidl::endpoints::ServerEnd::new(server_end.into_channel());
    parent.clone(flags, node_request).expect("cloned node");

    assert_read_dirents_overflow(&clone, expected_dirents).await;
}

#[fuchsia::test]
async fn read_dirents() {
    for dir in dirs_to_test().await {
        read_dirents_per_package_source(dir).await
    }
}

async fn read_dirents_per_package_source(root_dir: DirectoryProxy) {
    // Handle overflow cases (e.g. when size of total dirents exceeds MAX_BUF).
    assert_read_dirents_overflow(
        &root_dir,
        vec![
            DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
            DirEntry { name: "dir_overflow_readdirents".to_string(), kind: DirentKind::Directory },
            DirEntry { name: "exceeds_max_buf".to_string(), kind: DirentKind::File },
            DirEntry { name: "file".to_string(), kind: DirentKind::File },
            DirEntry { name: "meta".to_string(), kind: DirentKind::Directory },
        ],
    )
    .await;
    assert_read_dirents_overflow(
        &io_util::directory::open_directory(&root_dir, "meta", 0).await.expect("open meta as dir"),
        vec![
            DirEntry { name: "contents".to_string(), kind: DirentKind::File },
            DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
            DirEntry { name: "dir_overflow_readdirents".to_string(), kind: DirentKind::Directory },
            DirEntry { name: "exceeds_max_buf".to_string(), kind: DirentKind::File },
            DirEntry { name: "file".to_string(), kind: DirentKind::File },
            DirEntry { name: "package".to_string(), kind: DirentKind::File },
        ],
    )
    .await;
    assert_read_dirents_overflow(
        &io_util::directory::open_directory(&root_dir, "dir_overflow_readdirents", 0)
            .await
            .expect("open dir_overflow_readdirents"),
        vec![],
    )
    .await;
    assert_read_dirents_overflow(
        &io_util::directory::open_directory(&root_dir, "meta/dir_overflow_readdirents", 0)
            .await
            .expect("open meta/dir_overflow_readdirents"),
        vec![],
    )
    .await;

    // Handle no-overflow cases (e.g. when size of total dirents does not exceed MAX_BUF).
    assert_read_dirents_no_overflow(
        &io_util::directory::open_directory(&root_dir, "dir", 0).await.expect("open dir"),
        vec![
            DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
            DirEntry { name: "file".to_string(), kind: DirentKind::File },
        ],
    )
    .await;
    assert_read_dirents_no_overflow(
        &io_util::directory::open_directory(&root_dir, "meta/dir", 0).await.expect("open meta/dir"),
        vec![
            DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
            DirEntry { name: "file".to_string(), kind: DirentKind::File },
        ],
    )
    .await;
}

/// For a particular directory, verify that the overflow case is being hit on ReadDirents (e.g. it
/// should take two ReadDirents calls to read all of the directory entries).
/// Note: we considered making this a unit test for pkg-harness, but opted to include this in the
/// integration tests so all the test cases are in one place.
async fn assert_read_dirents_overflow(dir: &DirectoryProxy, additional_contents: Vec<DirEntry>) {
    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "first call should yield non-empty buffer");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "second call should yield non-empty buffer");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert_eq!(buf, []);

    assert_eq!(
        files_async::readdir(dir).await.unwrap().into_iter().sorted().collect::<Vec<_>>(),
        ('a'..='z')
            .chain('A'..='E')
            .map(|seed| DirEntry {
                name: repeat_by_n(seed, fidl_fuchsia_io::MAX_FILENAME.try_into().unwrap()),
                kind: DirentKind::File
            })
            .chain(additional_contents)
            .sorted()
            .collect::<Vec<_>>()
    );
}

/// For a particular directory, verify that the overflow case is NOT being hit on ReadDirents
/// (e.g. it should only take one ReadDirents call to read all of the directory entries).
async fn assert_read_dirents_no_overflow(dir: &DirectoryProxy, expected_dirents: Vec<DirEntry>) {
    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "first call should yield non-empty buffer");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert_eq!(buf, []);

    assert_eq!(
        files_async::readdir(dir).await.unwrap().into_iter().sorted().collect::<Vec<_>>(),
        expected_dirents.into_iter().sorted().collect::<Vec<_>>()
    );
}

#[fuchsia::test]
async fn rewind() {
    for dir in dirs_to_test().await {
        rewind_per_package_source(dir).await
    }
}

async fn rewind_per_package_source(root_dir: DirectoryProxy) {
    // Handle overflow cases.
    for path in [".", "meta", "dir_overflow_readdirents", "meta/dir_overflow_readdirents"] {
        let dir = io_util::directory::open_directory(&root_dir, path, 0).await.unwrap();
        assert_rewind_overflow_when_seek_offset_at_end(&dir).await;
        assert_rewind_overflow_when_seek_offset_in_middle(&dir).await;
    }

    // Handle non-overflow cases.
    for path in ["dir", "meta/dir"] {
        assert_rewind_no_overflow(
            &io_util::directory::open_directory(&root_dir, path, 0).await.unwrap(),
        )
        .await;
    }
}

async fn assert_rewind_overflow_when_seek_offset_at_end(dir: &DirectoryProxy) {
    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "first read_dirents call should yield non-empty buffer");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "second read_dirents call should yield non-empty buffer");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert_eq!(buf, []);

    let status = dir.rewind().await.unwrap();
    zx::Status::ok(status).expect("status ok");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "read_dirents call after rewind should yield non-empty buffer");
}

async fn assert_rewind_overflow_when_seek_offset_in_middle(dir: &DirectoryProxy) {
    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "first read_dirents call should yield non-empty buffer");

    let status = dir.rewind().await.unwrap();
    zx::Status::ok(status).expect("status ok");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "first read_dirents call after rewind should yield non-empty buffer");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf.is_empty(), "second read_dirents call after rewind should yield non-empty buffer");

    let (status, buf) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert_eq!(buf, []);
}

async fn assert_rewind_no_overflow(dir: &DirectoryProxy) {
    let (status, buf0) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf0.is_empty(), "first read_dirents call should yield non-empty buffer");

    let status = dir.rewind().await.unwrap();
    zx::Status::ok(status).expect("status ok");

    let (status, buf1) = dir.read_dirents(fidl_fuchsia_io::MAX_BUF).await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert!(!buf1.is_empty(), "first read_dirents call after rewind should yield non-empty buffer");

    // We can't guarantee ordering will be the same, so the next best thing is to verify the
    // returned buffers are the same length.
    assert_eq!(buf0.len(), buf1.len());
}

#[fuchsia::test]
async fn get_token() {
    for dir in dirs_to_test().await {
        get_token_per_package_source(dir).await
    }
}

async fn get_token_per_package_source(root_dir: DirectoryProxy) {
    for path in [".", "dir", "meta", "meta/dir"] {
        let dir = io_util::directory::open_directory(&root_dir, path, 0).await.unwrap();

        let (status, token) = dir.get_token().await.unwrap();

        zx::Status::ok(status).expect("status ok");
        // We can't do anything meaningful with this token beyond checking it's Some because
        // all the IO APIs that consume tokens are unsupported.
        let _token = token.expect("token present");
    }
}

#[fuchsia::test]
async fn unsupported() {
    for dir in dirs_to_test().await {
        unsupported_per_package_source(dir).await
    }
}

async fn unsupported_per_package_source(root_dir: DirectoryProxy) {
    // Test unsupported APIs for root directory and subdirectory.
    assert_unsupported_directory_calls(&root_dir, ".", "file").await;
    assert_unsupported_directory_calls(&root_dir, ".", "dir").await;
    assert_unsupported_directory_calls(&root_dir, ".", "meta").await;
    assert_unsupported_directory_calls(&root_dir, "dir", "file").await;
    assert_unsupported_directory_calls(&root_dir, "dir", "dir").await;

    // Test unsupported APIs for meta directory and subdirectory.
    assert_unsupported_directory_calls(&root_dir, "meta", "file").await;
    assert_unsupported_directory_calls(&root_dir, "meta", "dir").await;
    assert_unsupported_directory_calls(&root_dir, "meta/dir", "file").await;
    assert_unsupported_directory_calls(&root_dir, "meta/dir", "dir").await;
}

async fn assert_unsupported_directory_calls(
    package_root: &DirectoryProxy,
    parent_path: &str,
    child_base_path: &str,
) {
    let parent = io_util::directory::open_directory(package_root, parent_path, 0)
        .await
        .expect("open parent directory");

    // Verify unlink() is not supported.
    assert_eq!(
        zx::Status::from_raw(parent.unlink(child_base_path).await.unwrap()),
        zx::Status::NOT_SUPPORTED
    );

    // Verify link() is not supported.
    let (status, token) = parent.get_token().await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert_eq!(
        zx::Status::from_raw(parent.link(child_base_path, token.unwrap(), "link").await.unwrap()),
        zx::Status::NOT_SUPPORTED
    );

    // Verify rename() is not supported.
    let (status, token) = parent.get_token().await.unwrap();
    zx::Status::ok(status).expect("status ok");
    assert_eq!(
        zx::Status::from_raw(
            parent.rename(child_base_path, token.unwrap(), "renamed").await.unwrap()
        ),
        zx::Status::NOT_SUPPORTED
    );

    // Verify watch() is not supported.
    let (h0, _h1) = zx::Channel::create().unwrap();
    assert_eq!(
        zx::Status::from_raw(parent.watch(0, 0, h0).await.unwrap()),
        zx::Status::NOT_SUPPORTED
    );

    // Verify nodeGetFlags() is not supported.
    let (status, flags) = parent.node_get_flags().await.unwrap();
    assert_eq!(zx::Status::from_raw(status), zx::Status::NOT_SUPPORTED);
    assert_eq!(flags, 0);

    // Verify nodeSetFlags() is not supported.
    assert_eq!(
        zx::Status::from_raw(parent.node_set_flags(0).await.unwrap()),
        zx::Status::NOT_SUPPORTED
    );
}
