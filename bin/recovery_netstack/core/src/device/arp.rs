// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Address Resolution Protocol (ARP).

use std::collections::HashMap;
use std::hash::Hash;

use crate::wire::{arp::{ArpPacket, HType, PType},
                  BufferAndRange, SerializationCallback};
use crate::StackState;

/// The type of an ARP operation.
#[derive(Debug, Eq, PartialEq)]
#[allow(missing_docs)]
#[repr(u16)]
pub enum ArpOp {
    Request = ArpOp::REQUEST,
    Response = ArpOp::RESPONSE,
}

impl ArpOp {
    const REQUEST: u16 = 0x0001;
    const RESPONSE: u16 = 0x0002;

    /// Construct an `ArpOp` from a `u16`.
    ///
    /// `from_u16` returns the `ArpOp` with the numerical value `u`, or `None`
    /// if the value is unrecognized.
    pub fn from_u16(u: u16) -> Option<ArpOp> {
        match u {
            Self::REQUEST => Some(ArpOp::Request),
            Self::RESPONSE => Some(ArpOp::Response),
            _ => None,
        }
    }
}

/// An ARP hardware protocol.
#[derive(Debug, PartialEq)]
#[allow(missing_docs)]
#[repr(u16)]
pub enum ArpHardwareType {
    Ethernet = ArpHardwareType::ETHERNET,
}

impl ArpHardwareType {
    const ETHERNET: u16 = 0x0001;

    /// Construct an `ArpHardwareType` from a `u16`.
    ///
    /// `from_u16` returns the `ArpHardwareType` with the numerical value `u`,
    /// or `None` if the value is unrecognized.
    pub fn from_u16(u: u16) -> Option<ArpHardwareType> {
        match u {
            Self::ETHERNET => Some(ArpHardwareType::Ethernet),
            _ => None,
        }
    }
}

/// A device layer protocol which can support ARP.
///
/// An `ArpDevice<P>` is a device layer protocol which can support ARP with the
/// network protocol `P` (e.g., IPv4, IPv6, etc).
pub trait ArpDevice<P: PType + Eq + Hash>: Sized {
    /// The hardware address type used by this protocol.
    type HardwareAddr: HType;

    /// The broadcast address.
    const BROADCAST: Self::HardwareAddr;

    /// Send an ARP packet in a device layer frame.
    ///
    /// `send_arp_frame` accepts a device ID, a destination hardware address,
    /// and a callback. It computes the routing information and invokes the
    /// callback with the number of prefix bytes required by all encapsulating
    /// headers, and the minimum size of the body plus padding. The callback is
    /// expected to return a byte buffer and a range which corresponds to the
    /// desired body. The portion of the buffer beyond the end of the body range
    /// will be treated as padding. The total number of bytes in the body and
    /// the post-body padding must not be smaller than the minimum size passed
    /// to the callback.
    ///
    /// For more details on the callback, see the
    /// [`::wire::SerializationCallback`] documentation.
    ///
    /// # Panics
    ///
    /// `send_arp_frame` panics if the buffer returned from `get_buffer` does
    /// not have sufficient space preceding the body for all encapsulating
    /// headers or does not have enough body plus padding bytes to satisfy the
    /// requirement passed to the callback.
    fn send_arp_frame<B, F>(
        state: &mut StackState, device_id: u64, dst: Self::HardwareAddr, get_buffer: F,
    ) where
        B: AsRef<[u8]> + AsMut<[u8]>,
        F: SerializationCallback<B>;

    /// Get a mutable reference to a device's ARP state.
    fn get_arp_state(state: &mut StackState, device_id: u64) -> &mut ArpState<P, Self>;
}

/// Receive an ARP packet from a device.
///
/// The protocol and hardware types (`P` and `D::HardwareAddr` respectively)
/// must be set statically. Unless there is only one valid pair of protocol and
/// hardware types in a given context, it is the caller's responsibility to call
/// `peek_arp_types` in order to determine which types to use in calling this
/// function.
pub fn receive_arp_packet<P: PType + Eq + Hash, D: ArpDevice<P>, B: AsRef<[u8]> + AsMut<[u8]>>(
    state: &mut StackState, device_id: u64, src_addr: D::HardwareAddr, dst_addr: D::HardwareAddr,
    mut buffer: BufferAndRange<B>,
) {
    let packet = if let Ok(packet) = ArpPacket::<_, D::HardwareAddr, P>::parse(buffer.as_mut()) {
        packet
    } else {
        // TODO(joshlf): Do something else here?
        return;
    };
    log_unimplemented!((), "device::arp::receive_arp_frame: Not implemented");
}

/// Look up the hardware address for a network protocol address.
pub fn lookup<P: PType + Eq + Hash, D: ArpDevice<P>>(
    state: &mut StackState, device_id: u64, local_addr: D::HardwareAddr, lookup: P,
) -> Option<D::HardwareAddr> {
    // TODO(joshlf): Figure out what to do if a frame can't be sent right now
    // because it needs to wait for an ARP reply. Where do we put those frames?
    // How do we associate them with the right ARP reply? How do we retreive
    // them when we get that ARP reply? How do we time out so we don't hold onto
    // a stale frame forever?
    D::get_arp_state(state, device_id)
        .table
        .lookup(lookup)
        .map(|val| val.addr)
}

/// The state associated with an instance of the Address Resolution Protocol
/// (ARP).
///
/// Each device will contain an `ArpState` object for each of the network
/// protocols that it supports.
pub struct ArpState<P: PType + Hash + Eq, D: ArpDevice<P>> {
    // NOTE(joshlf): Taking an ArpDevice type parameter is technically
    // unnecessary here; we could instead just be parametric on a hardware type
    // and a network protocol type. However, doing it this way ensure that
    // device layer code doesn't accidentally invoke receive_arp_packet with
    // different ArpDevice implementations in different places (this would fail
    // to compile because the get_arp_state method on ArpDevice returns an
    // ArpState<_, Self>, which requires that the ArpDevice implementation
    // matches the type of the ArpState stored in that device's state).
    table: ArpTable<D::HardwareAddr, P>,
}

impl<P: PType + Hash + Eq, D: ArpDevice<P>> Default for ArpState<P, D> {
    fn default() -> Self {
        ArpState {
            table: ArpTable::default(),
        }
    }
}

struct ArpTable<H, P: Hash + Eq> {
    table: HashMap<P, ArpValue<H>>,
}

#[derive(Debug, Eq, PartialEq)] // for testing
struct ArpValue<H> {
    addr: H,
}

impl<H, P: Hash + Eq> ArpTable<H, P> {
    fn insert(&mut self, net: P, hw: H) {
        self.table.insert(net, ArpValue { addr: hw });
    }

    fn lookup(&self, addr: P) -> Option<&ArpValue<H>> {
        self.table.get(&addr)
    }
}

impl<H, P: Hash + Eq> Default for ArpTable<H, P> {
    fn default() -> Self {
        ArpTable {
            table: HashMap::default(),
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::device::arp::*;
    use crate::device::ethernet::Mac;
    use crate::ip::Ipv4Addr;

    #[test]
    fn test_arp_table() {
        let mut t: ArpTable<Mac, Ipv4Addr> = ArpTable::default();
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 1])), None);
        t.insert(Ipv4Addr::new([10, 0, 0, 1]), Mac::new([1, 2, 3, 4, 5, 6]));
        assert_eq!(
            t.lookup(Ipv4Addr::new([10, 0, 0, 1])).unwrap().addr,
            Mac::new([1, 2, 3, 4, 5, 6])
        );
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 2])), None);
    }
}
