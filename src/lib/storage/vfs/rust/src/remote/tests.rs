// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the remote node.

use super::remote_dir;

use crate::{assert_close, assert_read, assert_read_dirents, pseudo_directory};

use crate::{
    directory::{
        entry::DirectoryEntry,
        test_utils::{run_client, DirentsSameInodeBuilder},
    },
    execution_scope::ExecutionScope,
    file::vmo::read_only_static,
    path::Path,
};

use {
    fidl::{self, endpoints::ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FileMarker, DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE,
        INO_UNKNOWN, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_NO_REMOTE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_async as fasync,
};

fn set_up_remote(scope: ExecutionScope) -> DirectoryProxy {
    let r = pseudo_directory! {
        "a" => read_only_static("a content"),
        "dir" => pseudo_directory! {
            "b" => read_only_static("b content"),
        }
    };

    let (remote_proxy, remote_server_end) =
        fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
    r.open(
        scope,
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        MODE_TYPE_DIRECTORY,
        Path::dot(),
        ServerEnd::new(remote_server_end.into_channel()),
    );

    remote_proxy
}

#[fasync::run_singlethreaded(test)]
async fn test_set_up_remote() {
    let scope = ExecutionScope::new();
    let remote_proxy = set_up_remote(scope.clone());
    assert_close!(remote_proxy);
}

// Tests for opening a remote node with the NODE_REFERENCE flag. The remote node uses the existing
// Service connection type after construction, which is tested in service/tests/node_reference.rs.

#[test]
fn remote_dir_construction_open_node_ref() {
    let exec = fasync::TestExecutor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let remote_proxy = set_up_remote(scope.clone());
    let server = remote_dir(remote_proxy);

    run_client(exec, || async move {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let flags = OPEN_FLAG_NODE_REFERENCE;
        server.open(scope, flags, 0, Path::dot(), server_end.into_channel().into());
        assert_close!(proxy);
    })
}

#[test]
fn remote_dir_construction_open_no_remote() {
    let exec = fasync::TestExecutor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let remote_proxy = set_up_remote(scope.clone());
    let server = remote_dir(remote_proxy);

    run_client(exec, || async move {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let flags = OPEN_FLAG_NO_REMOTE;
        server.open(scope, flags, 0, Path::dot(), server_end.into_channel().into());
        assert_close!(proxy);
    })
}

#[test]
fn remote_dir_node_ref_with_path() {
    let exec = fasync::TestExecutor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let remote_proxy = set_up_remote(scope.clone());
    let server = remote_dir(remote_proxy);

    run_client(exec, || async move {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let flags = OPEN_FLAG_NODE_REFERENCE;
        server.open(
            scope,
            flags,
            0,
            Path::validate_and_split("dir/b").unwrap(),
            server_end.into_channel().into(),
        );
        assert_close!(proxy);
    })
}

// Tests for opening a remote node where we actually want the open request to be forwarded.

#[test]
fn remote_dir_direct_connection() {
    let exec = fasync::TestExecutor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let remote_proxy = set_up_remote(scope.clone());
    let server = remote_dir(remote_proxy);

    run_client(exec, || async move {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let flags = OPEN_RIGHT_READABLE;
        server.open(
            scope,
            flags,
            MODE_TYPE_DIRECTORY,
            Path::dot(),
            server_end.into_channel().into(),
        );

        let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
        expected
            // (10 + 1) = 11
            .add(DIRENT_TYPE_DIRECTORY, b".")
            // 11 + (10 + 1) = 22
            .add(DIRENT_TYPE_FILE, b"a");
        assert_read_dirents!(proxy, 22, expected.into_vec());

        assert_close!(proxy);
    })
}

#[test]
fn remote_dir_direct_connection_dir_contents() {
    let exec = fasync::TestExecutor::new().expect("Executor creation failed");
    let scope = ExecutionScope::new();

    let remote_proxy = set_up_remote(scope.clone());
    let server = remote_dir(remote_proxy);

    run_client(exec, || async move {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        let flags = OPEN_RIGHT_READABLE;
        let path = Path::validate_and_split("a").unwrap();
        server.open(scope, flags, MODE_TYPE_FILE, path, server_end.into_channel().into());

        assert_read!(proxy, "a content");
        assert_close!(proxy);
    })
}
