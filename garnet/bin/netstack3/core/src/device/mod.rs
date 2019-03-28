// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The device layer.

pub(crate) mod arp;
pub(crate) mod ethernet;

use std::collections::HashMap;
use std::fmt::{self, Debug, Display, Formatter};

use log::debug;
use packet::{MtuError, Serializer};

use crate::device::ethernet::{EthernetDeviceState, Mac};
use crate::ip::{ext, AddrSubnet, IpAddress, Ipv4Addr, Ipv6Addr};
use crate::{Context, EventDispatcher};

/// An ID identifying a device.
#[derive(Copy, Clone, Eq, PartialEq)]
pub struct DeviceId {
    id: u64,
    protocol: DeviceProtocol,
}

impl DeviceId {
    /// Construct a new `DeviceId` for an Ethernet device.
    pub(crate) fn new_ethernet(id: u64) -> DeviceId {
        DeviceId { id, protocol: DeviceProtocol::Ethernet }
    }

    #[allow(missing_docs)]
    pub fn id(self) -> u64 {
        self.id
    }
}

impl Display for DeviceId {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(f, "{}:{}", self.protocol, self.id)
    }
}

impl Debug for DeviceId {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        Display::fmt(self, f)
    }
}

#[derive(Copy, Clone, Eq, PartialEq)]
enum DeviceProtocol {
    Ethernet,
}

impl Display for DeviceProtocol {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(
            f,
            "{}",
            match self {
                DeviceProtocol::Ethernet => "Ethernet",
            }
        )
    }
}

// TODO(joshlf): Does the IP layer ever need to distinguish between broadcast
// and multicast frames?

/// The type of address used as the source address in a device-layer frame:
/// unicast or broadcast.
#[derive(Copy, Clone, Eq, PartialEq)]
pub(crate) enum FrameDestination {
    Unicast,
    Broadcast,
}

impl FrameDestination {
    /// Is this `FrameDestination::Broadcast`?
    pub(crate) fn is_broadcast(&self) -> bool {
        *self == FrameDestination::Broadcast
    }
}

/// The state associated with the device layer.
pub(crate) struct DeviceLayerState {
    // Invariant: even though each protocol has its own hash map, IDs (used as
    // keys in the hash maps) are unique across all hash maps. This is
    // guaranteed by allocating IDs sequentially, and never re-using an ID.
    next_id: u64,
    ethernet: HashMap<u64, EthernetDeviceState>,
}

impl DeviceLayerState {
    /// Add a new ethernet device to the device layer.
    ///
    /// `add` adds a new `EthernetDeviceState` with the given MAC address and
    /// MTU. The MTU will be taken as a limit on the size of Ethernet payloads -
    /// the Ethernet header is not counted towards the MTU.
    pub(crate) fn add_ethernet_device(&mut self, mac: Mac, mtu: u32) -> DeviceId {
        let id = self.allocate_id();
        self.ethernet.insert(id, EthernetDeviceState::new(mac, mtu));
        debug!("adding Ethernet device with ID {} and MTU {}", id, mtu);
        DeviceId::new_ethernet(id)
    }

    fn allocate_id(&mut self) -> u64 {
        let id = self.next_id;
        self.next_id += 1;
        id
    }
}

// `next_id` starts at 1 for compatiblity with the fuchsia.net.stack.Stack
// interface, which does not allow for IDs of zero.
impl Default for DeviceLayerState {
    fn default() -> DeviceLayerState {
        DeviceLayerState { next_id: 1, ethernet: HashMap::new() }
    }
}

/// The identifier for timer events in the device layer.
#[derive(Copy, Clone, PartialEq)]
pub(crate) enum DeviceLayerTimerId {
    /// A timer event in the ARP layer with a protocol type of IPv4
    ArpIpv4(arp::ArpTimerId<Ipv4Addr>),
}

/// Handle a timer event firing in the device layer.
pub(crate) fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: DeviceLayerTimerId) {
    match id {
        DeviceLayerTimerId::ArpIpv4(inner_id) => arp::handle_timeout(ctx, inner_id),
    }
}

/// An event dispatcher for the device layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait DeviceLayerEventDispatcher {
    /// Send a frame to a device driver.
    fn send_frame(&mut self, device: DeviceId, frame: &[u8]);
}

/// Send an IP packet in a device layer frame.
///
/// `send_ip_frame` accepts a device ID, a local IP address, and a
/// `SerializationRequest`. It computes the routing information and serializes
/// the request in a new device layer frame and sends it.
pub(crate) fn send_ip_frame<D: EventDispatcher, A, S>(
    ctx: &mut Context<D>,
    device: DeviceId,
    local_addr: A,
    body: S,
) -> Result<(), (MtuError<S::InnerError>, S)>
where
    A: IpAddress,
    S: Serializer,
{
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::send_ip_frame(ctx, device.id, local_addr, body),
    }
}

/// Receive a device layer frame from the network.
pub fn receive_frame<D: EventDispatcher>(ctx: &mut Context<D>, device: DeviceId, bytes: &mut [u8]) {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::receive_frame(ctx, device.id, bytes),
    }
}

/// Get the IP address and subnet associated with this device.
pub fn get_ip_addr_subnet<D: EventDispatcher, A: ext::IpAddress>(
    ctx: &mut Context<D>,
    device: DeviceId,
) -> Option<AddrSubnet<A>> {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ip_addr_subnet(ctx, device.id),
    }
}

/// Set the IP address and subnet associated with this device.
pub fn set_ip_addr_subnet<D: EventDispatcher, A: ext::IpAddress>(
    ctx: &mut Context<D>,
    device: DeviceId,
    addr_sub: AddrSubnet<A>,
) {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::set_ip_addr_subnet(ctx, device.id, addr_sub),
    }
}

/// Get the MTU associated with this device.
pub(crate) fn get_mtu<D: EventDispatcher>(ctx: &mut Context<D>, device: DeviceId) -> u32 {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_mtu(ctx, device.id),
    }
}

/// Gets the IPv6 link-local address associated with this device.
// TODO(brunodalbo) when our device model allows for multiple IPs we can have
// a single function go get all the IP addresses associated with a device, which
// would be cleaner and remove the need for this function.
pub fn get_ipv6_link_local_addr<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
) -> Ipv6Addr {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ipv6_link_local_addr(ctx, device.id),
    }
}
