// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx,
    io_conformance_util::{test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn open_dir_without_describe_flag() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    for dir_flags in harness.dir_rights.valid_combos() {
        assert_eq!(dir_flags & fio::OpenFlags::DESCRIBE, fio::OpenFlags::empty());
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");

        root_dir
            .open(dir_flags, fio::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_on_open_not_received(&client).await;
    }
}

#[fasync::run_singlethreaded(test)]
async fn open_file_without_describe_flag() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos() {
        assert_eq!(file_flags & fio::OpenFlags::DESCRIBE, fio::OpenFlags::empty());
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");

        test_dir
            .open(file_flags, fio::MODE_TYPE_FILE, TEST_FILE, server)
            .expect("Cannot open file");

        assert_on_open_not_received(&client).await;
    }
}

/// Checks that open fails with ZX_ERR_BAD_PATH when it should.
#[fasync::run_singlethreaded(test)]
async fn open_path() {
    let harness = TestHarness::new().await;
    if !harness.config.conformant_path_handling.unwrap_or_default() {
        return;
    }

    let root = root_directory(vec![directory("dir", vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    // Valid paths:
    for path in [".", "/", "/dir/"] {
        open_node::<fio::NodeMarker>(&root_dir, fio::OpenFlags::RIGHT_READABLE, 0, path).await;
    }

    // Invalid paths:
    for path in [
        "", "//", "///", "////", "./", "/dir//", "//dir//", "/dir//", "/dir/../", "/dir/..",
        "/dir/./", "/dir/.", "/./", "./dir",
    ] {
        assert_eq!(
            open_node_status::<fio::NodeMarker>(&root_dir, fio::OpenFlags::RIGHT_READABLE, 0, path)
                .await
                .expect_err("open succeeded"),
            zx::Status::INVALID_ARGS,
            "path: {}",
            path,
        );
    }
}

/// Check that a trailing flash with OPEN_FLAG_NOT_DIRECTORY returns ZX_ERR_INVALID_ARGS.
#[fasync::run_singlethreaded(test)]
async fn open_trailing_slash_with_not_directory() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::NOT_DIRECTORY,
            0,
            "foo/"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );
}

/// Checks that mode is ignored when opening existing nodes.
#[fasync::run_singlethreaded(test)]
async fn open_flags_and_mode() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![file(TEST_FILE, vec![]), directory("dir", vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    // mode should be ignored when opening an existing object.
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        TEST_FILE,
    )
    .await;
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_FILE,
        "dir",
    )
    .await;
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DIRECTORY,
        libc::S_IRWXU,
        "dir",
    )
    .await;

    // MODE_TYPE_DIRECTORY is incompatible with OPEN_FLAG_NOT_DIRECTORY
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::NOT_DIRECTORY,
            fio::MODE_TYPE_DIRECTORY,
            "foo"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // MODE_TYPE_FILE is incompatible with a path that specifies a directory
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_FILE,
            "foo/"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // MODE_TYPE_FILE is incompatible with OPEN_FLAG_DIRECTORY
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DIRECTORY,
            fio::MODE_TYPE_FILE,
            "foo"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // Can't open . with OPEN_FLAG_NOT_DIRECTORY
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::NOT_DIRECTORY,
            0,
            "."
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // Can't have OPEN_FLAG_DIRECTORY and OPEN_FLAG_NOT_DIRECTORY
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::DIRECTORY
                | fio::OpenFlags::NOT_DIRECTORY,
            0,
            "."
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );
}

// Validate allowed rights for Directory objects.
#[fasync::run_singlethreaded(test)]
async fn validate_directory_rights() {
    let harness = TestHarness::new().await;
    // Create a test directory and ensure we can open it with all supported rights.
    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let _root_dir = harness.get_directory(
        root,
        fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::RIGHT_EXECUTABLE,
    );
}

