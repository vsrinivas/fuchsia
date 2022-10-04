// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::layout;
use crate::socket;
use fidl::{
    endpoints::{ClientEnd, Proxy},
    HandleBased,
};
use fidl_fuchsia_dash as fdash;
use fidl_fuchsia_dash::LauncherError;
use fidl_fuchsia_hardware_pty as pty;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_pkg as fpkg;
use fidl_fuchsia_process as fproc;
use fidl_fuchsia_sys2 as fsys;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_runtime::{HandleInfo as HandleId, HandleType};
use fuchsia_zircon as zx;
use moniker::{RelativeMoniker, RelativeMonikerBase};
use std::sync::Arc;

// -s: force input from stdin
// -i: force interactive
const DASH_ARGS_FOR_INTERACTIVE: [&[u8]; 2] = ["-i".as_bytes(), "-s".as_bytes()];
// TODO(fxbug.dev/104634): Verbose (-v) or write-commands-to-stderr (-x) is required if a
// command is given, else it errors with `Can't open <cmd>`.
// -c: execute command
const DASH_ARGS_FOR_COMMAND: [&[u8]; 2] = ["-v".as_bytes(), "-c".as_bytes()];

pub async fn launch_with_socket(
    moniker: &str,
    socket: zx::Socket,
    tools_url: Option<String>,
    command: Option<String>,
    ns_layout: fdash::DashNamespaceLayout,
) -> Result<zx::Process, LauncherError> {
    let pty = socket::spawn_pty_forwarder(socket).await?;
    launch_with_pty(moniker, pty, tools_url, command, ns_layout).await
}

pub async fn launch_with_pty(
    moniker: &str,
    pty: ClientEnd<pty::DeviceMarker>,
    tools_url: Option<String>,
    command: Option<String>,
    ns_layout: fdash::DashNamespaceLayout,
) -> Result<zx::Process, LauncherError> {
    let (stdin, stdout, stderr) = split_pty_into_handles(pty)?;
    launch_with_handles(moniker, stdin, stdout, stderr, tools_url, command, ns_layout).await
}

