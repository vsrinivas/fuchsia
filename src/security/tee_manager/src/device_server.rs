// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::provider_server::ProviderServer,
    anyhow::{Context as _, Error},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_hardware_tee::DeviceConnectorProxy,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    std::path::PathBuf,
};

const STORAGE_DIR: &str = "/data";

/// Serves a fuchsia.tee.Device protocol request by passing it through the TeeDeviceConnection.
pub async fn serve_passthrough(
    device_connector: DeviceConnectorProxy,
    channel: zx::Channel,
) -> Result<(), Error> {
    // Create a ProviderServer to support the TEE driver
    let provider = ProviderServer::try_new(PathBuf::new().join(STORAGE_DIR))?;
    let (zx_provider_server_end, zx_provider_client_end) =
        zx::Channel::create().context("Could not create Provider channel pair")?;

    let provider_server_chan = fasync::Channel::from_channel(zx_provider_server_end)?;

    device_connector
        .connect_tee(Some(ClientEnd::new(zx_provider_client_end)), ServerEnd::new(channel))
        .context("Could not connect to TEE over DeviceConnectorProxy")?;

    provider.serve(provider_server_chan).await
}
