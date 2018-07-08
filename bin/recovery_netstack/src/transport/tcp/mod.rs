// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Transmission Control Protocol (TCP).

mod conn;
mod listen;
mod types;

pub use self::types::*;

use std::collections::HashMap;
use std::num::NonZeroU16;

use ip::{Ip, IpAddr, Ipv4, Ipv6};
use wire::tcp::TcpSegment;
use wire::BufferAndRange;
use StackState;

use self::conn::Conn;
use self::listen::Listener;

/// The state associated with the TCP protocol.
#[derive(Default)]
pub struct TcpState {
    v4: TcpStateInner<Ipv4>,
    v6: TcpStateInner<Ipv6>,
}

#[derive(Default)]
struct TcpStateInner<I: Ip> {
    conns: HashMap<FourTuple<I::Addr>, Conn>,
    listeners: HashMap<TwoTuple<I::Addr>, Listener>,
}

#[derive(Eq, PartialEq, Hash)]
struct TwoTuple<A: IpAddr> {
    local_ip: A,
    local_port: NonZeroU16,
}

#[derive(Eq, PartialEq, Hash)]
struct FourTuple<A: IpAddr> {
    local_ip: A,
    local_port: NonZeroU16,
    remote_ip: A,
    remote_port: NonZeroU16,
}

/// Receive a TCP segment in an IP packet.
pub fn receive_ip_packet<A: IpAddr, B: AsMut<[u8]>>(
    state: &mut StackState, src_ip: A, dst_ip: A, mut buffer: BufferAndRange<B>,
) {
    println!("received tcp packet: {:x?}", buffer.as_mut());
    let (segment, body_range) =
        if let Ok((segment, body_range)) = TcpSegment::parse(buffer.as_mut(), src_ip, dst_ip) {
            (segment, body_range)
        } else {
            // TODO(joshlf): Do something with ICMP here?
            return;
        };

    if segment.syn() {
        let _key = TwoTuple {
            local_ip: dst_ip,
            local_port: segment.dst_port(),
        };
    // TODO(joshlf): Lookup and handle
    } else {
        let _key = FourTuple {
            local_ip: dst_ip,
            local_port: segment.dst_port(),
            remote_ip: src_ip,
            remote_port: segment.src_port(),
        };
        // TODO(joshlf): Lookup and handle
    }
}
