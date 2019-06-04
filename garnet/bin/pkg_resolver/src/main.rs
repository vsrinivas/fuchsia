// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![deny(warnings)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_amber::ControlMarker as AmberMarker,
    fidl_fuchsia_pkg::PackageCacheMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{StreamExt, TryFutureExt},
    parking_lot::RwLock,
    std::io,
    std::sync::Arc,
};

mod amber;
mod repository_manager;
mod repository_service;
mod resolver_service;
mod rewrite_manager;
mod rewrite_service;

#[cfg(test)]
mod test_util;

use crate::repository_manager::{RepositoryManager, RepositoryManagerBuilder};
use crate::repository_service::RepositoryService;
use crate::rewrite_manager::{RewriteManager, RewriteManagerBuilder};
use crate::rewrite_service::RewriteService;

const SERVER_THREADS: usize = 2;

const STATIC_REPO_DIR: &str = "/config/data/repositories";
const DYNAMIC_REPO_PATH: &str = "/data/repositories.json";

const STATIC_RULES_PATH: &str = "/config/data/rewrites.json";
const DYNAMIC_RULES_PATH: &str = "/data/rewrites.json";

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg_resolver"]).expect("can't init logger");
    fx_log_info!("starting package resolver");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let amber = connect_to_service::<AmberMarker>().context("error connecting to amber")?;
    let cache =
        connect_to_service::<PackageCacheMarker>().context("error connecting to package cache")?;

    let repo_manager = Arc::new(RwLock::new(load_repo_manager()));
    let rewrite_manager = Arc::new(RwLock::new(load_rewrite_manager()));

    let resolver_cb = {
        // Capture a clone of rewrite_manager's Arc so the new client callback has a copy from
        // which to make new clones.
        let amber = amber.clone();
        let rewrite_manager = rewrite_manager.clone();
        move |stream| {
            fasync::spawn(
                resolver_service::run_resolver_service(
                    rewrite_manager.clone(),
                    amber.clone(),
                    cache.clone(),
                    stream,
                )
                .unwrap_or_else(|e| fx_log_err!("failed to spawn {:?}", e)),
            )
        }
    };

    let repo_cb = move |stream| {
        let repo_manager = repo_manager.clone();

        fasync::spawn(
            async move {
                let mut repo_service = RepositoryService::new(repo_manager);
                await!(repo_service.run(stream))
            }
                .unwrap_or_else(|e| fx_log_err!("error encountered: {:?}", e)),
        )
    };

    let rewrite_cb = move |stream| {
        let mut rewrite_service = RewriteService::new(rewrite_manager.clone(), amber.clone());

        fasync::spawn(
            async move { await!(rewrite_service.handle_client(stream)) }
                .unwrap_or_else(|e| fx_log_err!("while handling rewrite client {:?}", e)),
        )
    };

    let mut fs = ServiceFs::new();
    fs.dir("public")
        .add_fidl_service(resolver_cb)
        .add_fidl_service(repo_cb)
        .add_fidl_service(rewrite_cb);
    fs.take_and_serve_directory_handle()?;

    let () = executor.run(fs.collect(), SERVER_THREADS);

    Ok(())
}

fn load_repo_manager() -> RepositoryManager {
    // report any errors we saw, but don't error out because otherwise we won't be able
    // to update the system.
    RepositoryManagerBuilder::new(DYNAMIC_REPO_PATH)
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

fn load_rewrite_manager() -> RewriteManager {
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
        .static_rules_path(STATIC_RULES_PATH)
        .unwrap_or_else(|(builder, err)| {
            if err.kind() != io::ErrorKind::NotFound {
                fx_log_err!("unable to load static rewrite rules from disk: {}", err);
            }
            builder
        })
        .build()
}
