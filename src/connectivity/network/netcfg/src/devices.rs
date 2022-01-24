// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Display;

use fidl::endpoints::Proxy;
use fidl_fuchsia_device as fdev;
use fidl_fuchsia_hardware_ethernet as feth;
use fidl_fuchsia_hardware_ethernet_ext as feth_ext;
use fidl_fuchsia_hardware_network as fhwnet;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_zircon as zx;

use anyhow::Context as _;
use async_trait::async_trait;
use futures::StreamExt as _;

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

#[derive(Debug, Clone)]
pub(super) struct DeviceInfo {
    pub(super) device_class: fhwnet::DeviceClass,
    pub(super) mac: Option<fidl_fuchsia_net_ext::MacAddress>,
    pub(super) is_synthetic: bool,
    pub(super) topological_path: String,
}

impl DeviceInfo {
    pub(super) fn interface_type(&self) -> crate::InterfaceType {
        let Self { device_class, mac: _, is_synthetic: _, topological_path: _ } = self;
        match device_class {
            fhwnet::DeviceClass::Wlan | fhwnet::DeviceClass::WlanAp => crate::InterfaceType::Wlan,
            fhwnet::DeviceClass::Ethernet
            | fhwnet::DeviceClass::Virtual
            | fhwnet::DeviceClass::Ppp
            | fhwnet::DeviceClass::Bridge => crate::InterfaceType::Ethernet,
        }
    }

    pub(super) fn is_wlan_ap(&self) -> bool {
        /// The string present in the topological path of a WLAN AP interface.
        const WLAN_AP_TOPO_PATH_CONTAINS: &str = "wlanif-ap";

        let Self { device_class, mac: _, is_synthetic: _, topological_path } = self;
        match device_class {
            fhwnet::DeviceClass::WlanAp => true,
            fhwnet::DeviceClass::Wlan
            | fhwnet::DeviceClass::Ethernet
            | fhwnet::DeviceClass::Virtual
            | fhwnet::DeviceClass::Ppp
            | fhwnet::DeviceClass::Bridge => topological_path.contains(WLAN_AP_TOPO_PATH_CONTAINS),
        }
    }
}

/// A type of device that may be discovered.
#[async_trait]
// TODO(https://fxbug.dev/74532): Delete this trait once we migrate away from
// Ethernet devices.
pub(super) trait Device {
    /// The name of this device.
    const NAME: &'static str;

    /// The relative path from the root device directory to this `Device`'s class of
    /// devices.
    const PATH: &'static str;

    /// The type of an instantiable interface from the device.
    type DeviceInstance: std::fmt::Debug + 'static + Send;

    type InstanceStream: futures::Stream<Item = Result<Self::DeviceInstance, errors::Error>>
        + 'static
        + Send
        + Unpin;
    async fn get_instance_stream(
        installer: &fidl_fuchsia_net_interfaces_admin::InstallerProxy,
        path: &std::path::PathBuf,
    ) -> Result<Self::InstanceStream, errors::Error>;

    /// Get the device's information.
    async fn get_device_info(
        device_instance: &Self::DeviceInstance,
    ) -> Result<DeviceInfo, errors::Error>;

    /// Adds the device to the netstack.
    async fn add_to_stack(
        netcfg: &super::NetCfg<'_>,
        config: crate::InterfaceConfig,
        device_instance: &Self::DeviceInstance,
    ) -> Result<(u64, fidl_fuchsia_net_interfaces_ext::admin::Control), AddDeviceError>;
}

/// An implementation of [`Device`] for ethernet devices.
pub(super) enum EthernetDevice {}

pub(super) struct EthernetInstance {
    device: feth::DeviceProxy,
    topological_path: String,
    file_path: String,
}

impl std::fmt::Debug for EthernetInstance {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let EthernetInstance { device: _, topological_path, file_path } = self;
        write!(
            f,
            "EthernetInstance{{topological_path={}, file_path={}}}",
            topological_path, file_path
        )
    }
}

#[async_trait]
impl Device for EthernetDevice {
    const NAME: &'static str = "ethdev";
    const PATH: &'static str = "/dev/class/ethernet";
    type DeviceInstance = EthernetInstance;

