// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::blob_location::BlobLocation,
    crate::pkgfs_inspect::PkgfsInspectState,
    anyhow::{Context as _, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as finspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{future, StreamExt, TryFutureExt},
    std::sync::Arc,
    system_image::StaticPackages,
};

mod blob_location;
mod cache_service;
mod gc_service;
mod pkgfs_inspect;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg-cache"]).expect("can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    fx_log_info!("starting package cache service");

    let mut executor = fasync::Executor::new().context("error creating executor")?;
    executor.run_singlethreaded(main_inner_async())
}

async fn main_inner_async() -> Result<(), Error> {
    let inspector = finspect::Inspector::new();

    let mut fs = ServiceFs::new();

    let pkgfs_system =
        pkgfs::system::Client::open_from_namespace().context("error opening /pkgfs/system")?;
    let pkgfs_versions =
        pkgfs::versions::Client::open_from_namespace().context("error opening pkgfs/versions")?;
    let static_packages = get_static_packages(pkgfs_system.clone()).await;

    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn(
            cache_service::serve(Clone::clone(&pkgfs_versions), static_packages.clone(), stream)
                .unwrap_or_else(|e| fx_log_err!("error handling PackageCache connection {:?}", e)),
        )
    });

    let pkgfs_ctl =
        pkgfs::control::Client::open_from_namespace().context("error opening pkgfs/ctl")?;
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn(
            gc_service::serve(Clone::clone(&pkgfs_ctl), stream)
                .unwrap_or_else(|e| fx_log_err!("error handling SpaceManager connection {:?}", e)),
        )
    });

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
    fs.collect::<()>().await;

    Ok(())
}

// Deserializes the static packages list. Returns an empty StaticPackages on error.
async fn get_static_packages(pkgfs_system: pkgfs::system::Client) -> Arc<StaticPackages> {
    get_static_packages_impl(pkgfs_system).await.unwrap_or_else(|e| {
        fx_log_err!("Failed to load static packages, assumping empty: {:?}", e);
        Arc::new(StaticPackages::empty())
    })
}

async fn get_static_packages_impl(
    pkgfs_system: pkgfs::system::Client,
) -> Result<Arc<StaticPackages>, Error> {
    let file = pkgfs_system
        .open_file("data/static_packages")
        .await
        .context("failed to open data/static_packages from system image package")?;
    Ok(Arc::new(
        StaticPackages::deserialize(file).context("error deserializing data/static_packages")?,
    ))
}
