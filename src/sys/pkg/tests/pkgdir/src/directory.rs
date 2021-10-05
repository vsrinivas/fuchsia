// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{dirs_to_test, just_pkgfs_for_now, repeat_by_n, OpenFlags, PackageSource},
    anyhow::{anyhow, Context as _, Error},
    fidl::{endpoints::create_proxy, AsHandleRef},
    fidl_fuchsia_io::{
        ConnectorInfo, DirectoryInfo, DirectoryObject, DirectoryProxy, FileInfo, FileObject,
        NodeEvent, NodeInfo, NodeMarker, NodeProxy, Representation, Service,
        CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_BLOCK_DEVICE, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE,
        MODE_TYPE_SERVICE, MODE_TYPE_SOCKET, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_NO_REMOTE, OPEN_FLAG_POSIX,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_io2::UnlinkOptions,
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
    for source in dirs_to_test().await {
        open_per_package_source(source).await
    }
}

async fn open_per_package_source(source: PackageSource) {
    // Testing dimensions:
    //   1. Receiver of the open call: /, meta/, subdir below meta/, subdir not below meta/
    //   2. Type of node the path points at: self, meta/, subdir below meta/, file below meta/,
    //      subdir not below meta/, file not below meta/ (not all receivers can open every type of
    //      target)
    //   3. Whether the path being opened is segmented
    // The flags and modes are handled by the helper functions.
    assert_open_root_directory(&source, ".", ".").await;
    assert_open_content_directory(&source, ".", "dir").await;
    assert_open_content_directory(&source, ".", "dir/dir").await;
    assert_open_content_file(&source, ".", "file").await;
    assert_open_content_file(&source, ".", "dir/file").await;
    assert_open_meta_as_directory_and_file(&source, ".", "meta").await;
    assert_open_meta_subdirectory(&source, ".", "meta/dir").await;
    assert_open_meta_file(&source, ".", "meta/file").await;

    // Self-opening "meta" does not trigger the file/dir duality.
    assert_open_meta_subdirectory(&source, "meta", ".").await;
    assert_open_meta_subdirectory(&source, "meta", "dir").await;
    assert_open_meta_subdirectory(&source, "meta", "dir/dir").await;
    assert_open_meta_file(&source, "meta", "file").await;
    assert_open_meta_file(&source, "meta", "dir/file").await;

    assert_open_meta_subdirectory(&source, "meta/dir", ".").await;
    assert_open_meta_subdirectory(&source, "meta/dir", "dir").await;
    assert_open_meta_subdirectory(&source, "meta/dir", "dir/dir").await;
    assert_open_meta_file(&source, "meta/dir", "file").await;
    assert_open_meta_file(&source, "meta/dir", "dir/file").await;

    assert_open_content_directory(&source, "dir", ".").await;
    assert_open_content_directory(&source, "dir", "dir").await;
    assert_open_content_directory(&source, "dir", "dir/dir").await;
    assert_open_content_file(&source, "dir", "file").await;
    assert_open_content_file(&source, "dir", "dir/file").await;
}

const ALL_FLAGS: [u32; 16] = [
    0,
    OPEN_RIGHT_READABLE,
    OPEN_RIGHT_WRITABLE,
    OPEN_RIGHT_ADMIN,
    OPEN_RIGHT_EXECUTABLE,
    OPEN_FLAG_CREATE,
    OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT,
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
    source: &PackageSource,
    parent_path: &str,
    child_base_path: &str,
) {
    let package_root = &source.dir;

    let mut success_flags = vec![
        0,
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_EXECUTABLE,
        OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
    ];
    if source.is_pkgfs() {
        success_flags.extend_from_slice(&[
            // "OPEN_RIGHT_ADMIN not supported"
            OPEN_RIGHT_ADMIN,
            // "OPEN_RIGHT_WRITABLE not supported"
            OPEN_RIGHT_WRITABLE,
            // "OPEN_FLAG_TRUNCATE and OPEN_FLAG_APPEND not supported"
            OPEN_FLAG_TRUNCATE,
            OPEN_FLAG_APPEND,
            // "OPEN_FLAG_CREATE not supported"
            OPEN_FLAG_CREATE,
            OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT,
            // "OPEN_FLAG_CREATE_IF_ABSENT without OPEN_FLAG_CREATE"
            OPEN_FLAG_CREATE_IF_ABSENT,
            // TODO(fxbug.dev/83844): pkgdir doesn't accept OPEN_FLAG_NO_REMOTE
            OPEN_FLAG_NO_REMOTE,
            // "OPEN_FLAG_NOT_DIRECTORY enforced"
            OPEN_FLAG_NOT_DIRECTORY,
        ])
    }

    let child_paths = if source.is_pkgfs() {
        // See generate_lax_directory_paths for comments on how pkgfs path handling behavior differs.
        generate_lax_directory_paths(child_base_path)
    } else {
        generate_valid_directory_paths(child_base_path)
    };
    let lax_child_paths = generate_lax_directory_paths(child_base_path);
    let all_flag_mode_and_child_paths =
        product3(ALL_FLAGS, ALL_MODES, lax_child_paths.iter().map(String::as_str));

    let success_flags_modes_and_child_paths =
        product3(success_flags, ALL_MODES, child_paths.iter().map(String::as_str)).filter_map(
            if source.is_pkgdir() {
                // "mode checked for consistency with OPEN_FLAG{_NOT,}_DIRECTORY"
                filter_out_contradictory_open_parameters
            } else {
                dont_filter_anything
            },
        );
    assert_open_success(
        package_root,
        parent_path,
        success_flags_modes_and_child_paths.clone(),
        verify_directory_opened,
    )
    .await;

    assert_open_flag_mode_and_child_path_failure(
        package_root,
        parent_path,
        subtract(all_flag_mode_and_child_paths, success_flags_modes_and_child_paths).into_iter(),
        verify_open_failed,
    )
    .await;
}

