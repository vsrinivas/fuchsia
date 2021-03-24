// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Result},
    fidl_fuchsia_buildinfo as buildinfo, fidl_fuchsia_developer_remotecontrol as rcs,
    fidl_fuchsia_device as fdevice, fidl_fuchsia_hwinfo as hwinfo, fidl_fuchsia_net as fnet,
    fidl_fuchsia_net_stack as fnetstack, fuchsia_zircon as zx,
    tracing::*,
};

pub struct HostIdentifier {
    pub(crate) netstack_proxy: fnetstack::StackProxy,
    pub(crate) name_provider_proxy: fdevice::NameProviderProxy,
    pub(crate) device_info_proxy: hwinfo::DeviceProxy,
    pub(crate) build_info_proxy: buildinfo::ProviderProxy,
    pub(crate) boot_timestamp_nanos: u64,
}

impl HostIdentifier {
    pub fn new() -> Result<Self> {
        let netstack_proxy =
            fuchsia_component::client::connect_to_service::<fnetstack::StackMarker>()
                .map_err(|s| format_err!("Failed to connect to NetStack service: {}", s))?;
        let name_provider_proxy =
            fuchsia_component::client::connect_to_service::<fdevice::NameProviderMarker>()
                .map_err(|s| format_err!("Failed to connect to NameProviderService: {}", s))?;
        let device_info_proxy =
            fuchsia_component::client::connect_to_service::<hwinfo::DeviceMarker>()
                .map_err(|s| format_err!("Failed to connect to DeviceProxy: {}", s))?;
        let build_info_proxy =
            fuchsia_component::client::connect_to_service::<buildinfo::ProviderMarker>()
                .map_err(|s| format_err!("Failed to connect to BoardProxy: {}", s))?;
        let boot_timestamp_nanos = (fuchsia_runtime::utc_time().into_nanos()
            - zx::Time::get_monotonic().into_nanos()) as u64;
        return Ok(Self {
            netstack_proxy,
            name_provider_proxy,
            device_info_proxy,
            build_info_proxy,
            boot_timestamp_nanos,
        });
    }

    pub async fn identify(&self) -> Result<rcs::IdentifyHostResponse, rcs::IdentifyHostError> {
        let mut ilist = self.netstack_proxy.list_interfaces().await.map_err(|e| {
            error!(%e, "Getting interface list failed");
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
                .iter_mut()
                .flat_map(|int| int.properties.addresses.drain(..))
                .collect::<Vec<fnet::Subnet>>(),
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
