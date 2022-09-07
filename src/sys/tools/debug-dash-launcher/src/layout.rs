// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::DiscoverableProtocolMarker;
use fidl::endpoints::Proxy;
use fidl_fuchsia_component_runner as fcrunner;
use fidl_fuchsia_dash::LauncherError;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_process as fproc;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use tracing::warn;
use vfs::{
    directory::{entry::DirectoryEntry, helper::DirectlyMutable},
    execution_scope::ExecutionScope,
    path::Path,
    service::endpoint,
};

// PATH must contain the three possible sources of binaries:
// * binaries in the tools package.
// * binaries in the instance's package.
// * binaries in dash-launcher's package.
// Note that dash can handle paths that do not exist.
// For the nested root layout, the instance's package is under /ns/pkg.
const PATH_ENVVAR_NESTED_ROOT: &str = "PATH=/.dash/tools/bin:/ns/pkg/bin:/.dash/bin:";

// PATH must contain the three possible sources of binaries:
// * binaries in the tools package.
// * binaries in the instance's package.
// * binaries in dash-launcher's package.
// Note that dash can handle paths that do not exist.
// For the namespace root layout, the instance's package is under /pkg.
const PATH_ENVVAR_NS_ROOT: &str = "PATH=/.dash/tools/bin:/pkg/bin:/.dash/bin:";

/// Returns directory handles + paths for a nested layout using the given directories.
/// In this layout, all instance directories are nested as subdirectories of the root.
/// e.g - namespace is under `/ns`, outgoing directory is under `/out`, etc.
///
/// Also returns the corresponding PATH envvar that must be set for the dash shell.
pub fn nest_all_instance_dirs(
    bin_dir: fio::DirectoryProxy,
    ns_entries: Vec<fcrunner::ComponentNamespaceEntry>,
    exposed_dir: fio::DirectoryProxy,
    svc_dir: fio::DirectoryProxy,
    out_dir: Option<fio::DirectoryProxy>,
    runtime_dir: Option<fio::DirectoryProxy>,
    tools_pkg_dir: Option<fio::DirectoryProxy>,
) -> (Vec<fproc::NameInfo>, &'static str) {
    let mut name_infos = vec![];
    name_infos.push(to_name_info("/.dash/bin", bin_dir));
    name_infos.push(to_name_info("/exposed", exposed_dir));
    name_infos.push(to_name_info("/svc", svc_dir));

    for entry in ns_entries {
        let path = format!("/ns{}", entry.path.unwrap());
        let directory = entry.directory.unwrap();
        name_infos.push(fproc::NameInfo { path, directory });
    }

    if let Some(dir) = out_dir {
        name_infos.push(to_name_info("/out", dir));
    }

    if let Some(dir) = runtime_dir {
        name_infos.push(to_name_info("/runtime", dir));
    }

    if let Some(dir) = tools_pkg_dir {
        name_infos.push(to_name_info("/.dash/tools", dir));
    }

    (name_infos, PATH_ENVVAR_NESTED_ROOT)
}

/// Returns directory handles + paths for a namespace layout using the given directories.
/// In this layout, the instance namespace is the root. This is a layout that works
/// well for tools and closely resembles what the component instance would see if it queried its
/// own namespace.
///
/// To make the shell work correctly, we need to inject the following into the layout:
/// * fuchsia.process.Launcher into `/svc`
/// * dash launcher binaries into `/.dash/bin` and tools package into `/.dash/tools`
///
/// Also returns the corresponding PATH envvar that must be set for the dash shell.
pub async fn instance_namespace_is_root(
    bin_dir: fio::DirectoryProxy,
    ns_entries: Vec<fcrunner::ComponentNamespaceEntry>,
    tools_pkg_dir: Option<fio::DirectoryProxy>,
) -> (Vec<fproc::NameInfo>, &'static str) {
    let mut name_infos = vec![];
    name_infos.push(to_name_info("/.dash/bin", bin_dir));

    if let Some(dir) = tools_pkg_dir {
        name_infos.push(to_name_info("/.dash/tools", dir));
    }

    for entry in ns_entries {
        let path = entry.path.unwrap();
        let directory = entry.directory.unwrap();

        if path == "/svc" {
            let svc_dir = directory.into_proxy().unwrap();
            let svc_dir = inject_process_launcher(svc_dir).await;
            name_infos.push(to_name_info(&path, svc_dir));
        } else {
            name_infos.push(fproc::NameInfo { path, directory });
        }
    }

    (name_infos, PATH_ENVVAR_NS_ROOT)
}