fn dont_filter_anything((flag, mode, child_path): (u32, u32, &str)) -> Option<(u32, u32, &'_ str)> {
    Some((flag, mode, child_path))
}

fn filter_out_contradictory_open_parameters(
    (flag, mode, child_path): (u32, u32, &str),
) -> Option<(u32, u32, &'_ str)> {
    // See "mode checked for consistency with OPEN_FLAG{_NOT,}_DIRECTORY" in the README

    if flag & OPEN_FLAG_NOT_DIRECTORY != 0 && mode == MODE_TYPE_DIRECTORY {
        // Skipping invalid mode combination
        return None;
    }
    if (flag & OPEN_FLAG_DIRECTORY != 0 || child_path.ends_with('/'))
        && !(mode == 0 || mode == MODE_TYPE_DIRECTORY)
    {
        // Skipping invalid mode combination
        return None;
    }
    Some((flag, mode, child_path))
}

fn product<I, J, IT, JT>(xs: I, ys: J) -> impl Iterator<Item = (IT, JT)> + Clone
where
    I: IntoIterator<Item = IT>,
    <I as IntoIterator>::IntoIter: Clone,
    IT: Clone,
    J: IntoIterator<Item = JT>,
    <J as IntoIterator>::IntoIter: Clone,
{
    xs.into_iter().cartesian_product(ys)
}

#[test]
fn test_product() {
    assert_eq!(
        product(["a", "b"], [0, 1]).collect::<Vec<_>>(),
        [("a", 0), ("a", 1), ("b", 0), ("b", 1)]
    )
}

fn product3<I, J, K, IT, JT, KT>(xs: I, ys: J, zs: K) -> impl Iterator<Item = (IT, JT, KT)> + Clone
where
    I: IntoIterator<Item = IT>,
    <I as IntoIterator>::IntoIter: Clone,
    IT: Clone,
    J: IntoIterator<Item = JT>,
    <J as IntoIterator>::IntoIter: Clone,
    JT: Clone,
    K: IntoIterator<Item = KT>,
    <K as IntoIterator>::IntoIter: Clone,
{
    product(xs, ys).cartesian_product(zs).map(|((x, y), z)| (x, y, z))
}
#[test]

fn test_product3() {
    assert_eq!(
        product3(["a", "b"], [0, 1], ["x", "y"]).collect::<Vec<_>>(),
        [
            ("a", 0, "x"),
            ("a", 0, "y"),
            ("a", 1, "x"),
            ("a", 1, "y"),
            ("b", 0, "x"),
            ("b", 0, "y"),
            ("b", 1, "x"),
            ("b", 1, "y")
        ]
    )
}

async fn assert_open_success<V, Fut>(
    package_root: &DirectoryProxy,
    parent_path: &str,
    allowed_flags_modes_and_child_paths: impl Iterator<Item = (u32, u32, &str)>,
    verifier: V,
) where
    V: Fn(NodeProxy, u32) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let parent = open_parent(package_root, parent_path).await;
    for (flag, mode, child_path) in allowed_flags_modes_and_child_paths {
        let node = open_node(&parent, flag, mode, &child_path);
        if let Err(e) = verifier(node, flag).await {
            panic!(
                "failed to verify open. parent: {:?}, child: {:?}, flag: {:?}, \
                       mode: {:#x}, error: {:#}",
                parent_path,
                child_path,
                OpenFlags(flag),
                mode,
                e
            );
        }
    }
}

