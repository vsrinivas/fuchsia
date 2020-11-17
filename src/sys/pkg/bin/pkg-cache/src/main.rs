// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{blob_location::BlobLocation, pkgfs_inspect::PkgfsInspectState},
    anyhow::{anyhow, Context as _, Error},
    cobalt_sw_delivery_registry as metrics,
    fuchsia_async::Task,
    fuchsia_cobalt::{CobaltConnector, ConnectionType},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as finspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::prelude::*,
    std::sync::Arc,
    system_image::StaticPackages,
};

mod blob_location;
mod cache_service;
mod gc_service;
mod pkgfs_inspect;

const COBALT_CONNECTOR_BUFFER_SIZE: usize = 1000;

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg-cache"]).expect("can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    fx_log_info!("starting package cache service");

    let inspector = finspect::Inspector::new();

    let (cobalt_sender, cobalt_fut) = CobaltConnector { buffer_size: COBALT_CONNECTOR_BUFFER_SIZE }
        .serve(ConnectionType::project_id(metrics::PROJECT_ID));
    let cobalt_fut = Task::spawn(cobalt_fut);

    let pkgfs_system =
        pkgfs::system::Client::open_from_namespace().context("error opening /pkgfs/system")?;
    let pkgfs_versions =
        pkgfs::versions::Client::open_from_namespace().context("error opening pkgfs/versions")?;
    let pkgfs_ctl =
        pkgfs::control::Client::open_from_namespace().context("error opening pkgfs/ctl")?;

    let (static_packages, _blob_location, _pkgfs_inspect) = {
        let static_packages_fut = get_static_packages(pkgfs_system.clone());

        let blob_location_fut = BlobLocation::new(
            || Ok(pkgfs_system.clone()),
            || Ok(pkgfs_versions.clone()),
            inspector.root().create_child("blob-location"),
        );

        let pkgfs_inspect_fut = PkgfsInspectState::new(
            || Ok(pkgfs_system.clone()),
            inspector.root().create_child("pkgfs"),
        );

        future::join3(static_packages_fut, blob_location_fut, pkgfs_inspect_fut).await
    };

    enum IncomingService {
        PackageCache(fidl_fuchsia_pkg::PackageCacheRequestStream),
        SpaceManager(fidl_fuchsia_space::ManagerRequestStream),
    }

    let mut fs = ServiceFs::new();
    inspector.serve(&mut fs)?;
    fs.take_and_serve_directory_handle()?;
    fs.dir("svc")
        .add_fidl_service(IncomingService::PackageCache)
        .add_fidl_service(IncomingService::SpaceManager);

    let () = fs
        .for_each_concurrent(None, move |svc| {
            match svc {
                IncomingService::PackageCache(stream) => Task::spawn(
                    cache_service::serve(
                        pkgfs_versions.clone(),
                        pkgfs_ctl.clone(),
                        Arc::clone(&static_packages),
                        stream,
                        cobalt_sender.clone(),
                    )
                    .map(|res| res.context("while serving fuchsia.pkg.PackageCache")),
                ),
                IncomingService::SpaceManager(stream) => Task::spawn(
                    gc_service::serve(pkgfs_ctl.clone(), stream)
                        .map(|res| res.context("while serving fuchsia.space.Manager")),
                ),
            }
            .unwrap_or_else(|e| {
                fx_log_err!("error handling fidl connection: {:#}", anyhow!(e));
            })
        })
        .await;
    cobalt_fut.await;

    Ok(())
}

// Deserializes the static packages list. Returns an empty StaticPackages on error.
async fn get_static_packages(pkgfs_system: pkgfs::system::Client) -> Arc<StaticPackages> {
    Arc::new(get_static_packages_impl(pkgfs_system).await.unwrap_or_else(|e| {
        fx_log_err!("Failed to load static packages, assumping empty: {:#}", anyhow!(e));
        StaticPackages::empty()
    }))
}

async fn get_static_packages_impl(
    pkgfs_system: pkgfs::system::Client,
) -> Result<StaticPackages, Error> {
    let file = pkgfs_system
        .open_file("data/static_packages")
        .await
        .context("failed to open data/static_packages from system image package")?;
    StaticPackages::deserialize(file).context("error deserializing data/static_packages")
}
