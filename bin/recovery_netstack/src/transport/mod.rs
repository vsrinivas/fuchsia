// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The transport layer.

pub mod tcp;

use ip::{IpAddr, IpProto};
use wire::BufferAndRange;
use StackState;

/// The state associated with the transport layer.
pub struct TransportLayerState;

/// Receive a transport layer packet in an IP packet.
///
/// `receive_ip_packet` receives a transport layer packet. If the given protocol
/// is supported, the packet is delivered to that protocol, and
/// `receive_ip_packet` returns `true`. Otherwise, it returns `false`.
pub fn receive_ip_packet<A: IpAddr, B: AsMut<[u8]>>(
    state: &mut StackState, src_ip: A, dst_ip: A, proto: IpProto, buffer: BufferAndRange<B>,
) -> bool {
    match proto {
        IpProto::Tcp => unimplemented!(),
        IpProto::Udp => unimplemented!(),
    }
}
