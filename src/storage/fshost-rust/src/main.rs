// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::environment::FshostEnvironment,
    anyhow::{format_err, Result},
    fidl::prelude::*,
    fidl_fuchsia_fshost as fshost, fidl_fuchsia_io as fio,
    fuchsia_runtime::{take_startup_handle, HandleType},
    futures::channel::mpsc,
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
        remote::remote_dir,
    },
};

mod config;
mod device;
mod environment;
mod manager;
mod matcher;
mod service;
mod watcher;

#[fuchsia::main]
async fn main() -> Result<()> {
    log::info!("fshost started");

    let directory_request =
        take_startup_handle(HandleType::DirectoryRequest.into()).ok_or_else(|| {
            format_err!("missing DirectoryRequest startup handle - not launched as a component?")
        })?;

    let (shutdown_tx, shutdown_rx) = mpsc::channel::<service::FshostShutdownResponder>(1);

    let mut env = FshostEnvironment::new();
    let export = vfs::pseudo_directory! {
        "svc" => vfs::pseudo_directory! {
            fshost::AdminMarker::PROTOCOL_NAME => service::fshost_admin(shutdown_tx),
        },
        "blobfs" => remote_dir(env.blobfs_root()?),
    };

    let scope = ExecutionScope::new();
    export.open(
        scope.clone(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        fio::MODE_TYPE_DIRECTORY,
        Path::dot(),
        directory_request.into(),
    );

    // Run the main loop of fshost, handling devices as they appear according to our filesystem
    // policy.
    let mut fs_manager =
        manager::Manager::new(shutdown_rx, fshost_config::Config::take_from_startup_handle(), env);
    let shutdown_responder = fs_manager.device_handler().await?;

    log::info!("shutdown signal received");

    // Shutting down fshost involves sending asynchronous shutdown signals to several different
    // systems in order. If at any point we hit an error, we log loudly, but continue with the
    // shutdown procedure.

    // 0. Before fshost is told to shut down, almost everything that is running out of the
    //    filesystems is shut down by component manager.

    // 1. Shut down the scope for the export directory. This hosts the fshost services. This
    //    prevents additional connections to fshost services from being created.
    scope.shutdown();

    // 2. Shut down all the filesystems we started.
    fs_manager.shutdown().await?;

    // 3. Notify whoever asked for a shutdown that it's complete. After this point, it's possible
    //    the fshost process will be terminated externally.
    shutdown_responder.close()?;

    log::info!("fshost terminated");
    Ok(())
}
