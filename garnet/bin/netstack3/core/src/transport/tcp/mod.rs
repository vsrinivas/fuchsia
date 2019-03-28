// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Transmission Control Protocol (TCP).

mod conn;
mod listen;
mod types;

pub(crate) use self::types::*;

use std::collections::HashMap;
use std::num::NonZeroU16;

use packet::BufferMut;

use crate::ip::{Ip, IpAddress, Ipv4, Ipv6};
use crate::wire::tcp::{TcpParseArgs, TcpSegment};
use crate::{Context, EventDispatcher};

use self::conn::Conn;
use self::listen::Listener;

/// The state associated with the TCP protocol.
#[derive(Default)]
pub(crate) struct TcpState {
    v4: TcpStateInner<Ipv4>,
    v6: TcpStateInner<Ipv6>,
}

#[derive(Default)]
struct TcpStateInner<I: Ip> {
    conns: HashMap<FourTuple<I::Addr>, Conn>,
    listeners: HashMap<TwoTuple<I::Addr>, Listener>,
}

/// The identifier for timer events in the TCP layer.
#[derive(Copy, Clone, PartialEq)]
pub(crate) struct TcpTimerId {
    // TODO
}

#[derive(Eq, PartialEq, Hash)]
struct TwoTuple<A: IpAddress> {
    local_ip: A,
    local_port: NonZeroU16,
}

#[derive(Eq, PartialEq, Hash)]
struct FourTuple<A: IpAddress> {
    local_ip: A,
    local_port: NonZeroU16,
    remote_ip: A,
    remote_port: NonZeroU16,
}

/// Receive a TCP segment in an IP packet.
///
/// In the event of an unreachable port, `receive_ip_packet` returns the buffer
/// in its original state (with the TCP segment un-parsed) in the `Err` variant.
pub(crate) fn receive_ip_packet<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    mut buffer: B,
) -> Result<(), B> {
    println!("received tcp packet: {:x?}", buffer.as_mut());
    let segment = if let Ok(segment) =
        buffer.parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(src_ip, dst_ip))
    {
        segment
    } else {
        // TODO(joshlf): Do something with ICMP here?
        return Ok(());
    };

    if segment.syn() {
        let _key = TwoTuple { local_ip: dst_ip, local_port: segment.dst_port() };
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

    Ok(())
}