async fn assert_open_content_directory(
    source: &PackageSource,
    parent_path: &str,
    child_base_path: &str,
) {
    let package_root = &source.dir;

    let mut success_flags = vec![
        0,
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_EXECUTABLE,
        OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
    ];
    if source.is_pkgfs() {
        success_flags.extend_from_slice(&[
            // "OPEN_RIGHT_ADMIN not supported"
            OPEN_RIGHT_ADMIN,
            // "OPEN_FLAG_CREATE_IF_ABSENT without OPEN_FLAG_CREATE"
            OPEN_FLAG_CREATE_IF_ABSENT,
            // TODO(fxbug.dev/83844): pkgdir doesn't accept OPEN_FLAG_NO_REMOTE
            OPEN_FLAG_NO_REMOTE,
            // "OPEN_FLAG_NOT_DIRECTORY enforced"
            OPEN_FLAG_NOT_DIRECTORY,
        ])
    }
    let child_paths = if source.is_pkgfs() {
        // See generate_lax_directory_paths for comments on how pkgfs path handling behavior differs.
        generate_lax_directory_paths(child_base_path)
    } else {
        generate_valid_directory_paths(child_base_path)
    };
    let lax_child_paths = generate_lax_directory_paths(child_base_path);
    let all_flag_mode_and_child_paths =
        product3(ALL_FLAGS, ALL_MODES, lax_child_paths.iter().map(String::as_str));

    let success_flags_modes_and_child_paths =
        product3(success_flags, ALL_MODES, child_paths.iter().map(String::as_str)).filter_map(
            if source.is_pkgdir() {
                // "mode checked for consistency with OPEN_FLAG{_NOT,}_DIRECTORY"
                filter_out_contradictory_open_parameters
            } else {
                dont_filter_anything
            },
        );
    assert_open_success(
        package_root,
        parent_path,
        success_flags_modes_and_child_paths.clone(),
        verify_directory_opened,
    )
    .await;

    assert_open_flag_mode_and_child_path_failure(
        package_root,
        parent_path,
        subtract(all_flag_mode_and_child_paths, success_flags_modes_and_child_paths).into_iter(),
        verify_open_failed,
    )
    .await;
}

fn subtract<'a, I, J, T>(minuend: I, subtrahend: J) -> Vec<T>
where
    I: IntoIterator<Item = T>,
    <I as IntoIterator>::IntoIter: Clone + 'a,
    J: IntoIterator<Item = T>,
    T: Eq + std::hash::Hash + 'a,
{
    let subtrahend = HashSet::<T>::from_iter(subtrahend);
    minuend.into_iter().filter(|v| !subtrahend.contains(v)).collect()
}

#[test]
fn test_subtract() {
    assert_eq!(subtract(["foo", "bar"], ["bar", "baz"]), vec!["foo"]);
}

async fn assert_open_flag_mode_and_child_path_failure<V, Fut>(
    package_root: &DirectoryProxy,
    parent_path: &str,
    disallowed_flags_modes_and_child_paths: impl Iterator<Item = (u32, u32, &str)>,
    verifier: V,
) where
    V: Fn(NodeProxy) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let parent = open_parent(package_root, parent_path).await;
    for (flag, mode, child_path) in disallowed_flags_modes_and_child_paths {
        let node = open_node(&parent, flag, mode, &child_path);
        if let Err(e) = verifier(node).await {
            panic!(
                "failed to verify open failed. parent: {:?}, child: {:?}, flag: {:?}, \
                       mode: {:#x}, error: {:#}",
                parent_path,
                child_path,
                OpenFlags(flag),
                mode,
                e
            );
        }
    }
}

async fn assert_open_content_file(
    source: &PackageSource,
    parent_path: &str,
    child_base_path: &str,
) {
    let package_root = &source.dir;

    let mut success_flags = vec![
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_READABLE | OPEN_FLAG_NO_REMOTE,
        OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX,
        OPEN_RIGHT_READABLE | OPEN_FLAG_NOT_DIRECTORY,
        OPEN_RIGHT_EXECUTABLE,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_NO_REMOTE,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_POSIX,
        OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_NOT_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE,
    ];
    if source.is_pkgdir() {
        // "content files support OPEN_FLAG_POSIX"
        // TODO(fxbug.dev/85062): figure out why pkgfs rejects this and pkgdir doesn't
        success_flags.push(OPEN_FLAG_POSIX);
    }
    if source.is_pkgfs() {
        // "OPEN_FLAG_CREATE_IF_ABSENT without OPEN_FLAG_CREATE"
        success_flags.extend([
            OPEN_RIGHT_READABLE | OPEN_FLAG_CREATE_IF_ABSENT,
            OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_CREATE_IF_ABSENT,
        ]);
    }
    let mut success_modes =
        vec![0, MODE_TYPE_BLOCK_DEVICE, MODE_TYPE_FILE, MODE_TYPE_SOCKET, MODE_TYPE_SERVICE];
    if source.is_pkgdir() {
        // "mode is ignored other than consistency checking with
        // OPEN_FLAG{_NOT,}_DIRECTORY and meta-as-file/meta-as-dir duality"
        success_modes.push(MODE_TYPE_DIRECTORY);
    }

    let child_paths = if source.is_pkgfs() {
        // See generate_lax_directory_paths for comments on how pkgfs path handling behavior differs.
        // Extra Note: "trailing slash implies OPEN_FLAG_DIRECTORY"
        generate_lax_directory_paths(child_base_path)
    } else {
        generate_valid_file_paths(child_base_path)
    };
    let lax_child_paths = generate_lax_directory_paths(child_base_path);
    let all_flag_mode_and_child_paths =
        product3(ALL_FLAGS, ALL_MODES, lax_child_paths.iter().map(String::as_str));

    let success_flags_modes_and_child_paths =
        product3(success_flags, success_modes, child_paths.iter().map(String::as_str)).filter_map(
            if source.is_pkgdir() {
                // "mode checked for consistency with OPEN_FLAG{_NOT,}_DIRECTORY"
                filter_out_contradictory_open_parameters
            } else {
                dont_filter_anything
            },
        );
    assert_open_success(
        package_root,
        parent_path,
        success_flags_modes_and_child_paths.clone(),
        verify_content_file_opened,
    )
    .await;

    assert_open_flag_mode_and_child_path_failure(
        package_root,
        parent_path,
        subtract(all_flag_mode_and_child_paths, success_flags_modes_and_child_paths).into_iter(),
        verify_open_failed,
    )
    .await;
}

