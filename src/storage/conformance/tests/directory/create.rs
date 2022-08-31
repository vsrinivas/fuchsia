// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx,
    io_conformance_util::{test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn create_file_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_create.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        // Re-open directory with the flags being tested.
        let dir = open_dir_with_flags(&test_dir, dir_flags, ".").await;
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");

        dir.open(
            dir_flags | fio::OpenFlags::CREATE | fio::OpenFlags::DESCRIBE,
            fio::MODE_TYPE_FILE,
            TEST_FILE,
            server,
        )
        .expect("Cannot open file");

        assert_eq!(get_open_status(&client).await, zx::Status::OK);
        assert_eq!(read_file(&test_dir, TEST_FILE).await, &[]);
    }
}

#[fasync::run_singlethreaded(test)]
async fn create_file_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_create.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        // Re-open directory with the flags being tested.
        let dir = open_dir_with_flags(&test_dir, dir_flags, ".").await;
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");

        dir.open(
            dir_flags | fio::OpenFlags::CREATE | fio::OpenFlags::DESCRIBE,
            fio::MODE_TYPE_FILE,
            TEST_FILE,
            server,
        )
        .expect("Cannot open file");

        assert_eq!(get_open_status(&client).await, zx::Status::ACCESS_DENIED);
        assert_file_not_found(&test_dir, TEST_FILE).await;
    }
}
