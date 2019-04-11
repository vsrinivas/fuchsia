// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]
#![recursion_limit="256"]

use {
    failure::{Error, ResultExt},
    fdio,
    fidl::endpoints::{RequestStream, DiscoverableService},
    fidl_fuchsia_hardware_vsock::DeviceMarker,
    fidl_fuchsia_vsock::{ConnectorMarker, ConnectorRequestStream},
    fuchsia_app::server::ServicesServer,
    fuchsia_async as fasync,
    fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::TryFutureExt,
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
        await!(service::Vsock::new(dev)).context("Failed to initialize vsock service")?;

    let service_clone = service.clone();
    let server = ServicesServer::new()
        .add_service((ConnectorMarker::NAME, move |chan| {
            fasync::spawn(
                service_clone
                    .clone()
                    .run_client_connection(ConnectorRequestStream::from_channel(chan))
                    .unwrap_or_else(|err| fx_log_info!("Error {} during client connection", err)),
            );
        }))
        .start()
        .context("Error starting ServicesServer")?;

    // Spawn the services server with a wrapper to discard the return value.
    fasync::spawn(
        async {
            if let Err(err) = await!(server) {
                fx_log_err!("Services server failed with {}", err);
            }
        },
    );
    // Run the event loop until completion. The event loop only terminates
    // with an error.
    await!(event_loop)?;
    Ok(())
}