async fn assert_open_meta_as_directory_and_file(
    source: &PackageSource,
    parent_path: &str,
    child_base_path: &str,
) {
    let package_root = &source.dir;

    let mut base_directory_success_flags = vec![
        0,
        OPEN_RIGHT_READABLE,
        OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
    ];
    if source.is_pkgfs() {
        base_directory_success_flags.extend_from_slice(&[
            // "OPEN_RIGHT_ADMIN not supported"
            OPEN_RIGHT_ADMIN,
            // "OPEN_FLAG_CREATE_IF_ABSENT without OPEN_FLAG_CREATE"
            OPEN_FLAG_CREATE_IF_ABSENT,
            // TODO(fxbug.dev/83844): pkgdir doesn't accept OPEN_FLAG_NO_REMOTE
            OPEN_FLAG_NO_REMOTE,
            // "OPEN_FLAG_NOT_DIRECTORY enforced"
            OPEN_FLAG_NOT_DIRECTORY,
        ])
    }
    // pkgfs allows meta (file or directory) to be opened EXECUTABLE if opened directly from the
    // package root, but not if "re-opened" from itself, i.e. opening "." from the meta dir.
    if source.is_pkgfs() && parent_path == "." {
        // "meta/ directories and files may not be opened with OPEN_RIGHT_EXECUTABLE"
        base_directory_success_flags.push(OPEN_RIGHT_EXECUTABLE);
    }

    // To open "meta" as a directory:
    //  1. mode cannot be MODE_TYPE_FILE
    //  2. and at least one of the following must be true:
    //    a. MODE_TYPE_DIRECTORY is set
    //    b. OPEN_FLAG_DIRECTORY is set
    //    c. OPEN_FLAG_NODE_REFERENCE is set
    let directory_flags_and_modes =
        product(base_directory_success_flags.clone(), [MODE_TYPE_DIRECTORY])
            .chain(product(
                base_directory_success_flags.clone().into_iter().filter_map(|f| {
                    if f & OPEN_FLAG_NOT_DIRECTORY != 0 && source.is_pkgdir() {
                        // "OPEN_FLAG_DIRECTORY and OPEN_FLAG_NOT_DIRECTORY are mutually exclusive"
                        None
                    } else {
                        Some(f | OPEN_FLAG_DIRECTORY)
                    }
                }),
                [
                    0,
                    MODE_TYPE_DIRECTORY,
                    MODE_TYPE_BLOCK_DEVICE,
                    MODE_TYPE_SOCKET,
                    MODE_TYPE_SERVICE,
                ],
            ))
            .chain(product(
                base_directory_success_flags
                    .clone()
                    .into_iter()
                    .map(|f| f | OPEN_FLAG_NODE_REFERENCE),
                [
                    0,
                    MODE_TYPE_DIRECTORY,
                    MODE_TYPE_BLOCK_DEVICE,
                    MODE_TYPE_SOCKET,
                    MODE_TYPE_SERVICE,
                ],
            ));

    let directory_child_paths = if source.is_pkgfs() {
        // See generate_lax_directory_paths for comments on how pkgfs path handling behavior differs.
        generate_lax_directory_paths(child_base_path)
    } else {
        generate_valid_directory_paths(child_base_path)
    };
    let lax_child_paths = generate_lax_directory_paths(child_base_path);

    let directory_only_child_paths = generate_valid_directory_only_paths(child_base_path);
    let all_flag_mode_and_child_paths =
        product3(ALL_FLAGS, ALL_MODES, lax_child_paths.iter().map(String::as_str));

    let extra_directory_only_child_paths = if source.is_pkgdir() {
        // "trailing slash implies OPEN_FLAG_DIRECTORY"
        Some(directory_only_child_paths.as_slice())
    } else {
        None
    };

    let directory_flags_modes_and_child_paths =
        product(directory_flags_and_modes, directory_child_paths.iter().map(String::as_str))
            .map(|((flag, mode), path)| (flag, mode, path))
            .chain(product3(
                base_directory_success_flags,
                [
                    0,
                    MODE_TYPE_DIRECTORY,
                    MODE_TYPE_BLOCK_DEVICE,
                    MODE_TYPE_SOCKET,
                    MODE_TYPE_SERVICE,
                ],
                extra_directory_only_child_paths.into_iter().flatten().map(String::as_str),
            ))
            .filter_map(if source.is_pkgdir() {
                // "mode checked for consistency with OPEN_FLAG{_NOT,}_DIRECTORY"
                filter_out_contradictory_open_parameters
            } else {
                dont_filter_anything
            });
    assert_open_success(
        package_root,
        parent_path,
        directory_flags_modes_and_child_paths.clone(),
        verify_directory_opened,
    )
    .await;

    // To open "meta" as a file at least one of the following must be true:
    //  1. mode is MODE_TYPE_FILE
    //  2. none of the following are true:
    //    a. MODE_TYPE_DIRECTORY is set
    //    b. OPEN_FLAG_DIRECTORY is set
    //    c. OPEN_FLAG_NODE_REFERENCE is set
    let mut base_file_flags =
        vec![0, OPEN_RIGHT_READABLE, OPEN_FLAG_DESCRIBE, OPEN_FLAG_POSIX, OPEN_FLAG_NOT_DIRECTORY];
    if source.is_pkgfs() {
        base_file_flags.extend_from_slice(&[
            // "OPEN_RIGHT_ADMIN not supported"
            OPEN_RIGHT_ADMIN,
            // "meta/ directories and files may not be opened with OPEN_RIGHT_EXECUTABLE"
            OPEN_RIGHT_EXECUTABLE,
            // "OPEN_FLAG_CREATE_IF_ABSENT without OPEN_FLAG_CREATE"
            OPEN_FLAG_CREATE_IF_ABSENT,
            // TODO(fxbug.dev/83844): pkgdir doesn't accept OPEN_FLAG_NO_REMOTE
            OPEN_FLAG_NO_REMOTE,
        ])
    }

    let file_flags_and_modes = product(
        base_file_flags.iter().copied().chain([OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE]),
        [MODE_TYPE_FILE],
    )
    .chain(product(
        base_file_flags.iter().copied(),
        [0, MODE_TYPE_BLOCK_DEVICE, MODE_TYPE_SOCKET, MODE_TYPE_SERVICE],
    ));
    let file_child_paths = if source.is_pkgfs() {
        // See generate_lax_directory_paths for comments on how pkgfs path handling behavior differs.
        // Extra Note: "trailing slash implies OPEN_FLAG_DIRECTORY"
        generate_lax_directory_paths(child_base_path)
    } else {
        generate_valid_file_paths(child_base_path)
    };

    let file_flags_modes_and_child_paths =
        product(file_flags_and_modes, file_child_paths.iter().map(String::as_str))
            .map(|((flag, mode), path)| (flag, mode, path))
            .filter_map(if source.is_pkgdir() {
                // "mode checked for consistency with OPEN_FLAG{_NOT,}_DIRECTORY"
                filter_out_contradictory_open_parameters
            } else {
                dont_filter_anything
            });

    assert_open_success(
        package_root,
        parent_path,
        file_flags_modes_and_child_paths.clone(),
        verify_meta_as_file_opened,
    )
    .await;

    let failure_flags_modes_and_child_paths = subtract(
        subtract(all_flag_mode_and_child_paths, directory_flags_modes_and_child_paths),
        file_flags_modes_and_child_paths,
    )
    .into_iter();
    assert_open_flag_mode_and_child_path_failure(
        package_root,
        parent_path,
        failure_flags_modes_and_child_paths,
        verify_open_failed,
    )
    .await;
}

