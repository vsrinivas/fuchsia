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
    system_image::StaticPackages,
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
static DISABLE_RESTRICTIONS_FILE_PATH: &str = "data/pkgfs_disable_executability_restrictions";
static PKGFS_BOOT_ARG_KEY: &'static str = "zircon.system.pkgfs.cmd";
static PKGFS_BOOT_ARG_VALUE_PREFIX: &'static str = "bin/pkgsvr+";

#[derive(Debug, PartialEq, Eq)]
enum ExecutabilityRestrictions {
    Enforce,
    DoNotEnforce,
}

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

    let (executability_restrictions, base_packages) = if ignore_system_image {
        fx_log_info!("not loading system_image due to process arguments");
        inspector.root().record_string("system_image", "ignored");
        (ExecutabilityRestrictions::Enforce, None)
    } else {
        let boot_args = connect_to_protocol::<fidl_fuchsia_boot::ArgumentsMarker>()
            .context("error connecting to fuchsia.boot/Arguments")?;
        let system_image =
            get_system_image_hash(&boot_args).await.context("getting system_image hash")?;
        inspector.root().record_string("system_image", system_image.to_string());
        let system_image = package_directory::RootDir::new(blobfs.clone(), system_image)
            .await
            .context("creating RootDir for system_image")?;

        let load_cache_packages_fut = async {
            let cache_packages = system_image::CachePackages::deserialize(
                system_image
                    .read_file("data/cache_packages")
                    .await
                    .context("read system_image data/cache_packages")?
                    .as_slice(),
            )
            .context("deserialize data/cache_packages")?;

            index::load_cache_packages(&mut package_index, cache_packages, &pkgfs_versions).await
        };

        let base_packages_fut = load_base_packages(
            &system_image,
            &pkgfs_versions,
            inspector.root().create_child("base-packages"),
        );

        let (cache_packages_res, base_packages_res) =
            join!(load_cache_packages_fut, base_packages_fut);
        let () = cache_packages_res
            .unwrap_or_else(|e| fx_log_err!("Failed to load cache packages: {:#}", anyhow!(e)));

        (
            load_executability_restrictions(&system_image),
            Some(base_packages_res.context("loading base packages")?),
        )
    };

    inspector
        .root()
        .record_string("executability-restrictions", format!("{:?}", executability_restrictions));

    let commit_status_provider =
        fuchsia_component::client::connect_to_protocol::<CommitStatusProviderMarker>()
            .context("while connecting to commit status provider")?;

    enum IncomingService {
        PackageCache(fidl_fuchsia_pkg::PackageCacheRequestStream),
        RetainedPackages(fidl_fuchsia_pkg::RetainedPackagesRequestStream),
        SpaceManager(fidl_fuchsia_space::ManagerRequestStream),
    }

    let mut fs = ServiceFs::new();
    inspect_runtime::serve(&inspector, &mut fs)?;
    fs.take_and_serve_directory_handle().context("while serving directory handle")?;
    fs.dir("svc")
        .add_fidl_service(IncomingService::PackageCache)
        .add_fidl_service(IncomingService::RetainedPackages)
        .add_fidl_service(IncomingService::SpaceManager);

    let package_index = Arc::new(Mutex::new(package_index));
    let cache_inspect_id = Arc::new(AtomicU32::new(0));
    let cache_inspect_node = inspector.root().create_child("fuchsia.pkg.PackageCache");
    let cache_get_node = Arc::new(cache_inspect_node.create_child("get"));
    let base_packages = Arc::new(base_packages);

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
                        Arc::clone(&base_packages),
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
                        Arc::clone(&base_packages),
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

async fn load_base_packages(
    system_image: &package_directory::RootDir,
    pkgfs_versions: &pkgfs::versions::Client,
    node: finspect::Node,
) -> Result<BasePackages, Error> {
    let static_packages = system_image
        .read_file("data/static_packages")
        .await
        .context("failed to read data/static_packages from system_image package")?;
    let static_packages = StaticPackages::deserialize(static_packages.as_slice())
        .context("error deserializing data/static_packages")?;

    Ok(BasePackages::new(pkgfs_versions, static_packages, system_image.hash(), node)
        .await
        .context("loading base packages")?)
}

async fn get_system_image_hash(
    args: &fidl_fuchsia_boot::ArgumentsProxy,
) -> Result<fuchsia_hash::Hash, Error> {
    let hash = args
        .get_string(PKGFS_BOOT_ARG_KEY)
        .await
        .context("get pkgfs boot command")?
        .ok_or_else(|| anyhow!("boot args have no value for key {}", PKGFS_BOOT_ARG_KEY))?;
    let hash = hash
        .strip_prefix(PKGFS_BOOT_ARG_VALUE_PREFIX)
        .ok_or_else(|| anyhow!("malformated pkgfs boot arg {:?}", hash))?;
    hash.parse().with_context(|| format!("pkgfs boot arg hash invalid {:?}", hash))
}

fn load_executability_restrictions(
    system_image: &package_directory::RootDir,
) -> ExecutabilityRestrictions {
    match system_image.has_file(DISABLE_RESTRICTIONS_FILE_PATH) {
        true => ExecutabilityRestrictions::DoNotEnforce,
        false => ExecutabilityRestrictions::Enforce,
    }
}
