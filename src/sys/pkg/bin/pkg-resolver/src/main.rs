// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context as _, Error},
    cobalt_client::traits::AsEventCode as _,
    cobalt_sw_delivery_registry as metrics,
    fidl_fuchsia_pkg::{LocalMirrorMarker, PackageCacheMarker},
    fuchsia_async as fasync,
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    fuchsia_inspect as inspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    fuchsia_trace as trace,
    futures::{prelude::*, stream::FuturesUnordered},
    itertools::Itertools as _,
    parking_lot::RwLock,
    std::{io, sync::Arc, time::Instant},
    system_image::CachePackages,
};

mod args;
mod cache;
mod clock;
mod config;
mod experiment;
mod font_package_manager;
mod inspect_util;
mod metrics_util;
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
    args::Args,
    cache::PackageCache,
    config::Config,
    experiment::Experiments,
    font_package_manager::{FontPackageManager, FontPackageManagerBuilder},
    ota_channel::ChannelInspectState,
    repository_manager::{RepositoryManager, RepositoryManagerBuilder},
    repository_service::RepositoryService,
    resolver_service::ResolverServiceInspectState,
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
// Each fetch_blob call emits an event, and a system update fetches about 1,000 blobs in about a
// minute.
const COBALT_CONNECTOR_BUFFER_SIZE: usize = 1000;

const STATIC_REPO_DIR: &str = "/config/data/repositories";
const DYNAMIC_REPO_PATH: &str = "/data/repositories.json";

const STATIC_RULES_PATH: &str = "/config/data/rewrites.json";
const DYNAMIC_RULES_PATH: &str = "/data/rewrites.json";

const STATIC_FONT_REGISTRY_PATH: &str = "/config/data/font_packages.json";

