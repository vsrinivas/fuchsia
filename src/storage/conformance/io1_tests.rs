// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{
        endpoints::{create_proxy, ClientEnd, ProtocolMarker, Proxy},
        AsHandleRef,
    },
    fidl_fuchsia_io as fio, fidl_fuchsia_io_test as io_test,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_zircon as zx,
    futures::StreamExt,
    io_conformance_util::{flags::build_flag_combinations, test_harness::TestHarness},
    libc,
};

const TEST_FILE: &str = "testing.txt";

const TEST_FILE_CONTENTS: &[u8] = "abcdef".as_bytes();

const EMPTY_NODE_ATTRS: fio::NodeAttributes = fio::NodeAttributes {
    mode: 0,
    id: 0,
    content_size: 0,
    storage_size: 0,
    link_count: 0,
    creation_time: 0,
    modification_time: 0,
};

/// Listens for the `OnOpen` event and returns its [Status].
async fn get_open_status(node_proxy: &fio::NodeProxy) -> zx::Status {
    let mut events = node_proxy.take_event_stream();
    if let Some(result) = events.next().await {
        match result.expect("FIDL error") {
            fio::NodeEvent::OnOpen_ { s, info: _ } => zx::Status::from_raw(s),
            fio::NodeEvent::OnConnectionInfo { .. } => zx::Status::OK,
        }
    } else {
        zx::Status::PEER_CLOSED
    }
}

async fn assert_on_open_not_received(node_proxy: &fio::NodeProxy) {
    let mut events = node_proxy.take_event_stream();
    // Wait at most 200ms for an OnOpen event to appear.
    let event =
        events.next().on_timeout(zx::Duration::from_millis(200).after_now(), || Option::None).await;
    assert!(event.is_none(), "Unexpected OnOpen event received");
}

/// Converts a generic `NodeProxy` to either a file or directory proxy.
fn convert_node_proxy<T: ProtocolMarker>(proxy: fio::NodeProxy) -> T::Proxy {
    T::Proxy::from_channel(proxy.into_channel().expect("Cannot convert node proxy to channel"))
}

/// Helper function to open the desired node in the root folder.
/// Asserts that open_node_status succeeds.
async fn open_node<T: ProtocolMarker>(
    dir: &fio::DirectoryProxy,
    flags: fio::OpenFlags,
    mode: u32,
    path: &str,
) -> T::Proxy {
    open_node_status::<T>(dir, flags, mode, path)
        .await
        .expect(&format!("open_node_status failed for {}", path))
}

