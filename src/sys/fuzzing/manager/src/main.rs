// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod diagnostics;
mod events;
mod fuzzer;
mod manager;

#[cfg(test)]
mod test_support;

use {
    crate::manager::Manager,
    anyhow::{Context as _, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::StreamExt,
    std::sync::Arc,
};

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["fuzz-manager"]).context("failed to initialize logging")?;
    let mut executor = fasync::LocalExecutor::new().context("error creating executor")?;
    let mut fs = ServiceFs::new();
    let manager = Arc::new(Manager::new());
    fs.dir("svc").add_fidl_service(move |stream| {
        let manager_for_connection = manager.clone();
        fasync::Task::spawn(async move {
            manager_for_connection.serve(stream).await.expect("failed to serve manager")
        })
        .detach()
    });
    fs.take_and_serve_directory_handle().context("failed to take and serve directory handle")?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
