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
    anyhow::{Context as _, Error, Result},
    fidl_fuchsia_fuzzer as fuzz, fidl_fuchsia_test_manager as test_manager,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    futures::channel::mpsc,
    futures::{try_join, SinkExt, StreamExt, TryStreamExt},
    tracing::warn,
};

enum IncomingService {
    FuzzManager(fuzz::ManagerRequestStream),
}

struct RunBuilderEndpoint {}

impl manager::FidlEndpoint<test_manager::RunBuilderMarker> for RunBuilderEndpoint {
    fn create_proxy(&self) -> Result<test_manager::RunBuilderProxy, Error> {
        connect_to_protocol::<test_manager::RunBuilderMarker>()
    }
}

#[fuchsia::main(logging = true)]
async fn main() -> Result<()> {
    let (sender, receiver) = mpsc::unbounded::<fuzz::ManagerRequest>();
    let registry = connect_to_protocol::<fuzz::RegistryMarker>()
        .context("failed to connect to fuchsia.fuzzing.Registry")?;
    let run_builder = RunBuilderEndpoint {};
    let manager = Manager::new(registry, run_builder);
    let results = try_join!(multiplex_requests(sender), manager.serve(receiver));
    results.and(Ok(()))
}

// Concurrent calls to `connect` and `stop` can become complicated, especially given that the state
// of the fuzzer is replicated between the manager and registry. Moreover, fuzzing typically
// involves a small number of latency-tolerant clients perform testing. As a result, the simplest
// solution is to have the |fuzz::Manager| service multiple clients, but handle requests
// sequentially by multiplexing them into a single stream.
async fn multiplex_requests(sender: mpsc::UnboundedSender<fuzz::ManagerRequest>) -> Result<()> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::FuzzManager);
    fs.take_and_serve_directory_handle().context("failed to take and serve directory")?;
    const MAX_CONCURRENT: usize = 100;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::FuzzManager(stream)| async {
        let sender = sender.clone();
        let result = stream.map_err(Error::msg).forward(sender.sink_map_err(Error::msg)).await;
        if let Err(e) = result {
            warn!("failed to forward fuzz-manager request: {:?}", e);
        }
    })
    .await;
    Ok(())
}