/// Helper function to open the desired node in the root folder.
async fn open_node_status<T: ProtocolMarker>(
    dir: &fio::DirectoryProxy,
    flags: fio::OpenFlags,
    mode: u32,
    path: &str,
) -> Result<T::Proxy, zx::Status> {
    let flags = flags | fio::OpenFlags::DESCRIBE;
    let (node_proxy, node_server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy");
    dir.open(flags, mode, path, node_server).expect("Cannot open node");
    let status = get_open_status(&node_proxy).await;

    if status != zx::Status::OK {
        Err(status)
    } else {
        Ok(convert_node_proxy::<T>(node_proxy))
    }
}

/// Returns the specified node flags from the given NodeProxy.
async fn get_node_flags(node_proxy: &fio::NodeProxy) -> fio::OpenFlags {
    let (_, node_flags) = node_proxy.get_flags().await.expect("Failed to get node flags!");
    return node_flags;
}

/// Helper function to open a file with the given flags. Only use this if testing something other
/// than the open call directly.
async fn open_file_with_flags(
    parent_dir: &fio::DirectoryProxy,
    flags: fio::OpenFlags,
    path: &str,
) -> fio::FileProxy {
    open_node::<fio::FileMarker>(&parent_dir, flags, fio::MODE_TYPE_FILE, path).await
}

/// Helper function to open a sub-directory with the given flags. Only use this if testing
/// something other than the open call directly.
async fn open_dir_with_flags(
    parent_dir: &fio::DirectoryProxy,
    flags: fio::OpenFlags,
    path: &str,
) -> fio::DirectoryProxy {
    open_node::<fio::DirectoryMarker>(&parent_dir, flags, fio::MODE_TYPE_DIRECTORY, path).await
}

/// Helper function to open a sub-directory as readable and writable. Only use this if testing
/// something other than the open call directly.
async fn open_rw_dir(parent_dir: &fio::DirectoryProxy, path: &str) -> fio::DirectoryProxy {
    open_dir_with_flags(
        parent_dir,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        path,
    )
    .await
}

/// Helper function to call `get_token` on a directory. Only use this if testing something
/// other than the `get_token` call directly.
async fn get_token(dir: &fio::DirectoryProxy) -> fidl::Handle {
    let (status, token) = dir.get_token().await.expect("get_token failed");
    assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    token.expect("handle missing")
}

/// Helper function to read a file and return its contents. Only use this if testing something other
/// than the read call directly.
async fn read_file(dir: &fio::DirectoryProxy, path: &str) -> Vec<u8> {
    let file = open_node::<fio::FileMarker>(
        dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_FILE,
        path,
    )
    .await;
    file.read(100).await.expect("read failed").map_err(zx::Status::from_raw).expect("read error")
}

/// Attempts to open the given file, and checks the status is `NOT_FOUND`.
async fn assert_file_not_found(dir: &fio::DirectoryProxy, path: &str) {
    let (file_proxy, file_server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy");
    dir.open(
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DESCRIBE,
        fio::MODE_TYPE_FILE,
        path,
        file_server,
    )
    .expect("Cannot open file");
    assert_eq!(get_open_status(&file_proxy).await, zx::Status::NOT_FOUND);
}

/// Returns the .name field from a given DirectoryEntry, otherwise panics.
fn get_directory_entry_name(dir_entry: &io_test::DirectoryEntry) -> String {
    use io_test::DirectoryEntry;
    match dir_entry {
        DirectoryEntry::Directory(entry) => entry.name.as_ref(),
        DirectoryEntry::RemoteDirectory(entry) => entry.name.as_ref(),
        DirectoryEntry::File(entry) => entry.name.as_ref(),
        DirectoryEntry::VmoFile(entry) => entry.name.as_ref(),
        DirectoryEntry::ExecutableFile(entry) => entry.name.as_ref(),
    }
    .expect("DirectoryEntry name is None!")
    .clone()
}

/// Asserts that the given `vmo_rights` align with the `vmo_flags` passed to a get_backing_memory call.
/// We check that the returned rights align with and do not exceed those in the given flags, that
/// we have at least basic VMO rights, and that the flags align with the expected sharing mode.
fn validate_vmo_rights(vmo: &zx::Vmo, vmo_flags: fio::VmoFlags) {
    let vmo_rights: zx::Rights = vmo.basic_info().expect("failed to get VMO info").rights;

    // Ensure that we have at least some basic rights.
    assert!(vmo_rights.contains(zx::Rights::BASIC));
    assert!(vmo_rights.contains(zx::Rights::MAP));
    assert!(vmo_rights.contains(zx::Rights::GET_PROPERTY));

    // Ensure the returned rights match and do not exceed those we requested in `vmo_flags`.
    assert!(vmo_rights.contains(zx::Rights::READ) == vmo_flags.contains(fio::VmoFlags::READ));
    assert!(vmo_rights.contains(zx::Rights::WRITE) == vmo_flags.contains(fio::VmoFlags::WRITE));
    assert!(vmo_rights.contains(zx::Rights::EXECUTE) == vmo_flags.contains(fio::VmoFlags::EXECUTE));

    // Make sure we get SET_PROPERTY if we specified a private copy.
    if vmo_flags.contains(fio::VmoFlags::PRIVATE_CLONE) {
        assert!(vmo_rights.contains(zx::Rights::SET_PROPERTY));
    }
}

/// Creates a directory with the given DirectoryEntry, opening the file with the given
/// file flags, and returning a Buffer object initialized with the given vmo_flags.
async fn create_file_and_get_backing_memory(
    dir_entry: io_test::DirectoryEntry,
    test_harness: &TestHarness,
    file_flags: fio::OpenFlags,
    vmo_flags: fio::VmoFlags,
) -> Result<(zx::Vmo, (fio::DirectoryProxy, fio::FileProxy)), zx::Status> {
    let file_path = get_directory_entry_name(&dir_entry);
    let root = root_directory(vec![dir_entry]);
    let dir_proxy = test_harness.get_directory(root, file_flags);
    let file_proxy = open_node_status::<fio::FileMarker>(
        &dir_proxy,
        file_flags,
        fio::MODE_TYPE_FILE,
        &file_path,
    )
    .await?;
    let vmo = file_proxy
        .get_backing_memory(vmo_flags)
        .await
        .expect("get_backing_memory failed")
        .map_err(zx::Status::from_raw)?;
    Ok((vmo, (dir_proxy, file_proxy)))
}

fn root_directory(entries: Vec<io_test::DirectoryEntry>) -> io_test::Directory {
    // Convert the simple vector of entries into the convoluted FIDL field type.
    let entries: Vec<Option<Box<io_test::DirectoryEntry>>> =
        entries.into_iter().map(|e| Some(Box::new(e))).collect();
    io_test::Directory { name: None, entries: Some(entries), ..io_test::Directory::EMPTY }
}

fn directory(name: &str, entries: Vec<io_test::DirectoryEntry>) -> io_test::DirectoryEntry {
    let mut dir = root_directory(entries);
    dir.name = Some(name.to_string());
    io_test::DirectoryEntry::Directory(dir)
}

fn remote_directory(name: &str, remote_dir: fio::DirectoryProxy) -> io_test::DirectoryEntry {
    let remote_client = ClientEnd::<fio::DirectoryMarker>::new(
        remote_dir.into_channel().unwrap().into_zx_channel(),
    );

    io_test::DirectoryEntry::RemoteDirectory(io_test::RemoteDirectory {
        name: Some(name.to_string()),
        remote_client: Some(remote_client),
        ..io_test::RemoteDirectory::EMPTY
    })
}

fn file(name: &str, contents: Vec<u8>) -> io_test::DirectoryEntry {
    io_test::DirectoryEntry::File(io_test::File {
        name: Some(name.to_string()),
        contents: Some(contents),
        ..io_test::File::EMPTY
    })
}

fn vmo_file(name: &str, contents: &[u8], capacity: u64) -> io_test::DirectoryEntry {
    let vmo = zx::Vmo::create(capacity).expect("Cannot create VMO");
    let () = vmo.write(contents, 0).expect("Cannot write to VMO");
    let () = vmo.set_content_size(&(contents.len() as u64)).expect("Cannot set VMO content size");
    io_test::DirectoryEntry::VmoFile(io_test::VmoFile {
        name: Some(name.to_string()),
        vmo: Some(vmo),
        ..io_test::VmoFile::EMPTY
    })
}

fn executable_file(name: &str) -> io_test::DirectoryEntry {
    io_test::DirectoryEntry::ExecutableFile(io_test::ExecutableFile {
        name: Some(name.to_string()),
        ..io_test::ExecutableFile::EMPTY
    })
}

// Validate allowed rights for Directory objects.
#[fasync::run_singlethreaded(test)]
async fn validate_directory_rights() {
    let harness = TestHarness::new().await;
    // Create a test directory and ensure we can open it with all supported rights.
    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let _root_dir = harness.get_directory(
        root,
        fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::RIGHT_EXECUTABLE,
    );
}

// Validate allowed rights for File objects (ensures writable files cannot be opened as executable).
#[fasync::run_singlethreaded(test)]
async fn validate_file_rights() {
    let harness = TestHarness::new().await;
    // Create a test directory with a single File object, and ensure the directory has all rights.
    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    // Opening as READABLE must succeed.
    open_node::<fio::NodeMarker>(&root_dir, fio::OpenFlags::RIGHT_READABLE, 0, TEST_FILE).await;

    if harness.config.mutable_file.unwrap_or_default() {
        // Opening as WRITABLE must succeed.
        open_node::<fio::NodeMarker>(&root_dir, fio::OpenFlags::RIGHT_WRITABLE, 0, TEST_FILE).await;
        // Opening as EXECUTABLE must fail (W^X).
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_EXECUTABLE,
            0,
            TEST_FILE,
        )
        .await
        .expect_err("open succeeded");
    } else {
        // If files are immutable, check that opening as WRITABLE results in access denied.
        // All other combinations are valid in this case.
        assert_eq!(
            open_node_status::<fio::NodeMarker>(
                &root_dir,
                fio::OpenFlags::RIGHT_WRITABLE,
                0,
                TEST_FILE
            )
            .await
            .expect_err("open succeeded"),
            zx::Status::ACCESS_DENIED
        );
    }
}

// Validate allowed rights for VmoFile objects (ensures cannot be opened as executable).
#[fasync::run_singlethreaded(test)]
async fn validate_vmo_file_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_vmo_file.unwrap_or_default() {
        return;
    }
    // Create a test directory with a VmoFile object, and ensure the directory has all rights.
    let root = root_directory(vec![vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024)]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());
    // Opening with READ/WRITE should succeed.
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        0,
        TEST_FILE,
    )
    .await;
    // Opening with EXECUTE must fail to ensure W^X enforcement.
    assert!(matches!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_EXECUTABLE,
            0,
            TEST_FILE
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::ACCESS_DENIED | zx::Status::NOT_SUPPORTED
    ));
}

