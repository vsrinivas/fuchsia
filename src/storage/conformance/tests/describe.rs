// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    io_conformance_util::{test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn directory_describe() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![]);
    let test_dir = harness.get_directory(root, fio::OpenFlags::empty());

    let node_info = test_dir.describe_deprecated().await.expect("describe failed");

    assert!(matches!(node_info, fio::NodeInfoDeprecated::Directory { .. }));
}

#[fasync::run_singlethreaded(test)]
async fn file_describe() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let test_dir = harness.get_directory(root, fio::OpenFlags::RIGHT_READABLE);
    let file = open_node::<fio::FileMarker>(
        &test_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_FILE,
        TEST_FILE,
    )
    .await;

    let node_info = file.describe_deprecated().await.expect("describe failed");

    assert!(matches!(node_info, fio::NodeInfoDeprecated::File { .. }));
}

#[fasync::run_singlethreaded(test)]
async fn vmo_file_describe() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_vmo_file.unwrap_or_default() {
        return;
    }

    let root = root_directory(vec![vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024)]);
    let test_dir = harness
        .get_directory(root, fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
    let file = open_node::<fio::FileMarker>(
        &test_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_FILE,
        TEST_FILE,
    )
    .await;

    let node_info = file.describe_deprecated().await.expect("describe failed");

    assert!(matches!(node_info, fio::NodeInfoDeprecated::File { .. }));
}
