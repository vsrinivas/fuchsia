// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::environment::FshostEnvironment,
    anyhow::{format_err, Error, Result},
    fidl::prelude::*,
    fidl_fuchsia_fshost as fshost,
    fidl_fuchsia_fxfs::{CryptManagementMarker, KeyPurpose},
    fidl_fuchsia_io as fio,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_runtime::{take_startup_handle, HandleType},
    fuchsia_zircon as zx,
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
    let (watcher, device_stream) = watcher::Watcher::new().await?;

    let mut env = FshostEnvironment::new();
    let export = vfs::pseudo_directory! {
        "svc" => vfs::pseudo_directory! {
            fshost::AdminMarker::PROTOCOL_NAME => service::fshost_admin(shutdown_tx.clone()),
            fshost::BlockWatcherMarker::PROTOCOL_NAME =>
                service::fshost_block_watcher(watcher),
        },
        "fs" => vfs::pseudo_directory! {
            "blob" => remote_dir(env.blobfs_root()?),
            "data" => remote_dir(env.data_root()?),
        },
    };

    let _ = service::handle_lifecycle_requests(shutdown_tx);

    init_crypt_service().await?;

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
    let shutdown_responder = fs_manager.device_handler(device_stream).await?;

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

async fn init_crypt_service() -> Result<(), Error> {
    let crypt_management = connect_to_protocol::<CryptManagementMarker>()?;
    // TODO(fxbug.dev/94587): A hardware source should be used for keys.
    crypt_management
        .add_wrapping_key(
            0,
            &[
                0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
                0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
                0x1e, 0x1f,
            ],
        )
        .await?
        .map_err(zx::Status::from_raw)?;
    crypt_management
        .add_wrapping_key(
            1,
            &[
                0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2,
                0xf1, 0xf0, 0xef, 0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8, 0xe7, 0xe6, 0xe5, 0xe4,
                0xe3, 0xe2, 0xe1, 0xe0,
            ],
        )
        .await?
        .map_err(zx::Status::from_raw)?;
    crypt_management.set_active_key(KeyPurpose::Data, 0).await?.map_err(zx::Status::from_raw)?;
    crypt_management
        .set_active_key(KeyPurpose::Metadata, 1)
        .await?
        .map_err(zx::Status::from_raw)?;
    Ok(())
}
