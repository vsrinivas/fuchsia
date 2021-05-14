// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog, fuchsia_zircon as zx,
    futures::StreamExt,
    tracing::{info, warn},
};

fn main() -> Result<(), Error> {
    fuchsia_syslog::init()?;
    info!("started");
    let mut executor = fasync::LocalExecutor::new().context("error creating executor")?;
    let mut fs = ServiceFs::new_local();
    let test_map = test_manager_lib::TestMap::new(zx::Duration::from_minutes(5));
    let test_map_clone = test_map.clone();
    fs.dir("svc")
        .add_fidl_service(move |stream| {
            let test_map = test_map_clone.clone();
            fasync::Task::local(async move {
                test_manager_lib::run_test_manager(stream, test_map.clone())
                    .await
                    .unwrap_or_else(|error| warn!(?error, "test manager returned error"))
            })
            .detach();
        })
        .add_fidl_service(move |stream| {
            let test_map = test_map.clone();
            fasync::Task::local(async move {
                test_manager_lib::run_test_manager_info_server(stream, test_map.clone())
                    .await
                    .unwrap_or_else(|error| warn!(?error, "test manager returned error"))
            })
            .detach();
        });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
