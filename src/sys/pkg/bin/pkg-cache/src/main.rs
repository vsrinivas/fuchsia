// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{base_packages::BasePackages, index::PackageIndex},
    anyhow::{anyhow, Context as _, Error},
    argh::FromArgs,
    cobalt_sw_delivery_registry as metrics,
    fidl_fuchsia_update::CommitStatusProviderMarker,
    fuchsia_async::{futures::join, Task},
    fuchsia_cobalt::{CobaltConnector, ConnectionType},
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_inspect as finspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{lock::Mutex, prelude::*},
    std::sync::{atomic::AtomicU32, Arc},
};

mod base_packages;
mod cache_service;
mod compat;
mod gc_service;
mod index;
mod retained_packages_service;

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

    let pkgfs_versions =
        pkgfs::versions::Client::open_from_namespace().context("error opening /pkgfs/versions")?;
    let pkgfs_install =
        pkgfs::install::Client::open_from_namespace().context("error opening /pkgfs/install")?;
    let pkgfs_needs =
        pkgfs::needs::Client::open_from_namespace().context("error opening /pkgfs/needs")?;
    let blobfs = blobfs::Client::open_from_namespace().context("error opening blobfs")?;

    let mut package_index = PackageIndex::new(index_node);

    let (
        system_image,
        executability_restrictions,
        non_static_allow_list,
        base_packages,
        cache_packages,
    ) = if ignore_system_image {
        fx_log_info!("not loading system_image due to process arguments");
        inspector.root().record_string("system_image", "ignored");
        (
            None,
            system_image::ExecutabilityRestrictions::Enforce,
            system_image::NonStaticAllowList::empty(),
            BasePackages::empty(inspector.root().create_child("base-packages")),
            None,
        )
    } else {
        let boot_args = connect_to_protocol::<fidl_fuchsia_boot::ArgumentsMarker>()
            .context("error connecting to fuchsia.boot/Arguments")?;
        // TODO(fxbug.dev/88871) Use a blobfs client with RX rights (instead of RW) to create
        // system_image.
        let system_image = system_image::SystemImage::new(blobfs.clone(), &boot_args)
            .await
            .context("Accessing contents of system_image package")?;
        inspector.root().record_string("system_image", system_image.hash().to_string());

        let (base_packages_res, cache_packages_res, non_static_allow_list) = join!(
            BasePackages::new(
                &blobfs,
                &system_image,
                inspector.root().create_child("base-packages"),
            ),
            async {
                let cache_packages =
                    system_image.cache_packages().await.context("reading cache_packages")?;
                index::load_cache_packages(&mut package_index, &cache_packages, &blobfs).await;
                Ok(cache_packages)
            },
            system_image.non_static_allow_list(),
        );
        let base_packages = base_packages_res.context("loading base packages")?;
        let cache_packages = cache_packages_res.map_or_else(
            |e: anyhow::Error| {
                fx_log_err!("Failed to load cache packages: {e:#}");
                None
            },
            Some,
        );

        let executability_restrictions = system_image.load_executability_restrictions();

        (
            Some(system_image),
            executability_restrictions,
            non_static_allow_list,
            base_packages,
            cache_packages,
        )
    };

    let base_packages = Arc::new(base_packages);
    let package_index = Arc::new(Mutex::new(package_index));
    inspector
        .root()
        .record_string("executability-restrictions", format!("{:?}", executability_restrictions));
    inspector
        .root()
        .record_child("non_static_allow_list", |n| non_static_allow_list.record_inspect(n));

    enum IncomingService {
        PackageCache(fidl_fuchsia_pkg::PackageCacheRequestStream),
        RetainedPackages(fidl_fuchsia_pkg::RetainedPackagesRequestStream),
        SpaceManager(fidl_fuchsia_space::ManagerRequestStream),
    }

    // TODO(fxbug.dev/88871) Use VFS to serve the out dir because ServiceFs does not support
    // OPEN_RIGHT_EXECUTABLE and pkgfs/{packages|versions|system} require it.
    let mut fs = ServiceFs::new();

    inspect_runtime::serve(&inspector, &mut fs)?;

    fs.dir("svc")
        .add_fidl_service(IncomingService::PackageCache)
        .add_fidl_service(IncomingService::RetainedPackages)
        .add_fidl_service(IncomingService::SpaceManager);

    // TODO(fxbug.dev/88871) Use a blobfs client with RX rights (instead of RW) to serve pkgfs.
    let () = crate::compat::pkgfs::serve(
        fs.dir("pkgfs"),
        Arc::clone(&base_packages),
        Arc::clone(&package_index),
        non_static_allow_list,
        blobfs.clone(),
        system_image,
        vfs::execution_scope::ExecutionScope::new(),
    )
    .context("serve pkgfs compat directories")?;

    let commit_status_provider =
        fuchsia_component::client::connect_to_protocol::<CommitStatusProviderMarker>()
            .context("while connecting to commit status provider")?;

    let cache_inspect_id = Arc::new(AtomicU32::new(0));
    let cache_inspect_node = inspector.root().create_child("fuchsia.pkg.PackageCache");
    let cache_get_node = Arc::new(cache_inspect_node.create_child("get"));
    let cache_packages = Arc::new(cache_packages);

    fs.take_and_serve_directory_handle().context("while serving directory handle")?;
    let () = fs
        .for_each_concurrent(None, move |svc| {
            match svc {
                IncomingService::PackageCache(stream) => Task::spawn(
                    cache_service::serve(
                        pkgfs_versions.clone(),
                        pkgfs_install.clone(),
                        pkgfs_needs.clone(),
                        Arc::clone(&package_index),
                        blobfs.clone(),
                        base_packages.clone(),
                        Arc::clone(&cache_packages),
                        stream,
                        cobalt_sender.clone(),
                        Arc::clone(&cache_inspect_id),
                        Arc::clone(&cache_get_node),
                    )
                    .map(|res| res.context("while serving fuchsia.pkg.PackageCache")),
                ),
                IncomingService::RetainedPackages(stream) => Task::spawn(
                    retained_packages_service::serve(
                        Arc::clone(&package_index),
                        blobfs.clone(),
                        stream,
                    )
                    .map(|res| res.context("while serving fuchsia.pkg.RetainedPackages")),
                ),
                IncomingService::SpaceManager(stream) => Task::spawn(
                    gc_service::serve(
                        blobfs.clone(),
                        base_packages.clone(),
                        Arc::clone(&package_index),
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
