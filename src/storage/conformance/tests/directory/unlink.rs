// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx,
    io_conformance_util::{test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn unlink_file_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_unlink.unwrap_or_default() {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness
        .dir_rights
        .valid_combos_with(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE)
    {
        let root =
            root_directory(vec![directory("src", vec![file("file.txt", contents.to_vec())])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;

        let file = open_node::<fio::FileMarker>(
            &src_dir,
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_FILE,
            "file.txt",
        )
        .await;

        src_dir
            .unlink("file.txt", fio::UnlinkOptions::EMPTY)
            .await
            .expect("unlink fidl failed")
            .expect("unlink failed");

        // Check file is gone.
        assert_file_not_found(&test_dir, "src/file.txt").await;

        // Ensure file connection remains usable.
        let read_result = file
            .read(contents.len() as u64)
            .await
            .expect("read failed")
            .map_err(zx::Status::from_raw)
            .expect("read error");

        assert_eq!(read_result, contents);
    }
}

#[fasync::run_singlethreaded(test)]
async fn unlink_file_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_unlink.unwrap_or_default() {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.dir_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root =
            root_directory(vec![directory("src", vec![file("file.txt", contents.to_vec())])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;

        assert_eq!(
            src_dir
                .unlink("file.txt", fio::UnlinkOptions::EMPTY)
                .await
                .expect("unlink fidl failed")
                .expect_err("unlink succeeded"),
            zx::sys::ZX_ERR_BAD_HANDLE
        );

        // Check file still exists.
        assert_eq!(read_file(&test_dir, "src/file.txt").await, contents);
    }
}

#[fasync::run_singlethreaded(test)]
async fn unlink_directory_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_unlink.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.dir_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![directory("src", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        // Re-open dir with flags being tested.
        let dir = open_dir_with_flags(&test_dir, dir_flags, ".").await;

        dir.unlink("src", fio::UnlinkOptions::EMPTY)
            .await
            .expect("unlink fidl failed")
            .expect("unlink failed");
    }
}

#[fasync::run_singlethreaded(test)]
async fn unlink_directory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_unlink.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.dir_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![directory("src", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        // Re-open dir with flags being tested.
        let dir = open_dir_with_flags(&test_dir, dir_flags, ".").await;

        assert_eq!(
            dir.unlink("src", fio::UnlinkOptions::EMPTY)
                .await
                .expect("unlink fidl failed")
                .expect_err("unlink succeeded"),
            zx::sys::ZX_ERR_BAD_HANDLE
        );
    }
}

#[fasync::run_singlethreaded(test)]
async fn unlink_must_be_directory() {
    let harness = TestHarness::new().await;

    if !harness.config.supports_unlink.unwrap_or_default() {
        return;
    }

    let root = root_directory(vec![directory("dir", vec![]), file("file", vec![])]);
    let test_dir = harness.get_directory(root, harness.dir_rights.all());

    let must_be_directory = fio::UnlinkOptions {
        flags: Some(fio::UnlinkFlags::MUST_BE_DIRECTORY),
        ..fio::UnlinkOptions::EMPTY
    };
    test_dir
        .unlink("dir", must_be_directory.clone())
        .await
        .expect("unlink fidl failed")
        .expect("unlink dir failed");
    assert_eq!(
        test_dir
            .unlink("file", must_be_directory)
            .await
            .expect("unlink fidl failed")
            .expect_err("unlink file succeeded"),
        zx::sys::ZX_ERR_NOT_DIR
    );
}
