// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_pkg::PackageCacheMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    fuchsia_trace as trace,
    futures::{prelude::*, stream::FuturesUnordered},
    parking_lot::RwLock,
    std::{io, sync::Arc},
    system_image::CachePackages,
};

mod cache;
mod clock;
mod config;
mod experiment;
mod font_package_manager;
mod inspect_util;
mod ota_channel;
mod queue;
mod repository;
mod repository_manager;
mod repository_service;
mod resolver_service;
mod rewrite_manager;
mod rewrite_service;

#[cfg(test)]
mod test_util;

use crate::{
    cache::PackageCache,
    config::Config,
    experiment::Experiments,
    font_package_manager::{FontPackageManager, FontPackageManagerBuilder},
    ota_channel::ChannelInspectState,
    repository_manager::{RepositoryManager, RepositoryManagerBuilder},
    repository_service::RepositoryService,
    rewrite_manager::{RewriteManager, RewriteManagerBuilder},
    rewrite_service::RewriteService,
};

// FIXME: allow for multiple threads and sendable futures once repo updates support it.
// FIXME(43342): trace durations assume they start and end on the same thread, but since the
// package resolver's executor is multi-threaded, a trace duration that includes an 'await' may not
// end on the same thread it starts on, resulting in invalid trace events.
// const SERVER_THREADS: usize = 2;
const MAX_CONCURRENT_BLOB_FETCHES: usize = 5;
const MAX_CONCURRENT_PACKAGE_FETCHES: usize = 5;

const STATIC_REPO_DIR: &str = "/config/data/repositories";
const DYNAMIC_REPO_PATH: &str = "/data/repositories.json";

const STATIC_RULES_PATH: &str = "/config/data/rewrites.json";
const DYNAMIC_RULES_PATH: &str = "/data/rewrites.json";

const STATIC_FONT_REGISTRY_PATH: &str = "/config/data/font_packages.json";

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg-resolver"]).expect("can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    fx_log_info!("starting package resolver");

    let mut executor = fasync::Executor::new().context("error creating executor")?;
    executor.run_singlethreaded(main_inner_async())
}

