// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use {
    anyhow::{Context as _, Error},
    fdio,
    fidl_fuchsia_hardware_vsock::DeviceMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_info},
    fuchsia_zircon as zx,
    futures::{StreamExt, TryFutureExt},
};

use vsock_service_lib as service;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init().expect("Unable to initialize syslog");
    fx_log_info!("Starting vsock service");

    let (client, device) = zx::Channel::create()?;
    fdio::service_connect("/dev/class/vsock/000", device)?;
    let dev = fidl::endpoints::ClientEnd::<DeviceMarker>::new(client)
        .into_proxy()
        .context("Failed to make channel")?;

    let (service, event_loop) =
        service::Vsock::new(dev).await.context("Failed to initialize vsock service")?;

    let service_clone = service.clone();
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn(
            service_clone
                .clone()
                .run_client_connection(stream)
                .unwrap_or_else(|err| fx_log_info!("Error {} during client connection", err)),
        );
    });
    fs.take_and_serve_directory_handle()?;

    // Spawn the services server with a wrapper to discard the return value.
    fasync::spawn(fs.collect());

    // Run the event loop until completion. The event loop only terminates
    // with an error.
    event_loop.await?;
    Ok(())
}
