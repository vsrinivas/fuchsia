// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{base_packages::BasePackages, index::PackageIndex},
    anyhow::{anyhow, Context as _, Error},
    argh::FromArgs,
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::DiscoverableProtocolMarker as _,
    fidl_fuchsia_update::CommitStatusProviderMarker,
    fuchsia_async::{futures::join, Task},
    fuchsia_cobalt::{CobaltConnector, ConnectionType},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_inspect as finspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{lock::Mutex, prelude::*},
    std::sync::{atomic::AtomicU32, Arc},
    vfs::directory::{entry::DirectoryEntry as _, helper::DirectlyMutable as _},
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

    inspector
        .root()
        .record_string("executability-restrictions", format!("{:?}", executability_restrictions));
    inspector
        .root()
        .record_child("non_static_allow_list", |n| non_static_allow_list.record_inspect(n));

    let base_packages = Arc::new(base_packages);
    let package_index = Arc::new(Mutex::new(package_index));

    // Use VFS to serve the out dir because ServiceFs does not support OPEN_RIGHT_EXECUTABLE and
    // pkgfs/{packages|versions|system} require it.
    let svc_dir = vfs::pseudo_directory! {};
    let cache_inspect_node = inspector.root().create_child("fuchsia.pkg.PackageCache");
    {
        let pkgfs_versions = pkgfs_versions.clone();
        let pkgfs_install = pkgfs_install.clone();
        let pkgfs_needs = pkgfs_needs.clone();
        let package_index = Arc::clone(&package_index);
        let blobfs = blobfs.clone();
        let base_packages = Arc::clone(&base_packages);
        let cache_packages = Arc::new(cache_packages);
        let cobalt_sender = cobalt_sender.clone();
        let cache_inspect_id = Arc::new(AtomicU32::new(0));
        let cache_get_node = Arc::new(cache_inspect_node.create_child("get"));

        let () = svc_dir
            .add_entry(
                fidl_fuchsia_pkg::PackageCacheMarker::PROTOCOL_NAME,
                vfs::service::host(move |stream: fidl_fuchsia_pkg::PackageCacheRequestStream| {
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
                    .unwrap_or_else(|e| {
                        fx_log_err!(
                            "error handling fuchsia.pkg.PackageCache connection: {:#}",
                            anyhow!(e)
                        )
                    })
                }),
            )
            .context("adding fuchsia.pkg/PackageCache to /svc")?;
    }
    {
        let package_index = Arc::clone(&package_index);
        let blobfs = blobfs.clone();

        let () = svc_dir
            .add_entry(
                fidl_fuchsia_pkg::RetainedPackagesMarker::PROTOCOL_NAME,
                vfs::service::host(
                    move |stream: fidl_fuchsia_pkg::RetainedPackagesRequestStream| {
                        retained_packages_service::serve(
                            Arc::clone(&package_index),
                            blobfs.clone(),
                            stream,
                        )
                        .unwrap_or_else(|e| {
                            fx_log_err!(
                                "error handling fuchsia.pkg/RetainedPackages connection: {:#}",
                                anyhow!(e)
                            )
                        })
                    },
                ),
            )
            .context("adding fuchsia.pkg/RetainedPackages to /svc")?;
    }
    {
        let blobfs = blobfs.clone();
        let base_packages = Arc::clone(&base_packages);
        let package_index = Arc::clone(&package_index);
        let commit_status_provider =
            fuchsia_component::client::connect_to_protocol::<CommitStatusProviderMarker>()
                .context("while connecting to commit status provider")?;

        let () = svc_dir
            .add_entry(
                fidl_fuchsia_space::ManagerMarker::PROTOCOL_NAME,
                vfs::service::host(move |stream: fidl_fuchsia_space::ManagerRequestStream| {
                    gc_service::serve(
                        blobfs.clone(),
                        Arc::clone(&base_packages),
                        Arc::clone(&package_index),
                        commit_status_provider.clone(),
                        stream,
                    )
                    .unwrap_or_else(|e| {
                        fx_log_err!(
                            "error handling fuchsia.space/Manager connection: {:#}",
                            anyhow!(e)
                        )
                    })
                }),
            )
            .context("adding fuchsia.space/Manager to /svc")?;
    }

    let out_dir = vfs::pseudo_directory! {
        "svc" => svc_dir,
        "pkgfs" =>
            crate::compat::pkgfs::make_dir(
                Arc::clone(&base_packages),
                Arc::clone(&package_index),
                Arc::new(non_static_allow_list),
                executability_restrictions,
                // TODO(fxbug.dev/88871) Use a blobfs client with RX rights (instead of RW) to serve
                // pkgfs.
                blobfs.clone(),
                system_image,
            )
            .context("serve pkgfs compat directories")?,
        inspect_runtime::DIAGNOSTICS_DIR => inspect_runtime::create_diagnostics_dir(inspector),
    };

    let scope = vfs::execution_scope::ExecutionScope::new();
    let () = out_dir.open(
        scope.clone(),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE
            | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE
            | fidl_fuchsia_io::OPEN_RIGHT_EXECUTABLE,
        0,
        vfs::path::Path::dot(),
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest.into())
            .context("taking startup handle")?
            .into(),
    );
    let () = scope.wait().await;

    cobalt_fut.await;

    Ok(())
}