    type InstanceStream =
        futures::stream::Once<futures::future::Ready<Result<Self::DeviceInstance, errors::Error>>>;
    async fn get_instance_stream(
        _installer: &fidl_fuchsia_net_interfaces_admin::InstallerProxy,
        path: &std::path::PathBuf,
    ) -> Result<Self::InstanceStream, errors::Error> {
        let (topological_path, file_path, device) =
            get_topo_path_and_device::<feth::DeviceMarker>(path)
                .await
                .context("error getting topological path and device")?;
        Ok(futures::stream::once(futures::future::ok(EthernetInstance {
            device,
            topological_path,
            file_path,
        })))
    }

    async fn get_device_info(
        device_instance: &self::EthernetInstance,
    ) -> Result<DeviceInfo, errors::Error> {
        let EthernetInstance { device, topological_path, file_path: _ } = device_instance;
        let feth_ext::EthernetInfo { features, mac: feth_ext::MacAddress { octets }, mtu: _ } =
            device
                .get_info()
                .await
                .map(Into::into)
                .context("error getting device info for ethdev")
                .map_err(errors::Error::NonFatal)?;
        let is_synthetic = features.contains(feth::Features::SYNTHETIC);
        let device_class = if features.contains(feth::Features::WLAN) {
            fhwnet::DeviceClass::Wlan
        } else {
            fhwnet::DeviceClass::Ethernet
        };
        Ok(DeviceInfo {
            device_class,
            mac: Some(fidl_fuchsia_net_ext::MacAddress { octets }),
            is_synthetic,
            topological_path: topological_path.clone(),
        })
    }

    async fn add_to_stack(
        netcfg: &super::NetCfg<'_>,
        config: crate::InterfaceConfig,
        device_instance: &Self::DeviceInstance,
    ) -> Result<(u64, fidl_fuchsia_net_interfaces_ext::admin::Control), AddDeviceError> {
        let EthernetInstance { device: _, topological_path, file_path } = device_instance;
        let crate::InterfaceConfig { name, metric } = config;

        // NB: We have to reach out into devfs again to get a new instance of
        // the Ethernet device because:
        // - We can't `Clone` the instance, that's not supported by the FIDL
        // protocol.
        // - We can't hand out our instance, since this method is retried on
        // name collisions.
        let (client_end, server_end) = fidl::endpoints::create_endpoints::<feth::DeviceMarker>()
            .context("create ethdev endpoints")
            .map_err(errors::Error::NonFatal)?;
        let () = fuchsia_component::client::connect_channel_to_protocol_at_path(
            server_end.into_channel(),
            file_path,
        )
        .with_context(|| format!("connect ethdev at {}", file_path))
        .map_err(errors::Error::NonFatal)?;

        // NB: We create control ahead of time to prevent fallible operations
        // after we have installed the device.
        let (control, control_server_end) =
            fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                .context("create Control endpoints")
                .map_err(errors::Error::NonFatal)?;

        let res = netcfg
            .netstack
            .add_ethernet_device(
                topological_path,
                &mut fnetstack::InterfaceConfig { name, filepath: file_path.to_string(), metric },
                client_end,
            )
            .await
            .context("error sending add_ethernet_device request")
            .map_err(errors::Error::Fatal)?
            .map_err(zx::Status::from_raw)
            .map(Into::into);

        if res == Err(zx::Status::ALREADY_EXISTS) {
            return Err(AddDeviceError::AlreadyExists);
        }

        let interface_id =
            res.context("error adding ethernet device").map_err(errors::Error::NonFatal)?;

        let () = netcfg
            .debug
            .get_admin(interface_id, control_server_end)
            .context("calling get_admin")
            .map_err(errors::Error::Fatal)?;

        Ok((interface_id, control))
    }
}

/// An implementation of [`Device`] for network devices.
pub(super) enum NetworkDevice {}

/// An instance of a network device.
pub(super) struct NetworkDeviceInstance {
    port: fhwnet::PortProxy,
    port_id: fhwnet::PortId,
    device_control: fidl_fuchsia_net_interfaces_admin::DeviceControlProxy,
    topological_path: String,
}

