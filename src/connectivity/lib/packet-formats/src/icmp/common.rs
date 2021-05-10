// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common ICMP packets.

use core::num::NonZeroU16;

use zerocopy::{AsBytes, FromBytes, Unaligned};

use super::IdAndSeq;
use crate::U16;

/// An ICMP Destination Unreachable message.
#[derive(Copy, Clone, Debug, Default, Eq, PartialEq, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub struct IcmpDestUnreachable {
    // Rest of Header in ICMP, unused in ICMPv6.
    //
    // RFC 1191 outlines a method for path MTU discovery for IPv4. When sending a
    // Destination Unreachable message with code = FragmentationRequired (4) (when
    // the don't fragment flag is set and the packet size is too big to send out some
    // link due to its MTU being too small), the RFC requires nodes to include the MTU
    // of the link that was unable to send the packet in bytes 6 and 7 of the message.
    // The new ICMP Destination Unreachable message with code = Fragmentation Required
    // packet format now looks like this (RFC 1191 section 4):
    //
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |   Type = 3    |   Code = 4    |           Checksum            |
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |           unused = 0          |         Next-Hop MTU          |
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |      Internet Header + 64 bits of Original Datagram Data      |
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //
    // Note, the Next-Hop MTU field is still considered unused in all other ICMP
    // messages and all nodes that do not implement RFC 1191.
    _unused: [u8; 2],
    // Used only when DestinationUnreachable Code = FragmentationRequired (4);
    //
    // Note, if this value from an incoming ICMP message is `0`, then we assume the
    // source node of the ICMP message does not implement RFC 1191 and therefore
    // does not actually use the Next-Hop MTU field and still considers it as
    // an unused field.
    next_hop_mtu: U16,
    /* Body of IcmpDestUnreachable is entirely variable-length, so is stored in
     * the message_body field in IcmpPacket */
}

impl IcmpDestUnreachable {
    /// Create a new ICMP Destination Unreachable message for a message with
    /// Code = Fragmentation Required (4) which requires a next hop MTU value
    /// as defined in RFC 1191 section 4.
    pub fn new_for_frag_req(mtu: NonZeroU16) -> Self {
        Self { _unused: [0; 2], next_hop_mtu: U16::new(mtu.get()) }
    }

    /// Get the Next Hop MTU value as defined in RFC 1191 section 4.
    ///
    /// Note, this field is considered unused in all Destination Unreachable
    /// ICMP messages, except for ICMPv4 Destination Unreachable messages with
    /// Code = Fragmentation Required (4).
    pub fn next_hop_mtu(&self) -> Option<NonZeroU16> {
        NonZeroU16::new(self.next_hop_mtu.get())
    }
}

/// An ICMP Echo Request message.
#[derive(Copy, Clone, Debug, Eq, PartialEq, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub struct IcmpEchoRequest {
    pub(super) id_seq: IdAndSeq,
    /* The rest of of IcmpEchoRequest is variable-length, so is stored in the
     * message_body field in IcmpPacket */
}

impl IcmpEchoRequest {
    /// Constructs a new `IcmpEchoRequest`.
    pub fn new(id: u16, seq: u16) -> IcmpEchoRequest {
        IcmpEchoRequest { id_seq: IdAndSeq::new(id, seq) }
    }

    /// Constructs an Echo Reply to this Echo Request.
    ///
    /// `reply` constructs an `IcmpEchoReply` with the same ID and sequence
    /// number as the original request.
    pub fn reply(self) -> IcmpEchoReply {
        IcmpEchoReply { id_seq: self.id_seq }
    }

    /// The ID of this message.
    pub fn id(&self) -> u16 {
        self.id_seq.id.get()
    }

    /// The sequence number of this message.
    pub fn seq(&self) -> u16 {
        self.id_seq.seq.get()
    }
}

/// An ICMP Echo Reply message.
#[derive(Copy, Clone, Debug, Eq, PartialEq, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub struct IcmpEchoReply {
    pub(super) id_seq: IdAndSeq,
    /* The rest of of IcmpEchoReply is variable-length, so is stored in the
     * message_body field in IcmpPacket */
}

impl IcmpEchoReply {
    /// The ID of this message.
    pub fn id(&self) -> u16 {
        self.id_seq.id.get()
    }

    /// The sequence number of this message.
    pub fn seq(&self) -> u16 {
        self.id_seq.seq.get()
    }
}

/// An ICMP Time Exceeded message.
#[derive(Copy, Clone, Default, Debug, Eq, PartialEq, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub struct IcmpTimeExceeded {
    // Rest of Header in ICMP, unused in ICMPv6
    _unused: [u8; 4],
    /* Body of IcmpTimeExceeded is entirely variable-length, so is stored in
     * the message_body field in IcmpPacket */
}
