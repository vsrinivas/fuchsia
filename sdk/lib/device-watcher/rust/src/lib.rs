// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_io as fio,
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    futures::stream::{Stream, TryStreamExt},
    std::path::{Path, PathBuf},
};

/// Waits for a device to appear within `dev_dir` that has a topological path
/// that matches `topo_path`. Returns the path of the found device within
/// `dev_dir`. If no topological path match is found this function will wait
/// forever.
pub async fn wait_for_device_topo_path(
    dev_dir: &fio::DirectoryProxy,
    topo_path: &str,
) -> Result<String, anyhow::Error> {
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(fuchsia_fs::clone_directory(
        dev_dir,
        fio::OpenFlags::RIGHT_READABLE,
    )?)
    .await?;

    while let Some(msg) = watcher.try_next().await? {
        if msg.event != fuchsia_vfs_watcher::WatchEvent::EXISTING
            && msg.event != fuchsia_vfs_watcher::WatchEvent::ADD_FILE
        {
            continue;
        }
        if msg.filename == Path::new(".") {
            continue;
        }

        let filename = msg.filename.to_str().ok_or(format_err!("to_str for filename failed"))?;

        let (controller_proxy, server_end) = fidl::endpoints::create_proxy::<ControllerMarker>()?;
        dev_dir
            .open(
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                0,
                filename,
                server_end.into_channel().into(),
            )
            .with_context(|| format!("Failed to open \"{}\"", filename))?;

        if let Ok(Ok(path)) = controller_proxy.get_topological_path().await {
            if path == topo_path {
                return Ok(filename.to_string());
            }
        }
    }
    unreachable!();
}

/// Returns a stream that contains the paths of any existing files and
/// directories in `dir` and any new files or directories created after this
/// function was invoked. These paths are relative to `dir`.
pub async fn watch_for_files(
    dir: &fio::DirectoryProxy,
) -> Result<impl Stream<Item = Result<PathBuf>>> {
    let watcher = Watcher::new(fuchsia_fs::clone_directory(dir, fio::OpenFlags::RIGHT_READABLE)?)
        .await
        .context("Failed to create watcher")?;
    Ok(watcher
        .map_err(|err| anyhow::anyhow!("Failed to get watcher event: {}", err))
        .try_filter_map(|msg| async move {
            match msg.event {
                WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                    if msg.filename == Path::new(".") {
                        return Ok(None);
                    }
                    Ok(Some(msg.filename))
                }
                _ => Ok(None),
            }
        }))
}

async fn wait_for_file(dir: &fio::DirectoryProxy, name: &str) -> Result<()> {
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(fuchsia_fs::clone_directory(
        dir,
        fio::OpenFlags::RIGHT_READABLE,
    )?)
    .await?;
    while let Some(msg) = watcher.try_next().await? {
        if msg.event != fuchsia_vfs_watcher::WatchEvent::EXISTING
            && msg.event != fuchsia_vfs_watcher::WatchEvent::ADD_FILE
        {
            continue;
        }
        if msg.filename.to_str().unwrap() == name {
            return Ok(());
        }
    }
    unreachable!();
}

/// Open the path `name` within `initial_dir`. This function waits for each
/// directory to be available before it opens it. If the path never appears
/// this function will wait forever.
pub async fn recursive_wait_and_open_node_with_flags(
    initial_dir: &fio::DirectoryProxy,
    name: &str,
    flags: fio::OpenFlags,
    mode: u32,
) -> Result<fio::NodeProxy> {
    let mut dir = fuchsia_fs::clone_directory(initial_dir, flags)?;
    let path = std::path::Path::new(name);
    let components = path.components().collect::<Vec<_>>();
    for i in 0..(components.len() - 1) {
        let component = &components[i];
        match component {
            std::path::Component::Normal(file) => {
                wait_for_file(&dir, file.to_str().unwrap()).await?;
                dir = fuchsia_fs::open_directory(&dir, std::path::Path::new(file), flags)?;
            }
            _ => panic!("Path must contain only normal components"),
        }
    }
    match components[components.len() - 1] {
        std::path::Component::Normal(file) => {
            wait_for_file(&dir, file.to_str().unwrap()).await?;
            fuchsia_fs::open_node(&dir, std::path::Path::new(file), flags, mode)
        }
        _ => panic!("Path must contain only normal components"),
    }
}

