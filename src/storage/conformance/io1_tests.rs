// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fdio::fdio_sys::V_IRWXU,
    fidl::{
        endpoints::{create_endpoints, create_proxy, ProtocolMarker, Proxy},
        AsHandleRef,
    },
    fidl_fuchsia_io as io,
    fidl_fuchsia_io2::{UnlinkFlags, UnlinkOptions},
    fidl_fuchsia_io_test as io_test, fidl_fuchsia_mem,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_zircon as zx,
    futures::StreamExt,
    io_conformance_util::{flags::build_flag_combinations, test_harness::TestHarness},
};

const TEST_FILE: &str = "testing.txt";

const TEST_FILE_CONTENTS: &[u8] = "abcdef".as_bytes();

const EMPTY_NODE_ATTRS: io::NodeAttributes = io::NodeAttributes {
    mode: 0,
    id: 0,
    content_size: 0,
    storage_size: 0,
    link_count: 0,
    creation_time: 0,
    modification_time: 0,
};

/// Listens for the `OnOpen` event and returns its [Status].
async fn get_open_status(node_proxy: &io::NodeProxy) -> zx::Status {
    let mut events = node_proxy.take_event_stream();
    if let Some(result) = events.next().await {
        match result.expect("FIDL error") {
            io::NodeEvent::OnOpen_ { s, info: _ } => zx::Status::from_raw(s),
            io::NodeEvent::OnConnectionInfo { .. } => zx::Status::OK,
        }
    } else {
        zx::Status::PEER_CLOSED
    }
}

async fn assert_on_open_not_received(node_proxy: &io::NodeProxy) {
    let mut events = node_proxy.take_event_stream();
    // Wait at most 200ms for an OnOpen event to appear.
    let event =
        events.next().on_timeout(zx::Duration::from_millis(200).after_now(), || Option::None).await;
    assert!(event.is_none(), "Unexpected OnOpen event received");
}

/// Converts a generic `NodeProxy` to either a file or directory proxy.
fn convert_node_proxy<T: ProtocolMarker>(proxy: io::NodeProxy) -> T::Proxy {
    T::Proxy::from_channel(proxy.into_channel().expect("Cannot convert node proxy to channel"))
}

