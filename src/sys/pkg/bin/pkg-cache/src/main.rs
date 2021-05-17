// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        blob_location::BlobLocation, dynamic_index::DynamicIndex, pkgfs_inspect::PkgfsInspectState,
    },
    anyhow::{anyhow, Context as _, Error},
    argh::FromArgs,
    cobalt_sw_delivery_registry as metrics,
    fidl_fuchsia_update::CommitStatusProviderMarker,
    fuchsia_async::Task,
    fuchsia_cobalt::{CobaltConnector, ConnectionType},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as finspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{lock::Mutex, prelude::*},
    std::sync::Arc,
    system_image::StaticPackages,
};

mod blob_location;
mod blobs;
mod cache_service;
mod dynamic_index;
mod gc_service;
mod pkgfs_inspect;

#[cfg(test)]
mod test_utils;

const COBALT_CONNECTOR_BUFFER_SIZE: usize = 1000;

#[derive(FromArgs, Debug, PartialEq)]
/// Flags to the package cache.
pub struct Args {
    /// whether to ignore the system image when starting pkg-cache.
    #[argh(switch)]
    ignore_system_image: bool,
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg-cache"]).expect("can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    main_inner().await.map_err(|err| {
        // Use anyhow to print the error chain.
        let err = anyhow!(err);
        fx_log_err!("error running pkg-cache: {:#}", err);
        err
    })
}

async fn main_inner() -> Result<(), Error> {
    fx_log_info!("starting package cache service");

    let Args { ignore_system_image } = argh::from_env();

    let inspector = finspect::Inspector::new();
    let index_node = inspector.root().create_child("index");

    let (cobalt_sender, cobalt_fut) = CobaltConnector { buffer_size: COBALT_CONNECTOR_BUFFER_SIZE }
        .serve(ConnectionType::project_id(metrics::PROJECT_ID));
    let cobalt_fut = Task::spawn(cobalt_fut);

    let pkgfs_system =
        pkgfs::system::Client::open_from_namespace().context("error opening /pkgfs/system")?;
    let pkgfs_versions =
        pkgfs::versions::Client::open_from_namespace().context("error opening /pkgfs/versions")?;
    let pkgfs_ctl =
        pkgfs::control::Client::open_from_namespace().context("error opening /pkgfs/ctl")?;
    let pkgfs_install =
        pkgfs::install::Client::open_from_namespace().context("error opening /pkgfs/install")?;
    let pkgfs_needs =
        pkgfs::needs::Client::open_from_namespace().context("error opening /pkgfs/needs")?;
    let blobfs = blobfs::Client::open_from_namespace().context("error opening blobfs")?;

    let mut dynamic_index = DynamicIndex::new(index_node.create_child("dynamic"));
    let (static_packages, _pkgfs_inspect, blob_location, _load_cache_packages) = {
        let static_packages_fut = get_static_packages(&pkgfs_system);

        let pkgfs_inspect_fut =
            PkgfsInspectState::new(&pkgfs_system, inspector.root().create_child("pkgfs"));

        let blob_location_fut = BlobLocation::new(
            &pkgfs_system,
            &pkgfs_versions,
            inspector.root().create_child("blob-location"),
        );

        let load_cache_packages_fut =
            dynamic_index::load_cache_packages(&mut dynamic_index, &pkgfs_system, &pkgfs_versions)
                .unwrap_or_else(|e| fx_log_err!("Failed to load cache packages: {:#}", anyhow!(e)));

        future::join4(
            static_packages_fut,
            pkgfs_inspect_fut,
            blob_location_fut,
            load_cache_packages_fut,
        )
        .await
    };

    let mut _blob_location = None;
    let mut system_image_blobs = None;

    if !ignore_system_image {
        _blob_location = Some(blob_location?);
        system_image_blobs = Some(_blob_location.as_ref().unwrap().list_blobs().clone());
    }

    let commit_status_provider =
        fuchsia_component::client::connect_to_protocol::<CommitStatusProviderMarker>()
            .context("while connecting to commit status provider")?;

    enum IncomingService {
        PackageCache(fidl_fuchsia_pkg::PackageCacheRequestStream),
        SpaceManager(fidl_fuchsia_space::ManagerRequestStream),
    }

    let mut fs = ServiceFs::new();
    inspect_runtime::serve(&inspector, &mut fs)?;
    fs.take_and_serve_directory_handle().context("while serving directory handle")?;
    fs.dir("svc")
        .add_fidl_service(IncomingService::PackageCache)
        .add_fidl_service(IncomingService::SpaceManager);

    let dynamic_index = Arc::new(Mutex::new(dynamic_index));
    let system_image_blobs = Arc::new(system_image_blobs);

    let () = fs
        .for_each_concurrent(None, move |svc| {
            match svc {
                IncomingService::PackageCache(stream) => Task::spawn(
                    cache_service::serve(
                        pkgfs_versions.clone(),
                        pkgfs_ctl.clone(),
                        pkgfs_install.clone(),
                        pkgfs_needs.clone(),
                        Arc::clone(&dynamic_index),
                        blobfs.clone(),
                        Arc::clone(&static_packages),
                        stream,
                        cobalt_sender.clone(),
                    )
                    .map(|res| res.context("while serving fuchsia.pkg.PackageCache")),
                ),
                IncomingService::SpaceManager(stream) => Task::spawn(
                    gc_service::serve(
                        blobfs.clone(),
                        Arc::clone(&system_image_blobs),
                        Arc::clone(&dynamic_index),
                        commit_status_provider.clone(),
                        stream,
                    )
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
async fn get_static_packages(pkgfs_system: &pkgfs::system::Client) -> Arc<StaticPackages> {
    Arc::new(get_static_packages_impl(pkgfs_system).await.unwrap_or_else(|e| {
        fx_log_err!("Failed to load static packages, assumping empty: {:#}", anyhow!(e));
        StaticPackages::empty()
    }))
}

async fn get_static_packages_impl(
    pkgfs_system: &pkgfs::system::Client,
) -> Result<StaticPackages, Error> {
    let file = pkgfs_system
        .open_file("data/static_packages")
        .await
        .context("failed to open data/static_packages from system image package")?;
    StaticPackages::deserialize(file).context("error deserializing data/static_packages")
}
