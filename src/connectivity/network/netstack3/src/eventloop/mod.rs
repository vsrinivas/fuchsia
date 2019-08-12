// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The special-purpose event loop used by the recovery netstack.
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
//! The FIDL part of the event loop implements the fuchsia.net.stack.Stack and
//! fuchsia.posix.socket.Provider interfaces. The type of the event loop message for a FIDL call is
//! simply the generated FIDL type. When the event loop starts up, we use `fuchsia_component` to
//! start a FIDL server that simply sends all of the events it receives to the event loop
//! (via the sender end of the mpsc queue).
//!
//! When `EventLoop` receives this message, it calls the
//! `handle_fidl_stack_request` or `handle_fidl_socket_provider_request` method, which, depending
//! on what the request is, either:
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

#![allow(unused)]

#[cfg(test)]
mod integration_tests;
mod socket;
mod timers;
mod util;

use ethernet as eth;
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_zircon as zx;

use std::convert::TryFrom;
use std::fs::File;
use std::marker::PhantomData;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use failure::{bail, format_err, Error};
use fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker};
use fidl_fuchsia_hardware_ethernet as fidl_ethernet;
use fidl_fuchsia_hardware_ethernet_ext::{EthernetInfo, EthernetStatus, MacAddress};
use fidl_fuchsia_io;
use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_stack as fidl_net_stack;
use fidl_fuchsia_net_stack::{
    AdministrativeStatus, ForwardingEntry, InterfaceAddress, InterfaceInfo, InterfaceProperties,
    PhysicalStatus, StackAddEthernetInterfaceResponder, StackAddForwardingEntryResponder,
    StackAddInterfaceAddressResponder, StackDelEthernetInterfaceResponder,
    StackDelForwardingEntryResponder, StackDelInterfaceAddressResponder,
    StackDisableInterfaceResponder, StackEnableInterfaceResponder,
    StackGetForwardingTableResponder, StackGetInterfaceInfoResponder, StackListInterfacesResponder,
    StackMarker, StackRequest, StackRequestStream,
};
use fidl_fuchsia_posix_socket as psocket;
use fidl_fuchsia_posix_socket::ProviderRequest;
use futures::channel::mpsc;
use futures::future::{AbortHandle, Abortable};
use futures::prelude::*;
use futures::{select, TryFutureExt, TryStreamExt};
#[cfg(test)]
use integration_tests::TestEvent;
use log::{debug, error, info, trace};
use net_types::ethernet::Mac;
use net_types::ip::{AddrSubnet, AddrSubnetEither, IpAddr, IpVersion, Subnet, SubnetEither};
use packet::{Buf, BufferMut, Serializer};
use rand::{rngs::OsRng, Rng};
use std::convert::TryInto;
use util::{
    ContextCoreCompatible, ContextFidlCompatible, ConversionContext, CoreCompatible, FidlCompatible,
};

use crate::devices::{BindingId, CommonInfo, DeviceInfo, Devices, ToggleError};

use netstack3_core::icmp::{IcmpConnId, IcmpEventDispatcher};
use netstack3_core::{
    add_route, del_device_route, get_all_ip_addr_subnet, get_all_routes, handle_timeout,
    initialize_device, receive_frame, remove_device, set_ip_addr_subnet, Context, DeviceId,
    DeviceLayerEventDispatcher, EntryDest, EntryEither, EventDispatcher, IpLayerEventDispatcher,
    NetstackError, StackState, TimerId, TransportLayerEventDispatcher, UdpEventDispatcher,
};

macro_rules! stack_fidl_error {
    ($err:tt) => {
        fidl_net_stack::Error { type_: fidl_net_stack::ErrorType::$err }
    };
}