async fn assert_open_meta_subdirectory(
    source: &PackageSource,
    parent_path: &str,
    child_base_path: &str,
) {
    let package_root = &source.dir;

    let mut success_flags = vec![
        0,
        OPEN_RIGHT_READABLE,
        OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
    ];
    if source.is_pkgfs() {
        success_flags.extend_from_slice(&[
            // "OPEN_RIGHT_ADMIN not supported"
            OPEN_RIGHT_ADMIN,
            // "OPEN_FLAG_CREATE_IF_ABSENT without OPEN_FLAG_CREATE"
            OPEN_FLAG_CREATE_IF_ABSENT,
            // TODO(fxbug.dev/83844): pkgdir doesn't accept OPEN_FLAG_NO_REMOTE
            OPEN_FLAG_NO_REMOTE,
            // "OPEN_FLAG_NOT_DIRECTORY enforced"
            OPEN_FLAG_NOT_DIRECTORY,
        ])
    }

    let child_paths = if source.is_pkgfs() {
        // See generate_lax_directory_paths for comments on how pkgfs path handling behavior differs.
        generate_lax_directory_paths(child_base_path)
    } else {
        generate_valid_directory_paths(child_base_path)
    };

    let lax_child_paths = generate_lax_directory_paths(child_base_path);
    let all_flag_mode_and_child_paths =
        product3(ALL_FLAGS, ALL_MODES, lax_child_paths.iter().map(String::as_str));

    let success_flags_modes_and_child_paths =
        product3(success_flags, ALL_MODES, child_paths.iter().map(String::as_str)).filter_map(
            if source.is_pkgdir() {
                // "mode checked for consistency with OPEN_FLAG{_NOT,}_DIRECTORY"
                filter_out_contradictory_open_parameters
            } else {
                dont_filter_anything
            },
        );
    assert_open_success(
        package_root,
        parent_path,
        success_flags_modes_and_child_paths.clone(),
        verify_directory_opened,
    )
    .await;

    assert_open_flag_mode_and_child_path_failure(
        package_root,
        parent_path,
        subtract(all_flag_mode_and_child_paths, success_flags_modes_and_child_paths).into_iter(),
        verify_open_failed,
    )
    .await;
}

