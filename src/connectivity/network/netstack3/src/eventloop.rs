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
//! Having a single queue for all of the message types is beneficial, since in guarantees a FIFO
//! ordering for all messages - whichever messages arrive first, will be handled first.
//!
//! We'll look at each type of message, to see how each one is handled - starting with FIDL
//! messages, since they can be thought of as the entrypoint for the whole loop (as nothing happens
//! until a FIDL call is made).
//!
//! # FIDL Worker
//!
//! The FIDL part of the event loop implements the fuchsia.net.stack.Stack and
//! fuchsia.net.SocketProvider interfaces. The type of the event loop message for a FIDL call is
//! simply the generated FIDL type. When the event loop starts up, we use `fuchsia_app` to start a
//! FIDL server that simply sends all of the events it receives to the event loop (via the sender
//! end of the mpsc queue). When `EventLoop` receives this message, it calls the
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
//! drawbacks - notably, it can be difficult to reason about what exactly the behaviour of the
//! timers is - see the comment below on race conditions. Particularly, it's a bit tricky that the
//! timer is not cancelled when the timer trigger message is _sent_, but when it is _received_.

#![allow(unused)]

use ethernet as eth;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;

use std::fs::File;
use std::marker::PhantomData;
use std::time::{Duration, Instant};

use failure::{bail, Error};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_hardware_ethernet as fidl_ethernet;
use fidl_fuchsia_hardware_ethernet_ext::{EthernetInfo, EthernetStatus, MacAddress};
use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net::{SocketControlRequest, SocketProviderRequest};
use fidl_fuchsia_net_ext as fidl_net_ext;
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
use futures::channel::mpsc;
use futures::future::{AbortHandle, Abortable};
use futures::prelude::*;
use futures::{select, TryFutureExt, TryStreamExt};
use log::{error, info};

use netstack3_core::{
    add_device_route, del_device_route, get_all_routes, get_ip_addr_subnet, handle_timeout,
    receive_frame, set_ip_addr_subnet, AddrSubnet, AddrSubnetEither, Context, DeviceId,
    DeviceLayerEventDispatcher, EntryDest, EntryEither, EventDispatcher, Mac, NetstackError,
    StackState, Subnet, SubnetEither, TimerId, TransportLayerEventDispatcher, UdpEventDispatcher,
};

/// The message that is sent to the main event loop to indicate that an
/// ethernet device has been set up, and is ready to be added to the event
/// loop.
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
                let vmo = zx::Vmo::create_with_opts(
                    zx::VmoOptions::NON_RESIZABLE,
                    256 * eth::DEFAULT_BUFFER_SIZE as u64,
                )?;
                let eth_client = await!(eth::Client::new(
                    self.dev,
                    vmo,
                    eth::DEFAULT_BUFFER_SIZE,
                    "recovery-ns"
                ))?;
                let info = await!(eth_client.info())?;
                await!(eth_client.start())?;
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
    id: DeviceId,
    events: eth::EventStream,
}

impl EthernetWorker {
    fn new(id: DeviceId, events: eth::EventStream) -> Self {
        EthernetWorker { id, events }
    }

    fn spawn(mut self, sender: mpsc::UnboundedSender<Event>) {
        let mut events = self.events;
        let id = self.id;
        fasync::spawn_local(
            async move {
                while let Some(evt) = await!(events.try_next())? {
                    sender.unbounded_send(Event::EthEvent((id, evt)));
                }
                Ok(())
            }
                .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }
}

/// The events that can trigger an action in the event loop.
pub enum Event {
    /// A request from the fuchsia.net.stack.Stack FIDL interface.
    FidlStackEvent(StackRequest),
    /// A request from the fuchsia.net.SocketProvider FIDL interface.
    FidlSocketProviderEvent(SocketProviderRequest),
    /// A request from the fuchsia.net.SocketControl FIDL interface.
    FidlSocketControlEvent(SocketControlRequest),
    /// An event from an ethernet interface. Either a status change or a frame.
    EthEvent((DeviceId, eth::Event)),
    /// An indication that an ethernet device is ready to be used.
    EthSetupEvent(EthernetDeviceReady),
    /// A timer firing.
    TimerEvent(TimerId),
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
        EventLoop {
            ctx: Context::new(
                StackState::default(),
                EventLoopInner { devices: vec![], timers: vec![], event_send: event_send.clone() },
            ),
            event_recv,
        }
    }

