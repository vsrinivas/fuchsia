// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_pkg::PackageCacheMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{self as inspect, Property, StringProperty},
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    fuchsia_trace as trace,
    futures::{prelude::*, stream::FuturesUnordered},
    parking_lot::RwLock,
    std::{io, sync::Arc},
    sysconfig_client,
};

mod amber_connector;
mod cache;
mod clock;
mod config;
mod experiment;
mod font_package_manager;
mod inspect_util;
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
    amber_connector::AmberConnector,
    cache::PackageCache,
    config::Config,
    experiment::Experiments,
    font_package_manager::{FontPackageManager, FontPackageManagerBuilder},
    repository_manager::{RepositoryManager, RepositoryManagerBuilder},
    repository_service::RepositoryService,
    rewrite_manager::{RewriteManager, RewriteManagerBuilder},
    rewrite_service::RewriteService,
};

// FIXME: allow for multiple threads and sendable futures once rust_tuf repo updates support it.
// FIXME(43342): trace durations assume they start and end on the same thread, but since the
// package resolver's executor is multi-threaded, a trace duration that includes an 'await' may not
// end on the same thread it starts on, resulting in invalid trace events.
// const SERVER_THREADS: usize = 2;
const MAX_CONCURRENT_BLOB_FETCHES: usize = 5;

const STATIC_REPO_DIR: &str = "/config/data/repositories";
const DYNAMIC_REPO_PATH: &str = "/data/repositories.json";

const STATIC_RULES_PATH: &str = "/config/data/rewrites.json";
const DYNAMIC_RULES_PATH: &str = "/data/rewrites.json";

const STATIC_FONT_REGISTRY_PATH: &str = "/config/data/font_packages.json";

struct ChannelInspectState {
    channel_name: StringProperty,
    tuf_config_name: StringProperty,
    _node: inspect::Node,
}

impl ChannelInspectState {
    fn new(node: inspect::Node) -> Self {
        Self {
            channel_name: node
                .create_string("channel_name", format!("{:?}", Option::<String>::None)),
            tuf_config_name: node
                .create_string("tuf_config_name", format!("{:?}", Option::<String>::None)),
            _node: node,
        }
    }
}

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg_resolver"]).expect("can't init logger");
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    fx_log_info!("starting package resolver");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let config = Config::load_from_config_data_or_default();

    let pkg_cache =
        connect_to_service::<PackageCacheMarker>().context("error connecting to package cache")?;
    let pkgfs_install = pkgfs::install::Client::open_from_namespace()
        .context("error connecting to pkgfs/install")?;
    let pkgfs_needs =
        pkgfs::needs::Client::open_from_namespace().context("error connecting to pkgfs/needs")?;
    let cache = PackageCache::new(pkg_cache, pkgfs_install, pkgfs_needs);

    let inspector = fuchsia_inspect::Inspector::new();
    let main_inspect_node = inspector.root().create_child("main");
    let channel_inspect_state = ChannelInspectState::new(main_inspect_node.create_child("channel"));

    let amber_connector = AmberConnector::new();

    let experiment_state =
        Arc::new(RwLock::new(experiment::State::new(inspector.root().create_child("experiments"))));
    let experiments = Arc::clone(&experiment_state).into();

    let font_package_manager = Arc::new(load_font_package_manager());
    let repo_manager = Arc::new(RwLock::new(load_repo_manager(
        inspector.root().create_child("repository_manager"),
        amber_connector,
        experiments,
        &config,
    )));
    let rewrite_manager = Arc::new(RwLock::new(load_rewrite_manager(
        inspector.root().create_child("rewrite_manager"),
        &repo_manager.read(),
        &config,
        &channel_inspect_state,
    )));

    let futures = FuturesUnordered::new();

    let (blob_fetch_queue, blob_fetcher) = crate::cache::make_blob_fetch_queue(
        cache.clone(),
        MAX_CONCURRENT_BLOB_FETCHES,
        repo_manager.read().stats(),
    );
    futures.push(blob_fetch_queue.boxed_local());

    let resolver_cb = {
        // Capture a clone of repo and rewrite manager's Arc so the new client callback has a copy
        // from which to make new clones.
        let repo_manager = Arc::clone(&repo_manager);
        let rewrite_manager = Arc::clone(&rewrite_manager);
        let cache = cache.clone();
        let blob_fetcher = blob_fetcher.clone();
        move |stream| {
            fasync::spawn_local(
                resolver_service::run_resolver_service(
                    Arc::clone(&rewrite_manager),
                    Arc::clone(&repo_manager),
                    cache.clone(),
                    blob_fetcher.clone(),
                    stream,
                )
                .unwrap_or_else(|e| fx_log_err!("failed to spawn_local {:?}", e)),
            )
        }
    };

    let font_resolver_fb = {
        let repo_manager = Arc::clone(&repo_manager);
        let rewrite_manager = Arc::clone(&rewrite_manager);
        let cache = cache.clone();
        move |stream| {
            fasync::spawn_local(
                resolver_service::run_font_resolver_service(
                    Arc::clone(&font_package_manager),
                    Arc::clone(&rewrite_manager),
                    Arc::clone(&repo_manager),
                    cache.clone(),
                    blob_fetcher.clone(),
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

    let () = executor.run_singlethreaded(futures.collect());

    Ok(())
}

fn load_repo_manager(
    node: inspect::Node,
    amber_connector: AmberConnector,
    experiments: Experiments,
    config: &Config,
) -> RepositoryManager<AmberConnector> {
    // report any errors we saw, but don't error out because otherwise we won't be able
    // to update the system.
    let dynamic_repo_path =
        if config.disable_dynamic_configuration() { None } else { Some(DYNAMIC_REPO_PATH) };
    RepositoryManagerBuilder::new(dynamic_repo_path, amber_connector, experiments)
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

fn load_rewrite_manager(
    node: inspect::Node,
    repo_manager: &RepositoryManager<AmberConnector>,
    config: &Config,
    channel_inspect_state: &ChannelInspectState,
) -> RewriteManager {
    let dynamic_rules_path =
        if config.disable_dynamic_configuration() { None } else { Some(DYNAMIC_RULES_PATH) };
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

    // If we have a channel in sysconfig, we don't want to load the dynamic configs. Instead, we'll
    // construct a unique rule for that channel.
    let channel = match sysconfig_client::channel::read_channel_config() {
        Ok(channel) => channel,
        Err(err) => {
            fx_log_info!("unable to load channel from sysconfig, using defaults: {}", err);
            return builder.build();
        }
    };
    let tuf_config_name = channel.tuf_config_name();
    fx_log_info!("current TUF config name is {}", tuf_config_name);

    let repo = match repo_manager.get_repo_for_channel(tuf_config_name) {
        Some(repo) => repo,
        None => {
            fx_log_err!("unable to find repo for channel, using defaults");
            return builder.build();
        }
    };
    fx_log_info!("channel repo is {}", repo.repo_url());

    match fuchsia_url_rewrite::Rule::new("fuchsia.com", repo.repo_url().host(), "/", "/") {
        Ok(rule) => {
            channel_inspect_state
                .channel_name
                .set(format!("{:?}", Some(channel.channel_name())).as_str());
            channel_inspect_state
                .tuf_config_name
                .set(format!("{:?}", Some(channel.tuf_config_name())).as_str());
            builder.replace_dynamic_rules(vec![rule]).build()
        }
        Err(err) => {
            fx_log_err!(
                "failed to make rewrite rule for {}, using defaults: {}",
                repo.repo_url(),
                err
            );
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
