// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The special-purpose event loop used by netstack3.
//!
//! This event loop takes in events from all sources (currently ethernet devices, FIDL, and
//! timers), and handles them appropriately. It dispatches ethernet and timer events to the netstack
//! core, and implements handlers for FIDL calls.
//!
//! This is implemented with a single mpsc queue for all event types - `EventLoop` holds the
//! consumer, and any event handling that requires state within `EventLoop` holds a producer,
//! allowing it to delegate work to the `EventLoop` by sending a message. In this documentation, we
//! call anything that holds a producer a "worker".
//!
//! Having a single queue for all of the message types is beneficial, since it guarantees a FIFO
//! ordering for all messages - whichever messages arrive first, will be handled first.
//!
//! We'll look at each type of message, to see how each one is handled - starting with FIDL
//! messages, since they can be thought of as the entry point for the whole loop (as nothing happens
//! until a FIDL call is made).
//!
//! # FIDL Worker
//!
//! The FIDL part of the event loop implements the following interfaces:
//!
//! * `fuchsia.net.icmp.Provider`
//! * `fuchsia.net.stack.Stack`
//! * `fuchsia.posix.socket.Provider`
//!
//! The type of the event loop message for a FIDL call is simply the generated FIDL type. When the
//! event loop starts up, we use `fuchsia_component` to start a FIDL server that simply sends all
//! of the events it receives to the event loop (via the sender end of the mpsc queue).
//!
//! When `EventLoop` receives this message, it calls the appropriate `handle_fidl_*` method, which,
//! depending on what the request is, either:
//!
//! * Responds with the requested information.
//! * Modifies the state of the netstack in the requested way.
//! * Adds a new ethernet device to the event loop.
//!
//! Of these, only the last one is really interesting from the perspective of how the event loop
//! functions - when we add a new ethernet device, we spawn a new worker to handle ethernet setup.
//!
//! # Ethernet Setup Worker
//!
//! The `EthernetSetupWorker` creates an ethernet client, and sends an `EthernetDeviceReady`
//! message to the event loop when the device is ready. This message contains the newly-ready
//! ethernet client, some data about the client, and the handler to respond to the FIDL call
//! requesting that this device be added.
//!
//! When `EventLoop` receives a `EthernetDeviceReady` message, it assigns the ethernet device an ID
//! number, adds the ethernet client to it's list of clients, and spawns an `EthernetWorker`.
//!
//! # Ethernet Worker
//!
//! The ethernet worker simply waits for ethernet messages (either new frames or status changes),
//! and sends a message to the event loop when an ethernet message comes in. The event loop, upon
//! receiving such a message, forwards it to the netstack core.
//!
//! # Timers
//!
//! The logic for timers lives in the `EventLoopInner`. Upon a timer being set, `EventLoopInner`
//! spawns a cancellable future, scheduled to send a message to the event loop when the timer fires.
//! Upon receiving the message, the event loop calls the dispatcher function in the netstack core,
//! which triggers the correct action in response to the timer. The future that the
//! `EventLoopInner` spawns can be thought of as a sort of "Timer Worker".
//!
//! The mpsc queue design was chosen, in large part, to allow the `EventLoopInner` to set a timer
//! without requiring access to the full netstack state - instead the future that the
//! `schedule_timeout` function spawns can delegate performing actions that require the full
//! netstack state to the outer `EventLoop` by sending a message. However, this does come with a few
//! drawbacks - notably, it can be difficult to reason about what exactly the behavior of the
//! timers is - see the comment below on race conditions. Particularly, it's a bit tricky that the
//! timer is not cancelled when the timer trigger message is _sent_, but when it is _received_.

#[macro_use]
mod macros;

mod icmp;
#[cfg(test)]
mod integration_tests;
mod socket;
mod timers;
mod util;

use ethernet as eth;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;

use std::collections::HashMap;
use std::convert::TryFrom;
use std::time::Duration;

use anyhow::Error;
use fidl_fuchsia_hardware_ethernet as fidl_ethernet;
use fidl_fuchsia_hardware_ethernet_ext::{EthernetInfo, EthernetStatus};
use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_icmp as fidl_icmp;

