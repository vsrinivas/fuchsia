// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{StreamExt, TryFutureExt},
    std::fs::File,
};

mod cache_service;
mod gc_service;

const SERVER_THREADS: usize = 2;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pkg_cache"]).expect("can't init logger");
    fx_log_info!("starting package cache service");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let mut fs = ServiceFs::new();

    let pkgfs_versions =
        connect_to_pkgfs("versions").context("error connecting to pkgfs/versions")?;
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn(
            cache_service::serve(Clone::clone(&pkgfs_versions), stream)
                .unwrap_or_else(|e| fx_log_err!("error handling PackageCache connection {:?}", e)),
        )
    });

    let pkgfs_ctl = connect_to_pkgfs("ctl").context("error connecting to pkgfs/ctl")?;
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn(
            gc_service::serve(Clone::clone(&pkgfs_ctl), stream)
                .unwrap_or_else(|e| fx_log_err!("error handling SpaceManager connection {:?}", e)),
        )
    });

    fs.take_and_serve_directory_handle()?;

    let () = executor.run(fs.collect(), SERVER_THREADS);
    Ok(())
}

fn connect_to_pkgfs(sub_dir: &str) -> Result<DirectoryProxy, Error> {
    let f = File::open(&format!("/pkgfs/{}", sub_dir))?;
    let chan = fasync::Channel::from_channel(fdio::clone_channel(&f)?)?;
    Ok(DirectoryProxy::new(chan))
}
