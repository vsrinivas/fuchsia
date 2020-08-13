// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Display;

use fidl_fuchsia_device as fdev;
use fidl_fuchsia_hardware_ethernet as feth;
use fidl_fuchsia_hardware_ethernet_ext as feth_ext;
use fidl_fuchsia_hardware_network as fhwnet;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_zircon as zx;

use anyhow::Context as _;
use async_trait::async_trait;

use crate::errors::{self, ContextExt as _};

/// An error when adding a device.
pub(super) enum AddDeviceError {
    AlreadyExists,
    Other(errors::Error),
}

impl From<errors::Error> for AddDeviceError {
    fn from(e: errors::Error) -> AddDeviceError {
        AddDeviceError::Other(e)
    }
}

impl errors::ContextExt for AddDeviceError {
    fn context<C>(self, context: C) -> AddDeviceError
    where
        C: Display + Send + Sync + 'static,
    {
        match self {
            AddDeviceError::AlreadyExists => AddDeviceError::AlreadyExists,
            AddDeviceError::Other(e) => AddDeviceError::Other(e.context(context)),
        }
    }

    fn with_context<C, F>(self, f: F) -> AddDeviceError
    where
        C: Display + Send + Sync + 'static,
        F: FnOnce() -> C,
    {
        match self {
            AddDeviceError::AlreadyExists => AddDeviceError::AlreadyExists,
            AddDeviceError::Other(e) => AddDeviceError::Other(e.with_context(f)),
        }
    }
}

/// An abstraction over a [`Device`]'s info.
pub(super) trait DeviceInfo {
    /// Is the device a WLAN device?
    fn is_wlan(&self) -> bool;

    /// Is the device a physical device?
    fn is_physical(&self) -> bool;
}

impl DeviceInfo for feth_ext::EthernetInfo {
    fn is_wlan(&self) -> bool {
        feth_ext::is_wlan(self.features)
    }

    fn is_physical(&self) -> bool {
        feth_ext::is_physical(self.features)
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

    /// The type returned by [`get_topo_path_and_device`].
    type DeviceInstance;

    /// Returns the topological path for a device located at `filepath` and an instance
    /// of the device.
    ///
    /// It is expected that the node at `filepath` implements `fuchsia.device/Controller`
    /// and `Self::ServiceMarker`.
    async fn get_topo_path_and_device(
        filepath: &std::path::PathBuf,
    ) -> Result<(String, Self::DeviceInstance), errors::Error>;

    /// Get the device's information and MAC address.
    async fn device_info_and_mac(
        device_instance: &Self::DeviceInstance,
    ) -> Result<(Self::DeviceInfo, feth_ext::MacAddress), errors::Error>;

    /// Returns an [`EthernetInfo`] representation of the device.
    ///
    /// [`EthernetInfo`]: feth_ext::EthernetInfo
    fn eth_device_info(
        info: Self::DeviceInfo,
        mac: feth_ext::MacAddress,
        features: feth::Features,
    ) -> feth_ext::EthernetInfo;

    /// Adds the device to the netstack.
    async fn add_to_stack(
        netcfg: &mut super::NetCfg<'_>,
        topological_path: String,
        config: &mut fnetstack::InterfaceConfig,
        device_instance: Self::DeviceInstance,
    ) -> Result<u64, AddDeviceError>;
}

/// An implementation of [`Device`] for ethernet devices.
pub(super) enum EthernetDevice {}

#[async_trait]
impl Device for EthernetDevice {
    const NAME: &'static str = "ethdev";
    const PATH: &'static str = "class/ethernet";
    type ServiceMarker = feth::DeviceMarker;
    type DeviceInfo = feth_ext::EthernetInfo;
    type DeviceInstance = feth::DeviceProxy;

    async fn get_topo_path_and_device(
        filepath: &std::path::PathBuf,
    ) -> Result<(String, feth::DeviceProxy), errors::Error> {
        get_topo_path_and_device::<feth::DeviceMarker>(filepath).await
    }