use fidl_fuchsia_net_stack as fidl_net_stack;
use fidl_fuchsia_net_stack::{
    AdministrativeStatus, ForwardingEntry, InterfaceAddress, InterfaceInfo, InterfaceProperties,
    PhysicalStatus, StackAddEthernetInterfaceResponder, StackRequest,
};
use fidl_fuchsia_posix_socket as psocket;
use futures::channel::mpsc;
use futures::prelude::*;
#[cfg(test)]
use integration_tests::TestEvent;
use log::{debug, error, info, trace, warn};
use net_types::ethernet::Mac;
use net_types::ip::{Ip, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use net_types::SpecifiedAddr;
use packet::{Buf, BufferMut, Serializer};
use rand::rngs::OsRng;
use util::{ContextFidlCompatible, ConversionContext, CoreCompatible, FidlCompatible, IntoFidlExt};

use crate::devices::{BindingId, CommonInfo, DeviceInfo, Devices, ToggleError};

use netstack3_core::error::{NoRouteError, SocketError};
use netstack3_core::icmp::{
    self as core_icmp, BufferIcmpEventDispatcher, IcmpConnId, IcmpEventDispatcher, IcmpIpExt,
};
use netstack3_core::{
    add_ip_addr_subnet, add_route, del_device_route, del_ip_addr, get_all_ip_addr_subnets,
    get_all_routes, handle_timeout, initialize_device, receive_frame, remove_device, Context,
    DeviceId, DeviceLayerEventDispatcher, EntryEither, EventDispatcher, IpLayerEventDispatcher,
    StackStateBuilder, TimerId, TransportLayerEventDispatcher,
};

/// The message that is sent to the main event loop to indicate that an
/// ethernet device has been set up, and is ready to be added to the event
/// loop.
#[derive(Debug)]
pub struct EthernetDeviceReady {
    // We pass through the topological path for the device, so that it can later be shown to the
    // user, should they request it via FIDL call.
    path: String,
    client: eth::Client,
    info: EthernetInfo,
    // This struct needs to contain the responder, because we don't know the ID of the device until
    // it's been added to the netstack - thus, the `EthernetSetupWorker` can't respond to the FIDL
    // request.
    responder: StackAddEthernetInterfaceResponder,
}

/// The worker that sets up an ethernet device, sending an
/// `EthernetDeviceReady` to the event loop once it has finished.
///
/// `path` is not checked, since we do not have the required info by this point.
/// It must be set to the topological path of the device represented by
/// `DeviceProxy`.
pub struct EthernetSetupWorker {
    dev: fidl_ethernet::DeviceProxy,
    path: String,
    responder: StackAddEthernetInterfaceResponder,
}

impl EthernetSetupWorker {
    fn spawn(self, sender: mpsc::UnboundedSender<Event>) {
        fasync::spawn_local(
            async move {
                let vmo = zx::Vmo::create(256 * eth::DEFAULT_BUFFER_SIZE as u64)?;
                let eth_client =
                    eth::Client::new(self.dev, vmo, eth::DEFAULT_BUFFER_SIZE as u64, "netstack3")
                        .await?;
                let info = eth_client.info().await?;
                eth_client.start().await?;
                let eth_device_event = Event::EthSetupEvent(EthernetDeviceReady {
                    path: self.path,
                    client: eth_client,
                    info,
                    responder: self.responder,
                });
                sender.unbounded_send(eth_device_event)?;
                Ok(())
            }
            .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }
}

/// The worker that receives messages from the ethernet device, and passes them
/// on to the main event loop.
struct EthernetWorker {
    id: BindingId,
    events: eth::EventStream,
}

impl EthernetWorker {
    fn new(id: BindingId, events: eth::EventStream) -> Self {
        EthernetWorker { id, events }
    }

    fn spawn(self, sender: mpsc::UnboundedSender<Event>) {
        let mut events = self.events;
        let id = self.id;
        fasync::spawn_local(
            async move {
                while let Some(evt) = events.try_next().await? {
                    sender.unbounded_send(Event::EthEvent((id, evt)))?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }
}

/// The events that can trigger an action in the event loop.
#[derive(Debug)]
pub enum Event {
    /// A request from the fuchsia.net.icmp.Provider FIDL interface.
    FidlIcmpProviderEvent(fidl_icmp::ProviderRequest),
    /// A request from fuchsia.net.icmp.EchoSocket FIDL interface.
    FidlEchoSocketEvent(icmp::echo::Request),
    /// A request from the fuchsia.net.stack.Stack FIDL interface.
    FidlStackEvent(StackRequest),
    /// A request from the fuchsia.posix.socket.Provider FIDL interface.
    FidlSocketProviderEvent(psocket::ProviderRequest),
    /// A request handled by the `socket` mod.
    SocketEvent(socket::SocketEvent),
    /// An event from an ethernet interface. Either a status change or a frame.
    EthEvent((BindingId, eth::Event)),
    /// An indication that an ethernet device is ready to be used.
    EthSetupEvent(EthernetDeviceReady),
    /// A timer firing.
    TimerEvent(timers::TimerEvent<TimerId>),
}

impl From<timers::TimerEvent<TimerId>> for Event {
    fn from(e: timers::TimerEvent<TimerId>) -> Self {
        Event::TimerEvent(e)
    }
}

/// The event loop.
pub struct EventLoop {
    ctx: Context<EventLoopInner>,
    event_recv: mpsc::UnboundedReceiver<Event>,
}

impl EventLoop {
    /// # Panics
    ///
    /// Panics if a new `FidlWorker` cannot be spawned.
    pub fn new() -> Self {
        let (event_send, event_recv) = futures::channel::mpsc::unbounded::<Event>();
        let fidl_worker = crate::fidl_worker::FidlWorker;
        fidl_worker.spawn(event_send.clone()).expect("Unable to spawn fidl worker");
        Self::new_with_channels(StackStateBuilder::default(), event_send, event_recv)
    }

    fn new_with_channels(
        builder: StackStateBuilder,
        event_send: futures::channel::mpsc::UnboundedSender<Event>,
        event_recv: futures::channel::mpsc::UnboundedReceiver<Event>,
    ) -> Self {
        EventLoop {
            ctx: Context::new(
                builder.build(),
                EventLoopInner {
                    devices: Devices::default(),
                    timers: timers::TimerDispatcher::new(event_send.clone()),
                    icmpv4_echo_sockets: HashMap::new(),
                    icmpv6_echo_sockets: HashMap::new(),
                    // TODO(joshlf): Is unwrapping safe here? Alternatively,
                    // wait until we upgrade to rand 0.7, where OsRng is an
                    // empty struct.
                    rng: OsRng::new().unwrap(),
                    event_send: event_send.clone(),
                    #[cfg(test)]
                    test_events: None,
                    udp_sockets: Default::default(),
                },
            ),
            event_recv,
        }
    }

    fn clone_event_sender(&self) -> mpsc::UnboundedSender<Event> {
        self.ctx.dispatcher().event_send.clone()
    }

    async fn handle_event<'a>(
        &'a mut self,
        buf: &'a mut [u8],
        evt: Option<Event>,
    ) -> Result<(), Error> {
        trace!("Handling Event: {:?}", evt);
        match evt {
            Some(Event::EthSetupEvent(setup)) => {
                let (state, disp) = self.ctx.state_and_dispatcher();
                let client_stream = setup.client.get_stream();

                let online = setup
                    .client
                    .get_status()
                    .await
                    .map(|s| s.contains(EthernetStatus::ONLINE))
                    .unwrap_or(false);
                // We do not support updating the device's mac-address,
                // mtu, and features during it's lifetime, their cached
                // states are hence not updated once initialized.
                let comm_info = CommonInfo::new(
                    setup.path,
                    setup.client,
                    setup.info.mac.into(),
                    setup.info.mtu,
                    setup.info.features,
                    true,
                    online,
                );

                let id = if online {
                    let eth_id =
                        state.add_ethernet_device(Mac::new(setup.info.mac.octets), setup.info.mtu);
                    disp.devices.add_active_device(eth_id, comm_info)
                } else {
                    Some(disp.devices.add_device(comm_info))
                };
                match id {
                    Some(id) => {
                        let eth_worker = EthernetWorker::new(id, client_stream);
                        eth_worker.spawn(self.ctx.dispatcher().event_send.clone());
                        // If we have a core_id associated with id, that means
                        // the device was added in the active state, so we must
                        // initialize it using the new core_id.
                        if let Some(core_id) = self.ctx.dispatcher_mut().devices.get_core_id(id) {
                            initialize_device(&mut self.ctx, core_id);
                        }
                        responder_send!(setup.responder, &mut Ok(id));
                    }
                    None => {
                        // Send internal error if we can't allocate an id
                        responder_send!(setup.responder, &mut Err(fidl_net_stack::Error::Internal));
                    }
                }
            }
            Some(Event::FidlIcmpProviderEvent(req)) => {
                if let Err(err) = icmp::provider::handle_request(self, req) {
                    error!("Failed to handle ICMP Provider request: {}", err);
                }
            }
            Some(Event::FidlEchoSocketEvent(req)) => {
                req.handle_request(self);
            }
            Some(Event::FidlStackEvent(req)) => {
                self.handle_fidl_stack_request(req).await;
            }
            Some(Event::FidlSocketProviderEvent(req)) => {
                socket::handle_fidl_socket_provider_request(self, req);
            }
            Some(Event::SocketEvent(req)) => {
                req.handle_event(self);
            }
            Some(Event::EthEvent((id, eth::Event::StatusChanged))) => {
                info!("device {:?} status changed signal", id);
                // We need to call get_status even if we don't use the output, since calling it
                // acks the message, and prevents the device from sending more status changed
                // messages.
                if let Some(device) = self.ctx.dispatcher().get_device_info(id) {
                    if let Ok(status) = device.client().get_status().await {
                        info!("device {:?} status changed to: {:?}", id, status);
                        // Handle the new device state. If this results in no change, no state
                        // will be modified.
                        if status.contains(EthernetStatus::ONLINE) {
                            self.phy_enable_interface(id)
                                .await
                                .unwrap_or_else(|e| trace!("Phy enable interface failed: {:?}", e));
                        } else {
                            self.phy_disable_interface(id).unwrap_or_else(|e| {
                                trace!("Phy disable interface failed: {:?}", e)
                            });
                        }
                        #[cfg(test)]
                        self.ctx
                            .dispatcher_mut()
                            .send_test_event(TestEvent::DeviceStatusChanged { id, status });
                    }
                }
            }
            Some(Event::EthEvent((id, eth::Event::Receive(rx, _flags)))) => {
                // TODO(wesleyac): Check flags
                let len = rx.read(buf);
                if let Some(id) = self.ctx.dispatcher().devices.get_core_id(id) {
                    receive_frame(&mut self.ctx, id, Buf::new(&mut buf[..len as usize], ..));
                } else {
                    debug!("Received ethernet frame on disabled device: {}", id);
                }
            }
            Some(Event::TimerEvent(id)) => {
                // By reaching into the TimerDispatcher to commit the timer, we
                // guarantee that the timer hasn't been cancelled while it was
                // in the EventLoop's event stream. commit_timer will only
                // return Some if the timer is still valid.
                if let Some(id) = self.ctx.dispatcher_mut().timers.commit_timer(id) {
                    handle_timeout(&mut self.ctx, id);
                }
            }
            None => return Err(anyhow::format_err!("Stream of events ended unexpectedly")),
        }
        Ok(())
    }

    #[cfg(test)]
    async fn run_until<V>(&mut self, fut: impl Future<Output = V> + Unpin) -> Result<V, Error> {
        let mut buf = [0; 2048];
        let mut fut = Some(fut);
        loop {
            match futures::future::select(self.event_recv.next(), fut.take().unwrap()).await {
                future::Either::Left((evt, f)) => {
                    self.handle_event(&mut buf, evt).await?;
                    fut = Some(f);
                }
                future::Either::Right((result, _)) => break Ok(result),
            }
        }
    }

    pub async fn run(mut self) -> Result<(), Error> {
        let mut buf = [0; 2048];
        loop {
            let evt = self.event_recv.next().await;
            self.handle_event(&mut buf, evt).await?;
        }
    }

    async fn handle_fidl_stack_request(&mut self, req: StackRequest) {
        match req {
            StackRequest::AddEthernetInterface { topological_path, device, responder } => {
                self.fidl_add_ethernet_interface(topological_path, device, responder);
            }
            StackRequest::DelEthernetInterface { id, responder } => {
                responder_send!(responder, &mut self.fidl_del_ethernet_interface(id));
            }
            StackRequest::ListInterfaces { responder } => {
                responder_send!(responder, &mut self.fidl_list_interfaces().await.iter_mut());
            }
            StackRequest::GetInterfaceInfo { id, responder } => {
                responder_send!(responder, &mut self.fidl_get_interface_info(id).await);
            }
            StackRequest::EnableInterface { id, responder } => {
                responder_send!(responder, &mut self.fidl_enable_interface(id).await);
            }
            StackRequest::DisableInterface { id, responder } => {
                responder_send!(responder, &mut self.fidl_disable_interface(id));
            }
            StackRequest::AddInterfaceAddress { id, addr, responder } => {
                responder_send!(responder, &mut self.fidl_add_interface_address(id, addr));
            }
            StackRequest::DelInterfaceAddress { id, addr, responder } => {
                responder_send!(responder, &mut self.fidl_del_interface_address(id, addr));
            }
            StackRequest::GetForwardingTable { responder } => {
                responder_send!(responder, &mut self.fidl_get_forwarding_table().iter_mut());
            }
            StackRequest::AddForwardingEntry { entry, responder } => {
                responder_send!(responder, &mut self.fidl_add_forwarding_entry(entry));
            }
            StackRequest::DelForwardingEntry { subnet, responder } => {
                responder_send!(responder, &mut self.fidl_del_forwarding_entry(subnet));
            }
            StackRequest::EnablePacketFilter { id: _, responder: _ } => {
                // TODO(toshik)
            }
            StackRequest::DisablePacketFilter { id: _, responder: _ } => {
                // TODO(toshik)
            }
            StackRequest::EnableIpForwarding { responder: _ } => {
                // TODO(toshik)
            }
            StackRequest::DisableIpForwarding { responder: _ } => {
                // TODO(toshik)
            }
        }
    }

    fn fidl_add_ethernet_interface(
        &mut self,
        topological_path: String,
        device: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
        responder: StackAddEthernetInterfaceResponder,
    ) {
        let setup = EthernetSetupWorker {
            dev: device.into_proxy().unwrap(),
            path: topological_path,
            responder,
        };
        setup.spawn(self.ctx.dispatcher().event_send.clone());
    }

    fn fidl_del_ethernet_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        match self.ctx.dispatcher_mut().devices.remove_device(id) {
            Some(_info) => {
                // TODO(rheacock): ensure that the core client deletes all data
                Ok(())
            }
            None => {
                // Invalid device ID
                Err(fidl_net_stack::Error::NotFound)
            }
        }
    }

    async fn fidl_list_interfaces(&mut self) -> Vec<fidl_net_stack::InterfaceInfo> {
        let mut devices = vec![];
        for device in self.ctx.dispatcher().devices.iter_devices() {
            let mut addresses = vec![];
            if let Some(core_id) = device.core_id() {
                for addr in get_all_ip_addr_subnets(&self.ctx, core_id) {
                    match addr.try_into_fidl() {
                        Ok(addr) => addresses.push(addr),
                        Err(e) => {
                            error!("failed to map interface address/subnet into FIDL: {:?}", e)
                        }
                    }
                }
            };
            devices.push(InterfaceInfo {
                id: device.id(),
                properties: InterfaceProperties {
                    name: "[TBD]".to_owned(), // TODO(porce): Follow up to populate the name
                    topopath: device.path().clone(),
                    filepath: "[TBD]".to_owned(), // TODO(porce): Follow up to populate
                    mac: Some(Box::new(device.mac())),
                    mtu: device.mtu(),
                    features: device.features().bits(),
                    administrative_status: if device.admin_enabled() {
                        AdministrativeStatus::Enabled
                    } else {
                        AdministrativeStatus::Disabled
                    },
                    physical_status: if device.phy_up() {
                        PhysicalStatus::Up
                    } else {
                        PhysicalStatus::Down
                    },
                    addresses, // TODO(gongt) Handle tentative IPv6 addresses
                },
            });
        }
        devices
    }

    async fn fidl_get_interface_info(
        &mut self,
        id: u64,
    ) -> Result<fidl_net_stack::InterfaceInfo, fidl_net_stack::Error> {
        let device =
            self.ctx.dispatcher().get_device_info(id).ok_or(fidl_net_stack::Error::NotFound)?;
        let mut addresses = vec![];
        if let Some(core_id) = device.core_id() {
            for addr in get_all_ip_addr_subnets(&self.ctx, core_id) {
                match addr.try_into_fidl() {
                    Ok(addr) => addresses.push(addr),
                    Err(e) => error!("failed to map interface address/subnet into FIDL: {:?}", e),
                }
            }
        };
        return Ok(InterfaceInfo {
            id: device.id(),
            properties: InterfaceProperties {
                name: "[TBD]".to_owned(), // TODO(porce): Follow up to populate the name
                topopath: device.path().clone(),
                filepath: "[TBD]".to_owned(), // TODO(porce): Follow up to populate
                mac: Some(Box::new(device.mac())),
                mtu: device.mtu(),
                features: device.features().bits(),
                administrative_status: if device.admin_enabled() {
                    AdministrativeStatus::Enabled
                } else {
                    AdministrativeStatus::Disabled
                },
                physical_status: if device.phy_up() {
                    PhysicalStatus::Up
                } else {
                    PhysicalStatus::Down
                },
                addresses, // TODO(gongt) Handle tentative IPv6 addresses
            },
        });
    }

    fn fidl_add_interface_address(
        &mut self,
        id: u64,
        addr: InterfaceAddress,
    ) -> Result<(), fidl_net_stack::Error> {
        let device_info =
            self.ctx.dispatcher().get_device_info(id).ok_or(fidl_net_stack::Error::NotFound)?;
        // TODO(brunodalbo): We should probably allow adding static addresses
        // to interfaces that are not installed, return BadState for now
        let device_id = device_info.core_id().ok_or(fidl_net_stack::Error::BadState)?;

        add_ip_addr_subnet(&mut self.ctx, device_id, addr.try_into_core()?)
            .map_err(IntoFidlExt::into_fidl)
    }

    fn fidl_del_interface_address(
        &mut self,
        id: u64,
        addr: InterfaceAddress,
    ) -> Result<(), fidl_net_stack::Error> {
        let device_info =
            self.ctx.dispatcher().get_device_info(id).ok_or(fidl_net_stack::Error::NotFound)?;
        // TODO(gongt): Since addresses can't be added to inactive interfaces
        // they can't be deleted either; return BadState for now.
        let device_id = device_info.core_id().ok_or(fidl_net_stack::Error::BadState)?;
        let addr: SpecifiedAddr<_> = addr.ip_address.try_into_core()?;

        del_ip_addr(&mut self.ctx, device_id, addr.into()).map_err(IntoFidlExt::into_fidl)
    }

    fn fidl_get_forwarding_table(&self) -> Vec<fidl_net_stack::ForwardingEntry> {
        get_all_routes(&self.ctx)
            .filter_map(|entry| match entry.try_into_fidl_with_ctx(self.ctx.dispatcher()) {
                Ok(entry) => Some(entry),
                Err(_) => {
                    error!("Failed to map forwarding entry into FIDL");
                    None
                }
            })
            .collect()
    }

    fn fidl_add_forwarding_entry(
        &mut self,
        entry: ForwardingEntry,
    ) -> Result<(), fidl_net_stack::Error> {
        let entry = match EntryEither::try_from_fidl_with_ctx(self.ctx.dispatcher(), entry) {
            Ok(entry) => entry,
            Err(e) => return Err(e.into()),
        };
        add_route(&mut self.ctx, entry).map_err(IntoFidlExt::into_fidl)
    }

    fn fidl_del_forwarding_entry(
        &mut self,
        subnet: fidl_net::Subnet,
    ) -> Result<(), fidl_net_stack::Error> {
        if let Ok(subnet) = subnet.try_into_core() {
            del_device_route(&mut self.ctx, subnet).map_err(IntoFidlExt::into_fidl)
        } else {
            Err(fidl_net_stack::Error::InvalidArgs)
        }
    }

    async fn phy_enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        self.update_device_state(id, |dev_info| dev_info.set_phy_up(true));
        self.enable_interface(id).await
    }

    fn phy_disable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        self.update_device_state(id, |dev_info| dev_info.set_phy_up(false));
        self.disable_interface(id)
    }

    async fn fidl_enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        self.update_device_state(id, |dev_info| dev_info.set_admin_enabled(true));
        self.enable_interface(id).await
    }

    fn fidl_disable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        self.update_device_state(id, |dev_info| dev_info.set_admin_enabled(false));
        self.disable_interface(id)
    }

    /// Updates cached device state
    fn update_device_state<F: FnOnce(&mut DeviceInfo)>(&mut self, id: u64, handle_device_state: F) {
        if let Some(device_info) = self.ctx.dispatcher_mut().devices.get_device_mut(id) {
            handle_device_state(device_info);
        }
    }

    /// Enables an interface, adding it to the core if it is not currently enabled.
    /// Both `admin_enabled` and `phy_up` must be true for the interface to be enabled.
    async fn enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        let (state, disp) = self.ctx.state_and_dispatcher();
        let device = disp.get_device_info(id).ok_or(fidl_net_stack::Error::NotFound)?;

        if device.admin_enabled() && device.phy_up() {
            // TODO(rheacock, NET-2140): Handle core and driver state in two stages: add device to the
            // core to get an id, then reach into the driver to get updated info before triggering the
            // core to allow traffic on the interface.
            let generate_core_id = |info: &DeviceInfo| {
                state.add_ethernet_device(Mac::new(info.mac().octets), info.mtu())
            };
            match disp.devices.activate_device(id, generate_core_id) {
                Ok(device_info) => {
                    // we can unwrap core_id here because activate_device just succeeded.
                    let core_id = device_info.core_id().unwrap();
                    // don't forget to initialize the device in core!
                    initialize_device(&mut self.ctx, core_id);
                    Ok(())
                }
                Err(toggle_error) => {
                    match toggle_error {
                        ToggleError::NoChange => Ok(()),
                        ToggleError::NotFound => Err(fidl_net_stack::Error::NotFound), // Invalid device ID
                    }
                }
            }
        } else {
            Ok(())
        }
    }

    /// Disables an interface, removing it from the core if it is currently enabled.
    /// Either an Admin (fidl) or Phy change can disable an interface.
    fn disable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        match self.ctx.dispatcher_mut().devices.deactivate_device(id) {
            Ok((core_id, device_info)) => {
                // Sanity check that there is a reason that the device is disabled.
                assert!(!device_info.admin_enabled() || !device_info.phy_up());
                // Disabling the interface deactivates it in the bindings, and will remove
                // it completely from the core.
                match remove_device(&mut self.ctx, core_id) {
                    Some(_) => Ok(()), // TODO(rheacock): schedule and send the received frames
                    None => Ok(()),
                }
            }
            Err(toggle_error) => {
                match toggle_error {
                    ToggleError::NoChange => Ok(()),
                    ToggleError::NotFound => Err(fidl_net_stack::Error::NotFound), // Invalid device ID
                }
            }
        }
    }
}