/// Open the path `name` within `initial_dir`. This function waits for each
/// directory to be available before it opens it. If the path never appears
/// this function will wait forever.
/// This function opens node as a RW service. This has the correct permissions
/// to connect to a driver's API.
pub async fn recursive_wait_and_open_node(
    initial_dir: &fio::DirectoryProxy,
    name: &str,
) -> Result<fio::NodeProxy> {
    recursive_wait_and_open_node_with_flags(
        initial_dir,
        name,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        fio::MODE_TYPE_SERVICE,
    )
    .await
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_device as fdev, fuchsia_async as fasync,
        futures::StreamExt,
        std::{collections::HashSet, str::FromStr, sync::Arc},
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::read_only_static,
        },
    };

    fn create_controller_service(topo_path: String) -> Arc<vfs::service::Service> {
        vfs::service::host(move |mut stream: fdev::ControllerRequestStream| {
            let topo_path = topo_path.clone();
            async move {
                match stream.try_next().await.unwrap() {
                    Some(fdev::ControllerRequest::GetTopologicalPath { responder }) => {
                        let _ = responder.send(&mut Ok(topo_path));
                    }
                    e => panic!("Unexpected request: {:?}", e),
                }
            }
        })
    }

    #[fasync::run_singlethreaded(test)]
    async fn wait_for_device_by_topological_path() {
        let dir = vfs::pseudo_directory! {
          "a" => create_controller_service("/dev/test0/a/dev".to_string()),
          "1" => create_controller_service("/dev/test1/1/dev".to_string()),
          "x" => create_controller_service("/dev/test2/x/dev".to_string()),
          "y" => create_controller_service("/dev/test3/y/dev".to_string()),
        };

        let (dir_proxy, remote) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::new(remote.into_channel()),
        );

        let path = wait_for_device_topo_path(&dir_proxy, "/dev/test2/x/dev").await.unwrap();
        assert_eq!("x", path);
    }

    #[fasync::run_singlethreaded(test)]
    async fn watch_for_two_files() {
        let dir = vfs::pseudo_directory! {
          "a" => read_only_static(b"/a"),
          "b" => read_only_static(b"/b"),
        };

        let (dir_proxy, remote) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::new(remote.into_channel()),
        );

        let stream = watch_for_files(&dir_proxy).await.unwrap();
        futures::pin_mut!(stream);
        let actual: HashSet<PathBuf> =
            vec![stream.next().await.unwrap().unwrap(), stream.next().await.unwrap().unwrap()]
                .into_iter()
                .collect();
        let expected: HashSet<PathBuf> =
            vec![PathBuf::from_str("a").unwrap(), PathBuf::from_str("b").unwrap()]
                .into_iter()
                .collect();
        assert_eq!(actual, expected);
    }

    #[fasync::run_singlethreaded(test)]
    async fn wait_for_device_topo_path_allows_files_and_dirs() {
        let dir = vfs::pseudo_directory! {
          "1" => vfs::pseudo_directory! {
            "test" => read_only_static("test file 1"),
            "test2" => read_only_static("test file 2"),
          },
          "2" => read_only_static("file 2"),
          "x" => create_controller_service("/dev/test2/x/dev".to_string()),
          "3" => read_only_static("file 3"),
        };

        let (dir_proxy, remote) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::new(remote.into_channel()),
        );

        let path = wait_for_device_topo_path(&dir_proxy, "/dev/test2/x/dev").await.unwrap();
        assert_eq!("x", path);
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_two_directories() {
        let (client, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

        let fs_scope = vfs::execution_scope::ExecutionScope::new();
        let root = vfs::pseudo_directory! {
            "test" => vfs::pseudo_directory! {
                "dir" => vfs::pseudo_directory! {},
            },
        };
        root.open(
            fs_scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            0,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::new(server.into_channel()),
        );
        fasync::Task::spawn(async move { fs_scope.wait().await }).detach();

        recursive_wait_and_open_node_with_flags(
            &client,
            "test/dir",
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
            0,
        )
        .await
        .unwrap();
    }
}
