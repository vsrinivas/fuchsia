// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use fidl_fuchsia_io::DirectoryProxy;
use fuchsia_async::futures::TryStreamExt;

async fn wait_for_file(dir: &DirectoryProxy, name: &str) -> Result<(), anyhow::Error> {
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(io_util::clone_directory(
        dir,
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
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
    initial_dir: &DirectoryProxy,
    name: &str,
    flags: u32,
    mode: u32,
) -> Result<fidl_fuchsia_io::NodeProxy, anyhow::Error> {
    let mut dir = io_util::clone_directory(initial_dir, flags)?;
    let path = std::path::Path::new(name);
    let components = path.components().collect::<Vec<_>>();
    for i in 0..(components.len() - 1) {
        let component = &components[i];
        match component {
            std::path::Component::Normal(file) => {
                wait_for_file(&dir, file.to_str().unwrap()).await?;
                dir = io_util::open_directory(&dir, std::path::Path::new(file), flags)?;
            }
            _ => panic!("Path must contain only normal components"),
        }
    }
    match components[components.len() - 1] {
        std::path::Component::Normal(file) => {
            wait_for_file(&dir, file.to_str().unwrap()).await?;
            io_util::open_node(&dir, std::path::Path::new(file), flags, mode)
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
    initial_dir: &DirectoryProxy,
    name: &str,
) -> Result<fidl_fuchsia_io::NodeProxy, anyhow::Error> {
    recursive_wait_and_open_node_with_flags(
        initial_dir,
        name,
        fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        fidl_fuchsia_io::MODE_TYPE_SERVICE,
    )
    .await
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use vfs::directory::entry::DirectoryEntry;

    #[fasync::run_singlethreaded(test)]
    async fn open_two_directories() {
        let (client, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();

        let fs_scope = vfs::execution_scope::ExecutionScope::new();
        let root = vfs::pseudo_directory! {
            "test" => vfs::pseudo_directory! {
                "dir" => vfs::pseudo_directory! {},
            },
        };
        root.open(
            fs_scope.clone(),
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_EXECUTABLE,
            0,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::new(server.into_channel()),
        );
        fasync::Task::spawn(async move { fs_scope.wait().await }).detach();

        recursive_wait_and_open_node_with_flags(
            &client,
            "test/dir",
            io_util::OPEN_RIGHT_READABLE,
            0,
        )
        .await
        .unwrap();
    }
}