struct EventLoopInner {
    devices: Devices,
    timers: timers::TimerDispatcher<TimerId, Event>,
    rng: OsRng,
    icmpv4_echo_sockets: HashMap<IcmpConnId<Ipv4>, EchoSocket>,
    icmpv6_echo_sockets: HashMap<IcmpConnId<Ipv6>, EchoSocket>,
    event_send: mpsc::UnboundedSender<Event>,
    #[cfg(test)]
    test_events: Option<mpsc::UnboundedSender<TestEvent>>,
    udp_sockets: socket::udp::UdpSocketCollection,
}

struct EchoSocket {
    reply_tx: mpsc::Sender<fidl_icmp::EchoPacket>,
}

impl EventLoopInner {
    fn get_device_info(&self, id: u64) -> Option<&DeviceInfo> {
        self.devices.get_device(id)
    }

    /// Sends an event to a special test events listener.
    ///
    /// Only available for testing, use this function to expose internal events
    /// to testing code.
    #[cfg(test)]
    fn send_test_event(&mut self, event: TestEvent) {
        if let Some(evt) = self.test_events.as_mut() {
            evt.unbounded_send(event).expect("Can't send test event data");
        }
    }
}

impl ConversionContext for EventLoopInner {
    fn get_core_id(&self, binding_id: u64) -> Option<DeviceId> {
        self.devices.get_core_id(binding_id)
    }

