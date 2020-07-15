// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_fs::{AdminRequest, AdminRequestStream},
    fidl_fuchsia_io::{self as fio, DirectoryMarker},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_fatfs::FatFs,
    fuchsia_runtime::HandleType,
    fuchsia_syslog::{self, fx_log_err},
    fuchsia_zircon::{self as zx, Status},
    futures::future::TryFutureExt,
    futures::stream::{StreamExt, TryStreamExt},
    remote_block_device::RemoteBlockDevice,
    vfs::{execution_scope::ExecutionScope, path::Path, registry::token_registry},
};

enum Services {
    Admin(AdminRequestStream),
}

async fn handle_admin(mut stream: AdminRequestStream, _fs: &FatFs) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("Reading request")? {
        match request {
            AdminRequest::Shutdown { responder } => {
                // TODO(fxb/53796): Need to call shutdown and call unmount here.
                responder.send()?;
            }
        }
    }
    Ok(())
}

#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();

    // Open the remote block device.
    let device = Box::new(remote_block_device::Cache::new(RemoteBlockDevice::new_sync(
        zx::Channel::from(
            fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleInfo::new(
                HandleType::User0,
                1,
            ))
            .ok_or(format_err!("Missing device handle"))?,
        ),
    )?)?);

    // VFS initialization.
    let registry = token_registry::Simple::new();
    let scope =
        ExecutionScope::build(Box::new(fasync::EHandle::local())).token_registry(registry).new();

    // Start the filesystem and open the root directory.
    let fatfs = FatFs::new(device).map_err(|_| Status::IO)?;
    let (proxy, server) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
    fatfs.get_root().open(
        scope.clone(),
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        0,
        Path::empty(),
        server.into_channel().into(),
    );

    // Export the root directory in our outgoing directory.
    let mut fs = ServiceFs::new();
    fs.add_remote("root", proxy);
    fs.dir("svc").add_fidl_service(Services::Admin);
    fs.take_and_serve_directory_handle()?;

    // Handle all ServiceFs connections. VFS connections will be spawned as separate tasks.
    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |request| {
        match request {
            Services::Admin(request) => handle_admin(request, &fatfs),
        }
        .unwrap_or_else(|e| fx_log_err!("{:?}", e))
    })
    .await;

    // At this point all direct connections to ServiceFs will have been closed (and cannot be
    // resurrected), but before we finish, we must wait for all VFS connections to be closed.
    scope.wait().await;

    Ok(())
}
