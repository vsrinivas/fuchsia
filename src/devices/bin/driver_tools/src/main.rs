// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    driver_tools::args::DriverCommand,
    fidl::endpoints::{self, Proxy},
    fidl_fuchsia_device_manager as fdm, fidl_fuchsia_driver_development as fdd,
    fidl_fuchsia_driver_playground as fdp, fidl_fuchsia_driver_registrar as fdr,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::client,
    std::fs::File,
};

struct DriverConnector {}

impl DriverConnector {
    fn new() -> Self {
        Self {}
    }
}

#[async_trait::async_trait]
impl driver_tools::DriverConnector for DriverConnector {
    async fn get_driver_development_proxy(
        &self,
        select: bool,
    ) -> Result<fdd::DriverDevelopmentProxy> {
        if select {
            anyhow::bail!("The 'driver' tool cannot use the select flag. Please use 'ffx driver' in order to select a component.");
        }
        client::connect_to_protocol::<fdd::DriverDevelopmentMarker>()
            .context("Failed to connect to driver development service")
    }
    async fn get_dev_proxy(&self, select: bool) -> Result<fio::DirectoryProxy> {
        if select {
            anyhow::bail!("The 'driver' tool cannot use the select flag. Please use 'ffx driver' in order to select a component.");
        }
        let raw_dir = File::open("/dev")?;
        let zx_channel = fdio::clone_channel(&raw_dir)?;
        let fasync_channel = fasync::Channel::from_channel(zx_channel)?;
        Ok(fio::DirectoryProxy::from_channel(fasync_channel))
    }
    async fn get_device_watcher_proxy(&self) -> Result<fdm::DeviceWatcherProxy> {
        let (proxy, server) = endpoints::create_proxy::<fdm::DeviceWatcherMarker>()
            .context("Failed to create proxy")?;
        fdio::service_connect("/svc/fuchsia.hardware.usb.DeviceWatcher", server.into_channel())
            .context("Failed to connect to USB service")?;
        Ok(proxy)
    }
    async fn get_driver_registrar_proxy(&self, select: bool) -> Result<fdr::DriverRegistrarProxy> {
        if select {
            anyhow::bail!("The 'driver' tool cannot use the select flag. Please use 'ffx driver' in order to select a component.");
        }
        client::connect_to_protocol::<fdr::DriverRegistrarMarker>()
            .context("Failed to connect to driver registrar service")
    }
    async fn get_tool_runner_proxy(&self, select: bool) -> Result<fdp::ToolRunnerProxy> {
        if select {
            anyhow::bail!("The 'driver' tool cannot use the select flag. Please use 'ffx driver' in order to select a component.");
        }
        client::connect_to_protocol::<fdp::ToolRunnerMarker>()
            .context("Failed to connect to tool runner service")
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let cmd: DriverCommand = argh::from_env();
    driver_tools::driver(cmd, DriverConnector::new()).await
}