pub async fn launch_with_handles(
    moniker: &str,
    stdin: zx::Handle,
    stdout: zx::Handle,
    stderr: zx::Handle,
    tools_url: Option<String>,
    command: Option<String>,
    ns_layout: fdash::DashNamespaceLayout,
) -> Result<zx::Process, LauncherError> {
    // Get all directories needed to launch dash successfully
    let query =
        connect_to_protocol::<fsys::RealmQueryMarker>().map_err(|_| LauncherError::RealmQuery)?;
    let resolved_dirs = get_resolved_directories(&query, moniker).await?;

    let launcher_pkg_dir = get_pkg_from_launcher_namespace()?;

    let (out_dir, runtime_dir) = if let Some(execution_dirs) = resolved_dirs.execution_dirs {
        let out_dir = execution_dirs.out_dir.map(|d| d.into_proxy().unwrap());
        let runtime_dir = execution_dirs.runtime_dir.map(|d| d.into_proxy().unwrap());
        (out_dir, runtime_dir)
    } else {
        (None, None)
    };

    let ns_entries = resolved_dirs.ns_entries;
    let exposed_dir = resolved_dirs.exposed_dir.into_proxy().unwrap();
    let instance_pkg_dir = resolved_dirs.pkg_dir.map(|d| d.into_proxy().unwrap());

    let tools_pkg_dir = if let Some(tools_url) = tools_url {
        // Use the `fuchsia.pkg.PackageResolver` protocol to get the tools package directory.
        let tools_pkg_dir = get_tools_pkg_dir(&tools_url).await?;
        Some(tools_pkg_dir)
    } else {
        None
    };

    // Get the launcher's /pkg/bin.
    let bin_dir = fuchsia_fs::directory::open_directory(
        &launcher_pkg_dir,
        "bin",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .await
    .map_err(|_| LauncherError::Internal)?;

    // The dash-launcher can be asked to launch multiple dash processes, each of which can
    // make their own process hierarchies. This will look better topologically if we make a
    // child job for each dash process.
    let job =
        fuchsia_runtime::job_default().create_child_job().map_err(|_| LauncherError::Internal)?;

    // Create a library loader that loads libraries from 3 sources:
    // * the launcher's /pkg/lib dir.
    // * the instance's /pkg/lib dir.
    // * the tool's /pkg/lib dir.
    let ldsvc = create_loader_service(&launcher_pkg_dir, &instance_pkg_dir, &tools_pkg_dir).await;

    // Add handles for the current job, stdio, library loader and UTC time.
    let mut handle_infos = create_handle_infos(&job, stdin, stdout, stderr, ldsvc)?;

    // Add all the necessary entries into the dash namespace.
    let (mut name_infos, env_vars) = match ns_layout {
        fdash::DashNamespaceLayout::NestAllInstanceDirs => {
            // Add a custom `/svc` directory to dash that contains only `fuchsia.process.Launcher`
            let svc_dir = layout::serve_process_launcher_svc_dir()?;

            let (name_infos, path_envvar) = layout::nest_all_instance_dirs(
                bin_dir,
                ns_entries,
                exposed_dir,
                svc_dir,
                out_dir,
                runtime_dir,
                tools_pkg_dir,
            );
            let env_vars = vec![path_envvar.as_bytes()];
            (name_infos, env_vars)
        }
        fdash::DashNamespaceLayout::InstanceNamespaceIsRoot => {
            let (name_infos, path_envvar) =
                layout::instance_namespace_is_root(bin_dir, ns_entries, tools_pkg_dir).await;
            let env_vars = vec![path_envvar.as_bytes()];
            (name_infos, env_vars)
        }
    };

    // Set a name that's easy to find.
    // If moniker is `./core/foo`, process name is `sh-core-foo`.
    let mut process_name = moniker.replace('/', "-");
    process_name.remove(0);
    let process_name = format!("sh{}", process_name);

    let launcher = connect_to_protocol::<fproc::LauncherMarker>()
        .map_err(|_| LauncherError::ProcessLauncher)?;

    let opt_cmd: Option<Vec<&[u8]>> = command.as_ref().map(|s| vec![s.as_bytes()]);
    let mut args = Vec::new();
    if let Some(cmd) = opt_cmd {
        args.extend_from_slice(&DASH_ARGS_FOR_COMMAND);
        args.extend_from_slice(&cmd);
    } else {
        args.extend_from_slice(&DASH_ARGS_FOR_INTERACTIVE);
    };

    // Spawn the dash process.
    let mut info = create_launch_info(process_name, &job).await?;
    launcher.add_names(&mut name_infos.iter_mut()).map_err(|_| LauncherError::ProcessLauncher)?;
    launcher
        .add_handles(&mut handle_infos.iter_mut())
        .map_err(|_| LauncherError::ProcessLauncher)?;
    launcher.add_args(&mut args.into_iter()).map_err(|_| LauncherError::ProcessLauncher)?;
    launcher.add_environs(&mut env_vars.into_iter()).map_err(|_| LauncherError::ProcessLauncher)?;
    let (status, process) =
        launcher.launch(&mut info).await.map_err(|_| LauncherError::ProcessLauncher)?;
    zx::Status::ok(status).map_err(|_| LauncherError::ProcessLauncher)?;
    let process = process.ok_or(LauncherError::ProcessLauncher)?;

    // The job should be terminated when the dash process dies.
    job.set_critical(zx::JobCriticalOptions::empty(), &process)
        .map_err(|_| LauncherError::Internal)?;

    Ok(process)
}

fn split_pty_into_handles(
    pty: ClientEnd<pty::DeviceMarker>,
) -> Result<(zx::Handle, zx::Handle, zx::Handle), LauncherError> {
    let pty = pty.into_proxy().unwrap();

    // Split the PTY into 3 channels (stdin, stdout, stderr).
    let (stdout, to_pty_stdout) = fidl::endpoints::create_endpoints::<pty::DeviceMarker>().unwrap();
    let (stderr, to_pty_stderr) = fidl::endpoints::create_endpoints::<pty::DeviceMarker>().unwrap();
    let to_pty_stdout = to_pty_stdout.into_channel().into();
    let to_pty_stderr = to_pty_stderr.into_channel().into();

    // Clone the PTY to also be used for stdout and stderr.
    pty.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, to_pty_stdout).map_err(|_| LauncherError::Pty)?;
    pty.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, to_pty_stderr).map_err(|_| LauncherError::Pty)?;

    let stdin = pty.into_channel().unwrap().into_zx_channel().into_handle();
    let stdout = stdout.into_handle();
    let stderr = stderr.into_handle();

    Ok((stdin, stdout, stderr))
}

