// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{device::TeeDeviceConnection, service_provider_server::ServiceProviderServer},
    failure::{Error, ResultExt},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::{
        future::{abortable, Aborted},
        prelude::*,
    },
    std::path::PathBuf,
};

const STORAGE_DIR: &str = "/data";

/// Serves a fuchsia.tee.Device protocol request by passing it through the TeeDeviceConnection.
pub async fn serve_passthrough(
    dev_connection: TeeDeviceConnection,
    channel: zx::Channel,
) -> Result<(), Error> {
    // Create a ServiceProviderServer to support the TEE driver
    let service_provider = ServiceProviderServer::try_new(PathBuf::new().join(STORAGE_DIR))?;
    let (zx_provider_server_end, zx_provider_client_end) =
        zx::Channel::create().context("Could not create ServiceProvider channel pair")?;

    // Make the ServiceProviderServer abortable on the TeeDeviceConnection dying
    let (service_provider_server_fut, abort_handle) = abortable(
        service_provider
            .serve(fasync::Channel::from_channel(zx_provider_server_end)?)
            .unwrap_or_else(|e| fx_log_err!("{:?}", e)),
    );

    await!(dev_connection.register_abort_handle_on_closed(abort_handle));
    fasync::spawn(service_provider_server_fut.unwrap_or_else(|Aborted| ()));

    dev_connection
        .connector_proxy
        .connect_tee(Some(ClientEnd::new(zx_provider_client_end)), ServerEnd::new(channel))
        .context("Could not connect to TEE over DeviceConnectorProxy")?;

    Ok(())
}
