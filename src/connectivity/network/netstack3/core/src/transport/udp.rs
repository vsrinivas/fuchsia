// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The User Datagram Protocol (UDP).

use std::num::NonZeroU16;

use net_types::ip::{Ip, IpAddress};
use packet::{BufferMut, ParsablePacket, Serializer};
use specialize_ip_macro::specialize_ip;
use zerocopy::ByteSlice;

use crate::context::StateContext;
use crate::ip::{BufferTransportIpContext, IpPacketFromArgs, IpProto, TransportIpContext};
use crate::transport::{ConnAddrMap, ListenerAddrMap};
use crate::wire::udp::{UdpPacket, UdpPacketBuilder, UdpParseArgs};
use crate::{BufferDispatcher, Context, EventDispatcher};

/// The state associated with the UDP protocol.
#[derive(Default)]
pub struct UdpState<I: Ip> {
    conns: ConnAddrMap<Conn<I::Addr>>,
    listeners: ListenerAddrMap<Listener<I::Addr>>,
    wildcard_listeners: ListenerAddrMap<NonZeroU16>,
}

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
struct Conn<A: IpAddress> {
    local_addr: A,
    local_port: NonZeroU16,
    remote_addr: A,
    remote_port: NonZeroU16,
}

impl<A: IpAddress> Conn<A> {
    /// Construct a `Conn` from an incoming packet.
    ///
    /// The source is treated as the remote address/port, and the destination is
    /// treated as the local address/port. If there is no source port, then the
    /// packet cannot correspond to a connection, and so None is returned.
    fn from_packet<B: ByteSlice>(src_ip: A, dst_ip: A, packet: &UdpPacket<B>) -> Option<Conn<A>> {
        Some(Conn {
            local_addr: dst_ip,
            local_port: packet.dst_port(),
            remote_addr: src_ip,
            remote_port: packet.src_port()?,
        })
    }
}

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
struct Listener<A: IpAddress> {
    addr: A,
    port: NonZeroU16,
}

impl<A: IpAddress> Listener<A> {
    /// Construct a `Listener` from an incoming packet.
    ///
    /// The destination is treated as the local address/port.
    fn from_packet<B: ByteSlice>(dst_ip: A, packet: &UdpPacket<B>) -> Listener<A> {
        Listener { addr: dst_ip, port: packet.dst_port() }
    }
}

/// The ID identifying a UDP connection.
///
/// When a new UDP connection is added, it is given a unique `UdpConnId`. These
/// are opaque `usize`s which are intentionally allocated as densely as possible
/// around 0, making it possible to store any associated data in a `Vec` indexed
/// by the ID. `UdpConnId` implements `Into<usize>`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct UdpConnId(usize);

impl From<UdpConnId> for usize {
    fn from(id: UdpConnId) -> usize {
        id.0
    }
}

/// The ID identifying a UDP listener.
///
/// When a new UDP listener is added, it is given a unique `UdpListenerId`.
/// These are opaque `usize`s which are intentionally allocated as densely as
/// possible around 0, making it possible to store any associated data in a
/// `Vec` indexed by the ID. `UdpListenerId` implements `Into<usize>`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct UdpListenerId(usize);

impl From<UdpListenerId> for usize {
    fn from(id: UdpListenerId) -> usize {
        id.0
    }
}

/// An execution context for the UDP protocol.
pub trait UdpContext<I: Ip>: TransportIpContext<I> + StateContext<(), UdpState<I>> {}

/// An execution context for the UDP protocol when a buffer is provided.
///
/// `BufferUdpContext` is like [`UdpContext`], except that it also requires that
/// the context be capable of receiving frames in buffers of type `B`. This is
/// used when a buffer of type `B` is provided to UDP (in particular, in
/// [`send_udp_conn`] and [`send_udp_listener`]), and allows any generated
/// link-layer frames to reuse that buffer rather than needing to always
/// allocate a new one.
pub trait BufferUdpContext<I: Ip, B: BufferMut>:
    UdpContext<I> + BufferTransportIpContext<I, B>
{
    /// Receive a UDP packet for a connection.
    fn receive_udp_from_conn(&mut self, conn: UdpConnId, body: &[u8]) {
        log_unimplemented!((), "UdpEventDispatcher::receive_udp_from_conn: not implemented");
    }

    /// Receive a UDP packet for a listener.
    fn receive_udp_from_listen(
        &mut self,
        listener: UdpListenerId,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: &[u8],
    ) {
        log_unimplemented!((), "UdpEventDispatcher::receive_udp_from_listen: not implemented");
    }
}

