// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    collections::{hash_map::Entry, HashMap},
    convert::TryInto as _,
    sync::Arc,
};

use fidl_fuchsia_hardware_network as fhardware_network;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_interfaces as fnet_interfaces;

use futures::{lock::Mutex, FutureExt as _};

use crate::bindings::{
    devices, BindingId, DeviceId, InterfaceEventProducerFactory as _, Netstack, NetstackContext,
};

#[derive(Clone)]
struct Inner {
    device: netdevice_client::Client,
    session: netdevice_client::Session,
    // TODO(https://fxbug.dev/101297): Replace hash map with a salted slab.
    // `state` must be locked before any `NetstackContext` locks.
    state: Arc<Mutex<HashMap<netdevice_client::Port, DeviceId>>>,
}

/// The worker that receives messages from the ethernet device, and passes them
/// on to the main event loop.
pub(crate) struct NetdeviceWorker {
    ctx: NetstackContext,
    task: netdevice_client::Task,
    inner: Inner,
}

#[derive(thiserror::Error, Debug)]
pub(crate) enum Error {
    #[error("failed to create system resources: {0}")]
    SystemResource(fidl::Error),
    #[error("client error: {0}")]
    Client(#[from] netdevice_client::Error),
    #[error("port {0:?} already installed")]
    AlreadyInstalled(netdevice_client::Port),
    #[error("failed to connect to port: {0}")]
    CantConnectToPort(fidl::Error),
    #[error("invalid port info: {0}")]
    InvalidPortInfo(netdevice_client::client::PortInfoValidationError),
    #[error("invalid port status: {0}")]
    InvalidPortStatus(netdevice_client::client::PortStatusValidationError),
    #[error("unsupported configuration")]
    ConfigurationNotSupported,
    #[error("mac {mac} on port {port:?} is not a valid unicast address")]
    MacNotUnicast { mac: net_types::ethernet::Mac, port: netdevice_client::Port },
}

const DEFAULT_BUFFER_LENGTH: usize = 2048;

// TODO(https://fxbug.dev/101303): Decorate *all* logging with human-readable
// device debug information to disambiguate.
impl NetdeviceWorker {
    pub async fn new(
        ctx: NetstackContext,
        device: fidl::endpoints::ClientEnd<fhardware_network::DeviceMarker>,
    ) -> Result<Self, Error> {
        let device =
            netdevice_client::Client::new(device.into_proxy().expect("must be in executor"));
        let (session, task) = device
            .primary_session("netstack3", DEFAULT_BUFFER_LENGTH)
            .await
            .map_err(Error::Client)?;
        Ok(Self { ctx, inner: Inner { device, session, state: Default::default() }, task })
    }

    pub fn new_handler(&self) -> DeviceHandler {
        DeviceHandler { inner: self.inner.clone() }
    }

