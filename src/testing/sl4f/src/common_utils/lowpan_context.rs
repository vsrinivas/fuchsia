// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_factory_lowpan::{FactoryDeviceMarker, FactoryDeviceProxy, FactoryLookupMarker};
use fidl_fuchsia_lowpan_device::{
    DeviceExtraMarker, DeviceExtraProxy, DeviceMarker, DeviceProxy, LookupMarker, LookupProxy,
    Protocols,
};
use fidl_fuchsia_lowpan_test::{DeviceTestMarker, DeviceTestProxy};
use fuchsia_component::client::connect_to_service;
use futures::FutureExt;

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
        let lookup = connect_to_service::<LookupMarker>()
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
        let lookup = &self.lookup;

        let (client, server) = create_endpoints::<DeviceMarker>()?;

        lookup
            .lookup_device(
                &self.device_name,
                Protocols {
                    device: Some(server),
                    device_extra: None,
                    device_test: None,
                    ..Protocols::empty()
                },
            )
            .map(|x| match x {
                Ok(Ok(())) => Ok(()),
                Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
                Err(x) => Err(x.into()),
            })
            .await?;

        client.into_proxy().context("into_proxy() failed")
    }

    /// Returns the default FactoryDeviceProxy.
    pub async fn get_default_device_factory(&self) -> Result<FactoryDeviceProxy, Error> {
        let lookup = connect_to_service::<FactoryLookupMarker>()
            .context("Failed to connect to Lowpan FactoryLookup service")?;

        let (client, server) = create_endpoints::<FactoryDeviceMarker>()?;

        lookup
            .lookup(&self.device_name, server)
            .map(|x| match x {
                Ok(Ok(())) => Ok(()),
                Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
                Err(x) => Err(x.into()),
            })
            .await
            .context(format!("Unable to find {:?}", &self.device_name))?;

        client.into_proxy().context("into_proxy() failed")
    }

    /// Returns the default DeviceProxy, DeviceExtraProxy and DeviceTestProxy.
    pub async fn get_default_device_proxies(
        &self,
    ) -> Result<(DeviceProxy, DeviceExtraProxy, DeviceTestProxy), Error> {
        let lookup = &self.lookup;

        let (client, server) = create_endpoints::<DeviceMarker>()?;
        let (client_extra, server_extra) = create_endpoints::<DeviceExtraMarker>()?;
        let (client_test, server_test) = create_endpoints::<DeviceTestMarker>()?;

        lookup
            .lookup_device(
                &self.device_name,
                Protocols {
                    device: Some(server),
                    device_extra: Some(server_extra),
                    device_test: Some(server_test),
                    ..Protocols::empty()
                },
            )
            .map(|x| match x {
                Ok(Ok(())) => Ok(()),
                Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
                Err(x) => Err(x.into()),
            })
            .await?;
        //.context(format!("Unable to find {:?}", &self.device_name))?;

        Ok((
            client.into_proxy().context("into_proxy() failed")?,
            client_extra.into_proxy().context("into_proxy() failed")?,
            client_test.into_proxy().context("into_proxy() failed")?,
        ))
    }
}