    fn get_binding_id(&self, core_id: DeviceId) -> Option<u64> {
        self.devices.get_binding_id(core_id)
    }
}

/// A thin wrapper around `zx::Time` that implements `core::Instant`.
#[derive(PartialEq, Eq, PartialOrd, Ord, Copy, Clone, Debug)]
struct ZxTime(zx::Time);

impl netstack3_core::Instant for ZxTime {
    fn duration_since(&self, earlier: ZxTime) -> Duration {
        assert!(self.0 >= earlier.0);
        // guaranteed not to panic because the assertion ensures that the
        // difference is non-negative, and all non-negative i64 values are also
        // valid u64 values
        Duration::from_nanos(u64::try_from(self.0.into_nanos() - earlier.0.into_nanos()).unwrap())
    }

    fn checked_add(&self, duration: Duration) -> Option<ZxTime> {
        Some(ZxTime(zx::Time::from_nanos(
            self.0.into_nanos().checked_add(i64::try_from(duration.as_nanos()).ok()?)?,
        )))
    }

    fn checked_sub(&self, duration: Duration) -> Option<ZxTime> {
        Some(ZxTime(zx::Time::from_nanos(
            self.0.into_nanos().checked_sub(i64::try_from(duration.as_nanos()).ok()?)?,
        )))
    }
}

