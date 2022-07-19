// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::socket;
use fidl::{
    endpoints::{ClientEnd, Proxy},
    HandleBased,
};
use fidl_fuchsia_component as fcomp;
use fidl_fuchsia_dash::LauncherError;
use fidl_fuchsia_hardware_pty as pty;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_pkg as fpkg;
use fidl_fuchsia_process::{HandleInfo, LaunchInfo, LauncherMarker, NameInfo};
use fidl_fuchsia_sys2 as fsys;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_component::server::ServiceFs;
use fuchsia_runtime::{HandleInfo as HandleId, HandleType};
use fuchsia_zircon as zx;
use futures::prelude::*;
use moniker::{RelativeMoniker, RelativeMonikerBase};
use std::sync::Arc;
use tracing::*;

pub async fn launch_with_socket(
    moniker: &str,
    socket: zx::Socket,
    tools_url: Option<String>,
) -> Result<zx::Process, LauncherError> {
    let pty = socket::spawn_pty_forwarder(socket).await?;
    launch_with_pty(moniker, pty, tools_url).await
}

pub async fn launch_with_pty(
    moniker: &str,
    pty: ClientEnd<pty::DeviceMarker>,
    tools_url: Option<String>,
) -> Result<zx::Process, LauncherError> {
    let (stdin, stdout, stderr) = split_pty_into_handles(pty)?;
    launch_with_handles(moniker, stdin, stdout, stderr, tools_url).await
}

pub async fn launch_with_handles(
    moniker: &str,
    stdin: zx::Handle,
    stdout: zx::Handle,
    stderr: zx::Handle,
    tools_url: Option<String>,
) -> Result<zx::Process, LauncherError> {
    // Process moniker.
    let moniker = RelativeMoniker::parse(&moniker).map_err(|_| LauncherError::BadMoniker)?;
    if !moniker.up_path().is_empty() {
        return Err(LauncherError::BadMoniker);
    }
    let moniker = moniker.to_string();

    let launcher =
        connect_to_protocol::<LauncherMarker>().map_err(|_| LauncherError::ProcessLauncher)?;

    let query =
        connect_to_protocol::<fsys::RealmQueryMarker>().map_err(|_| LauncherError::RealmQuery)?;

    let instance_scope = InstanceScope::new(&query, &moniker).await?;

    // The dash-launcher can be asked to launch multiple dash processes, each of which can
    // make their own process hierarchies. This will look better topologically if we make a
    // child job for each dash process.
    let job =
        fuchsia_runtime::job_default().create_child_job().map_err(|_| LauncherError::Internal)?;

    let mut lib_dirs = vec![];

    // Add the dash-launcher's /pkg/lib dir to the library loader.
    let launcher_lib_dir = get_lib_from_launcher_namespace()?;
    lib_dirs.push(Arc::new(launcher_lib_dir));

    // Add the instance's /pkg/lib dir to the library loader.
    if let Some(dir) = instance_scope.lib_dir {
        info!("Using /pkg/lib dir of {} for loading libraries", moniker);
        lib_dirs.push(Arc::new(dir));
    }

    // If a tools package URL is provided, open the corresponding package directory.
    let tools_pkg_dir = if let Some(tools_url) = &tools_url {
        let pkg_dir = get_tools_pkg_dir_from_url(&tools_url).await?;

        // Add the tool's /pkg/lib dir to the library loader.
        if let Some(dir) = get_lib_dir_from_pkg_dir(&pkg_dir).await {
            info!("Using /pkg/lib dir of {} for loading libraries", tools_url);
            lib_dirs.push(Arc::new(dir));
        }

        let pkg_dir = pkg_dir.into_channel().unwrap().into_zx_channel();
        let pkg_dir = ClientEnd::new(pkg_dir);
        Some(pkg_dir)
    } else {
        None
    };

    let mut ns = create_dash_namespace(instance_scope.ns, tools_pkg_dir)?;
    let mut handles = create_dash_handles(&job, stdin, stdout, stderr, lib_dirs)?;

    // -s: force input from stdin
    // -i: force interactive
    let args = vec!["-i".as_bytes(), "-s".as_bytes()];

    // PATH must contain the three possible sources of binaries:
    // * binaries packaged with dash-launcher.
    // * binaries packaged with the component being explored.
    // * binaries packaged with the given tools package dir.
    // Note that dash can handle paths that do not exist.
    let path_envvar = "PATH=/bin:/ns/pkg/bin:/tools/bin";
    let env = vec![path_envvar.as_bytes()];

    let mut info = create_launch_info(&moniker, &job).await?;

    // Spawn the dash process.
    launcher.add_names(&mut ns.iter_mut()).map_err(|_| LauncherError::ProcessLauncher)?;
    launcher.add_handles(&mut handles.iter_mut()).map_err(|_| LauncherError::ProcessLauncher)?;
    launcher.add_args(&mut args.into_iter()).map_err(|_| LauncherError::ProcessLauncher)?;
    launcher.add_environs(&mut env.into_iter()).map_err(|_| LauncherError::ProcessLauncher)?;
    let (status, process) =
        launcher.launch(&mut info).await.map_err(|_| LauncherError::ProcessLauncher)?;
    zx::Status::ok(status).map_err(|_| LauncherError::ProcessLauncher)?;
    let process = process.ok_or(LauncherError::ProcessLauncher)?;

    // The job should be terminated when the dash process dies.
    job.set_critical(zx::JobCriticalOptions::empty(), &process)
        .map_err(|_| LauncherError::Internal)?;

    Ok(process)
}