    async fn device_info_and_mac(
        device_instance: &feth::DeviceProxy,
    ) -> Result<(feth_ext::EthernetInfo, feth_ext::MacAddress), errors::Error> {
        let info: feth_ext::EthernetInfo = device_instance
            .get_info()
            .await
            .map(Into::into)
            .context("error getting device info for ethdev")
            .map_err(errors::Error::NonFatal)?;
        let mac_addr = info.mac;
        Ok((info, mac_addr))
    }

    fn eth_device_info(
        info: feth_ext::EthernetInfo,
        _mac: feth_ext::MacAddress,
        _features: feth::Features,
    ) -> feth_ext::EthernetInfo {
        info
    }

    async fn add_to_stack(
        netcfg: &mut super::NetCfg<'_>,
        topological_path: String,
        config: &mut fnetstack::InterfaceConfig,
        device_instance: feth::DeviceProxy,
    ) -> Result<u64, AddDeviceError> {
        let client = device_instance
            .into_channel()
            .map_err(|_: feth::DeviceProxy| {
                errors::Error::Fatal(anyhow::anyhow!("failed to convert device proxy into channel"))
            })?
            .into_zx_channel();

        let res = netcfg
            .netstack
            .add_ethernet_device(
                &topological_path,
                config,
                fidl::endpoints::ClientEnd::<feth::DeviceMarker>::new(client),
            )
            .await
            .context("error sending add_ethernet_device request")
            .map_err(errors::Error::Fatal)?
            .map_err(zx::Status::from_raw)
            .map(Into::into);

        if res == Err(zx::Status::ALREADY_EXISTS) {
            Err(AddDeviceError::AlreadyExists)
        } else {
            res.context("error adding ethernet device")
                .map_err(errors::Error::NonFatal)
                .map_err(AddDeviceError::Other)
        }
    }
}

/// An implementation of [`Device`] for network devices.
pub(super) enum NetworkDevice {}

/// An instance of a network device.
pub(super) struct NetworkDeviceInstance {
    device: fhwnet::DeviceProxy,
    mac_addressing: fhwnet::MacAddressingProxy,
}

#[async_trait]
impl Device for NetworkDevice {
    const NAME: &'static str = "netdev";
    const PATH: &'static str = "class/network";
    type ServiceMarker = fhwnet::DeviceInstanceMarker;
    type DeviceInfo = fhwnet::Info;
    type DeviceInstance = NetworkDeviceInstance;

    async fn get_topo_path_and_device(
        filepath: &std::path::PathBuf,
    ) -> Result<(String, NetworkDeviceInstance), errors::Error> {
        let (path, device_instance) =
            get_topo_path_and_device::<fhwnet::DeviceInstanceMarker>(filepath)
                .await
                .context("error getting topological path and device instance")?;
        let (device, req) = fidl::endpoints::create_proxy()
            .context("error creating device proxy")
            .map_err(errors::Error::Fatal)?;
        let () = device_instance
            .get_device(req)
            .context("error geting device")
            .map_err(errors::Error::NonFatal)?;
        let (mac_addressing, req) = fidl::endpoints::create_proxy()
            .context("error creating mac addressing proxy")
            .map_err(errors::Error::Fatal)?;
        let () = device_instance
            .get_mac_addressing(req)
            .context("error getting MAC addressing client")
            .map_err(errors::Error::NonFatal)?;

        Ok((path, NetworkDeviceInstance { device, mac_addressing }))
    }

    async fn device_info_and_mac(
        device_instance: &NetworkDeviceInstance,
    ) -> Result<(fhwnet::Info, feth_ext::MacAddress), errors::Error> {
        let NetworkDeviceInstance { device, mac_addressing } = device_instance;
        let info = device
            .get_info()
            .await
            .context("error getting netdev info")
            .map_err(errors::Error::NonFatal)?;
        let mac_addr = feth_ext::MacAddress {
            octets: mac_addressing
                .get_unicast_address()
                .await
                .context("error getting unicast MAC address")
                .map_err(errors::Error::NonFatal)?
                .octets,
        };

        Ok((info, mac_addr))
    }