impl EventDispatcher for EventLoopInner {
    type Instant = ZxTime;

    fn now(&self) -> ZxTime {
        ZxTime(zx::Time::get(zx::ClockId::Monotonic))
    }

    fn schedule_timeout_instant(&mut self, time: ZxTime, id: TimerId) -> Option<ZxTime> {
        self.timers.schedule_timer(id, time)
    }

    fn cancel_timeout(&mut self, id: TimerId) -> Option<ZxTime> {
        self.timers.cancel_timer(&id)
    }

    fn cancel_timeouts_with<F: FnMut(&TimerId) -> bool>(&mut self, f: F) {
        self.timers.cancel_timers_with(f);
    }

    fn scheduled_instant(&self, id: TimerId) -> Option<ZxTime> {
        self.timers.scheduled_time(&id)
    }

    type Rng = OsRng;

    fn rng(&mut self) -> &mut OsRng {
        &mut self.rng
    }
}

impl<B: BufferMut> DeviceLayerEventDispatcher<B> for EventLoopInner {
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        frame: S,
    ) -> Result<(), S> {
        // TODO(wesleyac): Error handling
        let frame = frame.serialize_vec_outer().map_err(|(_, ser)| ser)?;
        match self.devices.get_core_device_mut(device) {
            Some(dev) => {
                dev.client_mut().send(frame.as_ref());
            }
            None => error!("Tried to send frame on device that is not listed: {:?}", device),
        }
        Ok(())
    }
}

