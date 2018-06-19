// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The transport layer.

pub mod tcp;
pub mod udp;

use ip::{IpAddr, IpProto};
use wire::BufferAndRange;
use StackState;

/// The state associated with the transport layer.
pub struct TransportLayerState {
    tcp: self::tcp::TcpState,
    udp: self::udp::UdpState,
}

/// Receive a transport layer packet in an IP packet.
///
/// `receive_ip_packet` receives a transport layer packet. If the given protocol
/// is supported, the packet is delivered to that protocol, and
/// `receive_ip_packet` returns `true`. Otherwise, it returns `false`.
pub fn receive_ip_packet<A: IpAddr, B: AsMut<[u8]>>(
    state: &mut StackState, src_ip: A, dst_ip: A, proto: IpProto, buffer: BufferAndRange<B>,
) -> bool {
    match proto {
        IpProto::Tcp => {
            self::tcp::receive_ip_packet(state, src_ip, dst_ip, buffer);
            true
        }
        IpProto::Udp => {
            self::udp::receive_ip_packet(state, src_ip, dst_ip, buffer);
            true
        }
    }
}