/// Helper function to open the desired node in the root folder.
/// Asserts that open_node_status succeeds.
async fn open_node<T: ProtocolMarker>(
    dir: &io::DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> T::Proxy {
    open_node_status::<T>(dir, flags, mode, path)
        .await
        .expect(&format!("open_node_status failed for {}", path))
}

/// Helper function to open the desired node in the root folder.
async fn open_node_status<T: ProtocolMarker>(
    dir: &io::DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> Result<T::Proxy, zx::Status> {
    let flags = flags | io::OPEN_FLAG_DESCRIBE;
    let (node_proxy, node_server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy");
    dir.open(flags, mode, path, node_server).expect("Cannot open node");
    let status = get_open_status(&node_proxy).await;

    if status != zx::Status::OK {
        Err(status)
    } else {
        Ok(convert_node_proxy::<T>(node_proxy))
    }
}

/// Returns the specified node flags from the given NodeProxy.
async fn get_node_flags(node_proxy: &io::NodeProxy) -> u32 {
    let (_, node_flags) = node_proxy.node_get_flags().await.expect("Failed to get node flags!");
    return node_flags;
}

/// Helper function to open a file with the given flags. Only use this if testing something other
/// than the open call directly.
async fn open_file_with_flags(
    parent_dir: &io::DirectoryProxy,
    flags: u32,
    path: &str,
) -> io::FileProxy {
    open_node::<io::FileMarker>(&parent_dir, flags, io::MODE_TYPE_FILE, path).await
}

/// Helper function to open a sub-directory with the given flags. Only use this if testing
/// something other than the open call directly.
async fn open_dir_with_flags(
    parent_dir: &io::DirectoryProxy,
    flags: u32,
    path: &str,
) -> io::DirectoryProxy {
    open_node::<io::DirectoryMarker>(&parent_dir, flags, io::MODE_TYPE_DIRECTORY, path).await
}

/// Helper function to open a sub-directory as readable and writable. Only use this if testing
/// something other than the open call directly.
async fn open_rw_dir(parent_dir: &io::DirectoryProxy, path: &str) -> io::DirectoryProxy {
    open_dir_with_flags(parent_dir, io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE, path).await
}

/// Helper function to call `get_token` on a directory. Only use this if testing something
/// other than the `get_token` call directly.
async fn get_token(dir: &io::DirectoryProxy) -> fidl::Handle {
    let (status, token) = dir.get_token().await.expect("get_token failed");
    assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    token.expect("handle missing")
}

/// Helper function to read a file and return its contents. Only use this if testing something other
/// than the read call directly.
async fn read_file(dir: &io::DirectoryProxy, path: &str) -> Vec<u8> {
    let file =
        open_node::<io::FileMarker>(dir, io::OPEN_RIGHT_READABLE, io::MODE_TYPE_FILE, path).await;
    let (status, data) = file.read(100).await.expect("read failed");
    assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    data
}

/// Attempts to open the given file, and checks the status is `NOT_FOUND`.
async fn assert_file_not_found(dir: &io::DirectoryProxy, path: &str) {
    let (file_proxy, file_server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy");
    dir.open(
        io::OPEN_RIGHT_READABLE | io::OPEN_FLAG_DESCRIBE,
        io::MODE_TYPE_FILE,
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
        DirectoryEntry::File(entry) => entry.name.as_ref(),
        DirectoryEntry::VmoFile(entry) => entry.name.as_ref(),
        DirectoryEntry::ExecFile(entry) => entry.name.as_ref(),
    }
    .expect("DirectoryEntry name is None!")
    .clone()
}

/// Asserts that the given `vmo_rights` align with the `vmo_flags` passed to a get_buffer call.
/// We check that the returned rights align with and do not exceed those in the given flags, that
/// we have at least basic VMO rights, and that the flags align with the expected sharing mode.
fn validate_vmo_rights(buffer: &fidl_fuchsia_mem::Buffer, vmo_flags: u32) {
    let vmo_rights: zx::Rights = buffer.vmo.basic_info().expect("failed to get VMO info").rights;

    // Ensure that we have at least some basic rights.
    assert!(vmo_rights.contains(zx::Rights::BASIC));
    assert!(vmo_rights.contains(zx::Rights::MAP));
    assert!(vmo_rights.contains(zx::Rights::GET_PROPERTY));

    // Ensure the returned rights match and do not exceed those we requested in `vmo_flags`.
    assert!(vmo_rights.contains(zx::Rights::READ) == (vmo_flags & io::VMO_FLAG_READ != 0));
    assert!(vmo_rights.contains(zx::Rights::WRITE) == (vmo_flags & io::VMO_FLAG_WRITE != 0));
    assert!(vmo_rights.contains(zx::Rights::EXECUTE) == (vmo_flags & io::VMO_FLAG_EXEC != 0));

    // Make sure we get SET_PROPERTY if we specified a private copy.
    if vmo_flags & io::VMO_FLAG_PRIVATE != 0 {
        assert!(vmo_rights.contains(zx::Rights::SET_PROPERTY));
    }
}

/// Creates a directory with the given DirectoryEntry, opening the file with the given
/// file flags, and returning a Buffer object initialized with the given vmo_flags.
async fn create_file_and_get_buffer(
    dir_entry: io_test::DirectoryEntry,
    test_harness: &TestHarness,
    file_flags: u32,
    vmo_flags: u32,
) -> Result<(fidl_fuchsia_mem::Buffer, (io::DirectoryProxy, io::FileProxy)), zx::Status> {
    let file_path = get_directory_entry_name(&dir_entry);
    let root = root_directory(vec![dir_entry]);
    let dir_proxy = test_harness.get_directory(root, file_flags);
    let file_proxy =
        open_node_status::<io::FileMarker>(&dir_proxy, file_flags, io::MODE_TYPE_FILE, &file_path)
            .await?;
    let (status, buffer) = file_proxy.get_buffer(vmo_flags).await.expect("Get buffer failed");
    zx::Status::ok(status)?;
    Ok((*buffer.expect("Buffer is missing"), (dir_proxy, file_proxy)))
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

fn file(name: &str, contents: Vec<u8>) -> io_test::DirectoryEntry {
    io_test::DirectoryEntry::File(io_test::File {
        name: Some(name.to_string()),
        contents: Some(contents),
        ..io_test::File::EMPTY
    })
}

fn vmo_file(name: &str, contents: &[u8]) -> io_test::DirectoryEntry {
    let size = contents.len() as u64;
    let vmo = zx::Vmo::create(size).expect("Cannot create VMO");
    vmo.write(contents, 0).expect("Cannot write to VMO");
    let range = fidl_fuchsia_mem::Range { vmo, offset: 0, size };
    io_test::DirectoryEntry::VmoFile(io_test::VmoFile {
        name: Some(name.to_string()),
        buffer: Some(range),
        ..io_test::VmoFile::EMPTY
    })
}

fn exec_file(name: &str) -> io_test::DirectoryEntry {
    io_test::DirectoryEntry::ExecFile(io_test::ExecutableFile {
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
    let mut all_rights =
        io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE | io::OPEN_RIGHT_EXECUTABLE;
    if !harness.config.no_admin.unwrap_or_default() {
        all_rights |= io::OPEN_RIGHT_ADMIN;
    }
    let _root_dir = harness.get_directory(root, all_rights);
}

// Validate allowed rights for File objects (ensures writable files cannot be opened as executable).
#[fasync::run_singlethreaded(test)]
async fn validate_file_rights() {
    let harness = TestHarness::new().await;
    // Create a test directory with a single File object, and ensure the directory has all rights.
    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());
    if harness.config.immutable_file.unwrap_or_default() {
        // If files are immutable, check that opening with OPEN_RIGHT_WRITABLE results in
        // access denied, and return (since all other combinations are valid in this case).
        assert_eq!(
            open_node_status::<io::NodeMarker>(&root_dir, io::OPEN_RIGHT_WRITABLE, 0, TEST_FILE)
                .await
                .expect_err("open succeeded"),
            zx::Status::ACCESS_DENIED
        );
        return;
    }
    // Opening with WRITE must succeed.
    open_node::<io::NodeMarker>(&root_dir, io::OPEN_RIGHT_WRITABLE, 0, TEST_FILE).await;
    // Opening with EXECUTE must fail.
    open_node_status::<io::NodeMarker>(&root_dir, io::OPEN_RIGHT_EXECUTABLE, 0, TEST_FILE)
        .await
        .expect_err("open succeeded");
}

// Validate allowed rights for VmoFile objects (ensures cannot be opened as executable).
#[fasync::run_singlethreaded(test)]
async fn validate_vmofile_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_vmofile.unwrap_or_default() {
        return;
    }
    // Create a test directory with a VmoFile object, and ensure the directory has all rights.
    let root = root_directory(vec![vmo_file(TEST_FILE, TEST_FILE_CONTENTS)]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());
    // Opening with READ/WRITE should succeed.
    open_node::<io::NodeMarker>(
        &root_dir,
        io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE,
        0,
        TEST_FILE,
    )
    .await;
    // Opening with EXECUTE must fail to ensure W^X enforcement.
    assert!(matches!(
        open_node_status::<io::NodeMarker>(&root_dir, io::OPEN_RIGHT_EXECUTABLE, 0, TEST_FILE)
            .await
            .expect_err("open succeeded"),
        zx::Status::ACCESS_DENIED | zx::Status::NOT_SUPPORTED
    ));
}

// Validate allowed rights for ExecFile objects (ensures cannot be opened as writable).
#[fasync::run_singlethreaded(test)]
async fn validate_execfile_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_execfile.unwrap_or_default() {
        return;
    }
    // Create a test directory with an ExecFile object, and ensure the directory has all rights.
    let root = root_directory(vec![exec_file(TEST_FILE)]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());
    // Opening with READABLE/EXECUTABLE should succeed.
    open_node::<io::NodeMarker>(
        &root_dir,
        io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_EXECUTABLE,
        0,
        TEST_FILE,
    )
    .await;
    // Opening with WRITABLE must fail to ensure W^X enforcement.
    assert_eq!(
        open_node_status::<io::NodeMarker>(&root_dir, io::OPEN_RIGHT_WRITABLE, 0, TEST_FILE)
            .await
            .expect_err("open succeeded"),
        zx::Status::ACCESS_DENIED
    );
}

/// Creates a directory with a remote mount inside of it, and checks that the remote can be opened.
#[fasync::run_singlethreaded(test)]
async fn open_remote_directory_test() {
    let harness = TestHarness::new().await;
    if harness.config.no_remote_dir.unwrap_or_default() {
        return;
    }

    let (remote_dir_client, remote_dir_server) =
        create_endpoints::<io::DirectoryMarker>().expect("Cannot create endpoints");
    // Create a logged directory client/server pair so we can intercept the Open() call.
    let remote_name = "remote_directory";

    // Request an extra directory connection from the harness to use as the remote.
    let root = root_directory(vec![]);
    harness
        .proxy
        .get_directory(root, io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE, remote_dir_server)
        .expect("Cannot get empty remote directory");

    // Create a directory with a remote directory inside of it.
    let root_dir = harness.get_directory_with_remote_directory(
        remote_dir_client,
        remote_name,
        io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE,
    );

    open_node::<io::DirectoryMarker>(
        &root_dir,
        io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE,
        io::MODE_TYPE_DIRECTORY,
        remote_name,
    )
    .await;
}

/// Creates a directory with a remote mount containing a file inside of it, and checks that the
/// file can be opened through the remote.
#[fasync::run_singlethreaded(test)]
async fn open_remote_file_test() {
    let harness = TestHarness::new().await;
    if harness.config.no_remote_dir.unwrap_or_default() {
        return;
    }

    let (remote_dir_client, remote_dir_server) =
        create_endpoints::<io::DirectoryMarker>().expect("Cannot create endpoints");

    let remote_name = "remote_directory";

    // Request an extra directory connection from the harness to use as the remote.
    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    harness
        .proxy
        .get_directory(root, io::OPEN_RIGHT_READABLE, remote_dir_server)
        .expect("Cannot get empty remote directory");

    // Create a directory with a remote directory inside of it.
    let root_dir = harness.get_directory_with_remote_directory(
        remote_dir_client,
        remote_name,
        io::OPEN_RIGHT_READABLE,
    );

    // Test opening file by opening the remote directory first and then opening the file.
    let remote_dir_proxy = open_node::<io::DirectoryMarker>(
        &root_dir,
        io::OPEN_RIGHT_READABLE,
        io::MODE_TYPE_DIRECTORY,
        remote_name,
    )
    .await;
    open_node::<io::NodeMarker>(&remote_dir_proxy, io::OPEN_RIGHT_READABLE, 0, TEST_FILE).await;

    // Test opening file directly though local directory by crossing remote automatically.
    open_node::<io::NodeMarker>(
        &root_dir,
        io::OPEN_RIGHT_READABLE,
        io::MODE_TYPE_DIRECTORY,
        [remote_name, "/", TEST_FILE].join("").as_str(),
    )
    .await;
}

/// Ensure specifying OPEN_FLAG_POSIX cannot cause rights escalation (fxbug.dev/40862).
/// The test sets up the following hierarchy of nodes:
///
/// ------------------ RW   --------------------------
/// | test_dir_proxy | ---> | test_dir_server        |
/// ------------------ (a)  |  - /remote_directory   | RWX  ---------------------
///                         |    (remote_dir_client) | ---> | remote_dir_server |
///                         -------------------------- (b)  ---------------------
///
/// To validate the right escalation issue has been resolved, we call Open() on the
/// test_dir_proxy passing in OPEN_FLAG_POSIX, which if handled correctly, should result
/// in opening remote_dir_server as RW (and NOT RWX, which can occur if the POSIX flag is
/// passed directly to the remote instead of OPEN_FLAG_POSIX_WRITABLE/EXECUTABLE).
#[fasync::run_singlethreaded(test)]
async fn open_remote_directory_right_escalation_test() {
    let harness = TestHarness::new().await;
    if harness.config.no_remote_dir.unwrap_or_default() {
        return;
    }

    // Use the test harness to serve a directory on the remote server with RWX permissions.
    let (remote_dir_client, remote_dir_server) =
        create_endpoints::<io::DirectoryMarker>().expect("Cannot create endpoints");
    let root = root_directory(vec![]);
    let remote_name = "remote_directory";
    harness
        .proxy
        .get_directory(
            root,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE | io::OPEN_RIGHT_EXECUTABLE,
            remote_dir_server,
        )
        .expect("Cannot get empty remote directory");

    // Mount the remote node through test_dir_proxy, and ensure that the root connection has
    // only RW permissions (and thus is a sub-set of those in the remote).
    let (test_dir_proxy, test_dir_server) =
        create_proxy::<io::DirectoryMarker>().expect("Cannot create proxy");
    harness
        .proxy
        .get_directory_with_remote_directory(
            remote_dir_client,
            remote_name,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE,
            test_dir_server,
        )
        .expect("Cannot get test harness directory");

    // Create a new proxy/server for opening the remote node through test_dir_proxy.
    // Here we pass the POSIX flag, which should only expand to the maximum set of
    // rights available along the open chain.
    let (node_proxy, node_server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy");
    test_dir_proxy
        .open(
            io::OPEN_RIGHT_READABLE | io::OPEN_FLAG_POSIX,
            io::MODE_TYPE_DIRECTORY,
            remote_name,
            node_server,
        )
        .expect("Cannot open remote directory");

    // Since the root node only has RW permissions, and even though the remote has RWX,
    // we should only get RW permissions back.
    let (_, node_flags) = node_proxy.node_get_flags().await.unwrap();
    assert_eq!(node_flags, io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE);
}

/// Creates a directory with all rights, and checks it can be opened for all subsets of rights.
#[fasync::run_singlethreaded(test)]
async fn open_dir_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    for dir_flags in harness.dir_rights.valid_combos() {
        let (client, server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");
        root_dir
            .open(dir_flags | io::OPEN_FLAG_DESCRIBE, io::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_eq!(get_open_status(&client).await, zx::Status::OK);
    }
}

/// Creates a directory with no rights, and checks opening it with any rights fails.
#[fasync::run_singlethreaded(test)]
async fn open_dir_with_insufficient_rights() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, 0);

    for dir_flags in harness.dir_rights.valid_combos() {
        let (client, server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");
        root_dir
            .open(dir_flags | io::OPEN_FLAG_DESCRIBE, io::MODE_TYPE_DIRECTORY, ".", server)
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
            open_node::<io::DirectoryMarker>(&root_dir, dir_flags, io::MODE_TYPE_DIRECTORY, ".")
                .await;

        // Open child directory with same flags as parent.
        let (child_dir_client, child_dir_server) =
            create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");
        parent_dir
            .open(
                dir_flags | io::OPEN_FLAG_DESCRIBE,
                io::MODE_TYPE_DIRECTORY,
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
    let root_dir = harness.get_directory(root, io::OPEN_RIGHT_READABLE);

    // Open parent as readable.
    let parent_dir = open_node::<io::DirectoryMarker>(
        &root_dir,
        io::OPEN_RIGHT_READABLE,
        io::MODE_TYPE_DIRECTORY,
        ".",
    )
    .await;

    // Opening child as writable should fail.
    let (child_dir_client, child_dir_server) =
        create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");
    parent_dir
        .open(
            io::OPEN_RIGHT_WRITABLE | io::OPEN_FLAG_DESCRIBE,
            io::MODE_TYPE_DIRECTORY,
            "child",
            child_dir_server,
        )
        .expect("Cannot open directory");

    assert_eq!(get_open_status(&child_dir_client).await, zx::Status::ACCESS_DENIED);
}

/// Creates a child directory and opens it with OPEN_FLAG_POSIX, ensuring that the requested
/// rights are expanded to only those which the parent directory connection has.
#[fasync::run_singlethreaded(test)]
async fn open_child_dir_with_posix_flag() {
    let harness = TestHarness::new().await;

    for dir_flags in harness.dir_rights.valid_combos() {
        let root = root_directory(vec![directory("child", vec![])]);
        let root_dir = harness.get_directory(root, dir_flags);
        let readable = dir_flags & io::OPEN_RIGHT_READABLE;
        let parent_dir =
            open_node::<io::DirectoryMarker>(&root_dir, dir_flags, io::MODE_TYPE_DIRECTORY, ".")
                .await;

        let (child_dir_client, child_dir_server) =
            create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");
        parent_dir
            .open(
                io::OPEN_FLAG_POSIX | readable | io::OPEN_FLAG_DESCRIBE,
                io::MODE_TYPE_DIRECTORY,
                "child",
                child_dir_server,
            )
            .expect("Cannot open directory");

        assert_eq!(
            get_open_status(&child_dir_client).await,
            zx::Status::OK,
            "Failed to open directory, flags = {}",
            dir_flags
        );
        // Ensure expanded rights do not exceed those of the parent directory connection.
        let expected_rights = dir_flags & !io::OPEN_RIGHT_ADMIN;
        assert_eq!(get_node_flags(&child_dir_client).await & expected_rights, expected_rights);
    }
}

#[fasync::run_singlethreaded(test)]
async fn open_dir_without_describe_flag() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    for dir_flags in harness.dir_rights.valid_combos() {
        assert_eq!(dir_flags & io::OPEN_FLAG_DESCRIBE, 0);
        let (client, server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");

        root_dir
            .open(dir_flags, io::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_on_open_not_received(&client).await;
    }
}

#[fasync::run_singlethreaded(test)]
async fn open_file_without_describe_flag() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_READABLE) {
        assert_eq!(file_flags & io::OPEN_FLAG_DESCRIBE, 0);
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let (client, server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");

        test_dir.open(file_flags, io::MODE_TYPE_FILE, TEST_FILE, server).expect("Cannot open file");

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
        (io::OPEN_RIGHT_READABLE, harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE)),
        (io::OPEN_RIGHT_WRITABLE, harness.file_rights.valid_combos_with(io::OPEN_RIGHT_READABLE)),
    ];

    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    for (dir_flags, file_flag_combos) in test_right_combinations.iter() {
        if file_flag_combos.is_empty() {
            continue;
        }

        let dir_proxy =
            open_node::<io::DirectoryMarker>(&root_dir, *dir_flags, io::MODE_TYPE_DIRECTORY, ".")
                .await;

        for file_flags in file_flag_combos {
            // Ensure the combination is valid (e.g. that file_flags is requesting more rights
            // than those in dir_flags).
            assert!(
                (file_flags & harness.dir_rights.all()) != (dir_flags & harness.dir_rights.all()),
                "Invalid test: file rights must exceed dir! (flags: dir = {}, file = {})",
                *dir_flags,
                *file_flags
            );

            let (client, server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");

            dir_proxy
                .open(*file_flags | io::OPEN_FLAG_DESCRIBE, io::MODE_TYPE_FILE, TEST_FILE, server)
                .expect("Cannot open file");

            assert_eq!(
                get_open_status(&client).await,
                zx::Status::ACCESS_DENIED,
                "Opened a file with more rights than the directory! (flags: dir = {}, file = {})",
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
    if harness.config.non_conformant_path_handling.unwrap_or_default() {
        return;
    }

    let root = root_directory(vec![directory("dir", vec![])]);
    let root_dir = harness.get_directory(root, harness.dir_rights.all());

    // Valid paths:
    for path in [".", "/", "/dir/"] {
        open_node::<io::NodeMarker>(&root_dir, io::OPEN_RIGHT_READABLE, 0, path).await;
    }

    // Invalid paths:
    for path in [
        "", "//", "///", "////", "./", "/dir//", "//dir//", "/dir//", "/dir/../", "/dir/..",
        "/dir/./", "/dir/.", "/./", "./dir",
    ] {
        assert_eq!(
            open_node_status::<io::NodeMarker>(&root_dir, io::OPEN_RIGHT_READABLE, 0, path)
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
        open_node_status::<io::NodeMarker>(
            &root_dir,
            io::OPEN_RIGHT_READABLE | io::OPEN_FLAG_NOT_DIRECTORY,
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
    open_node::<io::NodeMarker>(
        &root_dir,
        io::OPEN_RIGHT_READABLE,
        io::MODE_TYPE_DIRECTORY,
        TEST_FILE,
    )
    .await;
    open_node::<io::NodeMarker>(&root_dir, io::OPEN_RIGHT_READABLE, io::MODE_TYPE_FILE, "dir")
        .await;
    open_node::<io::NodeMarker>(
        &root_dir,
        io::OPEN_RIGHT_READABLE | io::OPEN_FLAG_DIRECTORY,
        V_IRWXU,
        "dir",
    )
    .await;

    // MODE_TYPE_DIRECTORY is incompatible with OPEN_FLAG_NOT_DIRECTORY
    assert_eq!(
        open_node_status::<io::NodeMarker>(
            &root_dir,
            io::OPEN_RIGHT_READABLE | io::OPEN_FLAG_NOT_DIRECTORY,
            io::MODE_TYPE_DIRECTORY,
            "foo"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // MODE_TYPE_FILE is incompatible with a path that specifies a directory
    assert_eq!(
        open_node_status::<io::NodeMarker>(
            &root_dir,
            io::OPEN_RIGHT_READABLE,
            io::MODE_TYPE_FILE,
            "foo/"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // MODE_TYPE_FILE is incompatible with OPEN_FLAG_DIRECTORY
    assert_eq!(
        open_node_status::<io::NodeMarker>(
            &root_dir,
            io::OPEN_RIGHT_READABLE | io::OPEN_FLAG_DIRECTORY,
            io::MODE_TYPE_FILE,
            "foo"
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // Can't open . with OPEN_FLAG_NOT_DIRECTORY
    assert_eq!(
        open_node_status::<io::NodeMarker>(
            &root_dir,
            io::OPEN_RIGHT_READABLE | io::OPEN_FLAG_NOT_DIRECTORY,
            0,
            "."
        )
        .await
        .expect_err("open succeeded"),
        zx::Status::INVALID_ARGS
    );

    // Can't have OPEN_FLAG_DIRECTORY and OPEN_FLAG_NOT_DIRECTORY
    assert_eq!(
        open_node_status::<io::NodeMarker>(
            &root_dir,
            io::OPEN_RIGHT_READABLE | io::OPEN_FLAG_DIRECTORY | io::OPEN_FLAG_NOT_DIRECTORY,
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
    if harness.config.immutable_dir.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        // Re-open directory with the flags being tested.
        let dir = open_dir_with_flags(&test_dir, dir_flags, ".").await;
        let (client, server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");

        dir.open(
            dir_flags | io::OPEN_FLAG_CREATE | io::OPEN_FLAG_DESCRIBE,
            io::MODE_TYPE_FILE,
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
    if harness.config.immutable_dir.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        // Re-open directory with the flags being tested.
        let dir = open_dir_with_flags(&test_dir, dir_flags, ".").await;
        let (client, server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");

        dir.open(
            dir_flags | io::OPEN_FLAG_CREATE | io::OPEN_FLAG_DESCRIBE,
            io::MODE_TYPE_FILE,
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

    for file_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_READABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _data) = file.read(0).await.expect("read failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_with_insufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_READABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _data) = file.read(0).await.expect("read failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_at_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_READABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _data) = file.read_at(0, 0).await.expect("read_at failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_at_with_insufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_READABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _data) = file.read_at(0, 0).await.expect("read_at failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _actual) = file.write("".as_bytes()).await.expect("write failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_with_insufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _actual) = file.write("".as_bytes()).await.expect("write failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_at_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _actual) = file.write_at("".as_bytes(), 0).await.expect("write_at failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_at_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    for file_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _actual) = file.write_at("".as_bytes(), 0).await.expect("write_at failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_truncate_with_sufficient_rights() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let status = file.truncate(0).await.expect("truncate failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_truncate_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    for file_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![file(TEST_FILE, vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let status = file.truncate(0).await.expect("truncate failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_in_subdirectory() {
    let harness = TestHarness::new().await;

    for file_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_READABLE) {
        let root = root_directory(vec![directory("subdir", vec![file("testing.txt", vec![])])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());

        let file = open_node::<io::FileMarker>(
            &test_dir,
            file_flags,
            io::MODE_TYPE_FILE,
            "subdir/testing.txt",
        )
        .await;
        let (status, _data) = file.read(0).await.expect("Read failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_readable_buffer_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_get_buffer.unwrap_or_default() {
        return;
    }

    for file_flags in harness.vmofile_rights.valid_combos_with(io::OPEN_RIGHT_READABLE) {
        // Should be able to get a readable VMO in default, exact, and private sharing modes.
        for sharing_mode in [0, io::VMO_FLAG_EXACT, io::VMO_FLAG_PRIVATE] {
            let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS);
            let (buffer, _) = create_file_and_get_buffer(
                file,
                &harness,
                file_flags,
                io::VMO_FLAG_READ | sharing_mode,
            )
            .await
            .expect("Failed to create file and obtain buffer");

            // Ensure that the returned VMO's rights are consistent with the expected flags.
            validate_vmo_rights(&buffer, io::VMO_FLAG_READ);

            // Check contents of buffer.
            let mut data = vec![0; buffer.size as usize];
            buffer.vmo.read(&mut data, 0).expect("VMO read failed");
            assert_eq!(&data, TEST_FILE_CONTENTS);
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_readable_buffer_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_get_buffer.unwrap_or_default() {
        return;
    }

    for file_flags in harness.vmofile_rights.valid_combos_without(io::OPEN_RIGHT_READABLE) {
        let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS);
        assert_eq!(
            create_file_and_get_buffer(file, &harness, file_flags, io::VMO_FLAG_READ)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_writable_buffer_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_get_buffer.unwrap_or_default() {
        return;
    }
    // Writable VMOs currently require private sharing mode.
    const VMO_FLAGS: u32 = io::VMO_FLAG_WRITE | io::VMO_FLAG_PRIVATE;

    for file_flags in harness.vmofile_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS);
        let (buffer, _) = create_file_and_get_buffer(file, &harness, file_flags, VMO_FLAGS)
            .await
            .expect("Failed to create file and obtain buffer");

        // Ensure that the returned VMO's rights are consistent with the expected flags.
        validate_vmo_rights(&buffer, VMO_FLAGS);

        // Ensure that we can actually write to the VMO.
        buffer.vmo.write("bbbbb".as_bytes(), 0).expect("vmo write failed");
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_writable_buffer_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_get_buffer.unwrap_or_default() {
        return;
    }
    const VMO_FLAGS: u32 = io::VMO_FLAG_WRITE | io::VMO_FLAG_PRIVATE;

    for file_flags in harness.vmofile_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let file = vmo_file(TEST_FILE, TEST_FILE_CONTENTS);
        assert_eq!(
            create_file_and_get_buffer(file, &harness, file_flags, VMO_FLAGS)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_executable_buffer_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_get_buffer.unwrap_or_default()
        || harness.config.no_execfile.unwrap_or_default()
    {
        return;
    }

    // We should be able to get an executable VMO in default, exact, and private sharing modes.
    // Note that the io.fidl protocol requires the connection to have OPEN_RIGHT_READABLE in
    // addition to OPEN_RIGHT_EXECUTABLE if passing VMO_FLAG_EXEC to the GetBuffer method.
    for sharing_mode in [0, io::VMO_FLAG_EXACT, io::VMO_FLAG_PRIVATE] {
        let file = exec_file(TEST_FILE);
        let vmo_flags = io::VMO_FLAG_READ | io::VMO_FLAG_EXEC | sharing_mode;
        let (buffer, _) = create_file_and_get_buffer(
            file,
            &harness,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_EXECUTABLE,
            vmo_flags,
        )
        .await
        .expect("Failed to create file and obtain buffer");
        // Ensure that the returned VMO's rights are consistent with the expected flags.
        validate_vmo_rights(&buffer, vmo_flags);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_get_executable_buffer_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_get_buffer.unwrap_or_default()
        || harness.config.no_execfile.unwrap_or_default()
    {
        return;
    }
    // We should fail to get the buffer if the connection lacks execute rights.
    for file_flags in harness.execfile_rights.valid_combos_without(io::OPEN_RIGHT_EXECUTABLE) {
        let file = exec_file(TEST_FILE);
        assert_eq!(
            create_file_and_get_buffer(file, &harness, file_flags, io::VMO_FLAG_EXEC)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
    // The io.fidl protocol additionally specifies that GetBuffer should fail if VMO_FLAG_EXEC is
    // specified but connection lacks OPEN_RIGHT_READABLE.
    for file_flags in harness.execfile_rights.valid_combos_without(io::OPEN_RIGHT_READABLE) {
        let file = exec_file(TEST_FILE);
        assert_eq!(
            create_file_and_get_buffer(file, &harness, file_flags, io::VMO_FLAG_EXEC)
                .await
                .expect_err("Error was expected"),
            zx::Status::ACCESS_DENIED
        );
    }
}

// Ensure that passing VMO_FLAG_EXACT to GetBuffer returns the same KOID as the backing VMO.
#[fasync::run_singlethreaded(test)]
async fn file_get_buffer_exact_same_koid() {
    let harness = TestHarness::new().await;
    if harness.config.no_get_buffer.unwrap_or_default() {
        return;
    }

    let size = TEST_FILE_CONTENTS.len() as u64;
    let vmo = zx::Vmo::create(size).expect("Cannot create VMO");
    let original_koid = vmo.get_koid();
    let vmofile_object = io_test::DirectoryEntry::VmoFile(io_test::VmoFile {
        name: Some(TEST_FILE.to_string()),
        buffer: Some(fidl_fuchsia_mem::Range { vmo, offset: 0, size }),
        ..io_test::VmoFile::EMPTY
    });

    let (buffer, _) = create_file_and_get_buffer(
        vmofile_object,
        &harness,
        io::OPEN_RIGHT_READABLE,
        io::VMO_FLAG_READ | io::VMO_FLAG_EXACT,
    )
    .await
    .expect("Failed to create file and obtain buffer");

    assert_eq!(original_koid, buffer.vmo.get_koid());
}

#[fasync::run_singlethreaded(test)]
async fn directory_describe() {
    let harness = TestHarness::new().await;
    let root = root_directory(vec![]);
    let test_dir = harness.get_directory(root, 0);

    let node_info = test_dir.describe().await.expect("describe failed");

    assert!(matches!(node_info, io::NodeInfo::Directory { .. }));
}

#[fasync::run_singlethreaded(test)]
async fn file_describe() {
    let harness = TestHarness::new().await;

    let root = root_directory(vec![file(TEST_FILE, vec![])]);
    let test_dir = harness.get_directory(root, io::OPEN_RIGHT_READABLE);
    let file = open_node::<io::FileMarker>(
        &test_dir,
        io::OPEN_RIGHT_READABLE,
        io::MODE_TYPE_FILE,
        TEST_FILE,
    )
    .await;

    let node_info = file.describe().await.expect("describe failed");

    // The node_info can be either File or Vmofile type.
    assert!(matches!(node_info, io::NodeInfo::File { .. } | io::NodeInfo::Vmofile { .. }));
}

#[fasync::run_singlethreaded(test)]
async fn vmo_file_describe() {
    let harness = TestHarness::new().await;
    if harness.config.no_vmofile.unwrap_or_default() {
        return;
    }

    let root = root_directory(vec![vmo_file(TEST_FILE, TEST_FILE_CONTENTS)]);
    let test_dir = harness.get_directory(root, io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE);
    let file = open_node::<io::FileMarker>(
        &test_dir,
        io::OPEN_RIGHT_READABLE,
        io::MODE_TYPE_FILE,
        TEST_FILE,
    )
    .await;

    let node_info = file.describe().await.expect("describe failed");

    if let io::NodeInfo::Vmofile { 0: vmo_file_obj } = node_info {
        assert_eq!(vmo_file_obj.offset, 0);
        assert_eq!(vmo_file_obj.length as usize, TEST_FILE_CONTENTS.len());
        // Ensure the permissions we get from describe do not exceed those of the connection.
        let vmo_rights: zx::Rights =
            vmo_file_obj.vmo.basic_info().expect("failed to get VMO info").rights;
        assert!(vmo_rights.contains(zx::Rights::READ));
        assert!(!vmo_rights.contains(zx::Rights::WRITE));
        assert!(!vmo_rights.contains(zx::Rights::EXECUTE));
    } else {
        panic!("Expected VmoFile, got {:?} instead!", node_info);
    }
}

#[fasync::run_singlethreaded(test)]
async fn get_token_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_get_token.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
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
    if harness.config.no_get_token.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![]);
        let test_dir = harness.get_directory(root, dir_flags);

        let (status, _handle) = test_dir.get_token().await.expect("get_token failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn rename_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_rename.unwrap_or_default()
        || harness.config.no_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
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
            .rename2("old.txt", zx::Event::from(dest_token), "new.txt")
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
    if harness.config.no_rename.unwrap_or_default()
        || harness.config.no_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
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
            .rename2("old.txt", zx::Event::from(dest_token), "new.txt")
            .await
            .expect("rename failed");
        assert!(status.is_err());
        assert_eq!(status.err().unwrap(), zx::Status::BAD_HANDLE.into_raw());
    }
}

#[fasync::run_singlethreaded(test)]
async fn rename_with_slash_in_path_fails() {
    let harness = TestHarness::new().await;
    if harness.config.no_rename.unwrap_or_default()
        || harness.config.no_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![
            directory("src", vec![file("old.txt", contents.to_vec())]),
            directory("dest", vec![]),
        ]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;
        let dest_dir = open_rw_dir(&test_dir, "dest").await;

        // Including a slash in the src or dest path should fail.
        let status = test_dir
            .rename2("src/old.txt", zx::Event::from(get_token(&dest_dir).await), "new.txt")
            .await
            .expect("rename failed");
        assert!(status.is_err());
        assert_eq!(status.err().unwrap(), zx::Status::INVALID_ARGS.into_raw());
        let status = src_dir
            .rename2("old.txt", zx::Event::from(get_token(&dest_dir).await), "nested/new.txt")
            .await
            .expect("rename failed");
        assert!(status.is_err());
        assert_eq!(status.err().unwrap(), zx::Status::INVALID_ARGS.into_raw());
    }
}

#[fasync::run_singlethreaded(test)]
async fn link_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_link.unwrap_or_default() || harness.config.no_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
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
    if harness.config.no_link.unwrap_or_default() || harness.config.no_get_token.unwrap_or_default()
    {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
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
    if harness.config.immutable_dir.unwrap_or_default() {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let root =
            root_directory(vec![directory("src", vec![file("file.txt", contents.to_vec())])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;

        src_dir
            .unlink("file.txt", UnlinkOptions::EMPTY)
            .await
            .expect("unlink fidl failed")
            .expect("unlink failed");

        // Check file is gone.
        assert_file_not_found(&test_dir, "src/file.txt").await;
    }
}

#[fasync::run_singlethreaded(test)]
async fn unlink_file_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.immutable_dir.unwrap_or_default() {
        return;
    }
    let contents = "abcdef".as_bytes();

    for dir_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let root =
            root_directory(vec![directory("src", vec![file("file.txt", contents.to_vec())])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let src_dir = open_dir_with_flags(&test_dir, dir_flags, "src").await;

        assert_eq!(
            src_dir
                .unlink("file.txt", UnlinkOptions::EMPTY)
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
    if harness.config.immutable_dir.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![directory("src", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        // Re-open dir with flags being tested.
        let dir = open_dir_with_flags(&test_dir, dir_flags, ".").await;

        dir.unlink("src", UnlinkOptions::EMPTY)
            .await
            .expect("unlink fidl failed")
            .expect("unlink failed");
    }
}

#[fasync::run_singlethreaded(test)]
async fn unlink_directory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.immutable_dir.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![directory("src", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        // Re-open dir with flags being tested.
        let dir = open_dir_with_flags(&test_dir, dir_flags, ".").await;

        assert_eq!(
            dir.unlink("src", UnlinkOptions::EMPTY)
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

    if harness.config.immutable_dir.unwrap_or_default() {
        return;
    }

    let root = root_directory(vec![directory("dir", vec![]), file("file", vec![])]);
    let test_dir = harness.get_directory(root, harness.dir_rights.all());

    let must_be_directory =
        UnlinkOptions { flags: Some(UnlinkFlags::MustBeDirectory), ..UnlinkOptions::EMPTY };
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
        for clone_flags in build_flag_combinations(0, file_flags) {
            let (proxy, server) = create_proxy::<io::NodeMarker>().expect("create_proxy failed");
            file.clone(clone_flags | io::OPEN_FLAG_DESCRIBE, server).expect("clone failed");
            let status = get_open_status(&proxy).await;
            assert_eq!(status, zx::Status::OK);

            // Check flags of cloned connection are correct.
            let proxy = convert_node_proxy::<io::FileMarker>(proxy);
            let (status, flags) = proxy.node_get_flags().await.expect("node_get_flags failed");
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
        let (proxy, server) = create_proxy::<io::NodeMarker>().expect("create_proxy failed");
        file.clone(io::CLONE_FLAG_SAME_RIGHTS | io::OPEN_FLAG_DESCRIBE, server)
            .expect("clone failed");
        let status = get_open_status(&proxy).await;
        assert_eq!(status, zx::Status::OK);

        // Check flags of cloned connection are correct.
        let proxy = convert_node_proxy::<io::FileMarker>(proxy);
        let (status, flags) = proxy.node_get_flags().await.expect("node_get_flags failed");
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
        for clone_flags in build_flag_combinations(file_flags, harness.dir_rights.all()) {
            if clone_flags == file_flags {
                continue;
            }
            let (proxy, server) = create_proxy::<io::NodeMarker>().expect("create_proxy failed");
            file.clone(clone_flags | io::OPEN_FLAG_DESCRIBE, server).expect("clone failed");
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
        for clone_flags in build_flag_combinations(0, dir_flags) {
            let (proxy, server) = create_proxy::<io::NodeMarker>().expect("create_proxy failed");
            dir.clone(clone_flags | io::OPEN_FLAG_DESCRIBE, server).expect("clone failed");
            let status = get_open_status(&proxy).await;
            assert_eq!(status, zx::Status::OK);

            // Check flags of cloned connection are correct.
            let (status, flags) = proxy.node_get_flags().await.expect("node_get_flags failed");
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
        let (proxy, server) = create_proxy::<io::NodeMarker>().expect("create_proxy failed");
        dir.clone(io::CLONE_FLAG_SAME_RIGHTS | io::OPEN_FLAG_DESCRIBE, server)
            .expect("clone failed");
        let status = get_open_status(&proxy).await;
        assert_eq!(status, zx::Status::OK);

        // Check flags of cloned connection are correct.
        let proxy = convert_node_proxy::<io::DirectoryMarker>(proxy);
        let (status, flags) = proxy.node_get_flags().await.expect("node_get_flags failed");
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
        for clone_flags in build_flag_combinations(dir_flags, harness.dir_rights.all()) {
            if clone_flags == dir_flags {
                continue;
            }
            let (proxy, server) = create_proxy::<io::NodeMarker>().expect("create_proxy failed");
            dir.clone(clone_flags | io::OPEN_FLAG_DESCRIBE, server).expect("clone failed");
            let status = get_open_status(&proxy).await;
            assert_eq!(status, zx::Status::ACCESS_DENIED);
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn set_attr_file_with_sufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_set_attr.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![file("file", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let file = open_file_with_flags(&test_dir, dir_flags, "file").await;

        let (status, old_attr) = file.get_attr().await.expect("get_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        // Set CREATION_TIME flag, but not MODIFICATION_TIME.
        let status = file
            .set_attr(
                io::NODE_ATTRIBUTE_FLAG_CREATION_TIME,
                &mut io::NodeAttributes {
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
        let expected = io::NodeAttributes { creation_time: 111, ..old_attr };
        assert_eq!(new_attr, expected);
    }
}

#[fasync::run_singlethreaded(test)]
async fn set_attr_file_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_set_attr.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![file("file", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let file = open_file_with_flags(&test_dir, dir_flags, "file").await;

        let status = file
            .set_attr(
                io::NODE_ATTRIBUTE_FLAG_CREATION_TIME,
                &mut io::NodeAttributes {
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
    if harness.config.no_set_attr.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_with(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![directory("dir", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let dir = open_dir_with_flags(&test_dir, dir_flags, "dir").await;

        let (status, old_attr) = dir.get_attr().await.expect("get_attr failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        // Set CREATION_TIME flag, but not MODIFICATION_TIME.
        let status = dir
            .set_attr(
                io::NODE_ATTRIBUTE_FLAG_CREATION_TIME,
                &mut io::NodeAttributes {
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
        let expected = io::NodeAttributes { creation_time: 111, ..old_attr };
        assert_eq!(new_attr, expected);
    }
}

#[fasync::run_singlethreaded(test)]
async fn set_attr_directory_with_insufficient_rights() {
    let harness = TestHarness::new().await;
    if harness.config.no_set_attr.unwrap_or_default() {
        return;
    }

    for dir_flags in harness.file_rights.valid_combos_without(io::OPEN_RIGHT_WRITABLE) {
        let root = root_directory(vec![directory("dir", vec![])]);
        let test_dir = harness.get_directory(root, harness.dir_rights.all());
        let dir = open_dir_with_flags(&test_dir, dir_flags, "dir").await;

        let status = dir
            .set_attr(
                io::NODE_ATTRIBUTE_FLAG_CREATION_TIME,
                &mut io::NodeAttributes {
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