async fn main_inner_async() -> Result<(), Error> {
    let config = Config::load_from_config_data_or_default();

    let pkg_cache =
        connect_to_service::<PackageCacheMarker>().context("error connecting to package cache")?;
    let pkgfs_install = pkgfs::install::Client::open_from_namespace()
        .context("error connecting to pkgfs/install")?;
    let pkgfs_needs =
        pkgfs::needs::Client::open_from_namespace().context("error connecting to pkgfs/needs")?;
    let cache = PackageCache::new(pkg_cache, pkgfs_install, pkgfs_needs);

    // The list of cache packages from the system image, not to be confused with the PackageCache.
    let system_cache_list = load_system_cache_list().await;
    let system_cache_list = Arc::new(system_cache_list);

    let inspector = fuchsia_inspect::Inspector::new();
    let channel_inspect_state =
        ChannelInspectState::new(inspector.root().create_child("omaha_channel"));

    let experiment_state = experiment::State::new(inspector.root().create_child("experiments"));
    let experiment_state = Arc::new(RwLock::new(experiment_state));
    let experiments = Arc::clone(&experiment_state).into();

    let font_package_manager = Arc::new(load_font_package_manager());
    let repo_manager = Arc::new(RwLock::new(load_repo_manager(
        inspector.root().create_child("repository_manager"),
        experiments,
        &config,
    )));
    let rewrite_manager = Arc::new(RwLock::new(
        load_rewrite_manager(
            inspector.root().create_child("rewrite_manager"),
            Arc::clone(&repo_manager),
            &config,
            &channel_inspect_state,
        )
        .await,
    ));

    let futures = FuturesUnordered::new();

    let (blob_fetch_queue, blob_fetcher) = crate::cache::make_blob_fetch_queue(
        cache.clone(),
        MAX_CONCURRENT_BLOB_FETCHES,
        repo_manager.read().stats(),
    );
    futures.push(blob_fetch_queue.boxed_local());

    let (package_fetch_queue, package_fetcher) = resolver_service::make_package_fetch_queue(
        cache.clone(),
        Arc::clone(&system_cache_list),
        Arc::clone(&repo_manager),
        Arc::clone(&rewrite_manager),
        blob_fetcher.clone(),
        MAX_CONCURRENT_PACKAGE_FETCHES,
    );
    futures.push(package_fetch_queue.boxed_local());
    let package_fetcher = Arc::new(package_fetcher);

    let resolver_cb = {
        let cache = cache.clone();
        let repo_manager = Arc::clone(&repo_manager);
        let rewrite_manager = Arc::clone(&rewrite_manager);
        let package_fetcher = Arc::clone(&package_fetcher);
        let system_cache_list = Arc::clone(&system_cache_list);
        move |stream| {
            fasync::spawn_local(
                resolver_service::run_resolver_service(
                    cache.clone(),
                    Arc::clone(&repo_manager),
                    Arc::clone(&rewrite_manager),
                    Arc::clone(&package_fetcher),
                    Arc::clone(&system_cache_list),
                    stream,
                )
                .unwrap_or_else(|e| fx_log_err!("failed to spawn_local {:?}", e)),
            )
        }
    };

    let font_resolver_fb = {
        let cache = cache.clone();
        let package_fetcher = Arc::clone(&package_fetcher);
        move |stream| {
            fasync::spawn_local(
                resolver_service::run_font_resolver_service(
                    Arc::clone(&font_package_manager),
                    cache.clone(),
                    Arc::clone(&package_fetcher),
                    stream,
                )
                .unwrap_or_else(|e| {
                    fx_log_err!("Failed to spawn_local font_resolver_service {:?}", e)
                }),
            )
        }
    };

    let repo_cb = move |stream| {
        let repo_manager = Arc::clone(&repo_manager);

        fasync::spawn_local(
            async move {
                let mut repo_service = RepositoryService::new(repo_manager);
                repo_service.run(stream).await
            }
            .unwrap_or_else(|e| fx_log_err!("error encountered: {:?}", e)),
        )
    };

    let rewrite_cb = move |stream| {
        let mut rewrite_service = RewriteService::new(Arc::clone(&rewrite_manager));

        fasync::spawn_local(
            async move { rewrite_service.handle_client(stream).await }
                .unwrap_or_else(|e| fx_log_err!("while handling rewrite client {:?}", e)),
        )
    };

    let admin_cb = move |stream| {
        let experiment_state = Arc::clone(&experiment_state);
        fasync::spawn_local(async move {
            experiment::run_admin_service(experiment_state, stream)
                .await
                .unwrap_or_else(|e| fx_log_err!("while handling admin client {:?}", e))
        });
    };

    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(resolver_cb)
        .add_fidl_service(font_resolver_fb)
        .add_fidl_service(repo_cb)
        .add_fidl_service(rewrite_cb)
        .add_fidl_service(admin_cb);

    inspector.serve(&mut fs)?;

    fs.take_and_serve_directory_handle()?;

    futures.push(fs.collect().boxed_local());

    trace::instant!("app", "startup", trace::Scope::Process);

    futures.collect::<()>().await;

    Ok(())
}

fn load_repo_manager(
    node: inspect::Node,
    experiments: Experiments,
    config: &Config,
) -> RepositoryManager {
    // report any errors we saw, but don't error out because otherwise we won't be able
    // to update the system.
    let dynamic_repo_path =
        if config.enable_dynamic_configuration() { Some(DYNAMIC_REPO_PATH) } else { None };
    RepositoryManagerBuilder::new(dynamic_repo_path, experiments)
        .unwrap_or_else(|(builder, err)| {
            fx_log_err!("error loading dynamic repo config: {}", err);
            builder
        })
        .inspect_node(node)
        .load_static_configs_dir(STATIC_REPO_DIR)
        .unwrap_or_else(|(builder, errs)| {
            for err in errs {
                match err {
                    crate::repository_manager::LoadError::Io { path: _, error }
                        if error.kind() == io::ErrorKind::NotFound =>
                    {
                        fx_log_info!("no statically configured repositories present");
                    }
                    _ => fx_log_err!("error loading static repo config: {}", err),
                };
            }
            builder
        })
        .build()
}