impl std::fmt::Debug for NetworkDeviceInstance {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let NetworkDeviceInstance { port: _, port_id, device_control: _, topological_path } = self;
        write!(
            f,
            "NetworkDeviceInstance{{topological_path={}, port={:?}}}",
            topological_path, port_id
        )
    }
}

#[async_trait]
impl Device for NetworkDevice {
    const NAME: &'static str = "netdev";
    const PATH: &'static str = "/dev/class/network";
    type DeviceInstance = NetworkDeviceInstance;

    type InstanceStream =
        futures::stream::BoxStream<'static, Result<Self::DeviceInstance, errors::Error>>;
    async fn get_instance_stream(
        installer: &fidl_fuchsia_net_interfaces_admin::InstallerProxy,
        path: &std::path::PathBuf,
    ) -> Result<Self::InstanceStream, errors::Error> {
        let (topological_path, _file_path, device_instance) =
            get_topo_path_and_device::<fhwnet::DeviceInstanceMarker>(path)
                .await
                .with_context(|| format!("open netdevice at {:?}", path))?;

        let get_device = || {
            let (device, device_server_end) =
                fidl::endpoints::create_endpoints::<fhwnet::DeviceMarker>()
                    .context("create device endpoints")
                    .map_err(errors::Error::NonFatal)?;
            let () = device_instance
                .get_device(device_server_end)
                .context("calling DeviceInstance get_device")
                .map_err(errors::Error::NonFatal)?;
            Ok(device)
        };

        let device = get_device()?
            .into_proxy()
            .context("create device proxy")
            .map_err(errors::Error::Fatal)?;

        let (port_watcher, port_watcher_server_end) =
            fidl::endpoints::create_proxy::<fhwnet::PortWatcherMarker>()
                .context("create port watcher endpoints")
                .map_err(errors::Error::NonFatal)?;
        let () = device
            .get_port_watcher(port_watcher_server_end)
            .context("calling Device get_port_watcher")
            .map_err(errors::Error::NonFatal)?;

        let (device_control, device_control_server_end) = fidl::endpoints::create_proxy::<
            fidl_fuchsia_net_interfaces_admin::DeviceControlMarker,
        >()
        .context("create device control endpoints")
        .map_err(errors::Error::NonFatal)?;

        let device_for_netstack = get_device()?;
        let () = installer
            .install_device(device_for_netstack, device_control_server_end)
            .context("calling Installer install_device")
            // NB: Failing to communicate with installer is a fatal error, that
            // means the Netstack is gone, which we don't tolerate.
            .map_err(errors::Error::Fatal)?;

        Ok(futures::stream::try_unfold(
            (port_watcher, device_control, device, topological_path),
            |(port_watcher, device_control, device, topological_path)| async move {
                loop {
                    let port_event = match port_watcher.watch().await {
                        Ok(port_event) => port_event,
                        Err(err) => {
                            break if err.is_closed() {
                                Ok(None)
                            } else {
                                Err(errors::Error::Fatal(err.into()))
                                    .context("calling PortWatcher watch")
                            };
                        }
                    };
                    match port_event {
                        fhwnet::DevicePortEvent::Idle(fhwnet::Empty {}) => {}
                        fhwnet::DevicePortEvent::Removed(port_id) => {
                            let _: fhwnet::PortId = port_id;
                        }
                        fhwnet::DevicePortEvent::Added(mut port_id)
                        | fhwnet::DevicePortEvent::Existing(mut port_id) => {
                            let (port, port_server_end) =
                                fidl::endpoints::create_proxy::<fhwnet::PortMarker>()
                                    .context("create port endpoints")
                                    .map_err(errors::Error::NonFatal)?;
                            let () = device
                                .get_port(&mut port_id, port_server_end)
                                .context("calling Device get_port")
                                .map_err(errors::Error::NonFatal)?;
                            break Ok(Some((
                                NetworkDeviceInstance {
                                    port,
                                    port_id,
                                    device_control: device_control.clone(),
                                    topological_path: topological_path.clone(),
                                },
                                (port_watcher, device_control, device, topological_path),
                            )));
                        }
                    }
                }
            },
        )
        .boxed())
    }

