// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx,
    io_conformance_util::{test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn get_token_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_token.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![]);
        let test_dir = harness.get_directory(root, dir_flags);

        let (status, _handle) = test_dir.get_token().await.expect("get_token failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        // Handle is tested in other test cases.
    }
}

#[fasync::run_singlethreaded(test)]
async fn get_token_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_token.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![]);
        let test_dir = harness.get_directory(root, dir_flags);

        let (status, _handle) = test_dir.get_token().await.expect("get_token failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);
    }
}