    pub async fn run(mut self) -> Result<(), Error> {
        let mut buf = [0; 2048];
        loop {
            match await!(self.event_recv.next()) {
                Some(Event::EthSetupEvent(setup)) => {
                    let (mut state, mut disp) = self.ctx.state_and_dispatcher();
                    let eth_id =
                        state.add_ethernet_device(Mac::new(setup.info.mac.octets), setup.info.mtu);
                    let eth_worker = EthernetWorker::new(eth_id, setup.client.get_stream());
                    disp.devices.push(DeviceInfo {
                        id: eth_id,
                        path: setup.path,
                        client: setup.client,
                    });
                    eth_worker.spawn(self.ctx.dispatcher().event_send.clone());
                    setup.responder.send(None, eth_id.id());
                }
                Some(Event::FidlStackEvent(req)) => {
                    await!(self.handle_fidl_stack_request(req));
                }
                Some(Event::FidlSocketProviderEvent(req)) => {
                    await!(self.handle_fidl_socket_provider_request(req));
                }
                Some(Event::FidlSocketControlEvent(req)) => {
                    await!(self.handle_fidl_socket_control_request(req));
                }
                Some(Event::EthEvent((id, eth::Event::StatusChanged))) => {
                    info!("device {:?} status changed", id.id());
                    // We need to call get_status even if we don't use the output, since calling it
                    // acks the message, and prevents the device from sending more status changed
                    // messages.
                    // TODO(wesleyac): Error checking on get_device_client - is a race possible?
                    await!(self
                        .ctx
                        .dispatcher()
                        .get_device_client(id.id())
                        .unwrap()
                        .client
                        .get_status());
                }
                Some(Event::EthEvent((id, eth::Event::Receive(rx, _flags)))) => {
                    // TODO(wesleyac): Check flags
                    let len = rx.read(&mut buf);
                    receive_frame(&mut self.ctx, id, &mut buf[..len]);
                }
                Some(Event::TimerEvent(id)) => {
                    // cancel_timeout() should be called before handle_timeout().
                    // Suppose handle_timeout() is called first and it reinstalls
                    // the timer event, the timer event will be erroneously cancelled by the
                    // cancel_timeout() before it's being triggered.
                    // TODO(NET-2138): Create a unit test for the processing logic.
                    self.ctx.dispatcher().cancel_timeout(id);
                    handle_timeout(&mut self.ctx, id);
                }
                None => bail!("Stream of events ended unexpectedly"),
            }
        }
        Ok(())
    }

    async fn handle_fidl_socket_provider_request(&mut self, req: SocketProviderRequest) {
        match req {
            SocketProviderRequest::Socket { domain, type_, protocol, responder } => {
                match (domain as i32, type_ as i32) {
                    _ => {
                        responder.send(libc::ENOSYS as i16, None);
                    }
                }
            }
            SocketProviderRequest::GetAddrInfo { node, service, hints, responder } => {
                // TODO(wesleyac)
            }
        }
    }

