// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_amber::ControlMarker as AmberMarker;
use fidl_fuchsia_pkg::{PackageCacheMarker, PackageResolverMarker};
use fuchsia_app::client::connect_to_service;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use futures::TryFutureExt;

mod resolver_service;

const SERVER_THREADS: usize = 2;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg_resolver"]).expect("can't init logger");
    fx_log_info!("starting package resolver");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let amber = connect_to_service::<AmberMarker>().context("error connecting to amber")?;
    let cache =
        connect_to_service::<PackageCacheMarker>().context("error connecting to package cache")?;

    let server = ServicesServer::new()
        .add_service((PackageResolverMarker::NAME, move |chan| {
            fx_log_info!("spawning resolver service");
            fasync::spawn(
                resolver_service::run_resolver_service(amber.clone(), cache.clone(), chan)
                    .unwrap_or_else(|e| fx_log_err!("failed to spawn {:?}", e)),
            )
        }))
        .start()
        .context("error starting package resolver server")?;

    executor.run(server, SERVER_THREADS).context("failed to execute package resolver future")?;
    Ok(())
}
