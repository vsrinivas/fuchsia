// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_component_tests::{ShutdownerRequest, ShutdownerRequestStream},
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
};

async fn run_server(stream: ShutdownerRequestStream) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                ShutdownerRequest::Shutdown { .. } => {
                    log::info!("Received request to shutdown. Exiting.");
                    std::process::exit(0);
                }
            }
        })
        .await
}

enum IncomingService {
    Shutdowner(ShutdownerRequestStream),
}

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Shutdowner);
    fs.take_and_serve_directory_handle()?;
    log::info!("Listening for incoming connections...");
    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Shutdowner(stream)| {
        run_server(stream).unwrap_or_else(|e| log::error!("{:?}", e))
    })
    .await;

    Ok(())
}
