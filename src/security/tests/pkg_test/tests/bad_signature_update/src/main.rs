// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Result},
    argh::{from_env, FromArgs},
    fidl::endpoints::{create_endpoints, create_proxy, ServerEnd},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::PackageUrl,
    fidl_fuchsia_sys2::{StorageAdminMarker, StorageIteratorMarker},
    fidl_fuchsia_update_installer::{
        Initiator, InstallerMarker, MonitorMarker, MonitorRequest, Options, RebootControllerMarker,
        State,
    },
    fidl_test_security_pkg::PackageServer_Marker,
    fuchsia_async::Task,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_fs::directory::readdir,
    fuchsia_hash::Hash,
    fuchsia_merkle::MerkleTree,
    futures::{channel::oneshot::channel, join, TryStreamExt},
    security_pkg_test_util::load_config,
    std::fs::File,
    tracing::info,
};

/// Flags for bad_signature_update.
#[derive(FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path to v1 update package (update.far) file used for
    /// designating its merkle root hash.
    #[argh(option)]
    v1_update_far_path: String,
    /// absolute path to shared test configuration file understood by
    /// security_pkg_test_util::load_config().
    #[argh(option)]
    test_config_path: String,

    /// switch used by rust test runner.
    #[argh(switch)]
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    nocapture: bool,
}

async fn get_storage_for_component_instance(moniker_prefix: &str) -> fio::DirectoryProxy {
    let storage_admin = connect_to_protocol::<StorageAdminMarker>().unwrap();
    let (storage_user_iterator, storage_user_iterator_server_end) =
        create_proxy::<StorageIteratorMarker>().unwrap();
    storage_admin
        .list_storage_in_realm(".", storage_user_iterator_server_end)
        .await
        .unwrap()
        .unwrap();
    let mut matching_storage_users = vec![];
    loop {
        let chunk = storage_user_iterator.next().await.unwrap();
        if chunk.is_empty() {
            break;
        }
        let mut matches: Vec<String> =
            chunk.into_iter().filter(|moniker| moniker.starts_with(moniker_prefix)).collect();
        matching_storage_users.append(&mut matches);
    }
    assert_eq!(1, matching_storage_users.len());
    let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
    storage_admin
        .open_component_storage(
            matching_storage_users.first().unwrap(),
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            ServerEnd::new(server_end.into_channel()),
        )
        .unwrap();
    proxy
}

async fn get_local_package_server_url() -> String {
    connect_to_protocol::<PackageServer_Marker>().unwrap().get_url().await.unwrap()
}

async fn get_hello_world_v1_update_merkle(v1_update_far_path: String) -> Hash {
    let (sender, receiver) = channel::<Hash>();
    Task::local(async move {
        let mut hello_world_v1_update = File::open(&v1_update_far_path).unwrap();
        let hello_world_v1_update_merkle =
            MerkleTree::from_reader(&mut hello_world_v1_update).unwrap().root();
        sender.send(hello_world_v1_update_merkle).unwrap();
    })
    .detach();
    receiver.await.unwrap()
}

async fn attempt_update(update_url: &str) -> Result<State> {
    let installer_proxy = connect_to_protocol::<InstallerMarker>().unwrap();
    let (monitor_client_end, monitor_server_end) = create_endpoints::<MonitorMarker>().unwrap();

    // Prevent reboot attempt by signalling that the client (this code) will
    // manage reboot via the provided RebootController.
    let (_reboot_controller_proxy, reboot_controller_server_end) =
        create_proxy::<RebootControllerMarker>().unwrap();
    installer_proxy
        .start_update(
            &mut PackageUrl { url: update_url.to_string() },
            Options {
                initiator: Some(Initiator::Service),
                allow_attach_to_existing_attempt: Some(false),
                should_write_recovery: Some(false),
                ..Options::EMPTY
            },
            monitor_client_end,
            Some(reboot_controller_server_end),
        )
        .await
        .unwrap()
        .unwrap();

    let mut monitor_stream = monitor_server_end.into_stream().unwrap();
    while let Some(request) = monitor_stream.try_next().await.unwrap() {
        match request {
            MonitorRequest::OnState { state, responder } => {
                info!("Update state change: {:#?}", state);
                responder.send().unwrap();
                match state {
                    // All terminal states plus `WaitToReboot` (which can only
                    // lead to successful terminal states).
                    State::WaitToReboot(_)
                    | State::Reboot(_)
                    | State::DeferReboot(_)
                    | State::Complete(_)
                    | State::FailPrepare(_)
                    | State::FailFetch(_) => {
                        return Ok(state);
                    }
                    _ => {}
                }
            }
        }
    }

    bail!("Unexpected exit from update monitor state machine loop");
}

#[fuchsia::test]
async fn bad_signature_update() {
    info!("Starting bad_signature_update test");
    let args @ Args { v1_update_far_path, test_config_path, .. } = &from_env();
    info!(?args, "Initalizing bad_signature_update");

    // Load test environment configuration.
    let config = load_config(test_config_path);

    // Setup storage capabilities.
    let pkg_resolver_storage_proxy = get_storage_for_component_instance("./pkg-resolver").await;
    // TODO(fxbug.dev/88453): Need a test that confirms assumption: Production
    // configuration is an empty mutable storage directory.
    assert!(readdir(&pkg_resolver_storage_proxy).await.unwrap().is_empty());

    info!("Gathering data and connecting to package server");

    // Setup package server and perform pre-update access check.
    let (update_merkle, package_server_url) = join!(
        get_hello_world_v1_update_merkle(v1_update_far_path.to_string()),
        get_local_package_server_url()
    );

    info!("Package server running on {}", package_server_url);

    // Placeholder assertion for well-formed local URL. Test will eventually use
    // URL to configure network connection for `pkg-resolver`.
    assert!(package_server_url.starts_with("https://localhost"));

    let update_url =
        format!("fuchsia-pkg://{}/update/0?hash={}", config.update_domain, update_merkle);

    info!("Initiating update: {}", update_url);

    let update_result = attempt_update(&update_url).await.unwrap();

    // Must not end in an "update complete" state.
    assert!(match update_result {
        State::WaitToReboot(_) | State::Reboot(_) | State::DeferReboot(_) | State::Complete(_) => {
            false
        }
        _ => {
            true
        }
    });
}
