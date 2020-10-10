// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    anyhow::{format_err, Error},
    directory_broker, fdio,
    fidl::endpoints::{create_endpoints, create_proxy, Proxy},
    fidl_fidl_examples_echo as fidl_echo, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys_internal as fsys_internal, fuchsia_async as fasync,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::HandleBased,
    io_util,
    matches::assert_matches,
    scoped_task,
    std::{ffi::CString, path::Path},
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::pcb::asynchronous::read_only_static, path::Path as pfsPath, pseudo_directory,
    },
};

/// This integration test creates a new appmgr process and confirms that it can connect to a sysmgr
/// run echo service through appmgr's outgoing directory once the log connection has been
/// established.

const APPMGR_BIN_PATH: &'static str = "/pkg/bin/appmgr";
const SYSMGR_SERVICES_CONFIG: &'static str = r#"{
  "services": {
    "fidl.examples.echo.Echo": "fuchsia-pkg://fuchsia.com/echo_server#meta/echo_server.cmx"
  }
}"#;
const APPMGR_SCHEME_MAP: &'static str = r#"{
    "launchers": {
        "package": [ "file", "fuchsia-pkg" ]
    }
}
"#;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let (appmgr_out_dir_proxy, appmgr_out_dir_server_end) = create_proxy::<fio::DirectoryMarker>()?;

    let mut spawn_actions = vec![];
    spawn_actions.push(fdio::SpawnAction::add_handle(
        HandleInfo::new(HandleType::DirectoryRequest, 0),
        appmgr_out_dir_server_end.into_channel().into_handle(),
    ));

    let pkg_dir = io_util::open_directory_in_namespace("/pkg", io_util::OPEN_RIGHT_READABLE)
        .expect("failed to open /pkg");
    let pkg_c_str = CString::new("/pkg").unwrap();
    spawn_actions.push(fdio::SpawnAction::add_namespace_entry(
        &pkg_c_str,
        pkg_dir.into_channel().unwrap().into_zx_channel().into_handle(),
    ));

    let svc_dir = io_util::open_directory_in_namespace(
        "/svc",
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )
    .expect("failed to open /svc");
    let svc_c_str = CString::new("/svc").unwrap();
    spawn_actions.push(fdio::SpawnAction::add_namespace_entry(
        &svc_c_str,
        svc_dir.into_channel().unwrap().into_zx_channel().into_handle(),
    ));

    let (pkgfs_client_end, pkgfs_server_end) = create_endpoints::<fio::NodeMarker>()?;
    fasync::Task::spawn(async move {
        let pkg_dir = io_util::open_directory_in_namespace("/pkg", fio::OPEN_RIGHT_READABLE)
            .expect("failed to open /pkg");
        let pkg_dir_2 = io_util::clone_directory(&pkg_dir, fio::CLONE_FLAG_SAME_RIGHTS)
            .expect("failed to clone /pkg handle");
        let fake_pkgfs = pseudo_directory! {
            "packages" => pseudo_directory! {
                "sysmgr" => pseudo_directory! {
                    "0" => directory_broker::DirectoryBroker::from_directory_proxy(pkg_dir),
                },
                "echo_server" => pseudo_directory! {
                    "0" => directory_broker::DirectoryBroker::from_directory_proxy(pkg_dir_2),
                },
                "config-data" => pseudo_directory! {
                    "0" => pseudo_directory! {
                        "meta" => pseudo_directory! {
                            "data" => pseudo_directory! {
                                "sysmgr" => pseudo_directory! {
                                    "services.config" => read_only_static(SYSMGR_SERVICES_CONFIG),
                                },
                                "appmgr" => pseudo_directory! {
                                    "scheme_map" => pseudo_directory! {
                                        "default.config" => read_only_static(APPMGR_SCHEME_MAP),
                                    }
                                },
                            },
                        },
                    },
                },
            },
        };
        fake_pkgfs.open(
            ExecutionScope::new(),
            fio::OPEN_RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            pfsPath::empty(),
            pkgfs_server_end,
        );
    })
    .detach();
    let pkgfs_c_str = CString::new("/pkgfs").unwrap();
    spawn_actions.push(fdio::SpawnAction::add_namespace_entry(
        &pkgfs_c_str,
        pkgfs_client_end.into_channel().into_handle(),
    ));

    let (svc_for_sys_client_end, svc_for_sys_server_end) = create_endpoints::<fio::NodeMarker>()?;
    fasync::Task::spawn(async move {
        let fake_svc_for_sys = pseudo_directory! {};
        fake_svc_for_sys.open(
            ExecutionScope::new(),
            fio::OPEN_RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            pfsPath::empty(),
            svc_for_sys_server_end,
        );
    })
    .detach();
    let svc_for_sys_c_str = CString::new("/svc_for_sys").unwrap();
    spawn_actions.push(fdio::SpawnAction::add_namespace_entry(
        &svc_for_sys_c_str,
        svc_for_sys_client_end.into_channel().into_handle(),
    ));

    let mut spawn_options = fdio::SpawnOptions::empty();
    spawn_options.insert(fdio::SpawnOptions::DEFAULT_LOADER);
    spawn_options.insert(fdio::SpawnOptions::CLONE_JOB);
    spawn_options.insert(fdio::SpawnOptions::CLONE_STDIO);

    let appmgr_bin_path_c_str = CString::new(APPMGR_BIN_PATH).unwrap();
    let _process = scoped_task::spawn_etc(
        scoped_task::job_default(),
        spawn_options,
        &appmgr_bin_path_c_str,
        &[&appmgr_bin_path_c_str],
        None,
        spawn_actions.as_mut_slice(),
    )
    .map_err(|(status, msg)| format_err!("failed to spawn appmgr ({}): {:?}", status, msg))?;

    let log_connector_node = io_util::open_node(
        &appmgr_out_dir_proxy,
        &Path::new("appmgr_svc/fuchsia.sys.internal.LogConnector"),
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        fio::MODE_TYPE_SERVICE,
    )?;
    let log_connector_node_chan =
        log_connector_node.into_channel().map_err(|e| format_err!("{:?}", e))?;
    let log_connector = fsys_internal::LogConnectorProxy::from_channel(log_connector_node_chan);
    assert_matches!(log_connector.take_log_connection_listener().await, Ok(Some(_)));

    let echo_service_node = io_util::open_node(
        &appmgr_out_dir_proxy,
        &Path::new("svc/fidl.examples.echo.Echo"),
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        fio::MODE_TYPE_SERVICE,
    )?;
    let echo_node_chan = echo_service_node.into_channel().map_err(|e| format_err!("{:?}", e))?;
    let echo_proxy = fidl_echo::EchoProxy::from_channel(echo_node_chan);

    assert_eq!(Some("hippos!".to_string()), echo_proxy.echo_string(Some("hippos!")).await?);
    Ok(())
}
