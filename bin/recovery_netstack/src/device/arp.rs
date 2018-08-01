// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Address Resolution Protocol (ARP).

use device::ethernet::Mac;
use ip::Ipv4Addr;
use std::collections::HashMap;
use std::hash::Hash;
use wire::arp::{ArpPacket, HType, PType};

/// The type of an ARP operation.
#[derive(Debug, PartialEq)]
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

struct ArpTable<H: HType, P: PType + Eq + Hash> {
    table: HashMap<ArpKey<P>, ArpValue<H>>,
}

#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
struct ArpKey<P: PType + Eq + Hash> {
    addr: P,
}

/// A struct to represent the data stored in an entry in the ARP table.
#[derive(Debug, PartialEq)]
pub struct ArpValue<H: HType> {
    /// The MAC address associated with this entry.
    // Using Mac as the value is actually incorrect - there should be a generic
    // hardware address type, if this ARP code is to actually be extensible.
    // However, this can be dealt with if/once we add more hardware types.
    pub mac: H,
}

impl<H: HType, P: PType + Eq + Hash> ArpTable<H, P> {
    pub fn new() -> Self {
        ArpTable {
            table: HashMap::new(),
        }
    }

    pub fn insert(&mut self, addr: P, mac: H) {
        self.table
            .insert(ArpKey { addr }, ArpValue { mac });
    }

    pub fn lookup(&self, addr: P) -> Option<&ArpValue<H>> {
        self.table.get(&ArpKey { addr })
    }
}

#[cfg(test)]
mod tests {
    use device::arp::*;

    #[test]
    fn test_arp_table() {
        let mut t: ArpTable<Mac, Ipv4Addr> = ArpTable::new();
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 1])), None);
        t.insert(Ipv4Addr::new([10, 0, 0, 1]), Mac::new([1, 2, 3, 4, 5, 6]));
        assert_eq!(
            t.lookup(Ipv4Addr::new([10, 0, 0, 1])).unwrap().mac,
            Mac::new([1, 2, 3, 4, 5, 6])
        );
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 2])), None);
    }
}