impl TransportLayerEventDispatcher<Ipv4> for EventLoopInner {}
impl TransportLayerEventDispatcher<Ipv6> for EventLoopInner {}

impl<I: IcmpIpExt> IcmpEventDispatcher<I> for EventLoopInner {
    fn receive_icmp_error(&mut self, _conn: IcmpConnId<I>, _seq_num: u16, _err: I::ErrorCode) {
        // TODO(joshlf)
        warn!("IcmpEventDispatcher::receive_icmp_error unimplemented; ignoring error");
    }

    fn close_icmp_connection(&mut self, _conn: IcmpConnId<I>, _err: NoRouteError) {
        // TODO(joshlf)
        unimplemented!()
    }
}

impl<I: IpExt, B: BufferMut> BufferIcmpEventDispatcher<I, B> for EventLoopInner {
    fn receive_icmp_echo_reply(&mut self, conn: IcmpConnId<I>, seq_num: u16, data: B) {
        trace!("Received ICMP echo reply w/ seq_num={}, len={}", seq_num, data.len());

        #[cfg(test)]
        self.send_test_event(TestEvent::IcmpEchoReply {
            conn: conn.into(),
            seq_num,
            data: data.as_ref().to_owned(),
        });

        let socket = match I::get_icmp_echo_sockets(self).get_mut(&conn) {
            Some(socket) => socket,
            None => {
                trace!("Received ICMP echo reply for unknown socket w/ id: {:?}", conn);
                return;
            }
        };

        let packet =
            fidl_icmp::EchoPacket { sequence_num: seq_num, payload: data.as_ref().to_vec() };

        // TODO(fxb/39186): Consider not dropping ICMP replies when the channel is full.
        match socket.reply_tx.try_send(packet) {
            Ok(()) => {
                trace!("Processed ICMP echo reply w/ seq_num={}, len={}", seq_num, data.len());
            }
            Err(e) => {
                // Channel is full or disconnected.
                debug!("Unable to handle ICMP echo reply: {:?}", e);
            }
        }
    }
}