// Validate allowed rights for ExecutableFile objects (ensures cannot be opened as writable).
#[fasync::run_singlethreaded(test)]
async fn validate_executable_file_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_executable_file.unwrap_or_default() {
        return;
    }
    // Create a test directory with an ExecutableFile object, and ensure the directory has all rights.
    let root = root_directory(vec![executable_file(TEST_FILE)]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());
    // Opening with READABLE/EXECUTABLE should succeed.
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        0,
        TEST_FILE,
    )
    .await;
    // Opening with WRITABLE must fail to ensure W^X enforcement.
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_WRITABLE,
            0,
            TEST_FILE
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::ACCESS_DENIED
    );
}

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

/// Creates a directory with all rights, and checks it can be opened for all subsets of rights.
#[fasync::run_singlethreaded(test)]
async fn open_dir_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    for dir_flags in harness.dir_rights.valid_combos() {
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
        root_dir
            .open(dir_flags | fio::OpenFlags::DESCRIBE, fio::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_eq!(get_open_status(&client).await, zx::Status::OK);
    }
}

/// Creates a directory with no rights, and checks opening it with any rights fails.
#[fasync::run_singlethreaded(test)]
async fn open_dir_with_insufficient_rights() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, fio::OpenFlags::empty());

    for dir_flags in harness.dir_rights.valid_combos() {
        if dir_flags.is_empty() {
            continue;
        }
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
        root_dir
            .open(dir_flags | fio::OpenFlags::DESCRIBE, fio::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_eq!(get_open_status(&client).await, zx::Status::ACCESS_DENIED);
    }
}

