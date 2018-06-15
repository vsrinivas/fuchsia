// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Ethernet protocol.

use zerocopy::{AsBytes, FromBytes, Unaligned};

/// The broadcast MAC address.
///
/// The broadcast MAC address, FF:FF:FF:FF:FF:FF, indicates that a frame should
/// be received by all receivers regardless of their local MAC address.
pub const BROADCAST_MAC: Mac = Mac([0xFF; 6]);

/// A media access control (MAC) address.
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
#[repr(transparent)]
pub struct Mac([u8; 6]);

unsafe impl FromBytes for Mac {}
unsafe impl AsBytes for Mac {}
unsafe impl Unaligned for Mac {}

impl Mac {
    /// Construct a new MAC address.
    pub const fn new(bytes: [u8; 6]) -> Mac {
        Mac(bytes)
    }

    /// Get the bytes of the MAC address.
    pub fn bytes(&self) -> [u8; 6] {
        self.0
    }

    /// Is this a unicast MAC address?
    ///
    /// Returns true if the least significant bit of the first byte of the
    /// address is 0.
    pub fn is_unicast(&self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        self.0[0] & 1 == 0
    }

    /// Is this a multicast MAC address?
    ///
    /// Returns true if the least significant bit of the first byte of the
    /// address is 1.
    pub fn is_multicast(&self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        self.0[0] & 1 == 1
    }

    /// Is this the broadcast MAC address?
    ///
    /// Returns true if this is the broadcast MAC address, FF:FF:FF:FF:FF:FF.
    pub fn is_broadcast(&self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        *self == BROADCAST_MAC
    }
}

/// An EtherType number.
#[allow(missing_docs)]
#[derive(Eq, PartialEq, Debug)]
#[repr(u16)]
pub enum EtherType {
    Ipv4 = EtherType::IPV4,
    Arp = EtherType::ARP,
    Ipv6 = EtherType::IPV6,
}

impl EtherType {
    const IPV4: u16 = 0x0800;
    const ARP: u16 = 0x0806;
    const IPV6: u16 = 0x86DD;

    /// Construct an `EtherType` from a `u16`.
    ///
    /// `from_u16` returns the `EtherType` with the numerical value `u`, or
    /// `None` if the value is unrecognized.
    pub fn from_u16(u: u16) -> Option<EtherType> {
        match u {
            Self::IPV4 => Some(EtherType::Ipv4),
            Self::ARP => Some(EtherType::Arp),
            Self::IPV6 => Some(EtherType::Ipv6),
            _ => None,
        }
    }
}

/// The state associated with an Ethernet device.
pub struct EthernetDeviceState;
