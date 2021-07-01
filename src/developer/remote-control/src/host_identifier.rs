// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{Context as _, Result},
    fidl_fuchsia_buildinfo as buildinfo, fidl_fuchsia_developer_remotecontrol as rcs,
    fidl_fuchsia_device as fdevice, fidl_fuchsia_hwinfo as hwinfo,
    fidl_fuchsia_net_interfaces as fnet_interfaces,
    fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext, fuchsia_zircon as zx,
    tracing::*,
};

pub struct HostIdentifier {
    pub(crate) interface_state_proxy: fnet_interfaces::StateProxy,
    pub(crate) name_provider_proxy: fdevice::NameProviderProxy,
    pub(crate) device_info_proxy: hwinfo::DeviceProxy,
    pub(crate) build_info_proxy: buildinfo::ProviderProxy,
    pub(crate) boot_timestamp_nanos: u64,
}

fn connect_to_protocol<S: fidl::endpoints::DiscoverableService>() -> Result<S::Proxy> {
    fuchsia_component::client::connect_to_protocol::<S>().context(S::SERVICE_NAME)
}

impl HostIdentifier {
    pub fn new() -> Result<Self> {
        let interface_state_proxy = connect_to_protocol::<fnet_interfaces::StateMarker>()?;
        let name_provider_proxy = connect_to_protocol::<fdevice::NameProviderMarker>()?;
        let device_info_proxy = connect_to_protocol::<hwinfo::DeviceMarker>()?;
        let build_info_proxy = connect_to_protocol::<buildinfo::ProviderMarker>()?;
        let boot_timestamp_nanos = (fuchsia_runtime::utc_time().into_nanos()
            - zx::Time::get_monotonic().into_nanos()) as u64;
        return Ok(Self {
            interface_state_proxy,
            name_provider_proxy,
            device_info_proxy,
            build_info_proxy,
            boot_timestamp_nanos,
        });
    }

    pub async fn identify(&self) -> Result<rcs::IdentifyHostResponse, rcs::IdentifyHostError> {
        let stream = fnet_interfaces_ext::event_stream_from_state(&self.interface_state_proxy)
            .map_err(|e| {
                error!(%e, "Getting interface watcher failed");
                rcs::IdentifyHostError::ListInterfacesFailed
            })?;
        let ilist = fnet_interfaces_ext::existing(stream, std::collections::HashMap::new())
            .await
            .map_err(|e| {
            error!(%e, "Getting existing interfaces failed");
            rcs::IdentifyHostError::ListInterfacesFailed
        })?;

        let serial_number = self
            .device_info_proxy
            .get_info()
            .await
            .map_err(|e| error!(%e, "DeviceProxy internal err"))
            .ok()
            .and_then(|i| i.serial_number);

        let (product_config, board_config) = self
            .build_info_proxy
            .get_build_info()
            .await
            .map_err(|e| error!(%e, "buildinfo::ProviderProxy internal err"))
            .ok()
            .and_then(|i| Some((i.product_config, i.board_config)))
            .unwrap_or((None, None));

        let addresses = Some(
            ilist
                .into_iter()
                .map(|(_, v): (u64, _)| v)
                .flat_map(
                    |fnet_interfaces_ext::Properties {
                         id: _,
                         name: _,
                         device_class: _,
                         online: _,
                         addresses,
                         has_default_ipv4_route: _,
                         has_default_ipv6_route: _,
                     }| {
                        addresses
                            .into_iter()
                            .map(|fnet_interfaces_ext::Address { addr, valid_until: _ }| addr)
                    },
                )
                .collect(),
        );

        let nodename = match self.name_provider_proxy.get_device_name().await {
            Ok(result) => match result {
                Ok(name) => Some(name),
                Err(err) => {
                    error!(%err, "NameProvider internal error");
                    return Err(rcs::IdentifyHostError::GetDeviceNameFailed);
                }
            },
            Err(err) => {
                error!(%err, "Getting nodename failed");
                return Err(rcs::IdentifyHostError::GetDeviceNameFailed);
            }
        };

        let boot_timestamp_nanos = Some(self.boot_timestamp_nanos);

        Ok(rcs::IdentifyHostResponse {
            nodename,
            addresses,
            serial_number,
            boot_timestamp_nanos,
            product_config,
            board_config,
            ..rcs::IdentifyHostResponse::EMPTY
        })
    }
}