/// Opens a directory, and checks that a child directory can be opened using the same rights.
#[fasync::run_singlethreaded(test)]
async fn open_child_dir_with_same_rights() {
    let harness = TestHarness::new().await;

    for dir_flags in harness.dir_rights.valid_combos() {
        let root = root_directory(vec![directory("child", vec![])]);
        let root_dir = harness.get_directory(root, harness.dir_rights.all());

        let parent_dir =
            open_node::<fio::DirectoryMarker>(&root_dir, dir_flags, fio::MODE_TYPE_DIRECTORY, ".")
                .await;

        // Open child directory with same flags as parent.
        let (child_dir_client, child_dir_server) =
            create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
        parent_dir
            .open(
                dir_flags | fio::OpenFlags::DESCRIBE,
                fio::MODE_TYPE_DIRECTORY,
                "child",
                child_dir_server,
            )
            .expect("Cannot open directory");

        assert_eq!(get_open_status(&child_dir_client).await, zx::Status::OK);
    }
}

/// Opens a directory as readable, and checks that a child directory cannot be opened as writable.
#[fasync::run_singlethreaded(test)]
async fn open_child_dir_with_extra_rights() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![directory("child", vec![])]);
    let root_dir = harness.get_directory(root, fio::OpenFlags::RIGHT_READABLE);

    // Open parent as readable.
    let parent_dir = open_node::<fio::DirectoryMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        ".",
    )
    .await;

    // Opening child as writable should fail.
    let (child_dir_client, child_dir_server) =
        create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
    parent_dir
        .open(
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::DESCRIBE,
            fio::MODE_TYPE_DIRECTORY,
            "child",
            child_dir_server,
        )
        .expect("Cannot open directory");

    assert_eq!(get_open_status(&child_dir_client).await, zx::Status::ACCESS_DENIED);
}

/// Creates a child directory and opens it with OPEN_FLAG_POSIX_WRITABLE/EXECUTABLE, ensuring that
/// the requested rights are expanded to only those which the parent directory connection has.
#[fasync::run_singlethreaded(test)]
async fn open_child_dir_with_posix_flags() {
    let harness = TestHarness::new().await;

    for dir_flags in harness.dir_rights.valid_combos() {
        let root = root_directory(vec![directory("child", vec![])]);
        let root_dir = harness.get_directory(root, dir_flags);
        let readable = dir_flags & fio::OpenFlags::RIGHT_READABLE;
        let parent_dir =
            open_node::<fio::DirectoryMarker>(&root_dir, dir_flags, fio::MODE_TYPE_DIRECTORY, ".")
                .await;

        let (child_dir_client, child_dir_server) =
            create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");
        parent_dir
            .open(
                readable
                    | fio::OpenFlags::POSIX_WRITABLE
                    | fio::OpenFlags::POSIX_EXECUTABLE
                    | fio::OpenFlags::DESCRIBE,
                fio::MODE_TYPE_DIRECTORY,
                "child",
                child_dir_server,
            )
            .expect("Cannot open directory");

        assert_eq!(
            get_open_status(&child_dir_client).await,
            zx::Status::OK,
            "Failed to open directory, flags = {:?}",
            dir_flags
        );
        // Ensure expanded rights do not exceed those of the parent directory connection.
        assert_eq!(get_node_flags(&child_dir_client).await & dir_flags, dir_flags);
    }
}

#[fasync::run_singlethreaded(test)]
async fn open_dir_without_describe_flag() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    for dir_flags in harness.dir_rights.valid_combos() {
        assert_eq!(dir_flags & fio::OpenFlags::DESCRIBE, fio::OpenFlags::empty());
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");

        root_dir
            .open(dir_flags, fio::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_on_open_not_received(&client).await;
    }
}