async fn load_rewrite_manager(
    node: inspect::Node,
    repo_manager: Arc<RwLock<RepositoryManager>>,
    config: &Config,
    channel_inspect_state: &ChannelInspectState,
) -> RewriteManager {
    let dynamic_rules_path =
        if config.enable_dynamic_configuration() { Some(DYNAMIC_RULES_PATH) } else { None };
    let builder = RewriteManagerBuilder::new(dynamic_rules_path)
        .unwrap_or_else(|(builder, err)| {
            if err.kind() != io::ErrorKind::NotFound {
                fx_log_err!(
                    "unable to load dynamic rewrite rules from disk, using defaults: {}",
                    err
                );
            }
            builder
        })
        .inspect_node(node)
        .static_rules_path(STATIC_RULES_PATH)
        .unwrap_or_else(|(builder, err)| {
            if err.kind() != io::ErrorKind::NotFound {
                fx_log_err!("unable to load static rewrite rules from disk: {}", err);
            }
            builder
        });

    // If we have a channel in vbmeta or sysconfig, we don't want to load the dynamic
    // configs. Instead, we'll construct a unique rule for that channel.
    match crate::ota_channel::create_rewrite_rule_for_ota_channel(
        &channel_inspect_state,
        &repo_manager.read(),
    )
    .await
    {
        Ok(Some(rule)) => {
            fx_log_info!("Created rewrite rule for ota channel: {:?}", rule);
            builder.replace_dynamic_rules(vec![rule]).build()
        }
        Ok(None) => {
            fx_log_info!("No ota channel present, so not creating rewrite rule.");
            builder.build()
        }
        Err(err) => {
            fx_log_err!("Failed to create rewrite rule for ota channel with error: {:?}. Falling back to defaults.", err);
            builder.build()
        }
    }
}

fn load_font_package_manager() -> FontPackageManager {
    FontPackageManagerBuilder::new()
        .add_registry_file(STATIC_FONT_REGISTRY_PATH)
        .unwrap_or_else(|(builder, errs)| {
            let errors = errs
                .iter()
                .filter(|err| {
                    if err.is_not_found() {
                        fx_log_info!("no font package registry present");
                        false
                    } else {
                        true
                    }
                })
                .fold(String::new(), |acc, err| acc + "\n" + format!("{}", err).as_str());
            if !errors.is_empty() {
                fx_log_err!("error(s) loading font package registry:{}", errors);
            }
            builder
        })
        .build()
}

// Read in the list of cache_packages from /system/data/cache_packages.
// If we can't load it for some reason, return an empty cache.
async fn load_system_cache_list() -> system_image::CachePackages {
    let system_image = pkgfs::system::Client::open_from_namespace();
    // Default to empty cache.
    let empty = CachePackages::from_entries(vec![]);
    let system_image = match system_image {
        Ok(s) => s,
        Err(e) => {
            fx_log_err!("failed to open system image: {}", e);
            return empty;
        }
    };
    let cache_file = system_image.open_file("data/cache_packages").await;
    let cache_file = match cache_file {
        Ok(f) => f,
        Err(e) => {
            fx_log_err!("failed to open data/cache_packages: {}", e);
            return empty;
        }
    };

    let cache_list = CachePackages::deserialize(cache_file);
    match cache_list {
        Ok(cl) => cl,
        Err(e) => {
            fx_log_err!("error opening package cache: {}", e);
            empty
        }
    }
}
