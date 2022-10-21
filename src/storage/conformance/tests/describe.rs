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

    {
        let dir = open_node::<fio::DirectoryMarker>(
            &test_dir,
            fio::OpenFlags::NODE_REFERENCE,
            fio::MODE_TYPE_DIRECTORY,
            ".",
        )
        .await;

        let protocol = dir.query().await.expect("query failed");
        assert_eq!(std::str::from_utf8(&protocol), Ok(fio::NODE_PROTOCOL_NAME));
    }
    {
        let dir = open_node::<fio::DirectoryMarker>(
            &test_dir,
            fio::OpenFlags::empty(),
            fio::MODE_TYPE_DIRECTORY,
            ".",
        )
        .await;

        let protocol = dir.query().await.expect("query failed");
        assert_eq!(std::str::from_utf8(&protocol), Ok(fio::DIRECTORY_PROTOCOL_NAME));
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_query() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let test_dir = harness.get_directory(root, fio::OpenFlags::empty());
    {
        let file = open_node::<fio::FileMarker>(
            &test_dir,
            fio::OpenFlags::NODE_REFERENCE,
            fio::MODE_TYPE_FILE,
            TEST_FILE,
        )
        .await;

        let protocol = file.query().await.expect("query failed");
        assert_eq!(std::str::from_utf8(&protocol), Ok(fio::NODE_PROTOCOL_NAME));
    }
    {
        let file = open_node::<fio::FileMarker>(
            &test_dir,
            fio::OpenFlags::empty(),
            fio::MODE_TYPE_FILE,
            TEST_FILE,
        )
        .await;

        let protocol = file.query().await.expect("query failed");
        assert_eq!(std::str::from_utf8(&protocol), Ok(fio::FILE_PROTOCOL_NAME));
    }
}

#[fasync::run_singlethreaded(test)]
async fn vmo_file_query() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_vmo_file.unwrap_or_default() {
        return;
    }

    let root = root_directory(vec![vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024)]);
    let test_dir = harness.get_directory(root, fio::OpenFlags::empty());
    {
        let file = open_node::<fio::FileMarker>(
            &test_dir,
            fio::OpenFlags::NODE_REFERENCE,
            fio::MODE_TYPE_FILE,
            TEST_FILE,
        )
        .await;

        let protocol = file.query().await.expect("query failed");
        assert_eq!(std::str::from_utf8(&protocol), Ok(fio::NODE_PROTOCOL_NAME));
    }
    {
        let file = open_node::<fio::FileMarker>(
            &test_dir,
            fio::OpenFlags::empty(),
            fio::MODE_TYPE_FILE,
            TEST_FILE,
        )
        .await;

        let protocol = file.query().await.expect("query failed");
        assert_eq!(std::str::from_utf8(&protocol), Ok(fio::FILE_PROTOCOL_NAME));
    }
}
