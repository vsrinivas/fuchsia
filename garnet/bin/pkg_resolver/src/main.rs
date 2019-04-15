// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl_fuchsia_amber::ControlMarker as AmberMarker;
use fidl_fuchsia_pkg::PackageCacheMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use futures::{StreamExt, TryFutureExt};
use parking_lot::RwLock;
use std::path::Path;
use std::sync::Arc;

mod repository_manager;
mod repository_service;
mod resolver_service;
mod rewrite_manager;
mod rewrite_service;

#[cfg(test)]
mod test_util;

use repository_manager::RepositoryManager;
use repository_service::RepositoryService;

use rewrite_manager::RewriteManager;
use rewrite_service::RewriteService;

const SERVER_THREADS: usize = 2;
const STATIC_REPO_DIR: &str = "/config/data/pkg_resolver/repositories";
const DYNAMIC_RULES_PATH: &str = "/data/rewrite_rules.json";

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg_resolver"]).expect("can't init logger");
    fx_log_info!("starting package resolver");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let repo_manager = static_repo_manager();

    let amber = connect_to_service::<AmberMarker>().context("error connecting to amber")?;
    let cache =
        connect_to_service::<PackageCacheMarker>().context("error connecting to package cache")?;

    let rewrite_manager = load_rewrite_manager();

    let mut fs = ServiceFs::new();
    fs.dir("public")
        .add_fidl_service(move |stream| {
            fasync::spawn(
                resolver_service::run_resolver_service(amber.clone(), cache.clone(), stream)
                    .unwrap_or_else(|e| fx_log_err!("failed to spawn {:?}", e)),
            )
        })
        .add_fidl_service(move |stream| {
            let repo_manager = repo_manager.clone();

            fasync::spawn(
                async move {
                    let mut repo_service = RepositoryService::new(repo_manager);
                    await!(repo_service.run(stream))
                }
                    .unwrap_or_else(|e| fx_log_err!("error encountered: {:?}", e)),
            )
        })
        .add_fidl_service(move |stream| {
            let mut rewrite_service = RewriteService::new(rewrite_manager.clone());

            fasync::spawn(
                async move { await!(rewrite_service.handle_client(stream)) }
                    .unwrap_or_else(|e| fx_log_err!("while handling rewrite client {:?}", e)),
            )
        });
    fs.take_and_serve_directory_handle()?;

    let () = executor.run(fs.collect(), SERVER_THREADS);

    Ok(())
}

fn static_repo_manager() -> Arc<RwLock<RepositoryManager>> {
    let static_repo_dir = Path::new(STATIC_REPO_DIR);

    if !static_repo_dir.exists() {
        return Arc::new(RwLock::new(RepositoryManager::new()));
    }

    match RepositoryManager::load_dir(static_repo_dir) {
        Ok((repo_manager, errors)) => {
            // report any errors we saw, but don't error out because otherwise we won't be able
            // to update the system.
            for err in errors {
                fx_log_err!("error loading static repo config: {}", err);
            }
            Arc::new(RwLock::new(repo_manager))
        }
        Err(err) => {
            fx_log_err!(
                "unable to load any static repo configs, defaulting to empty list: {}",
                err
            );
            Arc::new(RwLock::new(RepositoryManager::new()))
        }
    }
}

fn load_rewrite_manager() -> Arc<RwLock<RewriteManager>> {
    let dynamic_rules_path = Path::new(DYNAMIC_RULES_PATH);

    if !dynamic_rules_path.exists() {
        return Arc::new(RwLock::new(RewriteManager::new(dynamic_rules_path.to_owned())));
    }

    Arc::new(RwLock::new(RewriteManager::load(dynamic_rules_path).unwrap_or_else(|e| {
        fx_log_err!("unable to load dynamic rewrite rules from disk, using defaults: {}", e);
        RewriteManager::new(dynamic_rules_path.to_owned())
    })))
}
