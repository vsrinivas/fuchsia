// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx,
    io_conformance_util::{test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn file_write_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        let _: u64 = file
            .write("".as_bytes())
            .await
            .expect("write failed")
            .map_err(zx::Status::from_raw)
            .expect("write error");
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_with_insufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        let result =
            file.write("".as_bytes()).await.expect("write failed").map_err(zx::Status::from_raw);
        assert_eq!(result, Err(zx::Status::BAD_HANDLE))
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_at_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        let _: u64 = file
            .write_at("".as_bytes(), 0)
            .await
            .expect("write_at failed")
            .map_err(zx::Status::from_raw)
            .expect("write_at error");
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_at_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    for file_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        let result = file
            .write_at("".as_bytes(), 0)
            .await
            .expect("write_at failed")
            .map_err(zx::Status::from_raw);
        assert_eq!(result, Err(zx::Status::BAD_HANDLE));
    }
}

#[fasync::run_singlethreaded(test)]
async fn vmo_file_write_to_limits() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_vmo_file.unwrap_or_default() {
        return;
    }

    let capacity = 128 * 1024;
    let root = root_directory(vec![vmo_file(TEST_FILE, TEST_FILE_CONTENTS, capacity)]);
    let test_dir = harness
        .get_directory(root, fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
    let file = open_node::<fio::FileMarker>(
        &test_dir,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        fio::MODE_TYPE_FILE,
        TEST_FILE,
    )
    .await;
    let _: Vec<_> = file.query().await.expect("query failed");

    let data = vec![0u8];

    // The VMO file will have a capacity
    file.write_at(&data[..], capacity)
        .await
        .expect("fidl call failed")
        .expect_err("Write at end of file limit should fail");
    // An empty write should still succeed even if it targets an out-of-bounds offset.
    file.write_at(&[], capacity)
        .await
        .expect("fidl call failed")
        .expect("Zero-byte write at end of file limit should succeed");
}