/// Serves a VFS that only contains `fuchsia.process.Launcher`. This is used by the Nested
/// filesystem layout.
pub fn serve_process_launcher_svc_dir() -> Result<fio::DirectoryProxy, LauncherError> {
    // Serve a directory that only provides fuchsia.process.Launcher to dash.
    let (svc_dir, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .map_err(|_| LauncherError::Internal)?;

    let mut fs = ServiceFs::new();
    fs.add_proxy_service::<fproc::LauncherMarker, ()>();
    fs.serve_connection(server_end).map_err(|_| LauncherError::Internal)?;

    fasync::Task::spawn(async move {
        fs.collect::<()>().await;
    })
    .detach();

    Ok(svc_dir)
}

/// Gets the list of all protocols available in the given svc directory and serves a new VFS
/// that injects `fuchsia.process.Launcher` from the launcher namespace and forwards the calls
/// for the other protocols.
///
/// If the svc directory already contains `fuchsia.process.Launcher`, we will not inject it here.
async fn inject_process_launcher(svc_dir: fio::DirectoryProxy) -> fio::DirectoryProxy {
    let vfs = vfs::directory::immutable::simple();
    let entries = fuchsia_fs::directory::readdir(&svc_dir).await.unwrap();

    // Create forwarding entries for namespace protocols
    for entry in entries {
        let svc_dir = std::clone::Clone::clone(&svc_dir);
        let protocol_name = entry.name.clone();
        vfs.add_entry(
            protocol_name.clone(),
            endpoint(move |_, channel| {
                let server_end = channel.into_zx_channel().into();
                svc_dir
                    .open(
                        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                        fio::MODE_TYPE_SERVICE,
                        &protocol_name,
                        server_end,
                    )
                    .unwrap();
            }),
        )
        .unwrap();
    }

    if let Err(err) = vfs.add_entry(
        fproc::LauncherMarker::PROTOCOL_NAME,
        endpoint(|_, channel| {
            connect_channel_to_protocol::<fproc::LauncherMarker>(channel.into_zx_channel())
                .unwrap();
        }),
    ) {
        warn!(?err, "Could not inject fuchsia.process.Launcher into filesystem layout. Ignoring.");
    }

    let (svc_dir, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let server_end = server_end.into_channel().into();
    vfs.open(
        ExecutionScope::new(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        0,
        Path::dot(),
        server_end,
    );

    svc_dir
}

fn to_name_info(path: &str, directory: fio::DirectoryProxy) -> fproc::NameInfo {
    let directory = directory.into_channel().unwrap().into_zx_channel().into();
    fproc::NameInfo { path: path.to_string(), directory }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;
    use tempfile::TempDir;

    fn create_temp_dir(file_name: &str) -> fio::DirectoryProxy {
        // Create a temp directory and put a file with name `file_name` inside it.
        let temp_dir = TempDir::new().unwrap();
        let temp_dir_path = temp_dir.into_path();
        let file_path = temp_dir_path.join(file_name);
        std::fs::write(&file_path, "Hippos Rule!").unwrap();
        let temp_dir_path = temp_dir_path.display().to_string();
        fuchsia_fs::directory::open_in_namespace(&temp_dir_path, fio::OpenFlags::RIGHT_READABLE)
            .unwrap()
    }

    fn create_ns_entries() -> Vec<fcrunner::ComponentNamespaceEntry> {
        let ns_subdir = create_temp_dir("ns_subdir");
        let ns_subdir = ns_subdir.into_channel().unwrap().into_zx_channel().into();
        vec![fcrunner::ComponentNamespaceEntry {
            path: Some("/ns_subdir".to_string()),
            directory: Some(ns_subdir),
            ..fcrunner::ComponentNamespaceEntry::EMPTY
        }]
    }

    #[fuchsia::test]
    async fn nest_all_instance_dirs_started_with_tools() {
        let bin_dir = create_temp_dir("bin");
        let tools_dir = create_temp_dir("tools");
        let exposed_dir = create_temp_dir("exposed");
        let out_dir = create_temp_dir("out");
        let svc_dir = create_temp_dir("svc");
        let runtime_dir = create_temp_dir("runtime");
        let ns_entries = create_ns_entries();

        let (ns, _) = nest_all_instance_dirs(
            bin_dir,
            ns_entries,
            exposed_dir,
            svc_dir,
            Some(out_dir),
            Some(runtime_dir),
            Some(tools_dir),
        );
        assert_eq!(ns.len(), 7);

        let mut paths: Vec<String> = ns.iter().map(|n| n.path.clone()).collect();
        paths.sort();
        assert_eq!(
            paths,
            vec![
                "/.dash/bin",
                "/.dash/tools",
                "/exposed",
                "/ns/ns_subdir",
                "/out",
                "/runtime",
                "/svc"
            ]
        );

        // Make sure that the correct directories were mapped to the correct paths.
        for entry in ns {
            let dir = entry.directory.into_proxy().unwrap();
            let entries = fuchsia_fs::directory::readdir(&dir).await.unwrap();

            // These directories must contain a file with the same name
            let path = PathBuf::from(entry.path);
            let expected_file_name = path.file_name().unwrap().to_str().unwrap().to_string();
            assert_eq!(
                entries,
                vec![fuchsia_fs::directory::DirEntry {
                    name: expected_file_name,
                    kind: fuchsia_fs::directory::DirentKind::File
                }]
            );
        }
    }

    #[fuchsia::test]
    async fn nest_all_instance_dirs_started() {
        let bin_dir = create_temp_dir("bin");
        let exposed_dir = create_temp_dir("exposed");
        let out_dir = create_temp_dir("out");
        let svc_dir = create_temp_dir("svc");
        let runtime_dir = create_temp_dir("runtime");
        let ns_entries = create_ns_entries();

        let (ns, _) = nest_all_instance_dirs(
            bin_dir,
            ns_entries,
            exposed_dir,
            svc_dir,
            Some(out_dir),
            Some(runtime_dir),
            None,
        );
        assert_eq!(ns.len(), 6);

        let mut paths: Vec<String> = ns.iter().map(|n| n.path.clone()).collect();
        paths.sort();
        assert_eq!(
            paths,
            vec!["/.dash/bin", "/exposed", "/ns/ns_subdir", "/out", "/runtime", "/svc"]
        );

        // Make sure that the correct directories were mapped to the correct paths.
        for entry in ns {
            let dir = entry.directory.into_proxy().unwrap();
            let entries = fuchsia_fs::directory::readdir(&dir).await.unwrap();

            // These directories must contain a file with the same name
            let path = PathBuf::from(entry.path);
            let expected_file_name = path.file_name().unwrap().to_str().unwrap().to_string();
            assert_eq!(
                entries,
                vec![fuchsia_fs::directory::DirEntry {
                    name: expected_file_name,
                    kind: fuchsia_fs::directory::DirentKind::File
                }]
            );
        }
    }

    #[fuchsia::test]
    async fn nest_all_instance_dirs_resolved() {
        let bin_dir = create_temp_dir("bin");
        let exposed_dir = create_temp_dir("exposed");
        let svc_dir = create_temp_dir("svc");
        let ns_entries = create_ns_entries();

        let (ns, _) =
            nest_all_instance_dirs(bin_dir, ns_entries, exposed_dir, svc_dir, None, None, None);
        assert_eq!(ns.len(), 4);

        let mut paths: Vec<String> = ns.iter().map(|n| n.path.clone()).collect();
        paths.sort();
        assert_eq!(paths, vec!["/.dash/bin", "/exposed", "/ns/ns_subdir", "/svc"]);

        // Make sure that the correct directories were mapped to the correct paths.
        for entry in ns {
            let dir = entry.directory.into_proxy().unwrap();
            let entries = fuchsia_fs::directory::readdir(&dir).await.unwrap();

            // These directories must contain a file with the same name
            let path = PathBuf::from(entry.path);
            let expected_file_name = path.file_name().unwrap().to_str().unwrap().to_string();
            assert_eq!(
                entries,
                vec![fuchsia_fs::directory::DirEntry {
                    name: expected_file_name,
                    kind: fuchsia_fs::directory::DirentKind::File
                }]
            );
        }
    }

    #[fuchsia::test]
    async fn instance_namespace_is_root_resolved() {
        let bin_dir = create_temp_dir("bin");
        let ns_entries = create_ns_entries();

        let (ns, _) = instance_namespace_is_root(bin_dir, ns_entries, None).await;

        let mut paths: Vec<String> = ns.iter().map(|n| n.path.clone()).collect();
        paths.sort();
        assert_eq!(paths, vec!["/.dash/bin", "/ns_subdir"]);

        // Make sure that the correct directories were mapped to the correct paths.
        for entry in ns {
            let dir = entry.directory.into_proxy().unwrap();
            let entries = fuchsia_fs::directory::readdir(&dir).await.unwrap();

            // These directories must contain a file with the same name
            let path = PathBuf::from(entry.path);
            let expected_file_name = path.file_name().unwrap().to_str().unwrap().to_string();
            assert_eq!(
                entries,
                vec![fuchsia_fs::directory::DirEntry {
                    name: expected_file_name,
                    kind: fuchsia_fs::directory::DirentKind::File
                }]
            );
        }
    }
}
