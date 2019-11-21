// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fdio::{SpawnAction, SpawnOptions},
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, HandleBased},
    std::ffi::CString,
    test_utils,
};

#[fasync::run_singlethreaded(test)]
async fn route_echo_service() -> Result<(), Error> {
    test_utils::launch_component_and_expect_output(
        "fuchsia-pkg://fuchsia.com/routing_integration_test#meta/echo_realm.cm",
        "Hippos rule!\n".to_string(),
    )
    .await
}

// This test verifies that component_manager supports directory capabilities with differing rights
// when the source of the capability is another v2 component within the topology. See
// 'route_dirs_from_sibling.cml' for more details.
#[fasync::run_singlethreaded(test)]
async fn route_directories_from_sibling() -> Result<(), Error> {
    test_utils::launch_component_and_expect_output(
        "fuchsia-pkg://fuchsia.com/routing_integration_test#meta/route_dirs_from_sibling.cm",
        "All tests passed\n".to_string(),
    )
    .await
}

pub fn open_at(channel: &zx::Channel, path: &str, flags: u32) -> Result<zx::Channel, Error> {
    let (client, server) = zx::Channel::create()?;
    fdio::open_at(channel, path, flags, server)?;
    Ok(client)
}

// This test verifies that component_manager supports directory capabilities with differing rights
// when the source of the capability is component_manager's namespace. This uses the same two test
// helpers as the '_from_sibling' test above, but directly spawns the exposing side as a process,
// routes connections to the directories to the test component_manager instance's namespace, and
// then uses the check_dir_rights.cm component - which uses them - directly as the root component.
#[fasync::run_singlethreaded(test)]
async fn route_directories_from_component_manager_namespace() -> Result<(), Error> {
    // Originally this was just going to launch a 'expose_dirs.cmx' v1 component and connect
    // to the exposed directories through the directory_request handle, and then pipe those into
    // the test component_manager instance's namespace. However, appmgr intercepts the
    // directory_request handle during launch and connects this end of it to the new component's
    // '/svc', and furthermore does so with fdio_service_connect_at which only gives READABLE and
    // WRITABLE. So off to fdio_spawn we go...
    let (dir_request_server, dir_request_client) = zx::Channel::create()?;
    let dir_request_info = HandleInfo::new(HandleType::DirectoryRequest, 0);
    let bin = CString::new("/pkg/bin/expose_dirs")?;
    fdio::spawn_etc(
        &fuchsia_runtime::job_default(),
        SpawnOptions::DEFAULT_LOADER | SpawnOptions::CLONE_STDIO,
        &bin,
        &[&bin],
        None,
        &mut [SpawnAction::add_handle(dir_request_info, dir_request_server.into_handle())],
    )
    .map_err(|(status, msg)| {
        format_err!("Failed to spawn expose_dirs process: {}, {}", status, msg)
    })?;

    let flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_POSIX;
    let ro_channel = open_at(&dir_request_client, "read_only", flags)?;
    let rw_channel = open_at(&dir_request_client, "read_write", flags)?;
    let rx_channel = open_at(&dir_request_client, "read_exec", flags)?;
    let dir_handles = vec![
        ("/read_only".to_string(), ro_channel.into()),
        ("/read_write".to_string(), rw_channel.into()),
        ("/read_exec".to_string(), rx_channel.into()),
    ];

    test_utils::launch_component_and_expect_output_with_extra_dirs(
        "fuchsia-pkg://fuchsia.com/routing_integration_test#meta/check_dir_rights.cm",
        dir_handles,
        "All tests passed\n".to_string(),
    )
    .await
}
