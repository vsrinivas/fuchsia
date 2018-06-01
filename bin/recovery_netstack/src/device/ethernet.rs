// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Ethernet protocol.

/// The broadcast MAC address.
///
/// The broadcast MAC address, FF:FF:FF:FF:FF:FF, indicates that a frame should
/// be received by all receivers regardless of their local MAC address.
pub const BROADCAST_MAC: MAC = MAC([0xFF; 6]);

/// A media access control (MAC) address.
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub struct MAC([u8; 6]);

impl MAC {
    /// Construct a new MAC address.
    pub const fn new(bytes: [u8; 6]) -> MAC {
        MAC(bytes)
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
#[repr(u16)]
pub enum EtherType {
    Ipv4 = 0x0800,
    Arp = 0x0806,
    Ipv6 = 0x86DD,
}
