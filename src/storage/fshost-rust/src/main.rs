// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_fshost as fshost, fidl_fuchsia_io as fio,
    fuchsia_runtime::{take_startup_handle, HandleType},
    futures::{channel::mpsc, StreamExt},
    vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path},
};

mod service;

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    log::info!("fshost started");

    let directory_request =
        take_startup_handle(HandleType::DirectoryRequest.into()).ok_or_else(|| {
            format_err!("missing DirectoryRequest startup handle - not launched as a component?")
        })?;

    let (shutdown_tx, mut shutdown_rx) = mpsc::channel::<service::FshostShutdownResponder>(1);

    let export = vfs::pseudo_directory! {
        "svc" => vfs::pseudo_directory! {
            fshost::AdminMarker::NAME => service::fshost_admin(shutdown_tx),
        }
    };

    let scope = ExecutionScope::new();
    export.open(
        scope.clone(),
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        fio::MODE_TYPE_DIRECTORY,
        Path::dot(),
        directory_request.into(),
    );

    // when we get a message on the shutdown channel, it's time to exit.
    let shutdown_responder = shutdown_rx
        .next()
        .await
        .ok_or_else(|| format_err!("shutdown signal stream ended unexpectedly"))?;

    log::info!("shutdown signal recieved");

    // Shutting down fshost involves sending asynchronous shutdown signals to several different
    // systems in order. If at any point we hit an error, we log loudly, but continue with the
    // shutdown procedure.

    // 0. Before fshost is told to shut down, almost everything that is running out of the
    //    filesystems is shut down by component manager.

    // 1. Shut down the scope for the export directory. This hosts the fshost services. This
    //    prevents additional connections to fshost services from being created.
    scope.shutdown();

    // 2. Shut down all the filesystems we started.

    // 3. Notify whoever asked for a shutdown that it's complete. After this point, it's possible
    //    the fshost process will be terminated externally.
    shutdown_responder.close()?;

    log::info!("fshost terminated");
    Ok(())
}