async fn assert_open_meta_file(source: &PackageSource, parent_path: &str, child_base_path: &str) {
    let package_root = &source.dir;

    let mut success_flags = vec![
        0,
        OPEN_RIGHT_READABLE,
        OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_POSIX,
        OPEN_FLAG_NOT_DIRECTORY,
    ];
    if source.is_pkgfs() {
        success_flags.extend_from_slice(&[
            // "OPEN_RIGHT_ADMIN not supported"
            OPEN_RIGHT_ADMIN,
            // "OPEN_FLAG_CREATE_IF_ABSENT without OPEN_FLAG_CREATE"
            OPEN_FLAG_CREATE_IF_ABSENT,
            // TODO(fxbug.dev/83844): pkgdir doesn't accept OPEN_FLAG_NO_REMOTE
            OPEN_FLAG_NO_REMOTE,
            // "OPEN_FLAG_DIRECTORY enforced"
            OPEN_FLAG_DIRECTORY,
        ])
    }

    let child_paths = if source.is_pkgfs() {
        // See generate_lax_directory_paths for comments on how pkgfs path handling behavior differs.
        // Extra Note: "trailing slash implies OPEN_FLAG_DIRECTORY"
        generate_lax_directory_paths(child_base_path)
    } else {
        generate_valid_file_paths(child_base_path)
    };

    let lax_child_paths = generate_lax_directory_paths(child_base_path);
    let all_flag_mode_and_child_paths =
        product3(ALL_FLAGS, ALL_MODES, lax_child_paths.iter().map(String::as_str));

    let success_flags_modes_and_child_paths =
        product3(success_flags, ALL_MODES, child_paths.iter().map(String::as_str)).filter_map(
            if source.is_pkgdir() {
                // "mode checked for consistency with OPEN_FLAG{_NOT,}_DIRECTORY"
                filter_out_contradictory_open_parameters
            } else {
                dont_filter_anything
            },
        );
    assert_open_success(
        package_root,
        parent_path,
        success_flags_modes_and_child_paths.clone(),
        verify_meta_as_file_opened,
    )
    .await;

    assert_open_flag_mode_and_child_path_failure(
        package_root,
        parent_path,
        subtract(all_flag_mode_and_child_paths, success_flags_modes_and_child_paths).into_iter(),
        verify_open_failed,
    )
    .await;
}

async fn open_parent(package_root: &DirectoryProxy, parent_path: &str) -> DirectoryProxy {
    let parent_rights = if parent_path == "meta"
        || parent_path == "/meta"
        || parent_path.starts_with("meta/")
        || parent_path.starts_with("/meta/")
    {
        OPEN_RIGHT_READABLE
    } else {
        OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE
    };
    io_util::directory::open_directory(package_root, parent_path, parent_rights)
        .await
        .expect("open parent directory")
}

fn open_node(parent: &DirectoryProxy, flags: u32, mode: u32, path: &str) -> NodeProxy {
    let (node, server_end) = create_proxy::<NodeMarker>().expect("create_proxy");
    parent.open(flags, mode, path, server_end).expect("open node");
    node
}

/// Generates the same path variations as [`generate_valid_directory_paths`]
/// plus extra path variations that pkgfs accepts but pkgdir rejects.
fn generate_lax_directory_paths(base: &str) -> Vec<String> {
    let mut paths = generate_valid_directory_paths(base);
    if base == "." {
        // pkgfs doesn't give "." any special treatment, so add the "/"-variations
        // TODO(fxbug.dev/85012): figure out if pkgdir should accept these
        paths.extend([format!("{}/", base), format!("/{}", base), format!("/{}/", base)]);
    }
    // "path segment rules are checked"
    paths.extend([format!("./{}", base), format!("{}/.", base)]);
    if base.contains("/") {
        paths.push(base.replace("/", "//"));
        paths.push(base.replace("/", "/to-be-removed/../"));
        paths.push(base.replace("/", "/./"));
    }
    paths
}

/// Generates a set of path variations which are valid when opening directories.
fn generate_valid_directory_paths(base: &str) -> Vec<String> {
    if base == "." {
        vec![base.to_string()]
    } else {
        vec![base.to_string(), format!("{}/", base), format!("/{}", base), format!("/{}/", base)]
    }
}

/// Generates a set of path variations which are only valid when opening directories.
///
/// Paths ending in "/" can only be used when opening directories.
fn generate_valid_directory_only_paths(base: &str) -> Vec<String> {
    if base == "." {
        return vec![];
    }
    vec![format!("{}/", base), format!("/{}/", base)]
}

