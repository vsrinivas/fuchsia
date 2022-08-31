// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx,
    io_conformance_util::{flags::build_flag_combinations, test_harness::TestHarness, *},
};

#[fasync::run_singlethreaded(test)]
async fn clone_file_with_same_or_fewer_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos() {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let file = open_file_with_flags(&test_dir, file_flags, TEST_FILE).await;

        // Clone using every subset of flags.
        for clone_flags in build_flag_combinations(0, file_flags.bits()) {
            let clone_flags = fio::OpenFlags::from_bits_truncate(clone_flags);
            let (proxy, server) = create_proxy::<fio::NodeMarker>().expect("create_proxy failed");
            file.clone(clone_flags | fio::OpenFlags::DESCRIBE, server).expect("clone failed");
            let status = get_open_status(&proxy).await;
            assert_eq!(status, zx::Status::OK);

            // Check flags of cloned connection are correct.
            let proxy = convert_node_proxy::<fio::FileMarker>(proxy);
            let (status, flags) = proxy.get_flags().await.expect("get_flags failed");
            assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
            assert_eq!(flags, clone_flags);
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn clone_file_with_same_rights_flag() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos() {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let file = open_file_with_flags(&test_dir, file_flags, TEST_FILE).await;

        // Clone using CLONE_FLAG_SAME_RIGHTS.
        let (proxy, server) = create_proxy::<fio::NodeMarker>().expect("create_proxy failed");
        file.clone(fio::OpenFlags::CLONE_SAME_RIGHTS | fio::OpenFlags::DESCRIBE, server)
            .expect("clone failed");
        let status = get_open_status(&proxy).await;
        assert_eq!(status, zx::Status::OK);

        // Check flags of cloned connection are correct.
        let proxy = convert_node_proxy::<fio::FileMarker>(proxy);
        let (status, flags) = proxy.get_flags().await.expect("get_flags failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        assert_eq!(flags, file_flags);
    }
}

#[fasync::run_singlethreaded(test)]
async fn clone_file_with_additional_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos() {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let file = open_file_with_flags(&test_dir, file_flags, TEST_FILE).await;

        // Clone using every superset of flags, should fail.
        for clone_flags in
            build_flag_combinations(file_flags.bits(), harness.dir_rights.all().bits())
        {
            let clone_flags = fio::OpenFlags::from_bits_truncate(clone_flags);
            if clone_flags == file_flags {
                continue;
            }
            let (proxy, server) = create_proxy::<fio::NodeMarker>().expect("create_proxy failed");
            file.clone(clone_flags | fio::OpenFlags::DESCRIBE, server).expect("clone failed");
            let status = get_open_status(&proxy).await;
            assert_eq!(status, zx::Status::ACCESS_DENIED);
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn clone_directory_with_same_or_fewer_rights() {
    let harness = TestHarness::new().await;

    for dir_flags in harness.dir_rights.valid_combos() {
        let root = root_directory(vec![directory("dir", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let dir = open_dir_with_flags(&test_dir, dir_flags, "dir").await;

        // Clone using every subset of flags.
        for clone_flags in build_flag_combinations(0, dir_flags.bits()) {
            let clone_flags = fio::OpenFlags::from_bits_truncate(clone_flags);
            let (proxy, server) = create_proxy::<fio::NodeMarker>().expect("create_proxy failed");
            dir.clone(clone_flags | fio::OpenFlags::DESCRIBE, server).expect("clone failed");
            let status = get_open_status(&proxy).await;
            assert_eq!(status, zx::Status::OK);

            // Check flags of cloned connection are correct.
            let (status, flags) = proxy.get_flags().await.expect("get_flags failed");
            assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
            assert_eq!(flags, clone_flags);
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn clone_directory_with_same_rights_flag() {
    let harness = TestHarness::new().await;

    for dir_flags in harness.dir_rights.valid_combos() {
        let root = root_directory(vec![directory("dir", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let dir = open_dir_with_flags(&test_dir, dir_flags, "dir").await;

        // Clone using CLONE_FLAG_SAME_RIGHTS.
        let (proxy, server) = create_proxy::<fio::NodeMarker>().expect("create_proxy failed");
        dir.clone(fio::OpenFlags::CLONE_SAME_RIGHTS | fio::OpenFlags::DESCRIBE, server)
            .expect("clone failed");
        let status = get_open_status(&proxy).await;
        assert_eq!(status, zx::Status::OK);

        // Check flags of cloned connection are correct.
        let proxy = convert_node_proxy::<fio::DirectoryMarker>(proxy);
        let (status, flags) = proxy.get_flags().await.expect("get_flags failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        assert_eq!(flags, dir_flags);
    }
}

#[fasync::run_singlethreaded(test)]
async fn clone_directory_with_additional_rights() {
    let harness = TestHarness::new().await;

    for dir_flags in harness.dir_rights.valid_combos() {
        let root = root_directory(vec![directory("dir", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let dir = open_dir_with_flags(&test_dir, dir_flags, "dir").await;

        // Clone using every superset of flags, should fail.
        for clone_flags in
            build_flag_combinations(dir_flags.bits(), harness.dir_rights.all().bits())
        {
            let clone_flags = fio::OpenFlags::from_bits_truncate(clone_flags);
            if clone_flags == dir_flags {
                continue;
            }
            let (proxy, server) = create_proxy::<fio::NodeMarker>().expect("create_proxy failed");
            dir.clone(clone_flags | fio::OpenFlags::DESCRIBE, server).expect("clone failed");
            let status = get_open_status(&proxy).await;
            assert_eq!(status, zx::Status::ACCESS_DENIED);
        }
    }
}