impl<I: Ip, D: EventDispatcher> StateContext<(), UdpState<I>> for Context<D> {
    fn get_state(&self, _id: ()) -> &UdpState<I> {
        #[specialize_ip]
        fn get_state<I: Ip, D: EventDispatcher>(ctx: &Context<D>) -> &UdpState<I> {
            #[ipv4]
            return &ctx.state().transport.udpv4;
            #[ipv6]
            return &ctx.state().transport.udpv6;
        }

        get_state(self)
    }

    fn get_state_mut(&mut self, _id: ()) -> &mut UdpState<I> {
        #[specialize_ip]
        fn get_state_mut<I: Ip, D: EventDispatcher>(ctx: &mut Context<D>) -> &mut UdpState<I> {
            #[ipv4]
            return &mut ctx.state_mut().transport.udpv4;
            #[ipv6]
            return &mut ctx.state_mut().transport.udpv6;
        }

        get_state_mut(self)
    }
}

impl<I: Ip, D: EventDispatcher> UdpContext<I> for Context<D> {}

impl<I: Ip, B: BufferMut, D: BufferDispatcher<B>> BufferUdpContext<I, B> for Context<D> {
    fn receive_udp_from_conn(&mut self, conn: UdpConnId, body: &[u8]) {
        self.dispatcher_mut().receive_udp_from_conn(conn, body);
    }

    fn receive_udp_from_listen(
        &mut self,
        listener: UdpListenerId,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: &[u8],
    ) {
        self.dispatcher_mut().receive_udp_from_listen(listener, src_ip, dst_ip, src_port, body);
    }
}

/// An event dispatcher for the UDP layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait UdpEventDispatcher {
    /// Receive a UDP packet for a connection.
    fn receive_udp_from_conn(&mut self, conn: UdpConnId, body: &[u8]) {
        log_unimplemented!((), "UdpEventDispatcher::receive_udp_from_conn: not implemented");
    }

    /// Receive a UDP packet for a listener.
    fn receive_udp_from_listen<A: IpAddress>(
        &mut self,
        listener: UdpListenerId,
        src_ip: A,
        dst_ip: A,
        src_port: Option<NonZeroU16>,
        body: &[u8],
    ) {
        log_unimplemented!((), "UdpEventDispatcher::receive_udp_from_listen: not implemented");
    }
}

