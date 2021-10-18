// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_bluetooth_deviceid::DeviceIdentificationRequestStream;
use fuchsia_component::server::{ServiceFs, ServiceObj};
use futures::{channel::mpsc::Sender, FutureExt, SinkExt, StreamExt};
use tracing::warn;

/// The maximum number of fidl service client connections that will be serviced concurrently.
const MAX_CONCURRENT_CONNECTIONS: usize = 10;

/// All FIDL services that are exposed by this component's ServiceFs.
pub enum Services {
    DeviceId(DeviceIdentificationRequestStream),
}

pub async fn run_services(
    mut fs: ServiceFs<ServiceObj<'_, Services>>,
    stream_sender: Sender<DeviceIdentificationRequestStream>,
) -> Result<(), Error> {
    let _ = fs.dir("svc").add_fidl_service(Services::DeviceId);
    let _ = fs.take_and_serve_directory_handle().context("Failed to serve ServiceFs directory")?;
    fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, move |connection| match connection {
        Services::DeviceId(stream) => {
            let mut sender = stream_sender.clone();
            async move {
                if let Err(e) = sender.send(stream).await.context("component main loop halted") {
                    warn!("Couldn't relay client connection to main loop: {:?}", e);
                }
            }
            .boxed()
        }
    })
    .await;
    Ok(())
}
