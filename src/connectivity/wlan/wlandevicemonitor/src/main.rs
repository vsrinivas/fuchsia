// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod device_watch;
mod service;
#[cfg(test)]
mod watchable_map;

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryFutureExt},
    log::{error, info},
};

async fn serve_fidl() -> Result<(), Error> {
    info!("Starting");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |reqs| {
        let fut = service::serve_monitor_requests(reqs)
            .unwrap_or_else(|e| error!("error serving device monitor API: {}", e));
        fasync::Task::spawn(fut).detach()
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;

    error!("Exiting");
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    serve_fidl().await
}