fn get_bin_from_launcher_namespace() -> Result<ClientEnd<fio::DirectoryMarker>, LauncherError> {
    let (bin_dir, server) = fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();
    fuchsia_fs::node::connect_in_namespace(
        "/pkg/bin",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        server.into_channel(),
    )
    .map_err(|_| LauncherError::Internal)?;

    Ok(bin_dir)
}

async fn create_launch_info(moniker: &str, job: &zx::Job) -> Result<LaunchInfo, LauncherError> {
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

    // Set a name that's easy to find.
    // if moniker is `./core/foo`, process name is `sh-core-foo`.
    let mut process_name = moniker.replace('/', "-");
    process_name.remove(0);
    let process_name = format!("sh{}", process_name);

    Ok(LaunchInfo { name: process_name, job: job_dup, executable })
}

fn serve_dash_svc_dir() -> Result<ClientEnd<fio::DirectoryMarker>, LauncherError> {
    // Serve a directory that only provides fuchsia.process.Launcher to dash.
    let (svc_dir, server_end) =
        fidl::endpoints::create_endpoints().map_err(|_| LauncherError::Internal)?;

    let mut fs = ServiceFs::new();
    fs.add_proxy_service::<LauncherMarker, ()>();
    fs.serve_connection(server_end.into_channel()).map_err(|_| LauncherError::Internal)?;

    fasync::Task::spawn(async move {
        fs.collect::<()>().await;
    })
    .detach();

    Ok(svc_dir)
}

fn create_dash_namespace(
    instance_ns: Vec<NameInfo>,
    tools_pkg_dir: Option<ClientEnd<fio::DirectoryMarker>>,
) -> Result<Vec<NameInfo>, LauncherError> {
    let mut namespace = instance_ns;

    // Add the dash-launcher `/pkg/bin` to dash as `/bin`.
    let bin_dir = get_bin_from_launcher_namespace()?;
    namespace.push(NameInfo { path: "/bin".to_string(), directory: bin_dir });

    // Add a custom `/svc` directory to dash.
    let svc_dir = serve_dash_svc_dir()?;
    namespace.push(NameInfo { path: "/svc".to_string(), directory: svc_dir });

    // Add the tools package to dash as `/tools`.
    if let Some(tools_pkg_dir) = tools_pkg_dir {
        namespace.push(NameInfo { path: "/tools".to_string(), directory: tools_pkg_dir });
    }

    Ok(namespace)
}

fn split_pty_into_handles(
    pty: ClientEnd<pty::DeviceMarker>,
) -> Result<(zx::Handle, zx::Handle, zx::Handle), LauncherError> {
    let pty = pty.into_proxy().map_err(|_| LauncherError::Pty)?;

    // Split the PTY into 3 channels (stdin, stdout, stderr).
    let (stdout, to_pty_stdout) =
        fidl::endpoints::create_endpoints::<pty::DeviceMarker>().map_err(|_| LauncherError::Pty)?;
    let (stderr, to_pty_stderr) =
        fidl::endpoints::create_endpoints::<pty::DeviceMarker>().map_err(|_| LauncherError::Pty)?;
    let to_pty_stdout = to_pty_stdout.into_channel().into();
    let to_pty_stderr = to_pty_stderr.into_channel().into();

    // Clone the PTY to also be used for stdout and stderr.
    pty.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, to_pty_stdout).map_err(|_| LauncherError::Pty)?;
    pty.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, to_pty_stderr).map_err(|_| LauncherError::Pty)?;

    let stdin = pty.into_channel().map_err(|_| LauncherError::Pty)?.into_zx_channel().into_handle();
    let stdout = stdout.into_handle();
    let stderr = stderr.into_handle();

    Ok((stdin, stdout, stderr))
}