impl<B: BufferMut> IpLayerEventDispatcher<B> for EventLoopInner {}

/// An `Ip` extension trait that lets us write more generic code.
///
/// `IpExt` provides generic functionality backed by version-specific
/// implementations, allowing most code to be written agnostic to IP version.
trait IpExt: Ip + self::icmp::echo::IpExt + IcmpIpExt {
    /// Get the map of ICMP echo sockets.
    fn get_icmp_echo_sockets(
        evtloop: &mut EventLoopInner,
    ) -> &mut HashMap<IcmpConnId<Self>, EchoSocket>;

    /// Create a new ICMP connection.
    ///
    /// `new_icmp_connection` calls the core functions `new_icmpv4_connection`
    /// or `new_icmpv6_connection` as appropriate.
    fn new_icmp_connection<D: EventDispatcher>(
        ctx: &mut Context<D>,
        local_addr: Option<SpecifiedAddr<Self::Addr>>,
        remote_addr: SpecifiedAddr<Self::Addr>,
        icmp_id: u16,
    ) -> Result<IcmpConnId<Self>, SocketError>;
}

impl IpExt for Ipv4 {
    fn get_icmp_echo_sockets(
        evtloop: &mut EventLoopInner,
    ) -> &mut HashMap<IcmpConnId<Ipv4>, EchoSocket> {
        &mut evtloop.icmpv4_echo_sockets
    }

    fn new_icmp_connection<D: EventDispatcher>(
        ctx: &mut Context<D>,
        local_addr: Option<SpecifiedAddr<Ipv4Addr>>,
        remote_addr: SpecifiedAddr<Ipv4Addr>,
        icmp_id: u16,
    ) -> Result<IcmpConnId<Ipv4>, SocketError> {
        core_icmp::new_icmpv4_connection(ctx, local_addr, remote_addr, icmp_id)
    }
}

impl IpExt for Ipv6 {
    fn get_icmp_echo_sockets(
        evtloop: &mut EventLoopInner,
    ) -> &mut HashMap<IcmpConnId<Ipv6>, EchoSocket> {
        &mut evtloop.icmpv6_echo_sockets
    }

    fn new_icmp_connection<D: EventDispatcher>(
        ctx: &mut Context<D>,
        local_addr: Option<SpecifiedAddr<Ipv6Addr>>,
        remote_addr: SpecifiedAddr<Ipv6Addr>,
        icmp_id: u16,
    ) -> Result<IcmpConnId<Ipv6>, SocketError> {
        core_icmp::new_icmpv6_connection(ctx, local_addr, remote_addr, icmp_id)
    }
}