async fn get_resolved_directories(
    query: &fsys::RealmQueryProxy,
    moniker: &str,
) -> Result<Box<fsys::ResolvedDirectories>, LauncherError> {
    let moniker = RelativeMoniker::parse_str(moniker).map_err(|_| LauncherError::BadMoniker)?;
    let moniker = moniker.to_string();

    let resolved_dirs = query
        .get_instance_directories(&moniker)
        .await
        .map_err(|_| LauncherError::RealmQuery)?
        .map_err(|e| {
            if e == fsys::RealmQueryError::InstanceNotFound {
                LauncherError::InstanceNotFound
            } else {
                LauncherError::RealmQuery
            }
        })?;

    resolved_dirs.ok_or(LauncherError::InstanceNotResolved)
}

fn get_pkg_from_launcher_namespace() -> Result<fio::DirectoryProxy, LauncherError> {
    fuchsia_fs::directory::open_in_namespace(
        "/pkg",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .map_err(|_| LauncherError::Internal)
}

async fn get_tools_pkg_dir(tools_url: &str) -> Result<fio::DirectoryProxy, LauncherError> {
    let resolver = connect_to_protocol::<fpkg::PackageResolverMarker>()
        .map_err(|_| LauncherError::PackageResolver)?;
    let (tools_pkg_dir, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let _subpackage_context = resolver
        .resolve(tools_url, server)
        .await
        .map_err(|_| LauncherError::PackageResolver)?
        .map_err(|_| LauncherError::ToolsCannotResolve)?;
    Ok(tools_pkg_dir)
}

fn create_handle_infos(
    job: &zx::Job,
    stdin: zx::Handle,
    stdout: zx::Handle,
    stderr: zx::Handle,
    ldsvc: zx::Handle,
) -> Result<Vec<fproc::HandleInfo>, LauncherError> {
    let stdin_handle = fproc::HandleInfo {
        handle: stdin.into_handle(),
        id: HandleId::new(HandleType::FileDescriptor, 0).as_raw(),
    };

    let stdout_handle = fproc::HandleInfo {
        handle: stdout.into_handle(),
        id: HandleId::new(HandleType::FileDescriptor, 1).as_raw(),
    };

    let stderr_handle = fproc::HandleInfo {
        handle: stderr.into_handle(),
        id: HandleId::new(HandleType::FileDescriptor, 2).as_raw(),
    };

    let job_dup =
        job.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(|_| LauncherError::Internal)?;
    let job_handle = fproc::HandleInfo {
        handle: zx::Handle::from(job_dup),
        id: HandleId::new(HandleType::DefaultJob, 0).as_raw(),
    };

    let ldsvc_handle =
        fproc::HandleInfo { handle: ldsvc, id: HandleId::new(HandleType::LdsvcLoader, 0).as_raw() };

    let utc_clock = {
        let utc_clock = fuchsia_runtime::duplicate_utc_clock_handle(zx::Rights::SAME_RIGHTS)
            .map_err(|_| LauncherError::Internal)?;
        utc_clock.into_handle()
    };
    let utc_clock_handle = fproc::HandleInfo {
        handle: utc_clock,
        id: HandleId::new(HandleType::ClockUtc, 0).as_raw(),
    };

    Ok(vec![stdin_handle, stdout_handle, stderr_handle, job_handle, ldsvc_handle, utc_clock_handle])
}

async fn create_loader_service(
    launcher_pkg_dir: &fio::DirectoryProxy,
    instance_pkg_dir: &Option<fio::DirectoryProxy>,
    tools_pkg_dir: &Option<fio::DirectoryProxy>,
) -> zx::Handle {
    let mut lib_dirs = vec![];
    if let Ok(lib_dir) = fuchsia_fs::directory::open_directory(
        launcher_pkg_dir,
        "lib",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .await
    {
        lib_dirs.push(Arc::new(lib_dir));
    }

    if let Some(pkg_dir) = instance_pkg_dir {
        if let Ok(lib_dir) = fuchsia_fs::directory::open_directory(
            pkg_dir,
            "lib",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )
        .await
        {
            lib_dirs.push(Arc::new(lib_dir));
        }
    }

    if let Some(pkg_dir) = tools_pkg_dir {
        if let Ok(lib_dir) = fuchsia_fs::directory::open_directory(
            pkg_dir,
            "lib",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )
        .await
        {
            lib_dirs.push(Arc::new(lib_dir));
        }
    }

    let (ldsvc, server_end) = zx::Channel::create().unwrap();
    let ldsvc = ldsvc.into_handle();
    library_loader::start_with_multiple_dirs(lib_dirs, server_end);

    ldsvc
}

async fn create_launch_info(
    process_name: String,
    job: &zx::Job,
) -> Result<fproc::LaunchInfo, LauncherError> {
    // Load `/pkg/bin/sh` as an executable VMO and pass it to the Launcher.
    let dash_file = fuchsia_fs::file::open_in_namespace(
        "/pkg/bin/sh",
        fio::OpenFlags::RIGHT_EXECUTABLE | fio::OpenFlags::RIGHT_READABLE,
    )
    .map_err(|_| LauncherError::DashBinary)?;

    let executable = dash_file
        .get_backing_memory(
            fio::VmoFlags::READ | fio::VmoFlags::EXECUTE | fio::VmoFlags::PRIVATE_CLONE,
        )
        .await
        .map_err(|_| LauncherError::DashBinary)?
        .map_err(|_| LauncherError::DashBinary)?;

    let job_dup =
        job.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(|_| LauncherError::Internal)?;

    Ok(fproc::LaunchInfo { name: process_name, job: job_dup, executable })
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use futures::StreamExt;

    fn serve_realm_query(
        mut result: fsys::RealmQueryGetInstanceDirectoriesResult,
    ) -> fsys::RealmQueryProxy {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fsys::RealmQueryMarker>().unwrap();
        fasync::Task::spawn(async move {
            let mut stream = server_end.into_stream().unwrap();
            let request = stream.next().await.unwrap().unwrap();
            let (moniker, responder) = match request {
                fsys::RealmQueryRequest::GetInstanceDirectories { moniker, responder } => {
                    (moniker, responder)
                }
                _ => panic!("Unexpected RealmQueryRequest"),
            };
            assert_eq!(moniker, ".");
            responder.send(&mut result).unwrap();
        })
        .detach();
        proxy
    }

    #[fuchsia::test]
    async fn error_instance_unresolved() {
        let query = serve_realm_query(Ok(None));
        let error = get_resolved_directories(&query, ".").await.unwrap_err();
        assert_eq!(error, LauncherError::InstanceNotResolved);
    }

    #[fuchsia::test]
    async fn error_instance_not_found() {
        let query = serve_realm_query(Err(fsys::RealmQueryError::InstanceNotFound));
        let error = get_resolved_directories(&query, ".").await.unwrap_err();
        assert_eq!(error, LauncherError::InstanceNotFound);
    }
}