    async fn get_device_info(
        device_instance: &NetworkDeviceInstance,
    ) -> Result<DeviceInfo, errors::Error> {
        let NetworkDeviceInstance { port, port_id: _, device_control: _, topological_path } =
            device_instance;
        let fhwnet::PortInfo { id: _, class: device_class, rx_types: _, tx_types: _, .. } = port
            .get_info()
            .await
            .context("error getting port info")
            .map_err(errors::Error::NonFatal)?;
        let device_class = device_class.ok_or_else(|| {
            errors::Error::Fatal(anyhow::anyhow!("missing device class in port info"))
        })?;

        let (mac_addressing, mac_addressing_server_end) =
            fidl::endpoints::create_proxy::<fhwnet::MacAddressingMarker>()
                .context("create MacAddressing proxy")
                .map_err(errors::Error::NonFatal)?;
        let () = port
            .get_mac(mac_addressing_server_end)
            .context("calling Port get_mac")
            .map_err(errors::Error::NonFatal)?;

        let mac = mac_addressing
            .get_unicast_address()
            .await
            .map(Some)
            .or_else(|fidl_err| {
                if fidl_err.is_closed() {
                    Ok(None)
                } else {
                    Err(anyhow::Error::from(fidl_err))
                }
            })
            .map_err(errors::Error::NonFatal)?;
        Ok(DeviceInfo {
            device_class,
            mac: mac.map(Into::into),
            is_synthetic: false,
            topological_path: topological_path.clone(),
        })
    }

    async fn add_to_stack(
        _netcfg: &super::NetCfg<'_>,
        config: crate::InterfaceConfig,
        device_instance: &Self::DeviceInstance,
    ) -> Result<(u64, fidl_fuchsia_net_interfaces_ext::admin::Control), AddDeviceError> {
        let NetworkDeviceInstance { port: _, port_id, device_control, topological_path: _ } =
            device_instance;
        let crate::InterfaceConfig { name, metric } = config;

        let (control, control_server_end) =
            fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                .context("create Control proxy")
                .map_err(errors::Error::NonFatal)?;

        let () = device_control
            .create_interface(
                &mut port_id.clone(),
                control_server_end,
                fidl_fuchsia_net_interfaces_admin::Options {
                    name: Some(name),
                    metric: Some(metric),
                    ..fidl_fuchsia_net_interfaces_admin::Options::EMPTY
                },
            )
            .context("calling DeviceControl create_interface")
            .map_err(errors::Error::NonFatal)?;

        let interface_id = control.get_id().await.map_err(|err| {
            let other = match err {
                fidl_fuchsia_net_interfaces_ext::admin::TerminalError::Fidl(err) => err.into(),
                fidl_fuchsia_net_interfaces_ext::admin::TerminalError::Terminal(terminal_error) => {
                    match terminal_error {
                        fidl_fuchsia_net_interfaces_admin::InterfaceRemovedReason::DuplicateName => {
                            return AddDeviceError::AlreadyExists;
                        }
                        reason => {
                            anyhow::anyhow!("received terminal event {:?}", reason)
                        }
                    }
                }
            };
            AddDeviceError::Other(
                errors::Error::NonFatal(other).context("calling Control get_id"),
            )
        })?;
        Ok((interface_id, control))
    }
}

/// Returns the topological path for a device located at `filepath`, `filepath`
/// converted to `String`, and a proxy to `S`.
///
/// It is expected that the node at `filepath` implements `fuchsia.device/Controller`
/// and `S`.
async fn get_topo_path_and_device<S: fidl::endpoints::ProtocolMarker>(
    filepath: &std::path::PathBuf,
) -> Result<(String, String, S::Proxy), errors::Error> {
    let filepath = filepath
        .to_str()
        .ok_or_else(|| anyhow::anyhow!("failed to convert {:?} to str", filepath))
        .map_err(errors::Error::NonFatal)?;

    // Get the topological path using `fuchsia.device/Controller`.
    let (controller, req) = fidl::endpoints::create_proxy::<fdev::ControllerMarker>()
        .context("error creating fuchsia.device.Controller proxy")
        .map_err(errors::Error::Fatal)?;
    fdio::service_connect(filepath, req.into_channel().into())
        .with_context(|| format!("error calling fdio::service_connect({})", filepath))
        .map_err(errors::Error::NonFatal)?;
    let topological_path = controller
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

    Ok((topological_path, filepath.to_string(), device))
}
