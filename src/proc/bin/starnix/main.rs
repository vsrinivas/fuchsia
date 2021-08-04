// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use log::info;
use std::ffi::CString;

mod auth;
mod collections;
mod fs;
mod loader;
mod logging;
mod mm;
mod runner;
mod signals;
mod syscalls;
mod task;
mod types;
mod vmex_resource;

#[cfg(test)]
mod testing;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["starnix"]).expect("failed to initialize logger");
    info!("main");

    // The root kernel object for this instance of Starnix.
    let kernel = task::Kernel::new(&CString::new("kernel")?)?;

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        let kernel = kernel.clone();
        fasync::Task::local(async move {
            runner::start_runner(kernel, stream).await.expect("failed to start runner.")
        })
        .detach();
    });
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            runner::start_manager(stream).await.expect("failed to start manager.")
        })
        .detach();
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