    async fn handle_fidl_socket_control_request(&mut self, req: SocketControlRequest) {
        match req {
            SocketControlRequest::Close { responder } => {}
            SocketControlRequest::Ioctl { req, in_, responder } => {}
            SocketControlRequest::Connect { addr, responder } => {}
            SocketControlRequest::Accept { flags, responder } => {}
            SocketControlRequest::Bind { addr, responder } => {}
            SocketControlRequest::Listen { backlog, responder } => {}
            SocketControlRequest::GetSockName { responder } => {}
            SocketControlRequest::GetPeerName { responder } => {}
            SocketControlRequest::SetSockOpt { level, optname, optval, responder } => {}
            SocketControlRequest::GetSockOpt { level, optname, responder } => {}
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
                responder.send(&mut await!(self.fidl_list_interfaces()).iter_mut());
            }
            StackRequest::GetInterfaceInfo { id, responder } => {
                let (mut info, mut error) = match (self.fidl_get_interface_info(id)) {
                    Ok(info) => (Some(info), None),
                    Err(error) => (None, Some(error)),
                };
                responder.send(
                    info.as_mut().map(fidl::encoding::OutOfLine),
                    error.as_mut().map(fidl::encoding::OutOfLine),
                );
            }
            StackRequest::EnableInterface { id, responder } => {
                responder
                    .send(self.fidl_enable_interface(id).as_mut().map(fidl::encoding::OutOfLine));
            }
            StackRequest::DisableInterface { id, responder } => {
                responder
                    .send(self.fidl_disable_interface(id).as_mut().map(fidl::encoding::OutOfLine));
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
        None
    }

    async fn fidl_list_interfaces(&mut self) -> Vec<fidl_net_stack::InterfaceInfo> {
        let mut devices = vec![];
        for device in self.ctx.dispatcher().devices.iter() {
            // TODO(wesleyac): Cache info and status
            let info = await!(device.client.info());
            let status = await!(device.client.get_status());
            devices.push(InterfaceInfo {
                id: device.id.id(),
                properties: InterfaceProperties {
                    path: device.path.clone(),
                    mac: if let Ok(info) = &info { Some(Box::new(info.mac.into())) } else { None },
                    mtu: if let Ok(info) = &info { info.mtu } else { 0 },
                    features: if let Ok(info) = &info { info.features.bits() } else { 0 },
                    administrative_status: AdministrativeStatus::Enabled, // TODO(wesleyac) this
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
                    addresses: vec![], //TODO(wesleyac): this
                },
            });
        }
        devices
    }

    fn fidl_get_interface_info(
        &self,
        id: u64,
    ) -> Result<fidl_net_stack::InterfaceInfo, fidl_net_stack::Error> {
        // TODO(eyalsoha): Implement this.
        Err(fidl_net_stack::Error { type_: fidl_net_stack::ErrorType::NotFound })
    }

    fn fidl_enable_interface(&mut self, id: u64) -> Option<fidl_net_stack::Error> {
        // TODO(eyalsoha): Implement this.
        None
    }

    fn fidl_disable_interface(&mut self, id: u64) -> Option<fidl_net_stack::Error> {
        // TODO(eyalsoha): Implement this.
        None
    }

    fn fidl_add_interface_address(
        &mut self,
        id: u64,
        addr: InterfaceAddress,
    ) -> Option<fidl_net_stack::Error> {
        let device_id = self.ctx.dispatcher().get_device_client(id).map(|x| x.id);
        if let Some(device_id) = device_id {
            // TODO(wesleyac): Check for address already existing.
            // TODO(joshlf): Return an error if the address/subnet pair is invalid.
            if let Some(addr_sub) = AddrSubnetEither::new(
                fidl_net_ext::IpAddress::from(addr.ip_address).0.into(),
                addr.prefix_len,
            ) {
                set_ip_addr_subnet(&mut self.ctx, device_id, addr_sub);
            }
            None
        } else {
            Some(fidl_net_stack::Error { type_: fidl_net_stack::ErrorType::NotFound }) // Invalid device ID
        }
    }

    fn fidl_del_interface_address(
        &mut self,
        id: u64,
        addr: fidl_net::IpAddress,
    ) -> Option<fidl_net_stack::Error> {
        // TODO(eyalsoha): Implement this.
        None
    }

    fn fidl_get_forwarding_table(&self) -> Vec<fidl_net_stack::ForwardingEntry> {
        get_all_routes(&self.ctx)
            .map(|entry| match entry {
                EntryEither::V4(v4_entry) => fidl_net_stack::ForwardingEntry {
                    subnet: fidl_net::Subnet {
                        addr: fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address {
                            addr: v4_entry.subnet.network().ipv4_bytes(),
                        }),
                        prefix_len: v4_entry.subnet.prefix(),
                    },
                    destination: match v4_entry.dest {
                        EntryDest::Local { device } => {
                            fidl_net_stack::ForwardingDestination::DeviceId(device.id())
                        }
                        EntryDest::Remote { next_hop } => {
                            fidl_net_stack::ForwardingDestination::NextHop(
                                fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address {
                                    addr: next_hop.ipv4_bytes(),
                                }),
                            )
                        }
                    },
                },
                EntryEither::V6(v6_entry) => fidl_net_stack::ForwardingEntry {
                    subnet: fidl_net::Subnet {
                        addr: fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address {
                            addr: v6_entry.subnet.network().ipv6_bytes(),
                        }),
                        prefix_len: v6_entry.subnet.prefix(),
                    },
                    destination: match v6_entry.dest {
                        EntryDest::Local { device } => {
                            fidl_net_stack::ForwardingDestination::DeviceId(device.id())
                        }
                        EntryDest::Remote { next_hop } => {
                            fidl_net_stack::ForwardingDestination::NextHop(
                                fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address {
                                    addr: next_hop.ipv6_bytes(),
                                }),
                            )
                        }
                    },
                },
            })
            .collect()
    }

    fn fidl_add_forwarding_entry(
        &mut self,
        entry: ForwardingEntry,
    ) -> Option<fidl_net_stack::Error> {
        match entry.destination {
            fidl_net_stack::ForwardingDestination::DeviceId(id) => {
                if let Some(device_id) = self.ctx.dispatcher().get_device_client(id).map(|x| x.id) {
                    if let Some(subnet) = SubnetEither::new(
                        fidl_net_ext::IpAddress::from(entry.subnet.addr).0.into(),
                        entry.subnet.prefix_len,
                    ) {
                        match add_device_route(&mut self.ctx, subnet, device_id) {
                            Ok(_) => None,
                            Err(NetstackError::Exists) => {
                                // Subnet already in routing table.
                                Some(fidl_net_stack::Error {
                                    type_: fidl_net_stack::ErrorType::AlreadyExists,
                                })
                            }
                            Err(_) => unreachable!(),
                        }
                    } else {
                        // Invalid subnet
                        Some(fidl_net_stack::Error {
                            type_: fidl_net_stack::ErrorType::InvalidArgs,
                        })
                    }
                } else {
                    // Invalid device ID
                    Some(fidl_net_stack::Error { type_: fidl_net_stack::ErrorType::NotFound })
                }
            }
            fidl_net_stack::ForwardingDestination::NextHop(x) => None,
        }
    }

    fn fidl_del_forwarding_entry(
        &mut self,
        subnet: fidl_net::Subnet,
    ) -> Option<fidl_net_stack::Error> {
        if let Some(subnet) = SubnetEither::new(
            fidl_net_ext::IpAddress::from(subnet.addr).0.into(),
            subnet.prefix_len,
        ) {
            match del_device_route(&mut self.ctx, subnet) {
                Ok(_) => None,
                Err(NetstackError::NotFound) => {
                    Some(fidl_net_stack::Error { type_: fidl_net_stack::ErrorType::NotFound })
                }
                Err(_) => unreachable!(),
            }
        } else {
            Some(fidl_net_stack::Error { type_: fidl_net_stack::ErrorType::InvalidArgs })
        }
    }
}