#[fasync::run_singlethreaded(test)]
async fn open_file_without_describe_flag() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos() {
        assert_eq!(file_flags & fio::OpenFlags::DESCRIBE, fio::OpenFlags::empty());
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");

        test_dir
            .open(file_flags, fio::MODE_TYPE_FILE, TEST_FILE, server)
            .expect("Cannot open file");

        assert_on_open_not_received(&client).await;
    }
}

/// Ensures that opening a file with more rights than the directory connection fails
/// with Status::ACCESS_DENIED.
#[fasync::run_singlethreaded(test)]
async fn open_file_with_extra_rights() {
    let harness = TestHarness::new().await;

    // Combinations to test of the form (directory flags, [file flag combinations]).
    // All file flags should have more rights than those of the directory flags.
    let test_right_combinations = [
        (fio::OpenFlags::empty(), harness.file_rights.valid_combos()),
        (
            fio::OpenFlags::RIGHT_READABLE,
            harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE),
        ),
        (
            fio::OpenFlags::RIGHT_WRITABLE,
            harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_READABLE),
        ),
    ];

    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    for (dir_flags, file_flag_combos) in test_right_combinations.iter() {
        let dir_proxy =
            open_node::<fio::DirectoryMarker>(&root_dir, *dir_flags, fio::MODE_TYPE_DIRECTORY, ".")
                .await;

        for file_flags in file_flag_combos {
            if file_flags.is_empty() {
                continue; // The rights in file_flags must *exceed* those in dir_flags.
            }
            // Ensure the combination is valid (e.g. that file_flags is requesting more rights
            // than those in dir_flags).
            assert!(
                (*file_flags & harness.dir_rights.all()) != (*dir_flags & harness.dir_rights.all()),
                "Invalid test: file rights must exceed dir! (flags: dir = {:?}, file = {:?})",
                *dir_flags,
                *file_flags
            );

            let (client, server) = create_proxy::<fio::NodeMarker>().expect("Cannot create proxy.");

            dir_proxy
                .open(
                    *file_flags | fio::OpenFlags::DESCRIBE,
                    fio::MODE_TYPE_FILE,
                    TEST_FILE,
                    server,
                )
                .expect("Cannot open file");

            assert_eq!(
                get_open_status(&client).await,
                zx::Status::ACCESS_DENIED,
                "Opened a file with more rights than the directory! (flags: dir = {:?}, file = {:?})",
                *dir_flags,
                *file_flags
            );
        }
    }
}