    pub async fn run(self) -> Result<std::convert::Infallible, Error> {
        let Self { ctx, inner: Inner { device: _, session, state }, task } = self;
        // Allow buffer shuttling to happen in other threads.
        let mut task = fuchsia_async::Task::spawn(task).fuse();

        loop {
            // Extract result into an enum to avoid too much code in  macro.
            let rx: netdevice_client::Buffer<_> = futures::select! {
                r = session.recv().fuse() => r.map_err(Error::Client)?,
                r = task => match r {
                    Ok(()) => panic!("task should never end cleanly"),
                    Err(e) => return Err(Error::Client(e))
                }
            };
            let port = rx.port();
            let id = if let Some(id) = state.lock().await.get(&port) {
                *id
            } else {
                log::debug!("dropping frame for port {:?}, no device mapping available", port);
                continue;
            };

            // We don't need the context right now, we'll use it to feed frames.
            let _ = ctx;
            todo!(
                "https://fxbug.dev/48853 failed to receive data on interface {}, data path not implemented",
                id
            )
        }
    }
}

pub(crate) struct InterfaceOptions {
    pub name: Option<String>,
}

pub(crate) struct DeviceHandler {
    inner: Inner,
}

impl DeviceHandler {
    pub(crate) async fn add_port(
        &self,
        ns: &Netstack,
        InterfaceOptions { name }: InterfaceOptions,
        port: fhardware_network::PortId,
    ) -> Result<BindingId, Error> {
        let port = netdevice_client::Port::from(port);

        let DeviceHandler { inner: Inner { state, device, session: _ } } = self;
        let port_proxy = device.connect_port(port)?;
        let netdevice_client::client::PortInfo { id: _, class: device_class, rx_types, tx_types } =
            port_proxy
                .get_info()
                .await
                .map_err(Error::CantConnectToPort)?
                .try_into()
                .map_err(Error::InvalidPortInfo)?;

        // TODO(https://fxbug.dev/100871): support non-ethernet devices.
        let supports_ethernet_on_rx =
            rx_types.iter().any(|f| *f == fhardware_network::FrameType::Ethernet);
        let supports_ethernet_on_tx = tx_types.iter().any(
            |fhardware_network::FrameTypeSupport { type_, features: _, supported_flags: _ }| {
                *type_ == fhardware_network::FrameType::Ethernet
            },
        );
        if !(supports_ethernet_on_rx && supports_ethernet_on_tx) {
            return Err(Error::ConfigurationNotSupported);
        }

        let netdevice_client::client::PortStatus { flags: _, mtu } = port_proxy
            .get_status()
            .await
            .map_err(Error::CantConnectToPort)?
            .try_into()
            .map_err(Error::InvalidPortStatus)?;

        let (mac_proxy, mac_server) =
            fidl::endpoints::create_proxy::<fhardware_network::MacAddressingMarker>()
                .map_err(Error::SystemResource)?;
        let () = port_proxy.get_mac(mac_server).map_err(Error::CantConnectToPort)?;

        let mac_addr = {
            let fnet::MacAddress { octets } =
                mac_proxy.get_unicast_address().await.map_err(|e| {
                    // TODO(https://fxbug.dev/100871): support non-ethernet
                    // devices.
                    log::warn!("failed to get unicast address, sending not supported: {:?}", e);
                    Error::ConfigurationNotSupported
                })?;
            let mac = net_types::ethernet::Mac::new(octets);
            net_types::UnicastAddr::new(mac).ok_or_else(|| {
                log::error!("{} is not a valid unicast address", mac);
                Error::MacNotUnicast { mac, port }
            })?
        };

        let mut state = state.lock().await;
        let ctx = &mut ns.ctx.lock().await;
        let state_entry = match state.entry(port) {
            Entry::Occupied(occupied) => {
                log::warn!("attempted to install port {:?} which is already installed", port);
                return Err(Error::AlreadyInstalled(*occupied.key()));
            }
            Entry::Vacant(e) => e,
        };
        let core_id = ctx.state.add_ethernet_device(mac_addr, mtu);
        let _: &mut DeviceId = state_entry.insert(core_id);
        let make_info = |id| {
            let name = name.unwrap_or_else(|| format!("eth{}", id));
            devices::DeviceSpecificInfo::Netdevice(devices::NetdeviceInfo {
                common_info: devices::CommonInfo {
                    mtu,
                    admin_enabled: false,
                    events: ns.create_interface_event_producer(
                        id,
                        crate::bindings::InterfaceProperties {
                            name: name.clone(),
                            device_class: fnet_interfaces::DeviceClass::Device(device_class),
                        },
                    ),
                    name,
                },
                handler: PortHandler { id, port_id: port, inner: self.inner.clone() },
                mac: mac_addr,
                // TODO(https://fxbug.dev/48853): observe link changes. For now,
                // we assume the link is always offline. Observing link changes
                // is also how we'll be able to observe port removal.
                phy_up: false,
            })
        };

        Ok(ctx.dispatcher.devices.add_device(core_id, make_info).expect("duplicate core id in set"))
    }
}

pub struct PortHandler {
    id: BindingId,
    port_id: netdevice_client::Port,
    inner: Inner,
}

impl PortHandler {
    pub(crate) async fn attach(&self) -> Result<(), netdevice_client::Error> {
        let Self { id: _, port_id, inner: Inner { device: _, session, state: _ } } = self;
        session.attach(*port_id, [fhardware_network::FrameType::Ethernet]).await
    }

    pub(crate) async fn detach(&self) -> Result<(), netdevice_client::Error> {
        let Self { id: _, port_id, inner: Inner { device: _, session, state: _ } } = self;
        session.detach(*port_id).await
    }

    pub(crate) fn send(&self, _frame: &[u8]) -> Result<(), netdevice_client::Error> {
        todo!("https://fxbug.dev/48853 failed to send data on interface {}, data path not implemented", self.id)
    }
}

impl std::fmt::Debug for PortHandler {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let Self { id, port_id, inner: _ } = self;
        f.debug_struct("PortHandler").field("id", id).field("port_id", port_id).finish()
    }
}
