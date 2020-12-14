// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{create_endpoints, create_proxy, Proxy, ServiceMarker},
    fidl_fuchsia_io as io, fidl_fuchsia_io_test as io_test, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    fuchsia_zircon::Status,
    futures::StreamExt,
    io_conformance::io1_request_logger_factory::Io1RequestLoggerFactory,
};

const TEST_FILE: &str = "testing.txt";

pub async fn connect_to_harness() -> io_test::Io1HarnessProxy {
    // Connect to the realm to get acccess to the outgoing directory for the harness.
    let (client, server) = zx::Channel::create().expect("Cannot create channel");
    fuchsia_component::client::connect_channel_to_service::<fsys::RealmMarker>(server)
        .expect("Cannot connect to Realm service");
    let mut realm = fsys::RealmSynchronousProxy::new(client);
    // fs_test is the name of the child component defined in the manifest.
    let mut child_ref = fsys::ChildRef { name: "fs_test".to_string(), collection: None };
    let (client, server) = zx::Channel::create().expect("Cannot create channel");
    realm
        .bind_child(
            &mut child_ref,
            fidl::endpoints::ServerEnd::<io::DirectoryMarker>::new(server),
            zx::Time::INFINITE,
        )
        .expect("FIDL error when binding to child in Realm")
        .expect("Cannot bind to test harness child in Realm");

    let exposed_dir = io::DirectoryProxy::new(
        fidl::AsyncChannel::from_channel(client).expect("Cannot create async channel"),
    );

    fuchsia_component::client::connect_to_protocol_at_dir_root::<io_test::Io1HarnessMarker>(
        &exposed_dir,
    )
    .expect("Cannot connect to test harness protocol")
}

/// Listens for the `OnOpen` event and returns its [Status].
async fn get_open_status(node_proxy: &io::NodeProxy) -> Status {
    let mut events = node_proxy.take_event_stream();
    let io::NodeEvent::OnOpen_ { s, info: _ } =
        events.next().await.expect("OnOpen event not received").expect("FIDL error");
    Status::from_raw(s)
}

