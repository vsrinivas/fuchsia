// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::invocation::*;
use crate::prelude::*;
use fidl_fuchsia_factory_lowpan::*;
use fidl_fuchsia_lowpan::*;
use fidl_fuchsia_lowpan_device::*;
use fidl_fuchsia_lowpan_experimental::DeviceConnectorMarker as ExperimentalDeviceConnectorMarker;
use fidl_fuchsia_lowpan_experimental::DeviceExtraConnectorMarker as ExperimentalDeviceExtraConnectorMarker;
use fidl_fuchsia_lowpan_experimental::DeviceExtraMarker as ExperimentalDeviceExtraMarker;
use fidl_fuchsia_lowpan_experimental::DeviceMarker as ExperimentalDeviceMarker;
use fidl_fuchsia_lowpan_experimental::{
    DeviceRouteConnectorMarker, DeviceRouteExtraConnectorMarker, DeviceRouteExtraMarker,
    DeviceRouteMarker, LegacyJoiningConnectorMarker, LegacyJoiningMarker,
};
use fidl_fuchsia_lowpan_test::*;
use fidl_fuchsia_lowpan_thread::*;

/// This struct contains all of the transient state that can
/// be kept between invocations of commands when `lowpanctl` is
/// invoked in interactive mode. For single command execution
/// it is set up once and then discarded.
pub struct LowpanCtlContext {
    pub lookup: DeviceWatcherProxy,
    pub device_name: String,
}

macro_rules! impl_get_protocol_method {
    ($connector_marker:ty, $marker:ty, $method:ident) => {
        pub async fn $method(
            &self,
        ) -> Result<<$marker as fidl::endpoints::ProtocolMarker>::Proxy, Error> {
            let (client, server) = create_endpoints::<$marker>()?;

            connect_to_protocol::<$connector_marker>()
                .context("Failed to connect")?
                .connect(&self.device_name, server)?;

            client.into_proxy().context("into_proxy() failed")
        }
    };
}

impl LowpanCtlContext {
    pub fn from_invocation(args: &LowpanCtlInvocation) -> Result<LowpanCtlContext, Error> {
        let lookup = connect_to_protocol::<DeviceWatcherMarker>()
            .context("Failed to connect to Lowpan Lookup service")?;

        Ok(LowpanCtlContext {
            lookup,
            device_name: args.device_name.clone().unwrap_or("lowpan0".to_string()),
        })
    }

    impl_get_protocol_method!(DeviceConnectorMarker, DeviceMarker, get_default_device);
    impl_get_protocol_method!(
        DeviceExtraConnectorMarker,
        DeviceExtraMarker,
        get_default_device_extra_proxy
    );
    impl_get_protocol_method!(
        ExperimentalDeviceConnectorMarker,
        ExperimentalDeviceMarker,
        get_default_experimental_device
    );
    impl_get_protocol_method!(
        ExperimentalDeviceExtraConnectorMarker,
        ExperimentalDeviceExtraMarker,
        get_default_experimental_device_extra
    );
    impl_get_protocol_method!(
        DeviceRouteConnectorMarker,
        DeviceRouteMarker,
        get_default_device_route_proxy
    );
    impl_get_protocol_method!(
        DeviceRouteExtraConnectorMarker,
        DeviceRouteExtraMarker,
        get_default_device_route_extra_proxy
    );
    impl_get_protocol_method!(EnergyScanConnectorMarker, EnergyScanMarker, get_default_energy_scan);
    impl_get_protocol_method!(
        DeviceTestConnectorMarker,
        DeviceTestMarker,
        get_default_device_test_proxy
    );
    impl_get_protocol_method!(
        DatasetConnectorMarker,
        DatasetMarker,
        get_default_thread_dataset_proxy
    );
    impl_get_protocol_method!(
        LegacyJoiningConnectorMarker,
        LegacyJoiningMarker,
        get_default_legacy_joining_proxy
    );
    impl_get_protocol_method!(CountersConnectorMarker, CountersMarker, get_default_device_counters);

    pub async fn get_default_device_factory(&self) -> Result<FactoryDeviceProxy, Error> {
        let lookup = connect_to_protocol::<FactoryLookupMarker>()
            .context("Failed to connect to Lowpan FactoryLookup service")?;

        let (client, server) = create_endpoints::<FactoryDeviceMarker>()?;

        lookup.lookup(&self.device_name, server)?;

        client.into_proxy().context("into_proxy() failed")
    }
}
