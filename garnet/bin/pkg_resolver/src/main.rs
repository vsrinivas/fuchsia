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

mod resolver_service;

const SERVER_THREADS: usize = 2;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg_resolver"]).expect("can't init logger");
    fx_log_info!("starting package resolver");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let amber = connect_to_service::<AmberMarker>().context("error connecting to amber")?;
    let cache =
        connect_to_service::<PackageCacheMarker>().context("error connecting to package cache")?;

    let mut fs = ServiceFs::new();
    fs.dir("public").add_fidl_service(move |stream| {
        fx_log_info!("spawning resolver service");
        fasync::spawn(
            resolver_service::run_resolver_service(amber.clone(), cache.clone(), stream)
                .unwrap_or_else(|e| fx_log_err!("failed to spawn {:?}", e)),
        )
    });
    fs.take_and_serve_directory_handle()?;

    let () = executor.run(fs.collect(), SERVER_THREADS);
    Ok(())
}
