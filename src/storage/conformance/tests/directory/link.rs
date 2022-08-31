// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx,
    io_conformance_util::{test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn link_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_link.unwrap_or_default()
        || !harness.config.supports_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.dir_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![
            directory("src", vec![file("old.txt", contents.to_vec())]),
            directory("dest", vec![]),
        ]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;
        let dest_dir = open_rw_dir(&test_dir, "dest").await;
        let dest_token = get_token(&dest_dir).await;

        // Link src/old.txt -> dest/new.txt.
        let status = src_dir.link("old.txt", dest_token, "new.txt").await.expect("link failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        // Check dest/new.txt was created and has correct contents.
        assert_eq!(read_file(&test_dir, "dest/new.txt").await, contents);

        // Check src/old.txt still exists.
        assert_eq!(read_file(&test_dir, "src/old.txt").await, contents);
    }
}

#[fasync::run_singlethreaded(test)]
async fn link_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_link.unwrap_or_default()
        || !harness.config.supports_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.dir_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![
            directory("src", vec![file("old.txt", contents.to_vec())]),
            directory("dest", vec![]),
        ]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;
        let dest_dir = open_rw_dir(&test_dir, "dest").await;
        let dest_token = get_token(&dest_dir).await;

        // Link src/old.txt -> dest/new.txt.
        let status = src_dir.link("old.txt", dest_token, "new.txt").await.expect("link failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);

        // Check dest/new.txt was not created.
        assert_file_not_found(&test_dir, "dest/new.txt").await;

        // Check src/old.txt still exists.
        assert_eq!(read_file(&test_dir, "src/old.txt").await, contents);
    }
}
