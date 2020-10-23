// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fidl_test_components::{TriggerRequest, TriggerRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
    log::*,
};

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    /// A request to the fuchsia.sys2.ComponentResolver protocol.
    TriggerProtocol(TriggerRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut service_fs = ServiceFs::new_local();
    service_fs.dir("svc").add_fidl_service(IncomingRequest::TriggerProtocol);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::TriggerProtocol(stream) => match serve_trigger(stream).await {
                    Ok(()) => {}
                    Err(err) => error!("trigger failed: {}", err),
                },
            }
        })
        .await;

    Ok(())
}

async fn serve_trigger(mut stream: TriggerRequestStream) -> Result<(), Error> {
    while let Some(TriggerRequest::Run { responder }) =
        stream.try_next().await.context("failed to read request from stream")?
    {
        responder.send("Triggered").context("failed to send trigger response")?;
    }
    Ok(())
}