/// Receive a UDP packet in an IP packet.
///
/// In the event of an unreachable port, `receive_ip_packet` returns the buffer
/// in its original state (with the UDP packet un-parsed) in the `Err` variant.
pub(crate) fn receive_ip_packet<A: IpAddress, B: BufferMut, C: BufferUdpContext<A::Version, B>>(
    ctx: &mut C,
    src_ip: A,
    dst_ip: A,
    mut buffer: B,
) -> Result<(), B> {
    println!("received udp packet: {:x?}", buffer.as_mut());
    let packet = if let Ok(packet) =
        buffer.parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(src_ip, dst_ip))
    {
        packet
    } else {
        // TODO(joshlf): Do something with ICMP here?
        return Ok(());
    };

    let state = ctx.get_state(());

    if let Some(conn) =
        Conn::from_packet(src_ip, dst_ip, &packet).and_then(|conn| state.conns.get_by_addr(&conn))
    {
        ctx.receive_udp_from_conn(UdpConnId(conn), packet.body());
        Ok(())
    } else if let Some(listener) = state
        .listeners
        .get_by_addr(&Listener::from_packet(dst_ip, &packet))
        .or_else(|| state.wildcard_listeners.get_by_addr(&packet.dst_port()))
    {
        ctx.receive_udp_from_listen(
            UdpListenerId(listener),
            src_ip,
            dst_ip,
            packet.src_port(),
            packet.body(),
        );
        Ok(())
    } else if cfg!(feature = "udp-icmp-port-unreachable") {
        // NOTE: We currently have no way of enabling this feature from our
        // build system, so it is always disabled.
        //
        // TODO(joshlf): Support enabling this feature.

        // Responding with an ICMP Port Unreachable error is a vector for
        // reflected DoS attacks - the attacker can send a UDP packet to a
        // closed port with the source address set to the address of the victim,
        // and we will send the ICMP response there. Luckily, according to RFC
        // 1122, "if a datagram arrives addressed to a UDP port for which there
        // is no pending LISTEN call, UDP SHOULD send an ICMP Port Unreachable
        // message." Since it is not mandatory, we choose to disable it by
        // default.

        // Unfortunately, type inference isn't smart enough for us to just do
        // packet.parse_metadata().
        let meta = ParsablePacket::<_, crate::wire::udp::UdpParseArgs<A>>::parse_metadata(&packet);
        std::mem::drop(packet);
        buffer.undo_parse(meta);
        Err(buffer)
    } else {
        Ok(())
    }
}

/// Send a UDP packet on an existing connection.
///
/// # Panics
///
/// `send_udp_conn` panics if `conn` is not associated with a connection for this IP version.
pub(crate) fn send_udp_conn<I: Ip, B: BufferMut, C: BufferUdpContext<I, B>>(
    ctx: &mut C,
    conn: UdpConnId,
    body: B,
) {
    let state = ctx.get_state(());
    let Conn { local_addr, local_port, remote_addr, remote_port } =
        *state.conns.get_by_conn(conn.0).expect("transport::udp::send_udp_conn: no such conn");

    ctx.send_frame(
        IpPacketFromArgs::new(local_addr, remote_addr, IpProto::Udp),
        body.encapsulate(UdpPacketBuilder::new(
            local_addr,
            remote_addr,
            Some(local_port),
            remote_port,
        )),
    );
}

/// Send a UDP packet on an existing listener.
///
/// `send_udp_listener` sends a UDP packet on an existing listener. The caller
/// must specify the local address in order to disambiguate in case the listener
/// is bound to multiple local addresses. If the listener is not bound to the
/// local address provided, `send_udp_listener` will fail.
///
/// # Panics
///
/// `send_udp_listener` panics if `listener` is not associated with a listener
/// for this IP version.
pub(crate) fn send_udp_listener<A: IpAddress, B: BufferMut, C: BufferUdpContext<A::Version, B>>(
    ctx: &mut C,
    listener: UdpListenerId,
    local_addr: A,
    remote_addr: A,
    remote_port: NonZeroU16,
    body: B,
) {
    if !ctx.is_local_addr(local_addr) {
        // TODO(joshlf): Return error.
        panic!("transport::udp::send_udp::listener: invalid local addr");
    }

    let state = ctx.get_state(());

    let local_port: Result<_, ()> = state
        .listeners
        .get_by_listener(listener.0)
        .map(|addrs| {
            // We found the listener. Make sure at least one of the addresses
            // associated with it is the local_addr the caller passed. Return a
            // result with Ok if one of the addresses matched, and Err
            // otherwise.
            addrs
                .iter()
                .find_map(|addr| if addr.addr == local_addr { Some(addr.port) } else { None })
                .ok_or_else(|| unimplemented!())
        })
        .or_else(|| {
            // We didn't find the listener in state.listeners. Maybe it's a
            // wildcard listener. Wildcard listeners are only associated with
            // ports, so if we find it, we can return Ok immediately to match
            // the result that we produce if we find the listener in
            // state.listeners. This is OK since we already check that
            // local_addr is a local address in the if block above (we would do
            // it here, but it results in conflicting lifetimes).
            state.wildcard_listeners.get_by_listener(listener.0).map(|ports| Ok(ports[0]))
            // We didn't find the listener in either map, so we panic.
        })
        .expect("transport::udp::send_udp_listener: no such listener");

    // TODO(joshlf): Return an error rather than panicking.
    let local_port = local_port.unwrap();

    ctx.send_frame(
        IpPacketFromArgs::new(local_addr, remote_addr, IpProto::Udp),
        body.encapsulate(UdpPacketBuilder::new(
            local_addr,
            remote_addr,
            Some(local_port),
            remote_port,
        )),
    );
}

