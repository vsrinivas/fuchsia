// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_factory_lowpan::{FactoryDeviceMarker, FactoryDeviceProxy, FactoryLookupMarker};
use fidl_fuchsia_lowpan::{LookupMarker, LookupProxy};
use fidl_fuchsia_lowpan_device::{
    DeviceConnectorMarker, DeviceExtraConnectorMarker, DeviceExtraMarker, DeviceExtraProxy,
    DeviceMarker, DeviceProxy,
};
use fidl_fuchsia_lowpan_test::{DeviceTestConnectorMarker, DeviceTestMarker, DeviceTestProxy};
use fuchsia_component::client::connect_to_protocol;

/// This struct contains all of the transient state that can
/// be kept between invocations of commands when `lowpanctl` is
/// invoked in interactive mode. For single command execution
/// it is set up once and then discarded.
pub struct LowpanContext {
    pub lookup: LookupProxy,
    pub device_name: String,
}

impl LowpanContext {
    const DEFAULT_DEVICE_NAME: &'static str = "lowpan0";

    pub fn new(device_name: Option<String>) -> Result<LowpanContext, Error> {
        let lookup = connect_to_protocol::<LookupMarker>()
            .context("Failed to connect to Lowpan Lookup service")?;

        Ok(LowpanContext {
            lookup,
            device_name: device_name
                .clone()
                .unwrap_or(LowpanContext::DEFAULT_DEVICE_NAME.to_string()),
        })
    }

    /// Returns the default DeviceProxy.
    pub async fn get_default_device(&self) -> Result<DeviceProxy, Error> {
        let (client, server) = create_endpoints::<DeviceMarker>()?;

        connect_to_protocol::<DeviceConnectorMarker>()
            .context("Failed to connect to DeviceConnector")?
            .connect(&self.device_name, server)?;

        client.into_proxy().context("into_proxy() failed")
    }

    /// Returns the default FactoryDeviceProxy.
    pub async fn get_default_device_factory(&self) -> Result<FactoryDeviceProxy, Error> {
        let lookup = connect_to_protocol::<FactoryLookupMarker>()
            .context("Failed to connect to Lowpan FactoryLookup service")?;

        let (client, server) = create_endpoints::<FactoryDeviceMarker>()?;

        lookup.lookup(&self.device_name, server)?;

        client.into_proxy().context("into_proxy() failed")
    }

    /// Returns the default DeviceProxy, DeviceExtraProxy and DeviceTestProxy.
    pub async fn get_default_device_proxies(
        &self,
    ) -> Result<(DeviceProxy, DeviceExtraProxy, DeviceTestProxy), Error> {
        let (client, server) = create_endpoints::<DeviceMarker>()?;
        let (client_extra, server_extra) = create_endpoints::<DeviceExtraMarker>()?;
        let (client_test, server_test) = create_endpoints::<DeviceTestMarker>()?;

        connect_to_protocol::<DeviceConnectorMarker>()
            .context("Failed to connect to DeviceConnector")?
            .connect(&self.device_name, server)?;

        connect_to_protocol::<DeviceExtraConnectorMarker>()
            .context("Failed to connect to DeviceExtraConnector")?
            .connect(&self.device_name, server_extra)?;

        connect_to_protocol::<DeviceTestConnectorMarker>()
            .context("Failed to connect to DeviceTestConnector")?
            .connect(&self.device_name, server_test)?;

        Ok((
            client.into_proxy().context("into_proxy() failed")?,
            client_extra.into_proxy().context("into_proxy() failed")?,
            client_test.into_proxy().context("into_proxy() failed")?,
        ))
    }
}
