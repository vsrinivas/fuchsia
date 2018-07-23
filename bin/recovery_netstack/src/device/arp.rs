// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Address Resolution Protocol (ARP).

use device::ethernet::Mac;
use ip::Ipv4Addr;
use std::collections::HashMap;

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

struct ArpTable {
    table: HashMap<ArpKey, ArpValue>,
}

#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
struct ArpKey {
    addr: Ipv4Addr,
}

/// A struct to represent the data stored in an entry in the ARP table.
#[derive(Debug, PartialEq)]
pub struct ArpValue {
    /// The MAC address associated with this entry.
    // Using Mac as the value is actually incorrect - there should be a generic
    // hardware address type, if this ARP code is to actually be extensible.
    // However, this can be dealt with if/once we add more hardware types.
    pub mac: Mac,
}

impl ArpTable {
    pub fn new() -> Self {
        ArpTable {
            table: HashMap::new(),
        }
    }

    pub fn insert(&mut self, addr: Ipv4Addr, mac: Mac) {
        self.table
            .insert(ArpKey { addr }, ArpValue { mac });
    }

    pub fn lookup(&self, addr: Ipv4Addr) -> Option<&ArpValue> {
        self.table.get(&ArpKey { addr })
    }
}

#[cfg(test)]
mod tests {
    use device::arp::*;

    #[test]
    fn test_arp_table() {
        let mut t: ArpTable = ArpTable::new();
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 1])), None);
        t.insert(Ipv4Addr::new([10, 0, 0, 1]), Mac::new([1, 2, 3, 4, 5, 6]));
        assert_eq!(
            t.lookup(Ipv4Addr::new([10, 0, 0, 1])).unwrap().mac,
            Mac::new([1, 2, 3, 4, 5, 6])
        );
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 2])), None);
    }
}