// Validate allowed rights for File objects (ensures writable files cannot be opened as executable).
#[fasync::run_singlethreaded(test)]
async fn validate_file_rights() {
    let harness = TestHarness::new().await;
    // Create a test directory with a single File object, and ensure the directory has all rights.
    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    // Opening as READABLE must succeed.
    open_node::<fio::NodeMarker>(&root_dir, fio::OpenFlags::RIGHT_READABLE, 0, TEST_FILE).await;

    if harness.config.mutable_file.unwrap_or_default() {
        // Opening as WRITABLE must succeed.
        open_node::<fio::NodeMarker>(&root_dir, fio::OpenFlags::RIGHT_WRITABLE, 0, TEST_FILE).await;
        // Opening as EXECUTABLE must fail (W^X).
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_EXECUTABLE,
            0,
            TEST_FILE,
        )
        .await
        .expect_err("open succeeded");
    } else {
        // If files are immutable, check that opening as WRITABLE results in access denied.
        // All other combinations are valid in this case.
        assert_eq!(
            open_node_status::<fio::NodeMarker>(
                &root_dir,
                fio::OpenFlags::RIGHT_WRITABLE,
                0,
                TEST_FILE
            )
            .await
            .expect_err("open succeeded"),
            zx::Status::ACCESS_DENIED
        );
    }
}

// Validate allowed rights for VmoFile objects (ensures cannot be opened as executable).
#[fasync::run_singlethreaded(test)]
async fn validate_vmo_file_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_vmo_file.unwrap_or_default() {
        return;
    }
    // Create a test directory with a VmoFile object, and ensure the directory has all rights.
    let root = root_directory(vec![vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024)]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());
    // Opening with READ/WRITE should succeed.
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        0,
        TEST_FILE,
    )
    .await;
    // Opening with EXECUTE must fail to ensure W^X enforcement.
    assert!(matches!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_EXECUTABLE,
            0,
            TEST_FILE
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::ACCESS_DENIED | zx::Status::NOT_SUPPORTED
    ));
}

// Validate allowed rights for ExecutableFile objects (ensures cannot be opened as writable).
#[fasync::run_singlethreaded(test)]
async fn validate_executable_file_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_executable_file.unwrap_or_default() {
        return;
    }
    // Create a test directory with an ExecutableFile object, and ensure the directory has all rights.
    let root = root_directory(vec![executable_file(TEST_FILE)]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());
    // Opening with READABLE/EXECUTABLE should succeed.
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        0,
        TEST_FILE,
    )
    .await;
    // Opening with WRITABLE must fail to ensure W^X enforcement.
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_WRITABLE,
            0,
            TEST_FILE
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::ACCESS_DENIED
    );
}

/// Creates a directory with all rights, and checks it can be opened for all subsets of rights.
#[fasync::run_singlethreaded(test)]
async fn open_dir_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    for dir_flags in harness.dir_rights.valid_combos() {
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
        root_dir
            .open(dir_flags | fio::OpenFlags::DESCRIBE, fio::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_eq!(get_open_status(&client).await, zx::Status::OK);
    }
}

/// Creates a directory with no rights, and checks opening it with any rights fails.
#[fasync::run_singlethreaded(test)]
async fn open_dir_with_insufficient_rights() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, fio::OpenFlags::empty());

    for dir_flags in harness.dir_rights.valid_combos() {
        if dir_flags.is_empty() {
            continue;
        }
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
        root_dir
            .open(dir_flags | fio::OpenFlags::DESCRIBE, fio::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_eq!(get_open_status(&client).await, zx::Status::ACCESS_DENIED);
    }
}

/// Opens a directory, and checks that a child directory can be opened using the same rights.
#[fasync::run_singlethreaded(test)]
async fn open_child_dir_with_same_rights() {
    let harness = TestHarness::new().await;

    for dir_flags in harness.dir_rights.valid_combos() {
        let root = root_directory(vec![directory("child", vec![])]);
        let root_dir = harness.get_directory(root, harness.dir_rights.all());

        let parent_dir =
            open_node::<fio::DirectoryMarker>(&root_dir, dir_flags, fio::MODE_TYPE_DIRECTORY, ".")
                .await;

        // Open child directory with same flags as parent.
        let (child_dir_client, child_dir_server) =
            create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
        parent_dir
            .open(
                dir_flags | fio::OpenFlags::DESCRIBE,
                fio::MODE_TYPE_DIRECTORY,
                "child",
                child_dir_server,
            )
            .expect("Cannot open directory");

        assert_eq!(get_open_status(&child_dir_client).await, zx::Status::OK);
    }
}