struct TimerInfo {
    time: Instant,
    id: TimerId,
    abort_handle: AbortHandle,
}

struct DeviceInfo {
    id: DeviceId,
    path: String,
    client: eth::Client,
}

struct EventLoopInner {
    devices: Vec<DeviceInfo>,
    timers: Vec<TimerInfo>,
    event_send: mpsc::UnboundedSender<Event>,
}

impl EventLoopInner {
    fn get_device_client(&self, id: u64) -> Option<&DeviceInfo> {
        self.devices.iter().find(|d| d.id.id() == id)
    }
}

impl EventDispatcher for EventLoopInner {
    fn schedule_timeout(&mut self, duration: Duration, id: TimerId) -> Option<Instant> {
        // We need to separately keep track of the time at which the future completes (a Zircon
        // Time object) and the time at which the user expects it to complete. You cannot convert
        // between Zircon Time objects and std::time::Instant objects (since std::time::Instance is
        // opaque), so we generate two different time objects to keep track of.
        let zircon_time = zx::Time::after(zx::Duration::from(duration));
        let rust_time = Instant::now() + duration;

        let old_timer = self.cancel_timeout(id);

        let mut timer_send = self.event_send.clone();
        let timeout = async move {
            await!(fasync::Timer::new(zircon_time));
            timer_send.send(Event::TimerEvent(id));
            // The timer's cancel function is called by the receiver, so that
            // this async block does not need to have a reference to the
            // eventloop.
        };

        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        self.timers.push(TimerInfo { time: rust_time, id, abort_handle });

        let timeout = Abortable::new(timeout, abort_registration);
        let timeout = timeout.unwrap_or_else(|_| ());

        fasync::spawn_local(timeout);
        old_timer
    }

    fn schedule_timeout_instant(&mut self, _time: Instant, _id: TimerId) -> Option<Instant> {
        // It's not possible to convert a std::time::Instant to a Zircon Time, so this API will
        // need some more thought. Punting on this until we need it.
        unimplemented!()
    }

    // TODO(wesleyac): Fix race
    //
    // There is a potential race in the following condition:
    //
    // 1. Timer is set
    // 2. Ethernet event gets enqueued
    // 3. Timer future fires (enqueueing timer message - timer has _not_ been cancelled yet)
    // 4. Ethernet event gets handled. This attempts to reschedule the timer into the future.
    // 5. Timer message gets handled - event is triggered as if it happened at a time in the
    //    future!
    //
    // The fix to this should be to have the event loop drain the queue without yielding, thus
    // ensuring that the timer future cannot fire until the main queue is empty. See
    // `GroupAvailable` in `garnet/bin/wlan/wlanstack/src/future_util.rs` for an example of how to
    // do this.
    fn cancel_timeout(&mut self, id: TimerId) -> Option<Instant> {
        let index =
            self.timers.iter().enumerate().find_map(
                |x| {
                    if x.1.id == id {
                        Some(x.0)
                    } else {
                        None
                    }
                },
            )?;
        self.timers[index].abort_handle.abort();
        Some(self.timers.remove(index).time)
    }
}

impl DeviceLayerEventDispatcher for EventLoopInner {
    fn send_frame(&mut self, device: DeviceId, frame: &[u8]) {
        // TODO(wesleyac): Error handling
        for dev in self.devices.iter_mut() {
            if dev.id == device {
                dev.client.send(&frame);
            }
        }
    }
}

impl UdpEventDispatcher for EventLoopInner {
    type UdpConn = ();
    type UdpListener = ();
}

impl TransportLayerEventDispatcher for EventLoopInner {}
