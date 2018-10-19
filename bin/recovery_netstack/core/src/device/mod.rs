// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The device layer.

pub mod arp;
pub mod ethernet;

use std::collections::HashMap;
use std::fmt::{self, Debug, Display, Formatter};

use log::debug;

use crate::device::ethernet::{EthernetDeviceState, Mac};
use crate::ip::{IpAddr, Ipv4Addr, Subnet};
use crate::wire::SerializationRequest;
use crate::{Context, EventDispatcher};

/// An ID identifying a device.
#[derive(Copy, Clone, Eq, PartialEq)]
pub struct DeviceId {
    id: u64,
    protocol: DeviceProtocol,
}

impl DeviceId {
    fn new_ethernet(id: u64) -> DeviceId {
        DeviceId {
            id,
            protocol: DeviceProtocol::Ethernet,
        }
    }

    #[allow(missing_docs)]
    pub fn id(&self) -> u64 {
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

/// The state associated with the device layer.
#[derive(Default)]
pub struct DeviceLayerState {
    // Invariant: even though each protocol has its own hash map, IDs (used as
    // keys in the hash maps) are unique across all hash maps. This is
    // guaranteed by allocating IDs sequentially, and never re-using an ID.
    next_id: u64,
    ethernet: HashMap<u64, EthernetDeviceState>,
}

impl DeviceLayerState {
    /// Add a new ethernet device to the device layer.
    pub fn add_ethernet_device(&mut self, mac: Mac) -> DeviceId {
        let id = self.allocate_id();
        self.ethernet.insert(id, EthernetDeviceState::new(mac));
        debug!("adding Ethernet device with ID {}", id);
        DeviceId::new_ethernet(id)
    }

    fn allocate_id(&mut self) -> u64 {
        let id = self.next_id;
        self.next_id += 1;
        id
    }
}

/// The identifier for timer events in the device layer.
#[derive(Copy, Clone, PartialEq)]
pub enum DeviceLayerTimerId {
    /// A timer event in the ARP layer with a protocol type of IPv4
    ArpIpv4(arp::ArpTimerId<Ipv4Addr>),
}

/// Handle a timer event firing in the device layer.
pub fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: DeviceLayerTimerId) {
    unimplemented!()
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
pub fn send_ip_frame<D: EventDispatcher, A, S>(
    ctx: &mut Context<D>, device: DeviceId, local_addr: A, body: S,
) where
    A: IpAddr,
    S: SerializationRequest,
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
pub fn get_ip_addr<D: EventDispatcher, A: IpAddr>(
    ctx: &mut Context<D>, device: DeviceId,
) -> Option<(A, Subnet<A>)> {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ip_addr(ctx, device.id),
    }
}

/// Set the IP address and subnet associated with this device.
pub fn set_ip_addr<D: EventDispatcher, A: IpAddr>(
    ctx: &mut Context<D>, device: DeviceId, addr: A, subnet: Subnet<A>,
) {
    assert!(subnet.contains(addr));
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::set_ip_addr(ctx, device.id, addr, subnet),
    }
}
