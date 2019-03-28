// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Internet Group Management Protocol.
//!
//! The Internet Group Management Protocol (IGMP) is a communications protocol used
//! by hosts and adjacent routers on IPv4 networks to establish multicast group memberships.
//! IGMP is an integral part of IP multicast.

use crate::ip::IpAddress;
use crate::{Context, EventDispatcher};
use log::trace;
use packet::BufferMut;

/// Receive an IGMP message in an IP packet.
pub(crate) fn receive_igmp_packet<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    buffer: B,
) {
    log_unimplemented!((), "ip::igmp::receive_igmp_packet: Not implemented.")
}
