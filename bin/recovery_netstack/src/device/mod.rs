// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The device layer.

pub mod arp;
pub mod ethernet;

use std::collections::HashMap;

use device::ethernet::EthernetDeviceState;
use ip::{IpAddr, Subnet};
use wire::SerializationCallback;
use StackState;

/// An ID identifying a device.
#[derive(Copy, Clone)]
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
}

#[derive(Copy, Clone)]
enum DeviceProtocol {
    Ethernet,
}

/// The state associated with the device layer.
#[derive(Default)]
pub struct DeviceLayerState {
    // invariant: even though each protocol has its own hash map, keys are
    // unique across all hash maps
    ethernet: HashMap<u64, EthernetDeviceState>,
}

impl DeviceLayerState {
    /// Add a new ethernet device to the device layer.
    pub fn add_ethernet_device(&mut self, id: u64, state: EthernetDeviceState) -> DeviceId{
        self.ethernet.insert(id, state);
        DeviceId::new_ethernet(id)
    }
}

/// Send an IP packet in a device layer frame.
///
/// `send_ip_frame` accepts a device ID, a local IP address, and a callback. It
/// computes the routing information and invokes the callback with the number of
/// prefix bytes required by all encapsulating headers, and the minimum size of
/// the body plus padding. The callback is expected to return a byte buffer and
/// a range which corresponds to the desired body. The portion of the buffer
/// beyond the end of the body range will be treated as padding. The total
/// number of bytes in the body and the post-body padding must not be smaller
/// than the minimum size passed to the callback.
///
/// For more details on the callback, see the [`::wire::SerializationCallback`]
/// documentation.
///
/// # Panics
///
/// `send_ip_frame` panics if the buffer returned from `get_buffer` does not
/// have sufficient space preceding the body for all encapsulating headers or
/// does not have enough body plus padding bytes to satisfy the requirement
/// passed to the callback.
pub fn send_ip_frame<A, B, F>(
    state: &mut StackState, device: DeviceId, local_addr: A, get_buffer: F,
) where
    A: IpAddr,
    B: AsRef<[u8]> + AsMut<[u8]>,
    F: SerializationCallback<B>,
{
    match device.protocol {
        DeviceProtocol::Ethernet => {
            self::ethernet::send_ip_frame(state, device.id, local_addr, get_buffer)
        }
    }
}

/// Receive a device layer frame from the network.
pub fn receive_frame(state: &mut StackState, device: DeviceId, bytes: &mut [u8]) {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::receive_frame(state, device.id, bytes),
    }
}

/// Get the IP address and subnet associated with this device.
pub fn get_ip_addr<A: IpAddr>(state: &mut StackState, device: DeviceId) -> Option<(A, Subnet<A>)> {
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::get_ip_addr(state, device.id),
    }
}

/// Set the IP address and subnet associated with this device.
pub fn set_ip_addr<A: IpAddr>(
    state: &mut StackState, device: DeviceId, addr: A, subnet: Subnet<A>,
) {
    assert!(subnet.contains(addr));
    match device.protocol {
        DeviceProtocol::Ethernet => self::ethernet::set_ip_addr(state, device.id, addr, subnet),
    }
}