macro_rules! encoded_fidl_error {
    ($err:tt) => {
        Some(fidl::encoding::OutOfLine(&mut stack_fidl_error!($err)))
    };
}

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
    fn spawn(mut self, sender: mpsc::UnboundedSender<Event>) {
        fasync::spawn_local(
            async move {
                let vmo = zx::Vmo::create(256 * eth::DEFAULT_BUFFER_SIZE as u64)?;
                let eth_client = eth::Client::new(
                    self.dev,
                    vmo,
                    eth::DEFAULT_BUFFER_SIZE,
                    "recovery-ns",
                ).await?;
                let info = eth_client.info().await?;
                eth_client.start().await?;
                let eth_device_event = Event::EthSetupEvent(EthernetDeviceReady {
                    path: self.path,
                    client: eth_client,
                    info,
                    responder: self.responder,
                });
                sender.unbounded_send(eth_device_event);
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

    fn spawn(mut self, sender: mpsc::UnboundedSender<Event>) {
        let mut events = self.events;
        let id = self.id;
        fasync::spawn_local(
            async move {
                while let Some(evt) = events.try_next().await? {
                    sender.unbounded_send(Event::EthEvent((id, evt)));
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
    /// A request from the fuchsia.net.stack.Stack FIDL interface.
    FidlStackEvent(StackRequest),
    /// A request from the fuchsia.posix.socket.Provider FIDL interface.
    FidlSocketProviderEvent(psocket::ProviderRequest),
    /// A request from the fuchsia.posix.socket.Control FIDL interface.
    FidlSocketControlEvent((Arc<Mutex<socket::SocketControlWorkerInner>>, psocket::ControlRequest)),
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
    pub fn new() -> Self {
        let (event_send, event_recv) = futures::channel::mpsc::unbounded::<Event>();
        let fidl_worker = crate::fidl_worker::FidlWorker;
        fidl_worker.spawn(event_send.clone());
        Self::new_with_channels(event_send, event_recv)
    }

    fn new_with_channels(
        event_send: futures::channel::mpsc::UnboundedSender<Event>,
        event_recv: futures::channel::mpsc::UnboundedReceiver<Event>,
    ) -> Self {
        EventLoop {
            ctx: Context::new(
                StackState::default(),
                EventLoopInner {
                    devices: Devices::default(),
                    timers: timers::TimerDispatcher::new(event_send.clone()),
                    // TODO(joshlf): Is unwrapping safe here? Alternatively,
                    // wait until we upgrade to rand 0.7, where OsRng is an
                    // empty struct.
                    rng: OsRng::new().unwrap(),
                    event_send: event_send.clone(),
                    #[cfg(test)]
                    test_events: None,
                },
            ),
            event_recv,
        }
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
                let eth_id =
                    state.add_ethernet_device(Mac::new(setup.info.mac.octets), setup.info.mtu);
                match disp
                    .devices
                    .add_active_device(eth_id, CommonInfo::new(setup.path, setup.client))
                {
                    Some(id) => {
                        let eth_worker = EthernetWorker::new(id, client_stream);
                        eth_worker.spawn(self.ctx.dispatcher().event_send.clone());

                        initialize_device(&mut self.ctx, eth_id);

                        setup.responder.send(None, id);
                    }
                    None => {
                        // Send internal error if we can't allocate an id
                        setup.responder.send(encoded_fidl_error!(Internal), 0);
                    }
                }
            }
            Some(Event::FidlStackEvent(req)) => {
                self.handle_fidl_stack_request(req).await;
            }
            Some(Event::FidlSocketProviderEvent(req)) => {
                self.handle_fidl_socket_provider_request(req).await;
            }
            Some(Event::FidlSocketControlEvent((sock, req))) => {
                sock.lock().unwrap().handle_request(self, req);
            }
            Some(Event::EthEvent((id, eth::Event::StatusChanged))) => {
                info!("device {:?} status changed signal", id);
                // We need to call get_status even if we don't use the output, since calling it
                // acks the message, and prevents the device from sending more status changed
                // messages.
                if let Some(device) = self.ctx.dispatcher().get_device_info(id) {
                    if let Ok(status) = device.client().get_status().await {
                        info!("device {:?} status changed to: {:?}", id, status);
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
                    receive_frame(&mut self.ctx, id, Buf::new(&mut buf[..len], ..));
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
            None => bail!("Stream of events ended unexpectedly"),
        }
        Ok(())
    }

    async fn run_until<V>(&mut self, mut fut: impl Future<Output = V> + Unpin) -> Result<V, Error> {
        let mut buf = [0; 2048];
        let mut fut = Some(fut);
        loop {
            match futures::future::select(self.event_recv.next(), fut.take().unwrap()).await {
                future::Either::Left((evt, f)) => {
                    self.handle_event(&mut buf, evt).await?;
                    fut = Some(f);
                }
                future::Either::Right((result, _)) => break Ok(result),
                _ => continue,
            }
        }
    }

    pub async fn run(mut self) -> Result<(), Error> {
        let mut buf = [0; 2048];
        loop {
            let evt = self.event_recv.next().await;
            self.handle_event(&mut buf, evt).await?;
        }
        Ok(())
    }

    async fn handle_fidl_socket_provider_request(&mut self, req: psocket::ProviderRequest) {
        match req {
            psocket::ProviderRequest::Socket { domain, type_, protocol, responder } => {
                let domain = i32::from(domain);
                let nonblock = i32::from(type_) & libc::SOCK_NONBLOCK != 0;
                let type_ = i32::from(type_) & !(libc::SOCK_NONBLOCK | libc::SOCK_CLOEXEC);
                let net_proto = match domain {
                    libc::AF_INET => IpVersion::V4,
                    libc::AF_INET6 => IpVersion::V6,
                    _ => {
                        responder.send(libc::EAFNOSUPPORT as i16, None);
                        return;
                    }
                };
                let trans_proto = match i32::from(type_) {
                    libc::SOCK_DGRAM => socket::TransProto::UDP,
                    libc::SOCK_STREAM => socket::TransProto::TCP,
                    _ => {
                        responder.send(libc::EAFNOSUPPORT as i16, None);
                        return;
                    }
                };

                if let Ok((c0, c1)) = zx::Channel::create() {
                    let worker = socket::SocketControlWorker::new(
                        psocket::ControlRequestStream::from_channel(
                            fasync::Channel::from_channel(c0).unwrap(),
                        ),
                        net_proto,
                        trans_proto,
                        nonblock,
                    );
                    if let Ok(worker) = worker {
                        worker.spawn(self.ctx.dispatcher().event_send.clone());
                        responder.send(0, Some(ClientEnd::new(c1)));
                    } else {
                        responder.send(libc::ENOBUFS as i16, None);
                    }
                } else {
                    responder.send(libc::ENOBUFS as i16, None);
                }
            }
        }
    }

    async fn handle_fidl_stack_request(&mut self, req: StackRequest) {
        match req {
            StackRequest::AddEthernetInterface { topological_path, device, responder } => {
                self.fidl_add_ethernet_interface(topological_path, device, responder);
            }
            StackRequest::DelEthernetInterface { id, responder } => {
                responder.send(
                    self.fidl_del_ethernet_interface(id).as_mut().map(fidl::encoding::OutOfLine),
                );
            }
            StackRequest::ListInterfaces { responder } => {
                responder.send(&mut self.fidl_list_interfaces().await.iter_mut());
            }
            StackRequest::GetInterfaceInfo { id, responder } => {
                let (mut info, mut error) = match (self.fidl_get_interface_info(id).await) {
                    Ok(info) => (Some(info), None),
                    Err(error) => (None, Some(error)),
                };
                responder.send(
                    info.as_mut().map(fidl::encoding::OutOfLine),
                    error.as_mut().map(fidl::encoding::OutOfLine),
                );
            }
            StackRequest::EnableInterface { id, responder } => {
                responder.send(
                    self.fidl_enable_interface(id).await
                        .err()
                        .as_mut()
                        .map(fidl::encoding::OutOfLine),
                );
            }
            StackRequest::DisableInterface { id, responder } => {
                responder.send(
                    self.fidl_disable_interface(id).err().as_mut().map(fidl::encoding::OutOfLine),
                );
            }
            StackRequest::AddInterfaceAddress { id, addr, responder } => {
                responder.send(
                    self.fidl_add_interface_address(id, addr)
                        .as_mut()
                        .map(fidl::encoding::OutOfLine),
                );
            }
            StackRequest::DelInterfaceAddress { id, addr, responder } => {
                responder.send(
                    self.fidl_del_interface_address(id, addr)
                        .as_mut()
                        .map(fidl::encoding::OutOfLine),
                );
            }
            StackRequest::GetForwardingTable { responder } => {
                responder.send(&mut self.fidl_get_forwarding_table().iter_mut());
            }
            StackRequest::AddForwardingEntry { entry, responder } => {
                responder.send(
                    self.fidl_add_forwarding_entry(entry).as_mut().map(fidl::encoding::OutOfLine),
                );
            }
            StackRequest::DelForwardingEntry { subnet, responder } => {
                responder.send(
                    self.fidl_del_forwarding_entry(subnet).as_mut().map(fidl::encoding::OutOfLine),
                );
            }
            StackRequest::EnablePacketFilter { id, responder } => {
                // TODO(toshik)
            }
            StackRequest::DisablePacketFilter { id, responder } => {
                // TODO(toshik)
            }
            StackRequest::EnableIpForwarding { responder } => {
                // TODO(toshik)
            }
            StackRequest::DisableIpForwarding { responder } => {
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

    fn fidl_del_ethernet_interface(&mut self, id: u64) -> Option<fidl_net_stack::Error> {
        match self.ctx.dispatcher_mut().devices.remove_device(id) {
            Some(info) => {
                // TODO(rheacock): ensure that the core client deletes all data
                None
            }
            None => {
                // Invalid device ID
                Some(stack_fidl_error!(NotFound))
            }
        }
    }

    async fn fidl_list_interfaces(&mut self) -> Vec<fidl_net_stack::InterfaceInfo> {
        let mut devices = vec![];
        for device in self.ctx.dispatcher().devices.iter_devices() {
            // TODO(wesleyac): Cache info and status
            let info = device.client().info().await;
            let status = device.client().get_status().await;
            let is_active = device.is_active();
            let mut addresses = vec![];
            if let Some(core_id) = device.core_id() {
                for addr in get_all_ip_addr_subnet(&self.ctx, core_id) {
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
                    mac: if let Ok(info) = &info { Some(Box::new(info.mac.into())) } else { None },
                    mtu: if let Ok(info) = &info { info.mtu } else { 0 },
                    features: if let Ok(info) = &info { info.features.bits() } else { 0 },
                    administrative_status: if is_active {
                        AdministrativeStatus::Enabled
                    } else {
                        AdministrativeStatus::Disabled
                    },
                    physical_status: match status {
                        Ok(status) => {
                            if status.contains(EthernetStatus::ONLINE) {
                                PhysicalStatus::Up
                            } else {
                                PhysicalStatus::Down
                            }
                        }
                        Err(_) => PhysicalStatus::Down,
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
            self.ctx.dispatcher().get_device_info(id).ok_or(stack_fidl_error!(NotFound))?;
        // TODO(wesleyac): Cache info and status
        let info = device.client().info().await.map_err(|_| stack_fidl_error!(Internal))?;
        let status =
            device.client().get_status().await.map_err(|_| stack_fidl_error!(Internal))?;
        let is_active = device.is_active();
        let mut addresses = vec![];
        if let Some(core_id) = device.core_id() {
            for addr in get_all_ip_addr_subnet(&self.ctx, core_id) {
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
                mac: Some(Box::new(info.mac.into())),
                mtu: info.mtu,
                features: info.features.bits(),
                administrative_status: if is_active {
                    AdministrativeStatus::Enabled
                } else {
                    AdministrativeStatus::Disabled
                },
                physical_status: if status.contains(EthernetStatus::ONLINE) {
                    PhysicalStatus::Up
                } else {
                    PhysicalStatus::Down
                },
                addresses, // TODO(gongt) Handle tentative IPv6 addresses
            },
        });
    }

    async fn fidl_enable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        let (state, disp) = self.ctx.state_and_dispatcher();
        let device = disp.get_device_info(id).ok_or(stack_fidl_error!(NotFound))?;
        let info = device.client().info().await.map_err(|_| stack_fidl_error!(Internal))?;
        // TODO(rheacock, NET-2140): Handle core and driver state in two stages: add device to the
        // core to get an id, then reach into the driver to get updated info before triggering the
        // core to allow traffic on the interface.
        let generate_core_id =
            |dev_info: &DeviceInfo| state.add_ethernet_device(Mac::new(info.mac.octets), info.mtu);
        match disp.devices.activate_device(id, generate_core_id) {
            Ok(_) => Ok(()),
            Err(toggle_error) => {
                match toggle_error {
                    ToggleError::NoChange => Ok(()),
                    ToggleError::NotFound => Err(stack_fidl_error!(NotFound)), // Invalid device ID
                }
            }
        }
    }

    fn fidl_disable_interface(&mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        match self.ctx.dispatcher_mut().devices.deactivate_device(id) {
            Ok((core_id, device_info)) => {
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
                    ToggleError::NotFound => Err(stack_fidl_error!(NotFound)), // Invalid device ID
                }
            }
        }
    }

    fn fidl_add_interface_address(
        &mut self,
        id: u64,
        addr: InterfaceAddress,
    ) -> Option<fidl_net_stack::Error> {
        let device_info = self.ctx.dispatcher().get_device_info(id);
        if let Some(device_info) = device_info {
            match device_info.core_id() {
                Some(device_id) => {
                    // TODO(wesleyac): Check for address already existing.
                    // TODO(joshlf): Return an error if the address/subnet pair is invalid.
                    if let Ok(addr_sub) = addr.try_into_core() {
                        set_ip_addr_subnet(&mut self.ctx, device_id, addr_sub);
                    }
                    None
                }
                None => {
                    // TODO(brunodalbo): We should probably allow adding static addresses
                    // to interfaces that are not installed, return BadState for now
                    Some(stack_fidl_error!(BadState))
                }
            }
        } else {
            Some(stack_fidl_error!(NotFound)) // Invalid device ID
        }
    }

    fn fidl_del_interface_address(
        &mut self,
        id: u64,
        addr: fidl_net_stack::InterfaceAddress,
    ) -> Option<fidl_net_stack::Error> {
        // TODO(eyalsoha): Implement this.
        None
    }

    fn fidl_get_forwarding_table(&self) -> Vec<fidl_net_stack::ForwardingEntry> {
        get_all_routes(&self.ctx)
            .filter_map(|entry| match entry.try_into_fidl_with_ctx(self.ctx.dispatcher()) {
                Ok(entry) => Some(entry),
                Err(e) => {
                    error!("Failed to map forwarding entry into FIDL");
                    None
                }
            })
            .collect()
    }

    fn fidl_add_forwarding_entry(
        &mut self,
        entry: ForwardingEntry,
    ) -> Option<fidl_net_stack::Error> {
        let entry = match EntryEither::try_from_fidl_with_ctx(self.ctx.dispatcher(), entry) {
            Ok(entry) => entry,
            Err(e) => return Some(e.into()),
        };
        match add_route(&mut self.ctx, entry) {
            Ok(_) => None,
            Err(NetstackError::Exists) => {
                // Subnet already in routing table.
                Some(fidl_net_stack::Error { type_: fidl_net_stack::ErrorType::AlreadyExists })
            }
            Err(_) => unreachable!(),
        }
    }

    fn fidl_del_forwarding_entry(
        &mut self,
        subnet: fidl_net::Subnet,
    ) -> Option<fidl_net_stack::Error> {
        if let Ok(subnet) = subnet.try_into_core() {
            match del_device_route(&mut self.ctx, subnet) {
                Ok(_) => None,
                Err(NetstackError::NotFound) => Some(stack_fidl_error!(NotFound)),
                Err(_) => unreachable!(),
            }
        } else {
            Some(stack_fidl_error!(InvalidArgs))
        }
    }
}

struct TimerInfo {
    time: zx::Time,
    id: TimerId,
    abort_handle: AbortHandle,
}

struct EventLoopInner {
    devices: Devices,
    timers: timers::TimerDispatcher<TimerId, Event>,
    rng: OsRng,
    event_send: mpsc::UnboundedSender<Event>,
    #[cfg(test)]
    test_events: Option<mpsc::UnboundedSender<TestEvent>>,
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

    type Rng = OsRng;

    fn rng(&mut self) -> &mut OsRng {
        &mut self.rng
    }
}

impl<B: BufferMut> DeviceLayerEventDispatcher<B> for EventLoopInner {
    fn send_frame<S: Serializer>(&mut self, device: DeviceId, frame: S) -> Result<(), S> {
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

impl UdpEventDispatcher for EventLoopInner {}

impl TransportLayerEventDispatcher for EventLoopInner {}

impl<B: BufferMut> IcmpEventDispatcher<B> for EventLoopInner {
    fn receive_icmp_echo_reply(&mut self, conn: IcmpConnId, seq_num: u16, data: B) {
        #[cfg(test)]
        self.send_test_event(TestEvent::IcmpEchoReply {
            conn,
            seq_num,
            data: data.as_ref().to_owned(),
        });

        // TODO(brunodalbo) implement actual handling of icmp echo replies
    }
}

impl<B: BufferMut> IpLayerEventDispatcher<B> for EventLoopInner {}