/// Checks that open fails with ZX_ERR_BAD_PATH when it should.
#[fasync::run_singlethreaded(test)]
async fn open_path() {
    let harness = TestHarness::new().await;
    if !harness.config.conformant_path_handling.unwrap_or_default() {
        return;
    }

    let root = root_directory(vec![directory("dir", vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    // Valid paths:
    for path in [".", "/", "/dir/"] {
        open_node::<fio::NodeMarker>(&root_dir, fio::OpenFlags::RIGHT_READABLE, 0, path).await;
    }

    // Invalid paths:
    for path in [
        "", "//", "///", "////", "./", "/dir//", "//dir//", "/dir//", "/dir/../", "/dir/..",
        "/dir/./", "/dir/.", "/./", "./dir",
    ] {
        assert_eq!(
            open_node_status::<fio::NodeMarker>(&root_dir, fio::OpenFlags::RIGHT_READABLE, 0, path)
                .await
                .expect_err("open succeeded"),
            zx::Status::INVALID_ARGS,
            "path: {}",
            path,
        );
    }
}

/// Check that a trailing flash with OPEN_FLAG_NOT_DIRECTORY returns ZX_ERR_INVALID_ARGS.
#[fasync::run_singlethreaded(test)]
async fn open_trailing_slash_with_not_directory() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::NOT_DIRECTORY,
            0,
            "foo/"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );
}

/// Checks that mode is ignored when opening existing nodes.
#[fasync::run_singlethreaded(test)]
async fn open_flags_and_mode() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![file(TEST_FILE, vec![]), directory("dir", vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    // mode should be ignored when opening an existing object.
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        TEST_FILE,
    )
    .await;
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_FILE,
        "dir",
    )
    .await;
    open_node::<fio::NodeMarker>(
        &root_dir,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DIRECTORY,
        libc::S_IRWXU,
        "dir",
    )
    .await;

    // MODE_TYPE_DIRECTORY is incompatible with OPEN_FLAG_NOT_DIRECTORY
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::NOT_DIRECTORY,
            fio::MODE_TYPE_DIRECTORY,
            "foo"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // MODE_TYPE_FILE is incompatible with a path that specifies a directory
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_FILE,
            "foo/"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // MODE_TYPE_FILE is incompatible with OPEN_FLAG_DIRECTORY
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DIRECTORY,
            fio::MODE_TYPE_FILE,
            "foo"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // Can't open . with OPEN_FLAG_NOT_DIRECTORY
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::NOT_DIRECTORY,
            0,
            "."
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // Can't have OPEN_FLAG_DIRECTORY and OPEN_FLAG_NOT_DIRECTORY
    assert_eq!(
        open_node_status::<fio::NodeMarker>(
            &root_dir,
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::DIRECTORY
                | fio::OpenFlags::NOT_DIRECTORY,
            0,
            "."
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );
}

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

#[fasync::run_singlethreaded(test)]
async fn file_read_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_READABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        let _data: Vec<u8> = file
            .read(0)
            .await
            .expect("read failed")
            .map_err(zx::Status::from_raw)
            .expect("read error");
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_with_insufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_READABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        let result = file.read(0).await.expect("read failed").map_err(zx::Status::from_raw);
        assert_eq!(result, Err(zx::Status::BAD_HANDLE))
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_at_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_READABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        let _: Vec<u8> = file
            .read_at(0, 0)
            .await
            .expect("read_at failed")
            .map_err(zx::Status::from_raw)
            .expect("read_at error");
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_at_with_insufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_READABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        let result =
            file.read_at(0, 0).await.expect("read_at failed").map_err(zx::Status::from_raw);
        assert_eq!(result, Err(zx::Status::BAD_HANDLE))
    }
}

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
async fn file_resize_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        file.resize(0)
            .await
            .expect("resize failed")
            .map_err(zx::Status::from_raw)
            .expect("resize error")
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_resize_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    for file_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<fio::FileMarker>(&test_dir, file_flags, fio::MODE_TYPE_FILE, TEST_FILE)
                .await;
        let result = file.resize(0).await.expect("resize failed").map_err(zx::Status::from_raw);
        assert_eq!(result, Err(zx::Status::BAD_HANDLE));
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_in_subdirectory() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_READABLE) {
        let root = root_directory(vec![directory("subdir", vec![file("testing.txt", vec![])])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file = open_node::<fio::FileMarker>(
            &test_dir,
            file_flags,
            fio::MODE_TYPE_FILE,
            "subdir/testing.txt",
        )
        .await;
        let _data: Vec<u8> = file
            .read(0)
            .await
            .expect("read failed")
            .map_err(zx::Status::from_raw)
            .expect("read error");
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_readable_memory_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }

    for file_flags in harness.vmo_file_rights.valid_combos_with(fio::OpenFlags::RIGHT_READABLE) {
        // Should be able to get a readable VMO in default, exact, and private sharing modes.
        for sharing_mode in
            [fio::VmoFlags::empty(), fio::VmoFlags::SHARED_BUFFER, fio::VmoFlags::PRIVATE_CLONE]
        {
            let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024);
            let (vmo, _) = create_file_and_get_backing_memory(
                file,
                &harness,
                file_flags,
                fio::VmoFlags::READ | sharing_mode,
            )
            .await
            .expect("Failed to create file and obtain VMO");

            // Ensure that the returned VMO's rights are consistent with the expected flags.
            validate_vmo_rights(&vmo, fio::VmoFlags::READ);

            let size = vmo.get_content_size().expect("Failed to get vmo content size");

            // Check contents of buffer.
            let mut data = vec![0; size as usize];
            let () = vmo.read(&mut data, 0).expect("VMO read failed");
            assert_eq!(&data, TEST_FILE_CONTENTS);
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_readable_memory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }

    for file_flags in harness.vmo_file_rights.valid_combos_without(fio::OpenFlags::RIGHT_READABLE) {
        let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024);
        assert_eq!(
            create_file_and_get_backing_memory(file, &harness, file_flags, fio::VmoFlags::READ)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_writable_memory_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }
    // Writable VMOs currently require private sharing mode.
    const VMO_FLAGS: fio::VmoFlags =
        fio::VmoFlags::empty().union(fio::VmoFlags::WRITE).union(fio::VmoFlags::PRIVATE_CLONE);

    for file_flags in harness.vmo_file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024);
        let (vmo, _) = create_file_and_get_backing_memory(file, &harness, file_flags, VMO_FLAGS)
            .await
            .expect("Failed to create file and obtain VMO");

        // Ensure that the returned VMO's rights are consistent with the expected flags.
        validate_vmo_rights(&vmo, VMO_FLAGS);

        // Ensure that we can actually write to the VMO.
        let () = vmo.write("bbbbb".as_bytes(), 0).expect("vmo write failed");
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_writable_memory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }
    const VMO_FLAGS: fio::VmoFlags =
        fio::VmoFlags::empty().union(fio::VmoFlags::WRITE).union(fio::VmoFlags::PRIVATE_CLONE);

    for file_flags in harness.vmo_file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS, 128 * 1024);
        assert_eq!(
            create_file_and_get_backing_memory(file, &harness, file_flags, VMO_FLAGS)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_executable_memory_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default()
        || !harness.config.supports_executable_file.unwrap_or_default()
    {
        return;
    }

    // We should be able to get an executable VMO in default, exact, and private sharing modes. Note
    // that the fuchsia.io interface requires the connection to have OPEN_RIGHT_READABLE in addition
    // to OPEN_RIGHT_EXECUTABLE if passing VmoFlags::EXECUTE to the GetBackingMemory method.
    for sharing_mode in
        [fio::VmoFlags::empty(), fio::VmoFlags::SHARED_BUFFER, fio::VmoFlags::PRIVATE_CLONE]
    {
        let file = executable_file(TEST_FILE);
        let vmo_flags = fio::VmoFlags::READ | fio::VmoFlags::EXECUTE | sharing_mode;
        let (vmo, _) = create_file_and_get_backing_memory(
            file,
            &harness,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            vmo_flags,
        )
        .await
        .expect("Failed to create file and obtain VMO");
        // Ensure that the returned VMO's rights are consistent with the expected flags.
        validate_vmo_rights(&vmo, vmo_flags);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_executable_memory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default()
        || !harness.config.supports_executable_file.unwrap_or_default()
    {
        return;
    }
    // We should fail to get the backing memory if the connection lacks execute rights.
    for file_flags in
        harness.executable_file_rights.valid_combos_without(fio::OpenFlags::RIGHT_EXECUTABLE)
    {
        let file = executable_file(TEST_FILE);
        assert_eq!(
            create_file_and_get_backing_memory(file, &harness, file_flags, fio::VmoFlags::EXECUTE)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
    // The fuchsia.io interface additionally specifies that GetBackingMemory should fail if
    // VmoFlags::EXECUTE is specified but connection lacks OPEN_RIGHT_READABLE.
    for file_flags in
        harness.executable_file_rights.valid_combos_without(fio::OpenFlags::RIGHT_READABLE)
    {
        let file = executable_file(TEST_FILE);
        assert_eq!(
            create_file_and_get_backing_memory(file, &harness, file_flags, fio::VmoFlags::EXECUTE)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
}

// Ensure that passing VmoFlags::SHARED_BUFFER to GetBackingMemory returns the same KOID as the
// backing VMO.
#[fasync::run_singlethreaded(test)]
async fn file_get_backing_memory_exact_same_koid() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_get_backing_memory.unwrap_or_default() {
        return;
    }

    let vmo = zx::Vmo::create(1).expect("Cannot create VMO");
    let original_koid = vmo.get_koid();
    let vmofile_object = io_test::DirectoryEntry::VmoFile(io_test::VmoFile {
        name: Some(TEST_FILE.to_string()),
        vmo: Some(vmo),
        ..io_test::VmoFile::EMPTY
    });

    let (vmo, _) = create_file_and_get_backing_memory(
        vmofile_object,
        &harness,
        fio::OpenFlags::RIGHT_READABLE,
        fio::VmoFlags::READ | fio::VmoFlags::SHARED_BUFFER,
    )
    .await
    .expect("Failed to create file and obtain VMO");

    assert_eq!(original_koid, vmo.get_koid());
}

#[fasync::run_singlethreaded(test)]
async fn directory_describe() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![]);
    let test_dir = harness.get_directory(root, fio::OpenFlags::empty());

    let node_info = test_dir.describe().await.expect("describe failed");

    assert!(matches!(node_info, fio::NodeInfo::Directory { .. }));
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

    let node_info = file.describe().await.expect("describe failed");

    assert!(matches!(node_info, fio::NodeInfo::File { .. }));
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

    let node_info = file.describe().await.expect("describe failed");

    assert!(matches!(node_info, fio::NodeInfo::File { .. }));
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

    file.describe().await.expect("describe failed");
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

#[fasync::run_singlethreaded(test)]
async fn rename_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_rename.unwrap_or_default()
        || !harness.config.supports_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![
            directory("src", vec![file("old.txt", contents.to_vec())]),
            directory("dest", vec![]),
        ]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;
        let dest_dir = open_rw_dir(&test_dir, "dest").await;
        let dest_token = get_token(&dest_dir).await;

        // Rename src/old.txt -> dest/new.txt.
        let status = src_dir
            .rename("old.txt", zx::Event::from(dest_token), "new.txt")
            .await
            .expect("rename failed");
        assert!(status.is_ok());

        // Check dest/new.txt was created and has correct contents.
        assert_eq!(read_file(&test_dir, "dest/new.txt").await, contents);

        // Check src/old.txt no longer exists.
        assert_file_not_found(&test_dir, "src/old.txt").await;
    }
}

#[fasync::run_singlethreaded(test)]
async fn rename_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_rename.unwrap_or_default()
        || !harness.config.supports_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![
            directory("src", vec![file("old.txt", contents.to_vec())]),
            directory("dest", vec![]),
        ]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;
        let dest_dir = open_rw_dir(&test_dir, "dest").await;
        let dest_token = get_token(&dest_dir).await;

        // Try renaming src/old.txt -> dest/new.txt.
        let status = src_dir
            .rename("old.txt", zx::Event::from(dest_token), "new.txt")
            .await
            .expect("rename failed");
        assert!(status.is_err());
        assert_eq!(status.err().unwrap(), zx::Status::BAD_HANDLE.into_raw());
    }
}

