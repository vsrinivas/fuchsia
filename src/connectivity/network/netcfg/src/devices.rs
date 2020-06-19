// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hardware_ethernet as feth;
use fidl_fuchsia_hardware_ethernet_ext as feth_ext;
use fidl_fuchsia_hardware_network as fhwnet;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_netstack as fnetstack;

use anyhow::Context as _;
use async_trait::async_trait;

/// An abstraction over a [`Device`]'s info.
pub(super) trait DeviceInfo {
    /// Is the device a WLAN device?
    fn is_wlan(&self) -> bool;

    /// Is the device a physical device?
    fn is_physical(&self) -> bool;
}

impl DeviceInfo for feth_ext::EthernetInfo {
    fn is_wlan(&self) -> bool {
        self.features.is_wlan()
    }

    fn is_physical(&self) -> bool {
        self.features.is_physical()
    }
}

impl DeviceInfo for fhwnet::Info {
    fn is_wlan(&self) -> bool {
        self.class == fhwnet::DeviceClass::Wlan
    }

    fn is_physical(&self) -> bool {
        // We do not know if a network device is physical or virtual. For now, we consider
        // all network devices as physical. Virtual network devices do not show up on devfs.
        true
    }
}

/// A type of device that may be discovered.
#[async_trait]
pub(super) trait Device {
    /// The name of this device.
    const NAME: &'static str;

    /// The relative path from the root device directory to this `Device`'s class of
    /// devices.
    const PATH: &'static str;

    /// The service this device implements.
    type ServiceMarker: fidl::endpoints::ServiceMarker;

    /// The type returned by [`device_info_and_mac`].
    type DeviceInfo: DeviceInfo;

    /// Get the device's information and MAC address.
    async fn device_info_and_mac(
        device_instance: &<Self::ServiceMarker as fidl::endpoints::ServiceMarker>::Proxy,
    ) -> Result<(Self::DeviceInfo, feth_ext::MacAddress), anyhow::Error>;

    /// Returns an [`EthernetInfo`] representation of the device.
    ///
    /// [`EthernetInfo`]: feth_ext::EthernetInfo
    fn eth_device_info(
        info: Self::DeviceInfo,
        mac: feth_ext::MacAddress,
        features: feth_ext::EthernetFeatures,
    ) -> feth_ext::EthernetInfo;

    /// Adds the device to the netstack.
    async fn add_to_stack(
        netcfg: &mut super::NetCfg<'_>,
        topological_path: String,
        config: &mut fnetstack::InterfaceConfig,
        device_instance: <Self::ServiceMarker as fidl::endpoints::ServiceMarker>::Proxy,
    ) -> Result<u64, anyhow::Error>;
}

/// An implementation of [`Device`] for ethernet devices.
pub(super) enum EthernetDevice {}

#[async_trait]
impl Device for EthernetDevice {
    const NAME: &'static str = "ethdev";
    const PATH: &'static str = "class/ethernet";
    type ServiceMarker = feth::DeviceMarker;
    type DeviceInfo = feth_ext::EthernetInfo;

    async fn device_info_and_mac(
        device_instance: &feth::DeviceProxy,
    ) -> Result<(feth_ext::EthernetInfo, feth_ext::MacAddress), anyhow::Error> {
        let info: feth_ext::EthernetInfo = device_instance
            .get_info()
            .await
            .map(Into::into)
            .context("getting device info for ethdev")?;
        let mac_addr = info.mac;
        Ok((info, mac_addr))
    }

    fn eth_device_info(
        info: feth_ext::EthernetInfo,
        _mac: feth_ext::MacAddress,
        _features: feth_ext::EthernetFeatures,
    ) -> feth_ext::EthernetInfo {
        info
    }

    async fn add_to_stack(
        netcfg: &mut super::NetCfg<'_>,
        topological_path: String,
        config: &mut fnetstack::InterfaceConfig,
        device_instance: feth::DeviceProxy,
    ) -> Result<u64, anyhow::Error> {
        let client = device_instance
            .into_channel()
            .map_err(|_: feth::DeviceProxy| {
                anyhow::anyhow!("failed to convert device proxy into channel",)
            })?
            .into_zx_channel();

        let nic_id = netcfg
            .netstack
            .add_ethernet_device(
                &topological_path,
                config,
                fidl::endpoints::ClientEnd::<feth::DeviceMarker>::new(client),
            )
            .await
            .context("adding ethdev interface")?;

        Ok(nic_id.into())
    }
}

/// An implementation of [`Device`] for network devices.
pub(super) enum NetworkDevice {}

#[async_trait]
impl Device for NetworkDevice {
    const NAME: &'static str = "netdev";
    const PATH: &'static str = "class/network";
    type ServiceMarker = fhwnet::DeviceInstanceMarker;
    type DeviceInfo = fhwnet::Info;

    async fn device_info_and_mac(
        device_instance: &fhwnet::DeviceInstanceProxy,
    ) -> Result<(fhwnet::Info, feth_ext::MacAddress), anyhow::Error> {
        let (device, req) = fidl::endpoints::create_proxy().context("create device proxy")?;
        let () = device_instance.get_device(req).context("get device")?;
        let (mac_addressing, req) =
            fidl::endpoints::create_proxy().context("create mac addressing proxy")?;
        let () = device_instance.get_mac_addressing(req).context("get mac address")?;
        let info = device.get_info().await.context("get netdev info")?;
        let mac_addr = feth_ext::MacAddress {
            octets: mac_addressing
                .get_unicast_address()
                .await
                .context("get unicast address")?
                .octets,
        };
        Ok((info, mac_addr))
    }

    fn eth_device_info(
        _info: fhwnet::Info,
        mac: feth_ext::MacAddress,
        features: feth_ext::EthernetFeatures,
    ) -> feth_ext::EthernetInfo {
        feth_ext::EthernetInfo { features, mtu: 0, mac }
    }

    async fn add_to_stack(
        netcfg: &mut super::NetCfg<'_>,
        topological_path: String,
        config: &mut fnetstack::InterfaceConfig,
        device_instance: fhwnet::DeviceInstanceProxy,
    ) -> Result<u64, anyhow::Error> {
        let (device, req) = fidl::endpoints::create_proxy().context("create device proxy")?;
        let () = device_instance.get_device(req).context("get device")?;
        let (mac_addressing, req) =
            fidl::endpoints::create_proxy().context("create mac addressing proxy")?;
        let () = device_instance.get_mac_addressing(req).context("get mac address")?;

        let nic_id = netcfg
            .stack
            .add_interface(
                fnet_stack::InterfaceConfig {
                    name: Some(config.name.clone()),
                    topopath: Some(topological_path),
                    metric: Some(config.metric),
                },
                &mut fnet_stack::DeviceDefinition::Ethernet(fnet_stack::EthernetDeviceDefinition {
                    network_device: fidl::endpoints::ClientEnd::new(
                        device
                            .into_channel()
                            .map_err(|_: fhwnet::DeviceProxy| {
                                anyhow::anyhow!("failed to retrieve network device ClientEnd")
                            })?
                            .into(),
                    ),
                    mac: fidl::endpoints::ClientEnd::new(
                        mac_addressing
                            .into_channel()
                            .map_err(|_: fhwnet::MacAddressingProxy| {
                                anyhow::anyhow!("failed to retrieve mac addressing ClientEnd")
                            })?
                            .into(),
                    ),
                }),
            )
            .await
            .context("stack add interface request")?
            .map_err(|e: fnet_stack::Error| {
                anyhow::anyhow!("error adding netdev interface: {:?}", e,)
            })?;

        Ok(nic_id)
    }
}
