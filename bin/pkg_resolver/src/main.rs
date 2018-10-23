// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_amber::ControlMarker as AmberMarker;
use fidl_fuchsia_io::DirectoryProxy;
use fidl_fuchsia_pkg::PackageResolverMarker;
use fuchsia_app::client::connect_to_service;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use futures::TryFutureExt;
use std::fs::File;

mod resolver_service;

const SERVER_THREADS: usize = 2;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg_resolver"]).expect("can't init logger");
    fx_log_info!("starting package resolver");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let amber = connect_to_service::<AmberMarker>().context("error connecting to amber")?;
    let pkgfs = connect_to_pkgfs_versions().context("error connecting to pkgfs")?;

    let server = ServicesServer::new()
        .add_service((PackageResolverMarker::NAME, move |chan| {
            fx_log_info!("spawning resolver service");
            fasync::spawn(
                resolver_service::run_resolver_service(amber.clone(), Clone::clone(&pkgfs), chan)
                    .unwrap_or_else(|e| fx_log_err!("failed to spawn {:?}", e)),
            )
        }))
        .start()
        .context("Error starting package resolver server")?;

    executor
        .run(server, SERVER_THREADS)
        .context("failed to execute package resolver future")?;
    Ok(())
}

fn connect_to_pkgfs_versions() -> Result<DirectoryProxy, Error> {
    let f = File::open("/pkgfs/versions")?;
    let chan = fasync::Channel::from_channel(fdio::clone_channel(&f)?)?;
    Ok(DirectoryProxy::new(chan))
}
