// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::provider_server::ProviderServer,
    anyhow::{Context as _, Error},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_hardware_tee::DeviceConnectorProxy,
    fidl_fuchsia_tee as fuchsia_tee, fuchsia_async as fasync, fuchsia_zircon as zx,
    std::path::PathBuf,
};

const STORAGE_DIR: &str = "/data";

/// Serves a `fuchsia.tee.Application` protocol request by passing it through the
/// `TeeDeviceConnection` and specifying the application UUID.
pub async fn serve_application_passthrough(
    mut uuid: fuchsia_tee::Uuid,
    device_connector: DeviceConnectorProxy,
    channel: zx::Channel,
) -> Result<(), Error> {
    // Create a ProviderServer to support the TEE driver
    let provider = ProviderServer::try_new(PathBuf::new().join(STORAGE_DIR))?;
    let (zx_provider_server_end, zx_provider_client_end) =
        zx::Channel::create().context("Could not create Provider channel pair")?;

    let provider_server_chan = fasync::Channel::from_channel(zx_provider_server_end)?;

    device_connector
        .connect_to_application(
            &mut uuid,
            Some(ClientEnd::new(zx_provider_client_end)),
            ServerEnd::new(channel),
        )
        .context("Could not connect to fuchsia.tee.Application over DeviceConnectorProxy")?;

    provider.serve(provider_server_chan).await
}

/// Serves a `fuchsia.tee.DeviceInfo` protocol request by passing it through the
/// `TeeDeviceConnection`.
pub async fn serve_device_info_passthrough(
    device_connector: DeviceConnectorProxy,
    channel: zx::Channel,
) -> Result<(), Error> {
    device_connector
        .connect_to_device_info(ServerEnd::new(channel))
        .context("Could not connect to fuchsia.tee.DeviceInfo over DeviceConnectorProxy")?;

    Ok(())
}
