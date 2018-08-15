// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The transport layer.

pub mod tcp;
pub mod udp;

use crate::ip::{IpAddr, IpProto};
use crate::wire::BufferAndRange;
use crate::{Context, EventDispatcher};

/// The state associated with the transport layer.
#[derive(Default)]
pub struct TransportLayerState {
    tcp: self::tcp::TcpState,
    udp: self::udp::UdpState,
}

/// An event dispatcher for the transport layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait TransportLayerEventDispatcher {}

/// Receive a transport layer packet in an IP packet.
///
/// `receive_ip_packet` receives a transport layer packet. If the given protocol
/// is supported, the packet is delivered to that protocol, and
/// `receive_ip_packet` returns `true`. Otherwise, it returns `false`.
pub fn receive_ip_packet<D: EventDispatcher, A: IpAddr, B: AsMut<[u8]>>(
    ctx: &mut Context<D>, src_ip: A, dst_ip: A, proto: IpProto, buffer: BufferAndRange<B>,
) -> bool {
    match proto {
        IpProto::Tcp => {
            self::tcp::receive_ip_packet(ctx, src_ip, dst_ip, buffer);
            true
        }
        IpProto::Udp => {
            self::udp::receive_ip_packet(ctx, src_ip, dst_ip, buffer);
            true
        }
        // All other protocols are not "transport" protocols.
        _ => false,
    }
}