#[fasync::run_singlethreaded(test)]
async fn rename_with_slash_in_path_fails() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_rename.unwrap_or_default()
        || !harness.config.supports_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![
            directory("src", vec![file("old.txt", contents.to_vec())]),
            directory("dest", vec![]),
        ]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;
        let dest_dir = open_rw_dir(&test_dir, "dest").await;

        // Including a slash in the src or dest path should fail.
        let status = test_dir
            .rename("src/old.txt", zx::Event::from(get_token(&dest_dir).await), "new.txt")
            .await
            .expect("rename failed");
        assert!(status.is_err());
        assert_eq!(status.err().unwrap(), zx::Status::INVALID_ARGS.into_raw());
        let status = src_dir
            .rename("old.txt", zx::Event::from(get_token(&dest_dir).await), "nested/new.txt")
            .await
            .expect("rename failed");
        assert!(status.is_err());
        assert_eq!(status.err().unwrap(), zx::Status::INVALID_ARGS.into_raw());
    }
}

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

#[fasync::run_singlethreaded(test)]
async fn set_attr_file_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_set_attr.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![file("file", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let file = open_file_with_flags(&test_dir, dir_flags, "file").await;

        let (status, old_attr) = file.get_attr().await.expect("get_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        // Set CREATION_TIME flag, but not MODIFICATION_TIME.
        let status = file
            .set_attr(
                fio::NodeAttributeFlags::CREATION_TIME,
                &mut fio::NodeAttributes {
                    creation_time: 111,
                    modification_time: 222,
                    ..EMPTY_NODE_ATTRS
                },
            )
            .await
            .expect("set_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        let (status, new_attr) = file.get_attr().await.expect("get_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        // Check that only creation_time was updated.
        let expected = fio::NodeAttributes { creation_time: 111, ..old_attr };
        assert_eq!(new_attr, expected);
    }
}

#[fasync::run_singlethreaded(test)]
async fn set_attr_file_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_set_attr.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![file("file", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let file = open_file_with_flags(&test_dir, dir_flags, "file").await;

        let status = file
            .set_attr(
                fio::NodeAttributeFlags::CREATION_TIME,
                &mut fio::NodeAttributes {
                    creation_time: 111,
                    modification_time: 222,
                    ..EMPTY_NODE_ATTRS
                },
            )
            .await
            .expect("set_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn set_attr_directory_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_set_attr.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_with(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![directory("dir", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let dir = open_dir_with_flags(&test_dir, dir_flags, "dir").await;

        let (status, old_attr) = dir.get_attr().await.expect("get_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        // Set CREATION_TIME flag, but not MODIFICATION_TIME.
        let status = dir
            .set_attr(
                fio::NodeAttributeFlags::CREATION_TIME,
                &mut fio::NodeAttributes {
                    creation_time: 111,
                    modification_time: 222,
                    ..EMPTY_NODE_ATTRS
                },
            )
            .await
            .expect("set_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        let (status, new_attr) = dir.get_attr().await.expect("get_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        // Check that only creation_time was updated.
        let expected = fio::NodeAttributes { creation_time: 111, ..old_attr };
        assert_eq!(new_attr, expected);
    }
}

#[fasync::run_singlethreaded(test)]
async fn set_attr_directory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if !harness.config.supports_set_attr.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_without(fio::OpenFlags::RIGHT_WRITABLE) {
        let root = root_directory(vec![directory("dir", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let dir = open_dir_with_flags(&test_dir, dir_flags, "dir").await;

        let status = dir
            .set_attr(
                fio::NodeAttributeFlags::CREATION_TIME,
                &mut fio::NodeAttributes {
                    creation_time: 111,
                    modification_time: 222,
                    ..EMPTY_NODE_ATTRS
                },
            )
            .await
            .expect("set_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);
    }
}
