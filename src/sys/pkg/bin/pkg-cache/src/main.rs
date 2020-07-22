// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::blob_location::BlobLocation,
    crate::pkgfs_inspect::PkgfsInspectState,
    anyhow::{anyhow, Context as _, Error},
    cobalt_sw_delivery_registry as metrics, fuchsia_async as fasync,
    fuchsia_cobalt::{CobaltConnector, ConnectionType},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as finspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{future, prelude::*, stream::FuturesUnordered, StreamExt, TryFutureExt},
    std::sync::Arc,
    system_image::StaticPackages,
};

mod blob_location;
mod cache_service;
mod gc_service;
mod pkgfs_inspect;

const COBALT_CONNECTOR_BUFFER_SIZE: usize = 1000;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg-cache"]).expect("can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    fx_log_info!("starting package cache service");

    let mut executor = fasync::Executor::new().context("error creating executor")?;
    executor.run_singlethreaded(main_inner_async())
}

async fn main_inner_async() -> Result<(), Error> {
    let inspector = finspect::Inspector::new();
    let futures = FuturesUnordered::new();

    let (cobalt_sender, cobalt_fut) = CobaltConnector { buffer_size: COBALT_CONNECTOR_BUFFER_SIZE }
        .serve(ConnectionType::project_id(metrics::PROJECT_ID));
    futures.push(cobalt_fut.boxed_local());

    let pkgfs_system =
        pkgfs::system::Client::open_from_namespace().context("error opening /pkgfs/system")?;
    let pkgfs_versions =
        pkgfs::versions::Client::open_from_namespace().context("error opening pkgfs/versions")?;
    let pkgfs_ctl =
        pkgfs::control::Client::open_from_namespace().context("error opening pkgfs/ctl")?;

    let static_packages = get_static_packages(pkgfs_system.clone()).await;

    let cache_cb = {
        let pkgfs_ctl = Clone::clone(&pkgfs_ctl);
        move |stream| {
            let cobalt_sender = cobalt_sender.clone();
            fasync::Task::spawn(
                cache_service::serve(
                    Clone::clone(&pkgfs_versions),
                    Clone::clone(&pkgfs_ctl),
                    static_packages.clone(),
                    stream,
                    cobalt_sender,
                )
                .unwrap_or_else(|e| {
                    fx_log_err!("error handling PackageCache connection {:#}", anyhow!(e))
                }),
            )
            .detach()
        }
    };

    let gc_cb = move |stream| {
        fasync::Task::spawn(gc_service::serve(Clone::clone(&pkgfs_ctl), stream).unwrap_or_else(
            |e| fx_log_err!("error handling SpaceManager connection {:#}", anyhow!(e)),
        ))
        .detach()
    };

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(cache_cb).add_fidl_service(gc_cb);

    let blob_location_fut = BlobLocation::new(
        || Ok(pkgfs_system.clone()),
        || Ok(pkgfs::versions::Client::open_from_namespace()?),
        inspector.root().create_child("blob-location"),
    );

    let pkgfs_inspect_fut =
        PkgfsInspectState::new(|| Ok(pkgfs_system.clone()), inspector.root().create_child("pkgfs"));

    let (_blob_location, _pkgfs_inspect) = future::join(blob_location_fut, pkgfs_inspect_fut).await;

    inspector.serve(&mut fs)?;

    fs.take_and_serve_directory_handle()?;
    futures.push(fs.collect().boxed_local());

    futures.collect::<()>().await;

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
