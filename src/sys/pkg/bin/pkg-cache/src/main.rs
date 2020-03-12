// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::blob_location::BlobLocation,
    anyhow::{Context as _, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as finspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{StreamExt, TryFutureExt},
    std::sync::Arc,
    system_image::StaticPackages,
};

mod blob_location;
mod cache_service;
mod gc_service;

const SERVER_THREADS: usize = 2;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg-cache"]).expect("can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    fx_log_info!("starting package cache service");

    let inspector = finspect::Inspector::new();
    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let mut fs = ServiceFs::new();

    let pkgfs_versions =
        pkgfs::versions::Client::open_from_namespace().context("error opening pkgfs/versions")?;
    let static_packages = executor.run_singlethreaded(get_static_packages())?;

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

    let _blob_location = executor.run_singlethreaded(BlobLocation::new(
        || Ok(pkgfs::system::Client::open_from_namespace()?),
        || Ok(pkgfs::versions::Client::open_from_namespace()?),
        inspector.root().create_child("blob-location"),
    ));

    inspector.serve(&mut fs)?;

    fs.take_and_serve_directory_handle()?;

    let () = executor.run(fs.collect(), SERVER_THREADS);
    Ok(())
}

async fn get_static_packages() -> Result<Arc<StaticPackages>, Error> {
    let pkgfs_system =
        pkgfs::system::Client::open_from_namespace().context("error opening pkgfs/system")?;
    Ok(Arc::new(if let Ok(file) = pkgfs_system.open_file("data/static_packages").await {
        StaticPackages::deserialize(file).unwrap_or_else(|e| {
            fx_log_err!("error deserializing data/static_packages: {:?}", e);
            StaticPackages::empty()
        })
    } else {
        StaticPackages::empty()
    }))
}