fn main() -> Result<(), Error> {
    let startup_time = Instant::now();
    fuchsia_syslog::init_with_tags(&["pkg-resolver"]).expect("can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    fx_log_info!("starting package resolver");

    let mut executor = fasync::Executor::new().context("error creating executor")?;
    executor.run_singlethreaded(main_inner_async(startup_time, argh::from_env()))
}

async fn main_inner_async(startup_time: Instant, args: Args) -> Result<(), Error> {
    let config = Config::load_from_config_data_or_default();

    let pkg_cache =
        connect_to_service::<PackageCacheMarker>().context("error connecting to package cache")?;
    let local_mirror = if args.allow_local_mirror {
        Some(
            connect_to_service::<LocalMirrorMarker>()
                .context("error connecting to local mirror")?,
        )
    } else {
        None
    };

    let pkgfs_install = pkgfs::install::Client::open_from_namespace()
        .context("error connecting to pkgfs/install")?;
    let pkgfs_needs =
        pkgfs::needs::Client::open_from_namespace().context("error connecting to pkgfs/needs")?;
    let cache = PackageCache::new(pkg_cache, pkgfs_install, pkgfs_needs);

    let base_package_index =
        Arc::new(cache.base_package_index().await.context("failed to load base package index")?);

    // The list of cache packages from the system image, not to be confused with the PackageCache.
    let system_cache_list = Arc::new(load_system_cache_list().await);

    let inspector = fuchsia_inspect::Inspector::new();
    let channel_inspect_state =
        ChannelInspectState::new(inspector.root().create_child("omaha_channel"));

    let experiment_state = experiment::State::new(inspector.root().create_child("experiments"));
    let experiment_state = Arc::new(RwLock::new(experiment_state));
    let experiments = Arc::clone(&experiment_state).into();

    let futures = FuturesUnordered::new();

    let (mut cobalt_sender, cobalt_fut) =
        CobaltConnector { buffer_size: COBALT_CONNECTOR_BUFFER_SIZE }
            .serve(ConnectionType::project_id(metrics::PROJECT_ID));
    futures.push(cobalt_fut.boxed_local());

    let font_package_manager = Arc::new(load_font_package_manager(cobalt_sender.clone()));
    let repo_manager = Arc::new(RwLock::new(load_repo_manager(
        inspector.root().create_child("repository_manager"),
        experiments,
        &config,
        cobalt_sender.clone(),
    )));
    let rewrite_manager = Arc::new(RwLock::new(
        load_rewrite_manager(
            inspector.root().create_child("rewrite_manager"),
            Arc::clone(&repo_manager),
            &config,
            &channel_inspect_state,
            cobalt_sender.clone(),
        )
        .await,
    ));

    let (blob_fetch_queue, blob_fetcher) = crate::cache::make_blob_fetch_queue(
        cache.clone(),
        MAX_CONCURRENT_BLOB_FETCHES,
        repo_manager.read().stats(),
        cobalt_sender.clone(),
        local_mirror,
    );
    futures.push(blob_fetch_queue.boxed_local());

    let resolver_service_inspect_state = Arc::new(ResolverServiceInspectState::new(
        inspector.root().create_child("resolver_service"),
    ));
    let (package_fetch_queue, package_fetcher) = resolver_service::make_package_fetch_queue(
        cache.clone(),
        Arc::clone(&base_package_index),
        Arc::clone(&system_cache_list),
        Arc::clone(&repo_manager),
        Arc::clone(&rewrite_manager),
        blob_fetcher.clone(),
        MAX_CONCURRENT_PACKAGE_FETCHES,
        Arc::clone(&resolver_service_inspect_state),
    );
    futures.push(package_fetch_queue.boxed_local());
    let package_fetcher = Arc::new(package_fetcher);

    let resolver_cb = {
        let cache = cache.clone();
        let repo_manager = Arc::clone(&repo_manager);
        let rewrite_manager = Arc::clone(&rewrite_manager);
        let package_fetcher = Arc::clone(&package_fetcher);
        let base_package_index = Arc::clone(&base_package_index);
        let system_cache_list = Arc::clone(&system_cache_list);
        let cobalt_sender = cobalt_sender.clone();
        let resolver_service_inspect = Arc::clone(&resolver_service_inspect_state);
        move |stream| {
            fasync::Task::local(
                resolver_service::run_resolver_service(
                    cache.clone(),
                    Arc::clone(&repo_manager),
                    Arc::clone(&rewrite_manager),
                    Arc::clone(&package_fetcher),
                    Arc::clone(&base_package_index),
                    Arc::clone(&system_cache_list),
                    stream,
                    cobalt_sender.clone(),
                    Arc::clone(&resolver_service_inspect),
                )
                .unwrap_or_else(|e| fx_log_err!("failed to spawn_local {:#}", anyhow!(e))),
            )
            .detach()
        }
    };

    let font_resolver_fb = {
        let cache = cache.clone();
        let package_fetcher = Arc::clone(&package_fetcher);
        let cobalt_sender = cobalt_sender.clone();
        move |stream| {
            fasync::Task::local(
                resolver_service::run_font_resolver_service(
                    Arc::clone(&font_package_manager),
                    cache.clone(),
                    Arc::clone(&package_fetcher),
                    stream,
                    cobalt_sender.clone(),
                )
                .unwrap_or_else(|e| {
                    fx_log_err!("Failed to spawn_local font_resolver_service {:#}", anyhow!(e))
                }),
            )
            .detach()
        }
    };

    let repo_cb = move |stream| {
        let repo_manager = Arc::clone(&repo_manager);

        fasync::Task::local(
            async move {
                let mut repo_service = RepositoryService::new(repo_manager);
                repo_service.run(stream).await
            }
            .unwrap_or_else(|e| fx_log_err!("error encountered: {:#}", anyhow!(e))),
        )
        .detach()
    };

    let rewrite_cb = move |stream| {
        let mut rewrite_service = RewriteService::new(Arc::clone(&rewrite_manager));

        fasync::Task::local(
            async move { rewrite_service.handle_client(stream).await }
                .unwrap_or_else(|e| fx_log_err!("while handling rewrite client {:#}", anyhow!(e))),
        )
        .detach()
    };

    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(resolver_cb)
        .add_fidl_service(font_resolver_fb)
        .add_fidl_service(repo_cb)
        .add_fidl_service(rewrite_cb);

    inspector.serve(&mut fs)?;

    fs.take_and_serve_directory_handle()?;

    futures.push(fs.collect().boxed_local());

    cobalt_sender.log_elapsed_time(
        metrics::PKG_RESOLVER_STARTUP_DURATION_METRIC_ID,
        0,
        Instant::now().duration_since(startup_time).as_micros() as i64,
    );

    trace::instant!("app", "startup", trace::Scope::Process);

    futures.collect::<()>().await;

    Ok(())
}

fn load_repo_manager(
    node: inspect::Node,
    experiments: Experiments,
    config: &Config,
    mut cobalt_sender: CobaltSender,
) -> RepositoryManager {
    // report any errors we saw, but don't error out because otherwise we won't be able
    // to update the system.
    let dynamic_repo_path =
        if config.enable_dynamic_configuration() { Some(DYNAMIC_REPO_PATH) } else { None };
    match RepositoryManagerBuilder::new(dynamic_repo_path, experiments)
        .unwrap_or_else(|(builder, err)| {
            fx_log_err!("error loading dynamic repo config: {:#}", anyhow!(err));
            builder
        })
        .inspect_node(node)
        .load_static_configs_dir(STATIC_REPO_DIR)
    {
        Ok(builder) => {
            cobalt_sender.log_event_count(
                metrics::REPOSITORY_MANAGER_LOAD_STATIC_CONFIGS_METRIC_ID,
                metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult::Success
                    .as_event_code(),
                0,
                1
            );
            builder
        }
        Err((builder, errs)) => {
            for err in errs {
                let dimension_result: metrics::RepositoryManagerLoadStaticConfigsMetricDimensionResult
                    = (&err).into();
                cobalt_sender.log_event_count(
                    metrics::REPOSITORY_MANAGER_LOAD_STATIC_CONFIGS_METRIC_ID,
                    dimension_result.as_event_code(),
                    0,
                    1
                );
                match &err {
                    crate::repository_manager::LoadError::Io { path: _, error }
                        if error.kind() == io::ErrorKind::NotFound =>
                    {
                        fx_log_info!("no statically configured repositories present");
                    }
                    _ => fx_log_err!("error loading static repo config: {:#}", anyhow!(err)),
                };
            }
            builder
        }
    }.cobalt_sender(cobalt_sender)
    .build()
}

async fn load_rewrite_manager(
    node: inspect::Node,
    repo_manager: Arc<RwLock<RepositoryManager>>,
    config: &Config,
    channel_inspect_state: &ChannelInspectState,
    cobalt_sender: CobaltSender,
) -> RewriteManager {
    let dynamic_rules_path =
        if config.enable_dynamic_configuration() { Some(DYNAMIC_RULES_PATH) } else { None };
    let builder = RewriteManagerBuilder::new(dynamic_rules_path)
        .unwrap_or_else(|(builder, err)| {
            if err.kind() != io::ErrorKind::NotFound {
                fx_log_err!(
                    "unable to load dynamic rewrite rules from disk, using defaults: {:#}",
                    anyhow!(err)
                );
            }
            builder
        })
        .inspect_node(node)
        .static_rules_path(STATIC_RULES_PATH)
        .unwrap_or_else(|(builder, err)| {
            if err.kind() != io::ErrorKind::NotFound {
                fx_log_err!("unable to load static rewrite rules from disk: {:#}", anyhow!(err));
            }
            builder
        });

    // If we have a channel in vbmeta or sysconfig, we don't want to load the dynamic
    // configs. Instead, we'll construct a unique rule for that channel.
    match crate::ota_channel::create_rewrite_rule_for_ota_channel(
        &channel_inspect_state,
        &repo_manager.read(),
        cobalt_sender.clone(),
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
            fx_log_err!("Failed to create rewrite rule for ota channel with error, falling back to defaults. {:#}", anyhow!(err));
            builder.build()
        }
    }
}

