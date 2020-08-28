// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests that connect to the service node itself.

use super::endpoint;

// Macros are exported into the root of the crate.
use crate::{
    assert_close, assert_event, assert_get_attr, assert_read_err, clone_get_proxy_assert,
    clone_get_service_proxy_assert_ok,
};

use crate::{
    directory::entry::DirectoryEntry,
    execution_scope::ExecutionScope,
    file::test_utils::{run_client, run_server_client},
    path::Path,
};

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{
        FileEvent, FileMarker, NodeAttributes, NodeInfo, Service, CLONE_FLAG_SAME_RIGHTS,
        INO_UNKNOWN, MODE_TYPE_SERVICE, OPEN_FLAG_DESCRIBE, OPEN_FLAG_NODE_REFERENCE,
    },
    fuchsia_async::Executor,
    fuchsia_zircon::sys::ZX_OK,
    libc::{S_IRUSR, S_IWUSR},
};

#[test]
fn construction() {
    run_server_client(
        OPEN_FLAG_NODE_REFERENCE,
        endpoint(|_scope, _channel| ()),
        |proxy| async move {
            assert_close!(proxy);
        },
    );
}

#[test]
fn get_attr() {
    run_server_client(
        OPEN_FLAG_NODE_REFERENCE,
        endpoint(|_scope, _channel| ()),
        |proxy| async move {
            assert_get_attr!(
                proxy,
                NodeAttributes {
                    mode: MODE_TYPE_SERVICE | S_IRUSR | S_IWUSR,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                }
            );
            assert_close!(proxy);
        },
    );
}

#[test]
fn describe() {
    let exec = Executor::new().expect("Executor creation failed");

    let server = endpoint(|_scope, _channel| ());

    run_client(exec, || async move {
        let scope = ExecutionScope::new();
        let (proxy, server_end) =
            create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

        let flags = OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE;
        server.open(scope, flags, 0, Path::empty(), server_end.into_channel().into());

        assert_event!(proxy, FileEvent::OnOpen_ { s, info }, {
            assert_eq!(s, ZX_OK);
            assert_eq!(info, Some(Box::new(NodeInfo::Service(Service {}))));
        });
    });
}

#[test]
fn clone() {
    run_server_client(
        OPEN_FLAG_NODE_REFERENCE,
        endpoint(|_scope, _channel| ()),
        |first_proxy| async move {
            assert_get_attr!(
                first_proxy,
                NodeAttributes {
                    mode: MODE_TYPE_SERVICE | S_IRUSR | S_IWUSR,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                }
            );

            let second_proxy = clone_get_service_proxy_assert_ok!(
                &first_proxy,
                OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE
            );

            assert_read_err!(second_proxy, Status::ACCESS_DENIED);

            assert_get_attr!(
                first_proxy,
                NodeAttributes {
                    mode: MODE_TYPE_SERVICE | S_IRUSR | S_IWUSR,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                }
            );

            assert_close!(second_proxy);
            assert_close!(first_proxy);
        },
    );
}

#[test]
fn clone_same_rights() {
    run_server_client(
        OPEN_FLAG_NODE_REFERENCE,
        endpoint(|_scope, _channel| ()),
        |first_proxy| async move {
            assert_get_attr!(
                first_proxy,
                NodeAttributes {
                    mode: MODE_TYPE_SERVICE | S_IRUSR | S_IWUSR,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                }
            );

            let second_proxy = clone_get_service_proxy_assert_ok!(
                &first_proxy,
                CLONE_FLAG_SAME_RIGHTS | OPEN_FLAG_DESCRIBE
            );

            assert_read_err!(second_proxy, Status::ACCESS_DENIED);

            assert_get_attr!(
                first_proxy,
                NodeAttributes {
                    mode: MODE_TYPE_SERVICE | S_IRUSR | S_IWUSR,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                }
            );

            assert_close!(second_proxy);
            assert_close!(first_proxy);
        },
    );
}
