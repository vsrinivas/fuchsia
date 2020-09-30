// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fdio::{SpawnAction, SpawnOptions},
    fidl_fidl_test_components as ftest, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, HandleBased},
    scoped_task,
    std::ffi::CString,
    test_utils_lib::opaque_test::OpaqueTestBuilder,
};

const COMPONENT_MANAGER_URL: &str =
    "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/component_manager_for_rights_test.cmx";

async fn run_test(url: &str, expected_result: &str) {
    run_test_with_extra_dirs(url, vec![], expected_result).await;
}

async fn run_test_with_extra_dirs(
    url: &str,
    dir_handles: Vec<(String, zx::Handle)>,
    expected_result: &str,
) {
    let test = OpaqueTestBuilder::new(url)
        .component_manager_url(COMPONENT_MANAGER_URL)
        .extend_dir_handles(dir_handles)
        .build()
        .await
        .expect("failed to start component manager");
    let trigger = test
        .component_manager_app
        .connect_to_service::<ftest::TriggerMarker>()
        .expect("failed to connect to trigger");
    let result = trigger.run().await.expect("trigger failed");
    assert_eq!(result, expected_result, "Results did not match");
}

// Verifies that the component_manager supports routing capabilities with different rights and that
// offer right filtering and right inference are correctly working. The use statement will attempt
// to access permissions it isn't allowed and verify they return ACCESS_DENIED.
#[fasync::run_singlethreaded(test)]
async fn offer_dir_rights() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_offer_dir_rights.cm",
        "All tests passed",
    )
    .await
}

// Verifies that an invalidly configured use in a component will result in a failure on attempt to
// access that directory. Over accessing permissions will prevent that directory being routed to
// the component.
#[fasync::run_singlethreaded(test)]
async fn invalid_use_in_offer_dir_rights_prevented() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_invalid_use_in_offer_dir_rights.cm",
        "Directory rights test failed: /read_only - connection aborted",
    )
    .await
}

// Verifies that an invalid offer that offers more than is exposed to it is invalid and will result
// in the directory not being offered to the child process.
#[fasync::run_singlethreaded(test)]
async fn invalid_offer_dir_rights_prevented() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_invalid_offer_dir_rights.cm",
        "Directory rights test failed: /read_only - connection aborted",
    )
    .await
}

// Verifies that an invalid intermediate expose that attempts to increase its rights to a read only
// directory fails with that exposed direcotry not being mapped into the testing proccess.
#[fasync::run_singlethreaded(test)]
async fn invalid_intermediate_expose_prevented() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_invalid_expose_intermediate_offer_dir_rights.cm",
        "Directory rights test failed: /read_only - connection aborted",
    )
    .await
}

// Verifies that an intermediate expose that attempts to reduces the rights on a directory is able
// to have that propagate through to the rest of the system.
#[fasync::run_singlethreaded(test)]
async fn intermediate_expose_rights() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_expose_intermediate_offer_dir_rights.cm",
        "All tests passed",
    )
    .await
}

// Verifies that an intermediate offer that attempts to reduces the rights on a directory is able
// to have that propagate down to children nodes.
#[fasync::run_singlethreaded(test)]
async fn intermediate_offer_rights() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_offer_intermediate_offer_dir_rights.cm",
        "All tests passed",
    )
    .await
}

// Verifies that an intermediate offer that attempts to increase its rights to a directory results
// in that directory not being mapped into the child proccess.
#[fasync::run_singlethreaded(test)]
async fn invalid_intermediate_offer_prevented() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_invalid_offer_intermediate_offer_dir_rights.cm",
        "Directory rights test failed: /read_only - connection aborted",
    )
    .await
}

// Verifies that if the offer utilizes aliases instead of direct mapping rights scoping
// rules are still correctly enforced.
#[fasync::run_singlethreaded(test)]
async fn alias_offer_dir_rights() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_alias_offer_dir_rights.cm",
        "All tests passed",
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
async fn route_directories_from_component_manager_namespace() {
    // Originally this was just going to launch a 'expose_dirs.cmx' v1 component and connect
    // to the exposed directories through the directory_request handle, and then pipe those into
    // the test component_manager instance's namespace. However, appmgr intercepts the
    // directory_request handle during launch and connects this end of it to the new component's
    // '/svc', and furthermore does so with fdio_service_connect_at which only gives READABLE and
    // WRITABLE. So off to fdio_spawn we go...
    let (dir_request_server, dir_request_client) = zx::Channel::create().unwrap();
    let dir_request_info = HandleInfo::new(HandleType::DirectoryRequest, 0);
    let bin = CString::new("/pkg/bin/expose_dir_rights").unwrap();
    let _expose_process = scoped_task::spawn_etc(
        scoped_task::job_default(),
        SpawnOptions::DEFAULT_LOADER | SpawnOptions::CLONE_STDIO,
        &bin,
        &[&bin],
        None,
        &mut [SpawnAction::add_handle(dir_request_info, dir_request_server.into_handle())],
    )
    .map_err(|(status, msg)| {
        panic!("Failed to spawn expose_dirs process: {}, {}", status, msg);
    });

    let flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_POSIX;
    let ro_channel = open_at(&dir_request_client, "read_only", flags).unwrap();
    let rw_channel = open_at(&dir_request_client, "read_write", flags).unwrap();
    let rx_channel = open_at(&dir_request_client, "read_exec", flags).unwrap();
    let ra_channel =
        open_at(&dir_request_client, "read_admin", flags | fio::OPEN_RIGHT_ADMIN).unwrap();
    let rs_channel = open_at(&dir_request_client, "read_only_after_scoped", flags).unwrap();
    let rd_channel = open_at(&dir_request_client, "read_write", flags).unwrap();
    let dir_handles = vec![
        ("/read_only".to_string(), ro_channel.into()),
        ("/read_write".to_string(), rw_channel.into()),
        ("/read_exec".to_string(), rx_channel.into()),
        ("/read_admin".to_string(), ra_channel.into()),
        ("/read_only_after_scoped".to_string(), rs_channel.into()),
        ("/read_write_dup".to_string(), rd_channel.into()),
    ];

    run_test_with_extra_dirs(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/use_dir_rights.cm",
        dir_handles,
        "All tests passed",
    )
    .await
}

// Verifies that if the storage capability offered is valid then you can write to the storage.
#[fasync::run_singlethreaded(test)]
async fn storage_offer_from_rw_dir() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_storage_offer_rights.cm",
        "All tests passed",
    )
    .await
}

// Verifies you can't write to storage if its backing source capability is not writable.
#[fasync::run_singlethreaded(test)]
async fn storage_offer_from_r_dir_fails() {
    run_test(
        "fuchsia-pkg://fuchsia.com/rights_integration_test#meta/root_invalid_storage_offer_rights.cm",
        "Failed to write to file",
    )
    .await
}
