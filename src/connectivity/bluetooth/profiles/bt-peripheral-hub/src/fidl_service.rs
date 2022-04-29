// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_bluetooth_power::{ReporterRequestStream, WatcherRequestStream};
use fuchsia_component::server::{ServiceFs, ServiceObj};
use futures::{future::BoxFuture, FutureExt, StreamExt};
use std::sync::Arc;
use tracing::info;

use crate::peripheral_state::PeripheralState;
use crate::reporter::Reporter;
use crate::watcher::Watcher;

/// The maximum number of FIDL service client connections that will be serviced concurrently.
const MAX_CONCURRENT_CONNECTIONS: usize = 10;

/// All FIDL services that are exposed by this component's ServiceFs.
pub enum Service {
    Reporter(ReporterRequestStream),
    Watcher(WatcherRequestStream),
}

fn handle_reporter_client_connection(
    stream: ReporterRequestStream,
    state: Arc<PeripheralState>,
) -> BoxFuture<'static, ()> {
    info!("New power.Reporter client connection");
    let reporter = Reporter::new(state.clone());
    async move {
        let result = reporter.run(stream).await;
        info!("Reporter handler for FIDL client finished with result: {:?}", result);
    }
    .boxed()
}

fn handle_watcher_client_connection(
    stream: WatcherRequestStream,
    state: Arc<PeripheralState>,
) -> BoxFuture<'static, ()> {
    info!("New power.Watcher client connection");
    let watcher = Watcher::new(state);
    async move {
        let result = watcher.run(stream).await;
        info!("Watcher handler for FIDL client finished with result: {:?}", result);
    }
    .boxed()
}

pub async fn run_services(
    mut fs: ServiceFs<ServiceObj<'_, Service>>,
    state: Arc<PeripheralState>,
) -> Result<(), Error> {
    let _ = fs.dir("svc").add_fidl_service(Service::Reporter).add_fidl_service(Service::Watcher);
    let _ = fs.take_and_serve_directory_handle().context("Failed to serve ServiceFs directory")?;
    fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, move |connection| match connection {
        Service::Reporter(stream) => handle_reporter_client_connection(stream, state.clone()),
        Service::Watcher(stream) => handle_watcher_client_connection(stream, state.clone()),
    })
    .await;
    Ok(())
}
