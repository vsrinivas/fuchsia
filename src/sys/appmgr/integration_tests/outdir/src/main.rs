// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    anyhow::{format_err, Error},
    directory_broker, fdio,
    fidl::endpoints::{create_endpoints, create_proxy, Proxy},
    fidl_fidl_examples_echo as fidl_echo, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_vfs_pseudo_fs_mt::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::pcb::asynchronous::read_only_static, path::Path as pfsPath, pseudo_directory,
    },
    fuchsia_zircon::HandleBased,
    io_util,
    std::{ffi::CString, path::Path},
};

/// This integration test creates a new appmgr process and confirms that it can connect to a sysmgr
/// run echo service through appmgr's outgoing directory.

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
    fasync::spawn(async move {
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
        };
        fake_pkgfs.open(
            ExecutionScope::from_executor(Box::new(fasync::EHandle::local())),
            fio::OPEN_RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            pfsPath::empty(),
            pkgfs_server_end,
        );
    });
    let pkgfs_c_str = CString::new("/pkgfs").unwrap();
    spawn_actions.push(fdio::SpawnAction::add_namespace_entry(
        &pkgfs_c_str,
        pkgfs_client_end.into_channel().into_handle(),
    ));

    let (svc_for_sys_client_end, svc_for_sys_server_end) = create_endpoints::<fio::NodeMarker>()?;
    fasync::spawn(async move {
        let fake_svc_for_sys = pseudo_directory! {};
        fake_svc_for_sys.open(
            ExecutionScope::from_executor(Box::new(fasync::EHandle::local())),
            fio::OPEN_RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            pfsPath::empty(),
            svc_for_sys_server_end,
        );
    });
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
    let res = fdio::spawn_etc(
        &job_default(),
        spawn_options,
        &appmgr_bin_path_c_str,
        &[&appmgr_bin_path_c_str],
        None,
        spawn_actions.as_mut_slice(),
    );
    let _ =
        res.map_err(|(status, msg)| format_err!("failed to spawn appmgr ({}): {:?}", status, msg))?;

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
