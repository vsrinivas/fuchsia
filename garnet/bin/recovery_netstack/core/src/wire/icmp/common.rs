// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common ICMP packets.

use super::IdAndSeq;

/// An ICMP Destination Unreachable message.
#[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
#[repr(C, packed)]
pub struct IcmpDestUnreachable {
    // Rest of Header in ICMP, unused in ICMPv6
    _unused: [u8; 4],
    /* Body of IcmpDestUnreachable is entirely variable-length, so is stored in
     * the message_body field in IcmpPacket */
}

impl_from_bytes_as_bytes_unaligned!(IcmpDestUnreachable);

/// An ICMP Echo Request message.
#[derive(Copy, Clone, Debug)]
#[repr(C, packed)]
pub struct IcmpEchoRequest {
    pub(super) id_seq: IdAndSeq,
    /* The rest of of IcmpEchoRequest is variable-length, so is stored in the
     * message_body field in IcmpPacket */
}

impl IcmpEchoRequest {
    pub fn reply(self) -> IcmpEchoReply {
        IcmpEchoReply { id_seq: self.id_seq }
    }
}

/// An ICMP Echo Reply message.
#[derive(Copy, Clone, Debug)]
#[repr(C, packed)]
pub struct IcmpEchoReply {
    pub(super) id_seq: IdAndSeq,
    /* The rest of of IcmpEchoReply is variable-length, so is stored in the
     * message_body field in IcmpPacket */
}

impl_from_bytes_as_bytes_unaligned!(IcmpEchoRequest);
impl_from_bytes_as_bytes_unaligned!(IcmpEchoReply);

/// An ICMP Time Exceeded message.
#[derive(Copy, Clone, Default, Debug, Eq, PartialEq)]
#[repr(C, packed)]
pub struct IcmpTimeExceeded {
    // Rest of Header in ICMP, unused in ICMPv6
    _unused: [u8; 4],
    /* Body of IcmpTimeExceeded is entirely variable-length, so is stored in
     * the message_body field in IcmpPacket */
}

impl_from_bytes_as_bytes_unaligned!(IcmpTimeExceeded);