/// Create a UDP connection.
///
/// `connect_udp` binds `conn` as a connection to the remote address and port.
/// It is also bound to the local address and port, meaning that packets sent on
/// this connection will always come from that address and port. If `local_addr`
/// is `None`, then the local address will be chosen based on the route to the
/// remote address. If `local_port` is `None`, then one will be chosen from the
/// available local ports.
///
/// If both `local_addr` and `local_port` are specified, but conflict with an
/// existing connection or listener, `connect_udp` will fail. If one or both are
/// left unspecified, but there is still no way to satisfy the request (e.g.,
/// `local_addr` is specified, but there are no available local ports for that
/// address), `connect_udp` will fail. If there is no route to `remote_addr`,
/// `connect_udp` will fail.
///
/// # Panics
///
/// `connect_udp` panics if `conn` is already in use.
pub(crate) fn connect_udp<A: IpAddress, B: BufferMut, C: BufferUdpContext<A::Version, B>>(
    ctx: &mut C,
    local_addr: Option<A>,
    local_port: Option<NonZeroU16>,
    remote_addr: A,
    remote_port: NonZeroU16,
) -> UdpConnId {
    let default_local = if let Some(local) = ctx.local_address_for_remote(remote_addr) {
        local
    } else {
        // TODO(joshlf): There's no route to the remote, so return an error.
        panic!("connect_udp: no route to host");
    };

    let local_addr = local_addr.unwrap_or(default_local);
    if let Some(local_port) = local_port {
        let c = Conn { local_addr, local_port, remote_addr, remote_port };
        let listener = Listener { addr: local_addr, port: local_port };
        let state = ctx.get_state_mut(());
        if state.conns.get_by_addr(&c).is_some() || state.listeners.get_by_addr(&listener).is_some()
        {
            // TODO(joshlf): Return error
            panic!("UDP connection in use");
        }
        UdpConnId(state.conns.insert(c))
    } else {
        unimplemented!()
    }
}

/// Listen on for incoming UDP packets.
///
/// `listen_udp` registers `listener` as a listener for incoming UDP packets on
/// the given `port`. If `addrs` is empty, the listener is a "wildcard
/// listener", and is bound to all local addresses. See the `transport` module
/// documentation for more details.
///
/// If `addrs` is not empty, and any of the addresses in `addrs` is already
/// bound on the given port (either by a listener or a connection), `listen_udp`
/// will fail. If `addrs` is empty, and a wildcard listener is already bound to
/// the given port, `listen_udp` will fail.
///
/// # Panics
///
/// `listen_udp` panics if `listener` is already in use.
pub(crate) fn listen_udp<A: IpAddress, B: BufferMut, C: BufferUdpContext<A::Version, B>>(
    ctx: &mut C,
    addrs: Vec<A>,
    port: NonZeroU16,
) -> UdpListenerId {
    let mut state = ctx.get_state_mut(());
    if addrs.is_empty() {
        if state.wildcard_listeners.get_by_addr(&port).is_some() {
            // TODO(joshlf): Return error
            panic!("UDP listener address in use");
        }
        // TODO(joshlf): Check for connections bound to this IP:port.
        UdpListenerId(state.wildcard_listeners.insert(vec![port]))
    } else {
        for addr in &addrs {
            let listener = Listener { addr: *addr, port };
            if state.listeners.get_by_addr(&listener).is_some() {
                // TODO(joshlf): Return error
                panic!("UDP listener address in use");
            }
        }
        UdpListenerId(
            state.listeners.insert(addrs.into_iter().map(|addr| Listener { addr, port }).collect()),
        )
    }
}