    fn eth_device_info(
        _info: fhwnet::Info,
        mac: feth_ext::MacAddress,
        features: feth::Features,
    ) -> feth_ext::EthernetInfo {
        feth_ext::EthernetInfo { features, mtu: 0, mac }
    }

    async fn add_to_stack(
        netcfg: &mut super::NetCfg<'_>,
        topological_path: String,
        config: &mut fnetstack::InterfaceConfig,
        device_instance: NetworkDeviceInstance,
    ) -> Result<u64, AddDeviceError> {
        let NetworkDeviceInstance { device, mac_addressing } = device_instance;

        netcfg
            .stack
            .add_interface(
                fnet_stack::InterfaceConfig {
                    name: Some(config.name.clone()),
                    topopath: Some(topological_path.clone()),
                    metric: Some(config.metric),
                },
                &mut fnet_stack::DeviceDefinition::Ethernet(fnet_stack::EthernetDeviceDefinition {
                    network_device: fidl::endpoints::ClientEnd::new(
                        device
                            .into_channel()
                            .map_err(|_: fhwnet::DeviceProxy| {
                                errors::Error::Fatal(anyhow::anyhow!(
                                    "failed to retrieve network device ClientEnd"
                                ))
                            })?
                            .into(),
                    ),
                    mac: fidl::endpoints::ClientEnd::new(
                        mac_addressing
                            .into_channel()
                            .map_err(|_: fhwnet::MacAddressingProxy| {
                                errors::Error::Fatal(anyhow::anyhow!(
                                    "failed to retrieve mac addressing ClientEnd"
                                ))
                            })?
                            .into(),
                    ),
                }),
            )
            .await
            .context("error sending stack add interface request")
            .map_err(errors::Error::Fatal)?
            .map_err(|e: fnet_stack::Error| {
                if e == fnet_stack::Error::AlreadyExists {
                    AddDeviceError::AlreadyExists
                } else {
                    AddDeviceError::Other(errors::Error::NonFatal(anyhow::anyhow!(
                        "error adding netdev interface: {:?}",
                        e
                    )))
                }
            })
    }
}

/// Returns the topological path for a device located at `filepath` and a proxy to `S`.
///
/// It is expected that the node at `filepath` implements `fuchsia.device/Controller`
/// and `S`.
async fn get_topo_path_and_device<S: fidl::endpoints::ServiceMarker>(
    filepath: &std::path::PathBuf,
) -> Result<(String, S::Proxy), errors::Error> {
    let filepath = filepath
        .to_str()
        .ok_or_else(|| anyhow::anyhow!("failed to convert {} to str", filepath.display()))
        .map_err(errors::Error::NonFatal)?;

    // Get the topological path using `fuchsia.device/Controller`.
    let (controller, req) = fidl::endpoints::create_proxy::<fdev::ControllerMarker>()
        .context("error creating fuchsia.device.Controller proxy")
        .map_err(errors::Error::Fatal)?;
    fdio::service_connect(filepath, req.into_channel().into())
        .with_context(|| format!("error calling fdio::service_connect({})", filepath))
        .map_err(errors::Error::NonFatal)?;
    let path = controller
        .get_topological_path()
        .await
        .context("error sending get topological path request")
        .map_err(errors::Error::NonFatal)?
        .map_err(zx::Status::from_raw)
        .context("error getting topological path")
        .map_err(errors::Error::NonFatal)?;

    // The same channel is expeceted to implement `S`.
    let ch = controller
        .into_channel()
        .map_err(|_: fdev::ControllerProxy| anyhow::anyhow!("failed to get controller's channel"))
        .map_err(errors::Error::Fatal)?
        .into_zx_channel();
    let device = fidl::endpoints::ClientEnd::<S>::new(ch)
        .into_proxy()
        .context("error getting client end proxy")
        .map_err(errors::Error::Fatal)?;

    Ok((path, device))
}
