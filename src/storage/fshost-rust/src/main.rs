// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        boot_args::BootArgs, config::apply_boot_args_to_config, environment::FshostEnvironment,
    },
    anyhow::{format_err, Context, Result},
    fidl::prelude::*,
    fidl_fuchsia_fshost as fshost, fidl_fuchsia_io as fio,
    fuchsia_runtime::{take_startup_handle, HandleType},
    fuchsia_zircon::sys::zx_debug_write,
    futures::channel::mpsc,
    std::sync::Arc,
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
        remote::remote_dir,
    },
};

mod boot_args;
mod config;
mod crypt;
mod device;
mod environment;
mod manager;
mod matcher;
mod service;
mod volume;
mod watcher;

#[fuchsia::main]
async fn main() -> Result<()> {
    let boot_args = BootArgs::new().await.context("Failed to create boot_args")?;
    let mut config = fshost_config::Config::take_from_startup_handle();
    apply_boot_args_to_config(&mut config, &boot_args);
    let config = Arc::new(config);
    // NB There are tests that look for "fshost started".
    tracing::info!(?config, "fshost started");

    let directory_request =
        take_startup_handle(HandleType::DirectoryRequest.into()).ok_or_else(|| {
            format_err!("missing DirectoryRequest startup handle - not launched as a component?")
        })?;

    let (shutdown_tx, shutdown_rx) = mpsc::channel::<service::FshostShutdownResponder>(1);
    let (watcher, device_stream) = watcher::Watcher::new().await?;

    let mut env = FshostEnvironment::new(&config, &boot_args);
    let export = vfs::pseudo_directory! {
        "svc" => vfs::pseudo_directory! {
            fshost::AdminMarker::PROTOCOL_NAME => service::fshost_admin(&config),
            fshost::BlockWatcherMarker::PROTOCOL_NAME =>
                service::fshost_block_watcher(watcher),
        },
        "fs" => vfs::pseudo_directory! {
            "blob" => remote_dir(env.blobfs_root()?),
            "data" => remote_dir(env.data_root()?),
        },
    };

    let _ = service::handle_lifecycle_requests(shutdown_tx)?;

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
    let mut fs_manager = manager::Manager::new(shutdown_rx, &config, env);
    let shutdown_responder = fs_manager.device_handler(device_stream).await?;

    tracing::info!("shutdown signal received");

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

    // NB There are tests that look for this specific log message.  We write directly to serial
    // because writing via syslog has been found to not reliably make it to serial before shutdown
    // occurs.
    let data = b"fshost shutdown complete\n";
    unsafe {
        zx_debug_write(data.as_ptr(), data.len());
    }

    // 3. Notify whoever asked for a shutdown that it's complete. After this point, it's possible
    //    the fshost process will be terminated externally.
    shutdown_responder.close()?;

    Ok(())
}
