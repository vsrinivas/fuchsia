// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_pkg::PackageCacheMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{StreamExt, TryFutureExt},
    parking_lot::RwLock,
    std::io,
    std::sync::Arc,
};

mod amber_connector;
mod experiment;
mod font_package_manager;
mod repository_manager;
mod repository_service;
mod resolver_service;
mod rewrite_manager;
mod rewrite_service;

#[cfg(test)]
mod test_util;

use crate::amber_connector::AmberConnector;
use crate::experiment::Experiments;
use crate::font_package_manager::{FontPackageManager, FontPackageManagerBuilder};
use crate::repository_manager::{RepositoryManager, RepositoryManagerBuilder};
use crate::repository_service::RepositoryService;
use crate::rewrite_manager::{RewriteManager, RewriteManagerBuilder};
use crate::rewrite_service::RewriteService;

const SERVER_THREADS: usize = 2;

const STATIC_REPO_DIR: &str = "/config/data/repositories";
const DYNAMIC_REPO_PATH: &str = "/data/repositories.json";

const STATIC_RULES_PATH: &str = "/config/data/rewrites.json";
const DYNAMIC_RULES_PATH: &str = "/data/rewrites.json";

const STATIC_FONT_REGISTRY_PATH: &str = "/config/data/font_packages.json";

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg_resolver"]).expect("can't init logger");
    fx_log_info!("starting package resolver");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let cache =
        connect_to_service::<PackageCacheMarker>().context("error connecting to package cache")?;

    let inspector = fuchsia_inspect::Inspector::new();
    let rewrite_inspect_node = inspector.root().create_child("rewrite_manager");
    let experiment_inspect_node = inspector.root().create_child("experiments");

    let amber_connector = AmberConnector::new();

    let experiment_state = Arc::new(RwLock::new(experiment::State::new(experiment_inspect_node)));
    let experiments = Arc::clone(&experiment_state).into();

    let font_package_manager = Arc::new(load_font_package_manager());
    let repo_manager = Arc::new(RwLock::new(load_repo_manager(amber_connector, experiments)));
    let rewrite_manager = Arc::new(RwLock::new(load_rewrite_manager(rewrite_inspect_node)));

    let resolver_cb = {
        // Capture a clone of repo and rewrite manager's Arc so the new client callback has a copy
        // from which to make new clones.
        let repo_manager = Arc::clone(&repo_manager);
        let rewrite_manager = Arc::clone(&rewrite_manager);
        let cache = cache.clone();
        move |stream| {
            fasync::spawn(
                resolver_service::run_resolver_service(
                    Arc::clone(&rewrite_manager),
                    Arc::clone(&repo_manager),
                    cache.clone(),
                    stream,
                )
                .unwrap_or_else(|e| fx_log_err!("failed to spawn {:?}", e)),
            )
        }
    };

    let font_resolver_fb = {
        let repo_manager = Arc::clone(&repo_manager);
        let rewrite_manager = Arc::clone(&rewrite_manager);
        let cache = cache.clone();
        move |stream| {
            fasync::spawn(
                resolver_service::run_font_resolver_service(
                    Arc::clone(&font_package_manager),
                    Arc::clone(&rewrite_manager),
                    Arc::clone(&repo_manager),
                    cache.clone(),
                    stream,
                )
                .unwrap_or_else(|e| fx_log_err!("Failed to spawn font_resolver_service {:?}", e)),
            )
        }
    };

    let repo_cb = move |stream| {
        let repo_manager = Arc::clone(&repo_manager);

        fasync::spawn(
            async move {
                let mut repo_service = RepositoryService::new(repo_manager);
                repo_service.run(stream).await
            }
                .unwrap_or_else(|e| fx_log_err!("error encountered: {:?}", e)),
        )
    };

    let rewrite_cb = move |stream| {
        let mut rewrite_service = RewriteService::new(Arc::clone(&rewrite_manager));

        fasync::spawn(
            async move { rewrite_service.handle_client(stream).await }
                .unwrap_or_else(|e| fx_log_err!("while handling rewrite client {:?}", e)),
        )
    };

    let admin_cb = move |stream| {
        let experiment_state = Arc::clone(&experiment_state);
        fasync::spawn(async move {
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

    inspector.export(&mut fs);

    fs.take_and_serve_directory_handle()?;

    let () = executor.run(fs.collect(), SERVER_THREADS);

    Ok(())
}

fn load_repo_manager(
    amber_connector: AmberConnector,
    experiments: Experiments,
) -> RepositoryManager<AmberConnector> {
    // report any errors we saw, but don't error out because otherwise we won't be able
    // to update the system.
    RepositoryManagerBuilder::new(DYNAMIC_REPO_PATH, amber_connector, experiments)
        .unwrap_or_else(|(builder, err)| {
            fx_log_err!("error loading dynamic repo config: {}", err);
            builder
        })
        .load_static_configs_dir(STATIC_REPO_DIR)
        .unwrap_or_else(|(builder, errs)| {
            for err in errs {
                fx_log_err!("error loading static repo config: {}", err);
            }
            builder
        })
        .build()
}

fn load_rewrite_manager(node: inspect::Node) -> RewriteManager {
    RewriteManagerBuilder::new(DYNAMIC_RULES_PATH)
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
        })
        .build()
}

fn load_font_package_manager() -> FontPackageManager {
    FontPackageManagerBuilder::new()
        .add_registry_file(STATIC_FONT_REGISTRY_PATH)
        .unwrap_or_else(|(builder, errs)| {
            fx_log_err!(
                "error(s) loading font package registry:{}",
                errs.iter()
                    .fold(String::new(), |acc, err| acc + "\n" + format!("{}", err).as_str())
            );
            builder
        })
        .build()
}