/// Opens a directory as readable, and checks that a child directory cannot be opened as writable.
#[fasync::run_singlethreaded(test)]
async fn open_child_dir_with_extra_rights() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![directory("child", vec![])]);
    let root_dir = harness.get_directory(root, fio::OpenFlags::RIGHT_READABLE);

    // Open parent as readable.
    let parent_dir = open_node::<fio::DirectoryMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        ".",
    )
    .await;

    // Opening child as writable should fail.
    let (child_dir_client, child_dir_server) =
        create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
    parent_dir
        .open(
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::DESCRIBE,
            fio::MODE_TYPE_DIRECTORY,
            "child",
            child_dir_server,
        )
        .expect("Cannot open directory");

    assert_eq!(get_open_status(&child_dir_client).await, zx::Status::ACCESS_DENIED);
}

/// Creates a child directory and opens it with OPEN_FLAG_POSIX_WRITABLE/EXECUTABLE, ensuring that
/// the requested rights are expanded to only those which the parent directory connection has.
#[fasync::run_singlethreaded(test)]
async fn open_child_dir_with_posix_flags() {
    let harness = TestHarness::new().await;

    for dir_flags in harness.dir_rights.valid_combos() {
        let root = root_directory(vec![directory("child", vec![])]);
        let root_dir = harness.get_directory(root, dir_flags);
        let readable = dir_flags & fio::OpenFlags::RIGHT_READABLE;
        let parent_dir =
            open_node::<fio::DirectoryMarker>(&root_dir, dir_flags, fio::MODE_TYPE_DIRECTORY, ".")
                .await;

        let (child_dir_client, child_dir_server) =
            create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
        parent_dir
            .open(
                readable
                    | fio::OpenFlags::POSIX_WRITABLE
                    | fio::OpenFlags::POSIX_EXECUTABLE
                    | fio::OpenFlags::DESCRIBE,
                fio::MODE_TYPE_DIRECTORY,
                "child",
                child_dir_server,
            )
            .expect("Cannot open directory");

        assert_eq!(
            get_open_status(&child_dir_client).await,
            zx::Status::OK,
            "Failed to open directory, flags = {:?}",
            dir_flags
        );
        // Ensure expanded rights do not exceed those of the parent directory connection.
        assert_eq!(get_node_flags(&child_dir_client).await & dir_flags, dir_flags);
    }
}

/// Ensures that opening a file with more rights than the directory connection fails
/// with Status::ACCESS_DENIED.
#[fasync::run_singlethreaded(test)]
async fn open_file_with_extra_rights() {
    let harness = TestHarness::new().await;

    // Combinations to test of the form (directory flags, [file flag combinations]).
    // All file flags should have more rights than those of the directory flags.
    let test_right_combinations = [
        (fio::OpenFlags::empty(), harness.file_rights.valid_combos()),
        (
            fio::OpenFlags::RIGHT_READABLE,
            harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE),
        ),
        (
            fio::OpenFlags::RIGHT_WRITABLE,
            harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_READABLE),
        ),
    ];

    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    for (dir_flags, file_flag_combos) in test_right_combinations.iter() {
        let dir_proxy =
            open_node::<fio::DirectoryMarker>(&root_dir, *dir_flags, fio::MODE_TYPE_DIRECTORY, ".")
                .await;

        for file_flags in file_flag_combos {
            if file_flags.is_empty() {
                continue; // The rights in file_flags must *exceed* those in dir_flags.
            }
            // Ensure the combination is valid (e.g. that file_flags is requesting more rights
            // than those in dir_flags).
            assert!(
                (*file_flags & harness.dir_rights.all()) != (*dir_flags & harness.dir_rights.all()),
                "Invalid test: file rights must exceed dir! (flags: dir = {:?}, file = {:?})",
                *dir_flags,
                *file_flags
            );

            let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");

            dir_proxy
                .open(
                    *file_flags | fio::OpenFlags::DESCRIBE,
                    fio::MODE_TYPE_FILE,
                    TEST_FILE,
                    server,
                )
                .expect("Cannot open file");

            assert_eq!(
                get_open_status(&client).await,
                zx::Status::ACCESS_DENIED,
                "Opened a file with more rights than the directory! (flags: dir = {:?}, file = {:?})",
                *dir_flags,
                *file_flags
            );
        }
    }
}