fn load_font_package_manager(mut cobalt_sender: CobaltSender) -> FontPackageManager {
    match FontPackageManagerBuilder::new().add_registry_file(STATIC_FONT_REGISTRY_PATH) {
        Ok(builder) => {
            cobalt_sender.log_event_count(
                metrics::FONT_MANAGER_LOAD_STATIC_REGISTRY_METRIC_ID,
                metrics::FontManagerLoadStaticRegistryMetricDimensionResult::Success
                    .as_event_code(),
                0,
                1
            );
            builder
        }
        Err((builder, errs)) => {
            let err_str = format!("{:#}", errs
                .into_iter()
                .filter_map(|err| {
                    let dimension_result: metrics::FontManagerLoadStaticRegistryMetricDimensionResult = (&err).into();
                    cobalt_sender.log_event_count(
                        metrics::FONT_MANAGER_LOAD_STATIC_REGISTRY_METRIC_ID,
                        dimension_result.as_event_code(),
                        0,
                        1
                    );
                    if err.is_not_found() {
                        fx_log_info!("no font package registry present: {:#}", anyhow!(err));
                        None
                    } else {
                        Some(anyhow!(err))
                    }
                })
                .format("; "));
            if !err_str.is_empty() {
                fx_log_err!("error(s) loading font package registry: {}", err_str);
            }
            builder
        }
    }
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
            fx_log_err!("failed to open system image: {:#}", anyhow!(e));
            return empty;
        }
    };
    let cache_file = system_image.open_file("data/cache_packages").await;
    let cache_file = match cache_file {
        Ok(f) => f,
        Err(e) => {
            fx_log_err!("failed to open data/cache_packages: {:#}", anyhow!(e));
            return empty;
        }
    };

    let cache_list = CachePackages::deserialize(cache_file);
    match cache_list {
        Ok(cl) => cl,
        Err(e) => {
            fx_log_err!("error opening package cache: {:#}", anyhow!(e));
            empty
        }
    }
}
