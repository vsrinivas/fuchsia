// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io::{self as fio, DirectoryMarker, DirectoryProxy};
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::{ChildOptions, ChildRef, RealmBuilder};
use futures::prelude::*;
use std::fs;
use std::sync::Arc;
use vfs::test_utils::assertions::reexport::StreamExt;
use vfs::{directory::entry::DirectoryEntry, file::vmo::read_only_static, pseudo_directory};

pub(crate) async fn create_config_data(builder: &RealmBuilder) -> Result<ChildRef, Error> {
    let config_data_dir = pseudo_directory! {
        "data" => pseudo_directory! {
            "test_config.persist" =>
                read_only_static(include_str!("test_data/config/test_config.persist")),
        }
    };

    // Add a mock component that provides the `config-data` directory to the realm.
    Ok(builder
        .add_local_child(
            "config-data-server",
            move |handles| {
                let proxy = spawn_vfs(config_data_dir.clone());
                async move {
                    let mut fs = ServiceFs::new();
                    fs.add_remote("config", proxy);
                    fs.serve_connection(handles.outgoing_dir.into_channel())
                        .expect("failed to serve config-data ServiceFs");
                    fs.collect::<()>().await;
                    Ok::<(), anyhow::Error>(())
                }
                .boxed()
            },
            ChildOptions::new(),
        )
        .await?)
}

/// Create a /cache directory under /tmp so we can serve it as a directory to supply a "/cache"
/// directory to persistence in create_cache_server().
pub(crate) fn setup_backing_directories() {
    let path = "/tmp/cache";
    fs::create_dir_all(path)
        .map_err(|err| tracing::warn!(%path, ?err, "Could not create directory"))
        .ok();
}

// Returns a `DirectoryProxy` that serves the directory entry `dir`.
fn spawn_vfs(dir: Arc<dyn DirectoryEntry>) -> DirectoryProxy {
    let (client_end, server_end) = fidl::endpoints::create_endpoints::<DirectoryMarker>().unwrap();
    let scope = vfs::execution_scope::ExecutionScope::new();
    dir.open(
        scope,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(server_end.into_channel()),
    );
    client_end.into_proxy().unwrap()
}

pub(crate) async fn create_cache_server(builder: &RealmBuilder) -> Result<ChildRef, Error> {
    // Add a mock component that provides the `cache` directory to the realm.
    //let cache_server =
    Ok(builder
        .add_local_child(
            "cache-server",
            move |handles| {
                let cache_dir_proxy = fuchsia_fs::directory::open_in_namespace(
                    "/tmp/cache",
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                )
                .unwrap();
                async move {
                    let mut fs = ServiceFs::new();
                    fs.add_remote("cache", cache_dir_proxy);
                    fs.serve_connection(handles.outgoing_dir.into_channel())
                        .expect("failed to serve cache ServiceFs");
                    fs.collect::<()>().await;
                    Ok::<(), anyhow::Error>(())
                }
                .boxed()
            },
            ChildOptions::new(),
        )
        .await?)
}