/// Helper function to open the desired node in the root folder. Only use this
/// if testing something other than the open call directly.
async fn open_node<T: ServiceMarker>(
    dir: &io::DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> T::Proxy {
    let flags = flags | io::OPEN_FLAG_DESCRIBE;
    let (node_proxy, node_server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy");
    dir.open(flags, mode, path, node_server).expect("Cannot open node");

    assert_eq!(get_open_status(&node_proxy).await, Status::OK);
    T::Proxy::from_channel(node_proxy.into_channel().expect("Cannot convert node proxy to channel"))
}

/// Creates and returns a directory with the given structure from the test harness.
fn get_directory_from_harness(
    harness: &io_test::Io1HarnessProxy,
    root: io_test::Directory,
) -> io::DirectoryProxy {
    let (client, server) = create_proxy::<io::DirectoryMarker>().expect("Cannot create proxy");
    harness.get_directory(root, server).expect("Cannot get directory from test harness");
    return client;
}

/// Returns a constant representing the aggregate of all io.fidl rights that are supported by the
/// test harness.
async fn all_rights_for_harness(harness: &io_test::Io1HarnessProxy) -> u32 {
    let config = harness.get_config().await.expect("Cannot get config from harness");

    let mut all_rights = io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE;

    if !config.no_exec.unwrap_or_default() {
        all_rights |= io::OPEN_RIGHT_EXECUTABLE;
    }
    if !config.no_admin.unwrap_or_default() {
        all_rights |= io::OPEN_RIGHT_ADMIN;
    }
    all_rights
}

/// Returns a list of flag combinations to test. Returns a vector of the aggregate of
/// every constant flag and every combination of variable flags.
/// Ex. build_flag_combinations(100, 011) would return [100, 110, 101, 111]
/// for flags expressed as binary. We exclude the no rights case as that is an
/// invalid argument in most cases. Ex. build_flag_combinations(0, 011)
/// would return [010, 001, 011] without the 000 case.
fn build_flag_combinations(constant_flags: u32, variable_flags: u32) -> Vec<u32> {
    let mut vec = vec![constant_flags];

    for flag in split_flags(variable_flags) {
        let length = vec.len();
        for i in 0..length {
            vec.push(vec[i] | flag);
        }
    }
    vec.retain(|element| *element != 0);

    vec
}

/// Splits a bitset into a vector of its component bits. e.g. 1011 becomes [0001, 0010, 1000].
fn split_flags(flags: u32) -> Vec<u32> {
    let mut flags = flags;
    let mut bits = vec![];
    while flags != 0 {
        // x & -x returns the lowest bit set in x. Add it to the vec and then unset that bit.
        let lowest_bit = flags & flags.wrapping_neg();
        bits.push(lowest_bit);
        flags ^= lowest_bit;
    }
    bits
}

fn root_directory(flags: u32, entries: Vec<io_test::DirectoryEntry>) -> io_test::Directory {
    // Convert the simple vector of entries into the convoluted FIDL field type.
    let entries: Vec<Option<Box<io_test::DirectoryEntry>>> =
        entries.into_iter().map(|e| Some(Box::new(e))).collect();
    io_test::Directory {
        name: None,
        flags: Some(flags),
        entries: Some(entries),
        ..io_test::Directory::EMPTY
    }
}

fn directory(
    name: &str,
    flags: u32,
    entries: Vec<io_test::DirectoryEntry>,
) -> io_test::DirectoryEntry {
    let mut dir = root_directory(flags, entries);
    dir.name = Some(name.to_string());
    io_test::DirectoryEntry::Directory(dir)
}

fn file(name: &str, flags: u32, contents: Vec<u8>) -> io_test::DirectoryEntry {
    io_test::DirectoryEntry::File(io_test::File {
        name: Some(name.to_string()),
        flags: Some(flags),
        contents: Some(contents),
        ..io_test::File::EMPTY
    })
}

// Example test to start up a v2 component harness to test when opening a path that goes through a
// remote mount point, the server forwards the request to the remote correctly.
#[fasync::run_singlethreaded(test)]
async fn open_remote_directory_test() {
    let harness = connect_to_harness().await;

    let config = harness.get_config().await.expect("Could not get config from harness");
    if config.no_remote_dir.unwrap_or_default() {
        return;
    }

    let (remote_dir_client, remote_dir_server) =
        create_endpoints::<io::DirectoryMarker>().expect("Cannot create endpoints");

    let remote_name = "remote_directory";

    // Request an extra directory connection from the harness to use as the remote,
    // and interpose the requests from the server under test to this remote.
    let (logger, mut rx) = Io1RequestLoggerFactory::new();
    let remote_dir_server =
        logger.get_logged_directory(remote_name.to_string(), remote_dir_server).await;
    let root = root_directory(io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE, vec![]);
    harness.get_directory(root, remote_dir_server).expect("Cannot get empty remote directory");

    let (test_dir_proxy, test_dir_server) =
        create_proxy::<io::DirectoryMarker>().expect("Cannot create proxy");
    harness
        .get_directory_with_remote_directory(
            remote_dir_client,
            remote_name,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE,
            test_dir_server,
        )
        .expect("Cannot get test harness directory");

    let (_remote_dir_proxy, remote_dir_server) =
        create_proxy::<io::NodeMarker>().expect("Cannot create proxy");
    test_dir_proxy
        .open(io::OPEN_RIGHT_READABLE, io::MODE_TYPE_DIRECTORY, remote_name, remote_dir_server)
        .expect("Cannot open remote directory");

    // Wait on an open call to the interposed remote directory.
    let open_request_string = rx.next().await.expect("Local tx/rx channel was closed");

    // TODO(fxbug.dev/45613):: Bare-metal testing against returned request string. We need
    // to find a more ergonomic return format.
    assert_eq!(open_request_string, "remote_directory flags:1, mode:16384, path:.");
}

/// Creates a directory with all rights, and checks it can be opened for all subsets of rights.
#[fasync::run_singlethreaded(test)]
async fn open_dir_with_sufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    let root = root_directory(all_rights, vec![]);
    let root_dir = get_directory_from_harness(&harness, root);

    for dir_flags in build_flag_combinations(0, all_rights) {
        let (client, server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");
        root_dir
            .open(dir_flags | io::OPEN_FLAG_DESCRIBE, io::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_eq!(get_open_status(&client).await, Status::OK);
    }
}

/// Creates a directory with no rights, and checks opening it with any rights fails.
#[fasync::run_singlethreaded(test)]
async fn open_dir_with_insufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    let root = root_directory(0, vec![]);
    let root_dir = get_directory_from_harness(&harness, root);

    for dir_flags in build_flag_combinations(0, all_rights) {
        let (client, server) = create_proxy::<io::NodeMarker>().expect("Cannot create proxy.");
        root_dir
            .open(dir_flags | io::OPEN_FLAG_DESCRIBE, io::MODE_TYPE_DIRECTORY, ".", server)
            .expect("Cannot open directory");

        assert_eq!(get_open_status(&client).await, Status::ACCESS_DENIED);
    }
}

/// Opens a directory, and checks that a child directory can be opened using the same rights.
#[fasync::run_singlethreaded(test)]
async fn open_child_dir_with_same_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    for dir_flags in build_flag_combinations(0, all_rights) {
        let root = root_directory(all_rights, vec![directory("child", dir_flags, vec![])]);
        let root_dir = get_directory_from_harness(&harness, root);

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

        assert_eq!(get_open_status(&child_dir_client).await, Status::OK);
    }
}

/// Opens a directory as readable, and checks that a child directory cannot be opened as writable.
#[fasync::run_singlethreaded(test)]
async fn open_child_dir_with_extra_rights() {
    let harness = connect_to_harness().await;

    let root = root_directory(
        io::OPEN_RIGHT_READABLE,
        vec![directory("child", io::OPEN_RIGHT_READABLE, vec![])],
    );
    let root_dir = get_directory_from_harness(&harness, root);

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

    assert_eq!(get_open_status(&child_dir_client).await, Status::ACCESS_DENIED);
}

#[fasync::run_singlethreaded(test)]
async fn file_read_with_sufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    for file_flags in build_flag_combinations(io::OPEN_RIGHT_READABLE, all_rights) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _data) = file.read(0).await.expect("read failed");
        assert_eq!(Status::from_raw(status), Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_with_insufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    let not_readable_flags = all_rights & !io::OPEN_RIGHT_READABLE;

    for file_flags in build_flag_combinations(0, not_readable_flags) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _data) = file.read(0).await.expect("read failed");
        assert_eq!(Status::from_raw(status), Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_at_with_sufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    for file_flags in build_flag_combinations(io::OPEN_RIGHT_READABLE, all_rights) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _data) = file.read_at(0, 0).await.expect("read_at failed");
        assert_eq!(Status::from_raw(status), Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_at_with_insufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    let not_readable_flags = all_rights & !io::OPEN_RIGHT_READABLE;

    for file_flags in build_flag_combinations(0, not_readable_flags) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _data) = file.read_at(0, 0).await.expect("read_at failed");
        assert_eq!(Status::from_raw(status), Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_with_sufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    for file_flags in build_flag_combinations(io::OPEN_RIGHT_WRITABLE, all_rights) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _actual) = file.write("".as_bytes()).await.expect("write failed");
        assert_eq!(Status::from_raw(status), Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_with_insufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    let non_writable_flags = all_rights & !io::OPEN_RIGHT_WRITABLE;

    for file_flags in build_flag_combinations(0, non_writable_flags) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _actual) = file.write("".as_bytes()).await.expect("write failed");
        assert_eq!(Status::from_raw(status), Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_at_with_sufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    for file_flags in build_flag_combinations(io::OPEN_RIGHT_WRITABLE, all_rights) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _actual) = file.write_at("".as_bytes(), 0).await.expect("write_at failed");
        assert_eq!(Status::from_raw(status), Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_write_at_with_insufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    let non_writable_flags = all_rights & !io::OPEN_RIGHT_WRITABLE;

    for file_flags in build_flag_combinations(0, non_writable_flags) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let (status, _actual) = file.write_at("".as_bytes(), 0).await.expect("write_at failed");
        assert_eq!(Status::from_raw(status), Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_truncate_with_sufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    for file_flags in build_flag_combinations(io::OPEN_RIGHT_WRITABLE, all_rights) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let status = file.truncate(0).await.expect("truncate failed");
        assert_eq!(Status::from_raw(status), Status::OK);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_truncate_with_insufficient_rights() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    let non_writable_flags = all_rights & !io::OPEN_RIGHT_WRITABLE;

    for file_flags in build_flag_combinations(0, non_writable_flags) {
        let root = root_directory(all_rights, vec![file(TEST_FILE, file_flags, vec![])]);
        let test_dir = get_directory_from_harness(&harness, root);

        let file =
            open_node::<io::FileMarker>(&test_dir, file_flags, io::MODE_TYPE_FILE, TEST_FILE).await;
        let status = file.truncate(0).await.expect("truncate failed");
        assert_eq!(Status::from_raw(status), Status::BAD_HANDLE);
    }
}

#[fasync::run_singlethreaded(test)]
async fn file_read_in_subdirectory() {
    let harness = connect_to_harness().await;
    let all_rights = all_rights_for_harness(&harness).await;

    for file_flags in build_flag_combinations(io::OPEN_RIGHT_READABLE, all_rights) {
        let root = root_directory(
            all_rights,
            vec![directory("subdir", all_rights, vec![file("testing.txt", file_flags, vec![])])],
        );
        let test_dir = get_directory_from_harness(&harness, root);

        let file = open_node::<io::FileMarker>(
            &test_dir,
            file_flags,
            io::MODE_TYPE_FILE,
            "subdir/testing.txt",
        )
        .await;
        let (status, _data) = file.read(0).await.expect("Read failed");
        assert_eq!(Status::from_raw(status), Status::OK);
    }
}

#[cfg(test)]
mod tests {
    use super::{build_flag_combinations, split_flags};

    #[test]
    fn test_build_flag_combinations() {
        let constant_flags = 0b100;
        let variable_flags = 0b011;
        let generated_combinations = build_flag_combinations(constant_flags, variable_flags);
        let expected_result = [0b100, 0b101, 0b110, 0b111];
        assert_eq!(generated_combinations, expected_result);
    }

    #[test]
    fn test_build_flag_combinations_without_empty_rights() {
        let constant_flags = 0;
        let variable_flags = 0b011;
        let generated_combinations = build_flag_combinations(constant_flags, variable_flags);
        let expected_result = [0b001, 0b010, 0b011];
        assert_eq!(generated_combinations, expected_result);
    }

    #[test]
    fn test_split_flags() {
        assert_eq!(split_flags(0), vec![]);
        assert_eq!(split_flags(0b001), vec![0b001]);
        assert_eq!(split_flags(0b101), vec![0b001, 0b100]);
        assert_eq!(split_flags(0b111), vec![0b001, 0b010, 0b100]);
    }
}
