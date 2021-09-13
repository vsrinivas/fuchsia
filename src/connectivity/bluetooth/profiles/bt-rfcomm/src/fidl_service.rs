// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_bluetooth_bredr::ProfileRequestStream;
use fidl_fuchsia_bluetooth_rfcomm_test::RfcommTestRequestStream;
use fuchsia_component::server::{ServiceFs, ServiceObj};
use futures::{channel::mpsc, Future, SinkExt, StreamExt};
use tracing::info;

/// The maximum number of FIDL service client connections that will be serviced concurrently.
const MAX_CONCURRENT_CONNECTIONS: usize = 10;

/// All FIDL services that are exposed by this component's ServiceFs.
pub enum Service {
    /// The `bredr.Profile` protocol.
    Profile(ProfileRequestStream),
    /// The `RfcommTest`protocol.
    RfcommTest(RfcommTestRequestStream),
}

pub fn run_services(
    mut fs: ServiceFs<ServiceObj<'_, Service>>,
    service_sender: mpsc::Sender<Service>,
) -> Result<impl Future<Output = ()> + '_, Error> {
    let _ = fs.dir("svc").add_fidl_service(Service::Profile).add_fidl_service(Service::RfcommTest);
    let _ = fs.take_and_serve_directory_handle().context("Failed to serve ServiceFs directory")?;

    let serve_fut = fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, move |connection| {
        let mut sender = service_sender.clone();
        async move {
            if let Err(e) = sender.send(connection).await {
                info!("Error handling FIDL Service connection: {:?}", e);
            }
        }
    });
    Ok(serve_fut)
}
