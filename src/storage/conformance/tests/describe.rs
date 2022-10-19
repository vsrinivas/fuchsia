// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    io_conformance_util::{test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn directory_query() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![]);
    let test_dir = harness.get_directory(root, fio::OpenFlags::empty());

    let protocol = test_dir.query().await.expect("query failed");
    assert_eq!(protocol, fio::DIRECTORY_PROTOCOL_NAME.as_bytes());
}

#[fasync::run_singlethreaded(test)]
async fn file_query() {
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

    let protocol = file.query().await.expect("query failed");
    assert_eq!(protocol, fio::FILE_PROTOCOL_NAME.as_bytes());
}

#[fasync::run_singlethreaded(test)]
async fn vmo_file_query() {
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

    let protocol = file.query().await.expect("query failed");
    assert_eq!(protocol, fio::FILE_PROTOCOL_NAME.as_bytes());
}
