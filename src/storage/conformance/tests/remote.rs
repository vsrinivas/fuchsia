// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    io_conformance_util::{
        file, open_node, remote_directory, root_directory, test_harness::TestHarness, TEST_FILE,
    },
};

/// Creates a directory with a remote mount inside of it, and checks that the remote can be opened.
#[fasync::run_singlethreaded(test)]
async fn open_remote_directory_test() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_remote_dir.unwrap_or_default() {
        return;
    }

    let remote_name = "remote_directory";
    let remote_mount = root_directory(vec![]);
    let remote_client = harness.get_directory(
        remote_mount,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    );

    // Create a directory with the remote directory inside of it.
    let root = root_directory(vec![remote_directory(remote_name, remote_client)]);
    let root_dir = harness
        .get_directory(root, fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);

    open_node::<fio::DirectoryMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        fio::MODE_TYPE_DIRECTORY,
        remote_name,
    )
    .await;
}

/// Creates a directory with a remote mount containing a file inside of it, and checks that the
/// file can be opened through the remote.
#[fasync::run_singlethreaded(test)]
async fn open_remote_file_test() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_remote_dir.unwrap_or_default() {
        return;
    }

    let remote_name = "remote_directory";
    let remote_dir = root_directory(vec![file(TEST_FILE, vec![])]);
    let remote_client = harness.get_directory(remote_dir, fio::OpenFlags::RIGHT_READABLE);

    // Create a directory with the remote directory inside of it.
    let root = root_directory(vec![remote_directory(remote_name, remote_client)]);
    let root_dir = harness
        .get_directory(root, fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);

    // Test opening file by opening the remote directory first and then opening the file.
    let remote_dir_proxy = open_node::<fio::DirectoryMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        remote_name,
    )
    .await;
    open_node::<fio::NodeMarker>(&remote_dir_proxy, fio::OpenFlags::RIGHT_READABLE, 0, TEST_FILE)
        .await;

    // Test opening file directly though local directory by crossing remote automatically.
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        [remote_name, "/", TEST_FILE].join("").as_str(),
    )
    .await;
}

/// Ensure specifying POSIX_* flags cannot cause rights escalation (fxbug.dev/40862).
/// The test sets up the following hierarchy of nodes:
///
/// --------------------- RW   --------------------------
/// |  root_proxy       | ---> |  root                  |
/// --------------------- (a)  |   - /mount_point       | RWX  ---------------------
///                            |     (remote_proxy)     | ---> |  remote_dir       |
///                            -------------------------- (b)  ---------------------
///
/// To validate the right escalation issue has been resolved, we call Open() on the test_dir_proxy
/// passing in both POSIX_* flags, which if handled correctly, should result in opening
/// remote_dir_server as RW (and NOT RWX, which can occur if both flags are passed directly to the
/// remote instead of being removed).
#[fasync::run_singlethreaded(test)]
async fn open_remote_directory_right_escalation_test() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_remote_dir.unwrap_or_default() {
        return;
    }

    let mount_point = "mount_point";

    // Use the test harness to serve a directory with RWX permissions.
    let remote_dir = root_directory(vec![]);
    let remote_proxy = harness.get_directory(
        remote_dir,
        fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::RIGHT_EXECUTABLE,
    );

    // Mount the remote directory through root, and ensure that the connection only has RW
    // RW permissions (which is thus a sub-set of the permissions the remote_proxy has).
    let root = root_directory(vec![remote_directory(mount_point, remote_proxy)]);
    let root_proxy = harness
        .get_directory(root, fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);

    // Create a new proxy/server for opening the remote node through test_dir_proxy.
    // Here we pass the POSIX flag, which should only expand to the maximum set of
    // rights available along the open chain.
    let (node_proxy, node_server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy");
    root_proxy
        .open(
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::POSIX_WRITABLE
                | fio::OpenFlags::POSIX_EXECUTABLE,
            fio::MODE_TYPE_DIRECTORY,
            mount_point,
            node_server,
        )
        .expect("Cannot open remote directory");

    // Since the root node only has RW permissions, and even though the remote has RWX,
    // we should only get RW permissions back.
    let (_, node_flags) = node_proxy.get_flags().await.unwrap();
    assert_eq!(node_flags, fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
}
