// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::invocation::*;
use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_factory_lowpan::{FactoryDeviceMarker, FactoryDeviceProxy, FactoryLookupMarker};
use fidl_fuchsia_lowpan_device::{
    CountersMarker, CountersProxy, DeviceExtraMarker, DeviceExtraProxy, DeviceMarker, DeviceProxy,
    DeviceRouteExtraMarker, DeviceRouteExtraProxy, DeviceRouteMarker, DeviceRouteProxy,
    LookupMarker, LookupProxy, Protocols,
};
use fidl_fuchsia_lowpan_test::{DeviceTestMarker, DeviceTestProxy};
use fidl_fuchsia_lowpan_thread::{LegacyJoiningMarker, LegacyJoiningProxy};
use fuchsia_component::client::connect_to_protocol;
use futures::prelude::*;

/// This struct contains all of the transient state that can
/// be kept between invocations of commands when `lowpanctl` is
/// invoked in interactive mode. For single command execution
/// it is set up once and then discarded.
pub struct LowpanCtlContext {
    pub lookup: LookupProxy,
    pub device_name: String,
}

impl LowpanCtlContext {
    pub fn from_invocation(args: &LowpanCtlInvocation) -> Result<LowpanCtlContext, Error> {
        let lookup = connect_to_protocol::<LookupMarker>()
            .context("Failed to connect to Lowpan Lookup service")?;

        Ok(LowpanCtlContext {
            lookup,
            device_name: args.device_name.clone().unwrap_or("lowpan0".to_string()),
        })
    }

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
                    ..Protocols::EMPTY
                },
            )
            .map(|x| match x {
                Ok(Ok(())) => Ok(()),
                Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
                Err(x) => Err(x.into()),
            })
            .await
            .context(format!("Unable to find {:?}", &self.device_name))?;

        client.into_proxy().context("into_proxy() failed")
    }

    pub async fn get_default_device_factory(&self) -> Result<FactoryDeviceProxy, Error> {
        let lookup = connect_to_protocol::<FactoryLookupMarker>()
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
                    ..Protocols::EMPTY
                },
            )
            .map(|x| match x {
                Ok(Ok(())) => Ok(()),
                Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
                Err(x) => Err(x.into()),
            })
            .await
            .context(format!("Unable to find {:?}", &self.device_name))?;

        Ok((
            client.into_proxy().context("into_proxy() failed")?,
            client_extra.into_proxy().context("into_proxy() failed")?,
            client_test.into_proxy().context("into_proxy() failed")?,
        ))
    }

    pub async fn get_default_device_route_proxy(&self) -> Result<DeviceRouteProxy, Error> {
        let lookup = &self.lookup;

        let (client, server) = create_endpoints::<DeviceRouteMarker>()?;

        lookup
            .lookup_device(
                &self.device_name,
                Protocols { device_route: Some(server), ..Protocols::EMPTY },
            )
            .map(|x| match x {
                Ok(Ok(())) => Ok(()),
                Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
                Err(x) => Err(x.into()),
            })
            .await
            .context(format!("Unable to get device route interface for {:?}", &self.device_name))?;

        client.into_proxy().context("into_proxy() failed")
    }

    pub async fn get_default_device_route_extra_proxy(
        &self,
    ) -> Result<DeviceRouteExtraProxy, Error> {
        let lookup = &self.lookup;

        let (client, server) = create_endpoints::<DeviceRouteExtraMarker>()?;

        lookup
            .lookup_device(
                &self.device_name,
                Protocols { device_route_extra: Some(server), ..Protocols::EMPTY },
            )
            .map(|x| match x {
                Ok(Ok(())) => Ok(()),
                Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
                Err(x) => Err(x.into()),
            })
            .await
            .context(format!(
                "Unable to get device route extra interface for {:?}",
                &self.device_name
            ))?;

        client.into_proxy().context("into_proxy() failed")
    }

    pub async fn get_default_legacy_joining_proxy(&self) -> Result<LegacyJoiningProxy, Error> {
        let lookup = &self.lookup;

        let (client, server) = create_endpoints::<LegacyJoiningMarker>()?;

        lookup
            .lookup_device(
                &self.device_name,
                Protocols { thread_legacy_joining: Some(server), ..Protocols::EMPTY },
            )
            .map(|x| match x {
                Ok(Ok(())) => Ok(()),
                Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
                Err(x) => Err(x.into()),
            })
            .await
            .context(format!(
                "Unable to get legacy joining interface for {:?}",
                &self.device_name
            ))?;

        client.into_proxy().context("into_proxy() failed")
    }

    pub async fn get_default_device_counters(&self) -> Result<CountersProxy, Error> {
        let lookup = &self.lookup;

        let (client, server) = create_endpoints::<CountersMarker>()?;

        lookup
            .lookup_device(
                &self.device_name,
                Protocols {
                    device: None,
                    device_extra: None,
                    device_test: None,
                    device_route: None,
                    device_route_extra: None,
                    counters: Some(server),
                    ..Protocols::EMPTY
                },
            )
            .map(|x| match x {
                Ok(Ok(())) => Ok(()),
                Ok(Err(x)) => Err(format_err!("Service Error: {:?}", x)),
                Err(x) => Err(x.into()),
            })
            .await
            .context(format!("Unable to find {:?}", &self.device_name))?;

        client.into_proxy().context("into_proxy() failed")
    }
}