/// Generates a set of path variations which are valid when opening files.
fn generate_valid_file_paths(base: &str) -> Vec<String> {
    vec![base.to_string(), format!("/{}", base)]
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
            Some(Ok(NodeEvent::OnConnectionInfo { info })) => {
                assert_eq!(
                    info.representation,
                    Some(Representation::Directory(DirectoryInfo::EMPTY))
                );
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
                Some(Ok(NodeEvent::OnConnectionInfo { info })) => {
                    assert_eq!(
                        info.representation,
                        Some(Representation::Connector(ConnectorInfo::EMPTY))
                    );
                    return Ok(());
                }
                Some(Ok(other)) => return Err(anyhow!("wrong node type returned: {:?}", other)),
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
                Some(Ok(NodeEvent::OnConnectionInfo { info })) => {
                    match info.representation {
                        Some(Representation::File(FileInfo { observer: Some(event), .. })) => {
                            match event.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST) {
                                Ok(_) => return Ok(()),
                                Err(_) => return Err(anyhow!("FILE_SIGNAL_READABLE not set")),
                            }
                        }
                        _ => return Err(anyhow!("expected FileObject")),
                    };
                }
                Some(Ok(other)) => return Err(anyhow!("wrong node type returned: {:?}", other)),
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
            Some(Ok(NodeEvent::OnConnectionInfo { info })) => match info.representation {
                Some(Representation::File(_)) => return Ok(()),
                _ => return Err(anyhow!("wrong NodeInfo returned")),
            },
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
        Err(fidl::Error::ClientChannelClosed { status: _, protocol_name: _ }) => Ok(()),
        Err(e) => Err(e).context("failed with unexpected error"),
    }
}

// TODO(fxbug.dev/81447) enhance Clone tests.
#[fuchsia::test]
async fn clone() {
    for source in dirs_to_test().await {
        clone_per_package_source(source).await
    }
}