fn get_lib_from_launcher_namespace() -> Result<fio::DirectoryProxy, LauncherError> {
    let (lib_dir, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    fuchsia_fs::node::connect_in_namespace(
        "/pkg/lib",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        server.into_channel(),
    )
    .map_err(|_| LauncherError::Internal)?;

    Ok(lib_dir)
}

fn create_dash_handles(
    job: &zx::Job,
    stdin: zx::Handle,
    stdout: zx::Handle,
    stderr: zx::Handle,
    lib_dirs: Vec<Arc<fio::DirectoryProxy>>,
) -> Result<Vec<HandleInfo>, LauncherError> {
    let stdin_handle = HandleInfo {
        handle: stdin.into_handle(),
        id: HandleId::new(HandleType::FileDescriptor, 0).as_raw(),
    };

    let stdout_handle = HandleInfo {
        handle: stdout.into_handle(),
        id: HandleId::new(HandleType::FileDescriptor, 1).as_raw(),
    };

    let stderr_handle = HandleInfo {
        handle: stderr.into_handle(),
        id: HandleId::new(HandleType::FileDescriptor, 2).as_raw(),
    };

    let job_dup =
        job.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(|_| LauncherError::Internal)?;
    let job_handle = HandleInfo {
        handle: zx::Handle::from(job_dup),
        id: HandleId::new(HandleType::DefaultJob, 0).as_raw(),
    };

    let (ldsvc, server_end) = zx::Channel::create().map_err(|_| LauncherError::Internal)?;
    let ldsvc = ldsvc.into_handle();
    library_loader::start_with_multiple_dirs(lib_dirs, server_end);

    let ldsvc_handle =
        HandleInfo { handle: ldsvc, id: HandleId::new(HandleType::LdsvcLoader, 0).as_raw() };

    let utc_clock = {
        let utc_clock = fuchsia_runtime::duplicate_utc_clock_handle(zx::Rights::SAME_RIGHTS)
            .map_err(|_| LauncherError::Internal)?;
        utc_clock.into_handle()
    };
    let utc_clock_handle =
        HandleInfo { handle: utc_clock, id: HandleId::new(HandleType::ClockUtc, 0).as_raw() };

    Ok(vec![stdin_handle, stdout_handle, stderr_handle, job_handle, ldsvc_handle, utc_clock_handle])
}

async fn get_tools_pkg_dir_from_url(tools_url: &str) -> Result<fio::DirectoryProxy, LauncherError> {
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

async fn get_lib_dir_from_pkg_dir(pkg_dir: &fio::DirectoryProxy) -> Option<fio::DirectoryProxy> {
    fuchsia_fs::directory::open_directory(
        pkg_dir,
        "lib",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .await
    .ok()
}

#[derive(Debug)]
struct InstanceScope {
    lib_dir: Option<fio::DirectoryProxy>,
    ns: Vec<NameInfo>,
}

impl InstanceScope {
    async fn new(query: &fsys::RealmQueryProxy, moniker: &str) -> Result<Self, LauncherError> {
        let (_, resolved) = query
            .get_instance_info(moniker)
            .await
            .map_err(|_| LauncherError::RealmQuery)?
            .map_err(|e| {
                if e == fcomp::Error::InstanceNotFound {
                    LauncherError::InstanceNotFound
                } else {
                    LauncherError::RealmQuery
                }
            })?;

        let mut ns = vec![];
        let mut lib_dir = None;

        let resolved = if let Some(resolved) = resolved {
            resolved
        } else {
            return Err(LauncherError::InstanceNotResolved);
        };

        if let Some(started) = resolved.started {
            if let Some(out_dir) = started.out_dir {
                ns.push(NameInfo { path: "/out".to_string(), directory: out_dir });
            }
            if let Some(runtime_dir) = started.runtime_dir {
                ns.push(NameInfo { path: "/runtime".to_string(), directory: runtime_dir });
            }
        }
        ns.push(NameInfo { path: "/ns".to_string(), directory: resolved.ns_dir });
        ns.push(NameInfo { path: "/exposed".to_string(), directory: resolved.exposed_dir });

        // If available, use the component's /pkg/lib dir to load libraries.
        if let Some(pkg_dir) = resolved.pkg_dir {
            let pkg_dir = pkg_dir.into_proxy().map_err(|_| LauncherError::Internal)?;
            lib_dir = get_lib_dir_from_pkg_dir(&pkg_dir).await;
        }
        Ok(Self { ns, lib_dir })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn create_temp_dir(file_name: &str) -> ClientEnd<fio::DirectoryMarker> {
        // Create a temp directory and put a file with name `file_name` inside it.
        let temp_dir = TempDir::new().unwrap();
        let temp_dir_path = temp_dir.into_path();
        let file_path = temp_dir_path.join(file_name);
        std::fs::write(&file_path, "Hippos Rule!").unwrap();
        let temp_dir_path = temp_dir_path.display().to_string();
        let dir_proxy =
            fuchsia_fs::open_directory_in_namespace(&temp_dir_path, fio::OpenFlags::RIGHT_READABLE)
                .unwrap();
        let channel = dir_proxy.into_channel().unwrap().into_zx_channel();
        channel.into()
    }

    fn serve_realm_query(
        instance_info: fsys::InstanceInfo,
        resolved: Option<Box<fsys::ResolvedState>>,
    ) -> fsys::RealmQueryProxy {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fsys::RealmQueryMarker>().unwrap();
        fasync::Task::spawn(async move {
            let mut stream = server_end.into_stream().unwrap();
            let fsys::RealmQueryRequest::GetInstanceInfo { moniker, responder } =
                stream.next().await.unwrap().unwrap();
            assert_eq!(moniker, ".");
            responder.send(&mut Ok((instance_info, resolved))).unwrap();
        })
        .detach();
        proxy
    }

    #[fuchsia::test]
    async fn check_namespace_started_with_tools() {
        let instance_info = fsys::InstanceInfo {
            moniker: ".".to_string(),
            url: "test://foo.com#meta/bar.cm".to_string(),
            instance_id: None,
            state: fsys::InstanceState::Started,
        };

        let tools_dir = create_temp_dir("tools");
        let exposed_dir = create_temp_dir("exposed");
        let ns_dir = create_temp_dir("ns");
        let out_dir = create_temp_dir("out");
        let runtime_dir = create_temp_dir("runtime");

        let started = Some(Box::new(fsys::StartedState {
            out_dir: Some(out_dir),
            runtime_dir: Some(runtime_dir),
            start_reason: "Debug".to_string(),
        }));

        let resolved = Some(Box::new(fsys::ResolvedState {
            uses: vec![],
            exposes: vec![],
            config: None,
            pkg_dir: None,
            started,
            exposed_dir,
            ns_dir,
        }));

        let query = serve_realm_query(instance_info, resolved);

        let instance_scope = InstanceScope::new(&query, ".").await.unwrap();
        let ns = create_dash_namespace(instance_scope.ns, Some(tools_dir)).unwrap();

        assert_eq!(ns.len(), 7);

        // Make sure that the correct directories were mapped to the correct paths.
        for entry in ns {
            let dir = entry.directory.into_proxy().unwrap();
            let entries = fuchsia_fs::directory::readdir(&dir).await.unwrap();
            match entry.path.as_str() {
                "/bin" => {
                    // The contents of this directory are /pkg/bin of the test.
                    // We cannot assert much here.
                    assert!(entries.len() > 0);
                }
                "/exposed" | "/ns" | "/out" | "/runtime" | "/tools" => {
                    // These directories must contain a file with the same name
                    let expected_file_name = entry.path[1..].to_string();
                    assert_eq!(
                        entries,
                        vec![fuchsia_fs::directory::DirEntry {
                            name: expected_file_name,
                            kind: fuchsia_fs::directory::DirentKind::File
                        }]
                    )
                }
                "/svc" => {
                    assert_eq!(
                        entries,
                        vec![fuchsia_fs::directory::DirEntry {
                            name: "fuchsia.process.Launcher".to_string(),
                            kind: fuchsia_fs::directory::DirentKind::Service
                        }]
                    )
                }
                path => panic!("Unexpected namespace entry: {}", path),
            }
        }
    }

    #[fuchsia::test]
    async fn check_namespace_started() {
        let instance_info = fsys::InstanceInfo {
            moniker: ".".to_string(),
            url: "test://foo.com#meta/bar.cm".to_string(),
            instance_id: None,
            state: fsys::InstanceState::Started,
        };

        let exposed_dir = create_temp_dir("exposed");
        let ns_dir = create_temp_dir("ns");
        let out_dir = create_temp_dir("out");
        let runtime_dir = create_temp_dir("runtime");

        let started = Some(Box::new(fsys::StartedState {
            out_dir: Some(out_dir),
            runtime_dir: Some(runtime_dir),
            start_reason: "Debug".to_string(),
        }));

        let resolved = Some(Box::new(fsys::ResolvedState {
            uses: vec![],
            exposes: vec![],
            config: None,
            pkg_dir: None,
            started,
            exposed_dir,
            ns_dir,
        }));

        let query = serve_realm_query(instance_info, resolved);

        let instance_scope = InstanceScope::new(&query, ".").await.unwrap();
        let ns = create_dash_namespace(instance_scope.ns, None).unwrap();

        assert_eq!(ns.len(), 6);

        // Make sure that the correct directories were mapped to the correct paths.
        for entry in ns {
            let dir = entry.directory.into_proxy().unwrap();
            let entries = fuchsia_fs::directory::readdir(&dir).await.unwrap();
            match entry.path.as_str() {
                "/bin" => {
                    // The contents of this directory are /pkg/bin of the test.
                    // We cannot assert much here.
                    assert!(entries.len() > 0);
                }
                "/exposed" | "/ns" | "/out" | "/runtime" => {
                    // These directories must contain a file with the same name
                    let expected_file_name = entry.path[1..].to_string();
                    assert_eq!(
                        entries,
                        vec![fuchsia_fs::directory::DirEntry {
                            name: expected_file_name,
                            kind: fuchsia_fs::directory::DirentKind::File
                        }]
                    )
                }
                "/svc" => {
                    assert_eq!(
                        entries,
                        vec![fuchsia_fs::directory::DirEntry {
                            name: "fuchsia.process.Launcher".to_string(),
                            kind: fuchsia_fs::directory::DirentKind::Service
                        }]
                    )
                }
                path => panic!("Unexpected namespace entry: {}", path),
            }
        }
    }

    #[fuchsia::test]
    async fn check_namespace_resolved() {
        let instance_info = fsys::InstanceInfo {
            moniker: ".".to_string(),
            url: "test://foo.com#meta/bar.cm".to_string(),
            instance_id: None,
            state: fsys::InstanceState::Resolved,
        };

        let exposed_dir = create_temp_dir("exposed");
        let ns_dir = create_temp_dir("ns");

        let resolved = Some(Box::new(fsys::ResolvedState {
            uses: vec![],
            exposes: vec![],
            config: None,
            pkg_dir: None,
            started: None,
            exposed_dir,
            ns_dir,
        }));

        let query = serve_realm_query(instance_info, resolved);

        let instance_scope = InstanceScope::new(&query, ".").await.unwrap();
        let ns = create_dash_namespace(instance_scope.ns, None).unwrap();

        assert_eq!(ns.len(), 4);

        // Make sure that the correct directories were mapped to the correct paths.
        for entry in ns {
            let dir = entry.directory.into_proxy().unwrap();
            let entries = fuchsia_fs::directory::readdir(&dir).await.unwrap();
            match entry.path.as_str() {
                "/bin" => {
                    // The contents of this directory are /pkg/bin of the test.
                    // We cannot assert much here.
                    assert!(entries.len() > 0);
                }
                "/exposed" | "/ns" => {
                    // These directories must contain a file with the same name
                    let expected_file_name = entry.path[1..].to_string();
                    assert_eq!(
                        entries,
                        vec![fuchsia_fs::directory::DirEntry {
                            name: expected_file_name,
                            kind: fuchsia_fs::directory::DirentKind::File
                        }]
                    )
                }
                "/svc" => {
                    assert_eq!(
                        entries,
                        vec![fuchsia_fs::directory::DirEntry {
                            name: "fuchsia.process.Launcher".to_string(),
                            kind: fuchsia_fs::directory::DirentKind::Service
                        }]
                    )
                }
                path => panic!("Unexpected namespace entry: {}", path),
            }
        }
    }

    #[fuchsia::test]
    async fn check_namespace_unresolved() {
        let instance_info = fsys::InstanceInfo {
            moniker: ".".to_string(),
            url: "test://foo.com#meta/bar.cm".to_string(),
            instance_id: None,
            state: fsys::InstanceState::Unresolved,
        };

        let resolved = None;
        let query = serve_realm_query(instance_info, resolved);

        let err = InstanceScope::new(&query, ".").await.unwrap_err();
        assert_eq!(err, LauncherError::InstanceNotResolved);
    }
}