async fn clone_per_package_source(source: PackageSource) {
    let root_dir = &source.dir;
    for flag in [
        0,
        OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
        OPEN_RIGHT_ADMIN,
        OPEN_RIGHT_EXECUTABLE,
        OPEN_FLAG_APPEND,
        OPEN_FLAG_NO_REMOTE,
        OPEN_FLAG_DESCRIBE,
        CLONE_FLAG_SAME_RIGHTS,
    ] {
        if source.is_pkgdir() && (flag & OPEN_FLAG_NO_REMOTE != 0) {
            // TODO(fxbug.dev/83844): pkgdir doesn't accept OPEN_FLAG_NO_REMOTE
            continue;
        }
        if source.is_pkgdir() && (flag & OPEN_FLAG_APPEND != 0) {
            // "OPEN_FLAG_TRUNCATE and OPEN_FLAG_APPEND not supported"
            continue;
        }
        if source.is_pkgdir() && (flag & OPEN_RIGHT_ADMIN != 0) {
            // "OPEN_RIGHT_ADMIN not supported"
            continue;
        }
        if source.is_pkgdir() && (flag & OPEN_RIGHT_WRITABLE != 0) {
            // "OPEN_RIGHT_WRITABLE not supported"
            continue;
        }
        assert_clone_directory_overflow(
            root_dir,
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
                DirEntry { name: "file_0".to_string(), kind: DirentKind::File },
                DirEntry { name: "file_1".to_string(), kind: DirentKind::File },
                DirEntry { name: "file_4095".to_string(), kind: DirentKind::File },
                DirEntry { name: "file_4096".to_string(), kind: DirentKind::File },
                DirEntry { name: "file_4097".to_string(), kind: DirentKind::File },
            ],
        )
        .await;
        assert_clone_directory_no_overflow(
            root_dir,
            "dir",
            flag,
            vec![
                DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
                DirEntry { name: "file".to_string(), kind: DirentKind::File },
            ],
        )
        .await;
        if flag & OPEN_RIGHT_EXECUTABLE != 0 {
            // pkgdir requires a directory to have EXECUTABLE rights for it to be cloned
            // ("Hierarchical rights enforcement"), Since both pkgfs and pkgdir reject opening
            // meta dirs with EXECUTABLE rights, we can't get a valid parent directory to
            // test with. So no test is possible here.
        } else {
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
                    DirEntry { name: "file_0".to_string(), kind: DirentKind::File },
                    DirEntry { name: "file_1".to_string(), kind: DirentKind::File },
                    DirEntry { name: "file_4095".to_string(), kind: DirentKind::File },
                    DirEntry { name: "file_4096".to_string(), kind: DirentKind::File },
                    DirEntry { name: "file_4097".to_string(), kind: DirentKind::File },
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
}

async fn assert_clone_directory_no_overflow(
    package_root: &DirectoryProxy,
    path: &str,
    flags: u32,
    expected_dirents: Vec<DirEntry>,
) {
    let parent =
        open_directory(package_root, path, flags & (OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE))
            .await
            .expect("open parent directory");
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
    let parent = open_parent(package_root, path).await;
    let (clone, server_end) =
        create_proxy::<fidl_fuchsia_io::DirectoryMarker>().expect("create_proxy");

    let node_request = fidl::endpoints::ServerEnd::new(server_end.into_channel());
    parent.clone(flags, node_request).expect("cloned node");

    assert_read_dirents_overflow(&clone, expected_dirents).await;
}

#[fuchsia::test]
async fn read_dirents() {
    for source in dirs_to_test().await {
        read_dirents_per_package_source(source).await
    }
}

async fn read_dirents_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
    // Handle overflow cases (e.g. when size of total dirents exceeds MAX_BUF).
    assert_read_dirents_overflow(
        &root_dir,
        vec![
            DirEntry { name: "dir".to_string(), kind: DirentKind::Directory },
            DirEntry { name: "dir_overflow_readdirents".to_string(), kind: DirentKind::Directory },
            DirEntry { name: "exceeds_max_buf".to_string(), kind: DirentKind::File },
            DirEntry { name: "file".to_string(), kind: DirentKind::File },
            DirEntry { name: "meta".to_string(), kind: DirentKind::Directory },
            DirEntry { name: "file_0".to_string(), kind: DirentKind::File },
            DirEntry { name: "file_1".to_string(), kind: DirentKind::File },
            DirEntry { name: "file_4095".to_string(), kind: DirentKind::File },
            DirEntry { name: "file_4096".to_string(), kind: DirentKind::File },
            DirEntry { name: "file_4097".to_string(), kind: DirentKind::File },
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
            DirEntry { name: "file_0".to_string(), kind: DirentKind::File },
            DirEntry { name: "file_1".to_string(), kind: DirentKind::File },
            DirEntry { name: "file_4095".to_string(), kind: DirentKind::File },
            DirEntry { name: "file_4096".to_string(), kind: DirentKind::File },
            DirEntry { name: "file_4097".to_string(), kind: DirentKind::File },
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
    for source in dirs_to_test().await {
        rewind_per_package_source(source).await
    }
}

async fn rewind_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
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
    for source in dirs_to_test().await {
        get_token_per_package_source(source).await
    }
}

async fn get_token_per_package_source(source: PackageSource) {
    let root_dir = &source.dir;
    for path in [".", "dir", "meta", "meta/dir"] {
        let dir = io_util::directory::open_directory(root_dir, path, 0).await.unwrap();

        let (status, token) = dir.get_token().await.unwrap();
        let status = zx::Status::ok(status);
        if source.is_pkgdir() {
            // "GetToken() not supported"
            assert_eq!(status, Err(zx::Status::NOT_SUPPORTED));
            assert!(token.is_none(), "token should be absent");
        } else {
            status.expect("status ok");
            // We can't do anything meaningful with this token beyond checking it's Some because
            // all the IO APIs that consume tokens are unsupported.
            let _token = token.expect("token present");
        }
    }
}

#[fuchsia::test]
async fn node_get_flags() {
    for source in dirs_to_test().await {
        node_get_flags_per_package_source(source).await
    }
}

async fn node_get_flags_per_package_source(source: PackageSource) {
    // Test get_flags APIs for root directory and subdirectory.
    assert_node_get_flags_directory_calls(&source, ".").await;
    assert_node_get_flags_directory_calls(&source, "dir").await;

    // Test get_flags APIs for meta directory and subdirectory.
    assert_node_get_flags_directory_calls(&source, "meta").await;
    assert_node_get_flags_directory_calls(&source, "meta/dir").await;
}

async fn assert_node_get_flags_directory_calls(source: &PackageSource, path: &str) {
    let package_root = &source.dir;
    let dir = io_util::directory::open_directory(
        package_root,
        path,
        OPEN_RIGHT_READABLE | OPEN_FLAG_DIRECTORY | OPEN_FLAG_POSIX,
    )
    .await
    .expect("open directory");

    let (status, flags) = dir.node_get_flags().await.unwrap();
    let status = zx::Status::ok(status);

    if source.is_pkgdir() {
        // "NodeGetFlags() is supported on directories"
        let result = status.map(|()| OpenFlags(flags));
        assert_eq!(result, Ok(OpenFlags(OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE)))
    } else {
        // Verify nodeGetFlags() is not supported.
        assert_eq!(status, Err(zx::Status::NOT_SUPPORTED));
        assert_eq!(flags, 0);
    }
}

#[fuchsia::test]
async fn unsupported() {
    for source in just_pkgfs_for_now().await {
        unsupported_per_package_source(source).await
    }
}

async fn unsupported_per_package_source(source: PackageSource) {
    let root_dir = source.dir;
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
        parent.unlink(child_base_path, UnlinkOptions::EMPTY).await.unwrap(),
        Err(zx::Status::NOT_SUPPORTED.into_raw())
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
        parent.rename2(child_base_path, zx::Event::from(token.unwrap()), "renamed").await.unwrap(),
        Err(zx::sys::ZX_ERR_NOT_SUPPORTED)
    );

    // Verify watch() is not supported.
    let (h0, _h1) = zx::Channel::create().unwrap();
    assert_eq!(
        zx::Status::from_raw(parent.watch(0, 0, h0).await.unwrap()),
        zx::Status::NOT_SUPPORTED
    );

    // Verify nodeSetFlags() is not supported.
    assert_eq!(
        zx::Status::from_raw(parent.node_set_flags(0).await.unwrap()),
        zx::Status::NOT_SUPPORTED
    );
}
