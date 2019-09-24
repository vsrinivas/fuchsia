// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The User Datagram Protocol (UDP).

use std::num::NonZeroU16;

use log::trace;
use net_types::ip::{Ip, IpAddress};
use net_types::{SpecifiedAddr, Witness};
use packet::{BufferMut, ParsablePacket, Serializer};
use specialize_ip_macro::specialize_ip;
use zerocopy::ByteSlice;

use crate::algorithm::{PortAlloc, PortAllocImpl, ProtocolFlowId};
use crate::context::{RngContext, RngContextExt, StateContext};
use crate::ip::{BufferTransportIpContext, IpPacketFromArgs, IpProto, TransportIpContext};
use crate::transport::{ConnAddrMap, ListenerAddrMap};
use crate::wire::udp::{UdpPacket, UdpPacketBuilder, UdpParseArgs};
use crate::{BufferDispatcher, Context, EventDispatcher};
use std::num::NonZeroUsize;
use std::ops::RangeInclusive;

/// The state associated with the UDP protocol.
#[derive(Default)]
pub struct UdpState<I: Ip> {
    conn_state: UdpConnectionState<I>,
    /// port_aloc is lazy-initialized when it's used
    lazy_port_alloc: Option<PortAlloc<UdpConnectionState<I>>>,
}

/// Holder structure that keeps all the connection maps for UDP connections.
///
/// `UdpConnectionState` provides a [`PortAllocImpl`] implementation to
/// allocate unused local ports.
#[derive(Default)]
struct UdpConnectionState<I: Ip> {
    conns: ConnAddrMap<Conn<I::Addr>>,
    listeners: ListenerAddrMap<Listener<I::Addr>>,
    wildcard_listeners: ListenerAddrMap<NonZeroU16>,
}

/// Helper function to allocate a local port.
///
/// Attempts to allocate a new unused local port with the given flow identifier
/// `id`.
fn try_alloc_local_port<I: Ip, C: UdpContext<I>>(
    ctx: &mut C,
    id: &ProtocolFlowId<I::Addr>,
) -> Option<NonZeroU16> {
    // TODO(brunodalbo): We're crating a split rng context here because we
    // don't have a way to access different contexts mutably. We should pass
    // directly the RngContext defined by the UdpContext once we can do
    // that.
    let mut rng = ctx.new_xorshift_rng();
    let state = ctx.get_state_mut();
    // lazily init port_alloc if it hasn't been inited yet.
    let port_alloc = state.lazy_port_alloc.get_or_insert_with(|| PortAlloc::new(&mut rng));
    port_alloc.try_alloc(&id, &state.conn_state).and_then(NonZeroU16::new)
}

impl<I: Ip> PortAllocImpl for UdpConnectionState<I> {
    const TABLE_SIZE: NonZeroUsize = unsafe { NonZeroUsize::new_unchecked(20) };
    const EPHEMERAL_RANGE: RangeInclusive<u16> = 49152..=65535;
    type Id = ProtocolFlowId<I::Addr>;

    fn is_port_available(&self, id: &Self::Id, port: u16) -> bool {
        // we can safely unwrap here, because the ports received in
        // `is_port_available` are guaranteed to be in `EPHEMERAL_RANGE`.
        let port = NonZeroU16::new(port).unwrap();
        // check if we have any listeners:
        // return true if we have no listeners or active connections using the
        // selected local port:
        self.listeners.get_by_addr(&Listener { addr: *id.local_addr(), port: port }).is_none()
            && self.wildcard_listeners.get_by_addr(&port).is_none()
            && self
                .conns
                .get_id_by_addr(&Conn::from_protocol_flow_and_local_port(id, port))
                .is_none()
    }
}

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
struct Conn<A: IpAddress> {
    local_addr: SpecifiedAddr<A>,
    local_port: NonZeroU16,
    remote_addr: SpecifiedAddr<A>,
    remote_port: NonZeroU16,
}

impl<A: IpAddress> Conn<A> {
    /// Construct a `Conn` from an incoming packet.
    ///
    /// The source is treated as the remote address/port, and the destination is
    /// treated as the local address/port. If there is no source port, then the
    /// packet cannot correspond to a connection, and so None is returned.
    fn from_packet<B: ByteSlice>(
        src_ip: SpecifiedAddr<A>,
        dst_ip: SpecifiedAddr<A>,
        packet: &UdpPacket<B>,
    ) -> Option<Conn<A>> {
        Some(Conn {
            local_addr: dst_ip,
            local_port: packet.dst_port(),
            remote_addr: src_ip,
            remote_port: packet.src_port()?,
        })
    }

    fn from_protocol_flow_and_local_port(id: &ProtocolFlowId<A>, local_port: NonZeroU16) -> Self {
        Self {
            local_addr: *id.local_addr(),
            local_port,
            remote_addr: *id.remote_addr(),
            remote_port: id.remote_port(),
        }
    }
}

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
struct Listener<A: IpAddress> {
    addr: SpecifiedAddr<A>,
    port: NonZeroU16,
}

impl<A: IpAddress> Listener<A> {
    /// Construct a `Listener` from an incoming packet.
    ///
    /// The destination is treated as the local address/port.
    fn from_packet<B: ByteSlice>(dst_ip: SpecifiedAddr<A>, packet: &UdpPacket<B>) -> Listener<A> {
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

#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
enum ListenerType {
    Specified,
    Wildcard,
}

/// The ID identifying a UDP listener.
///
/// When a new UDP listener is added, it is given a unique `UdpListenerId`.
/// These are opaque `usize`s which are intentionally allocated as densely as
/// possible around 0, making it possible to store any associated data in a
/// `Vec` indexed by the ID. The `listener_type` field is used to look at the
/// correct backing `Vec`: `listeners` or `wildcard_listeners`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct UdpListenerId {
    id: usize,
    listener_type: ListenerType,
}

impl UdpListenerId {
    fn new_specified(id: usize) -> Self {
        UdpListenerId { id, listener_type: ListenerType::Specified }
    }

    fn new_wildcard(id: usize) -> Self {
        UdpListenerId { id, listener_type: ListenerType::Wildcard }
    }
}

/// An execution context for the UDP protocol.
pub trait UdpContext<I: Ip>:
    TransportIpContext<I> + StateContext<UdpState<I>> + RngContext
{
}

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
    fn receive_udp_from_conn(&mut self, _conn: UdpConnId, _body: &[u8]) {
        log_unimplemented!((), "UdpEventDispatcher::receive_udp_from_conn: not implemented");
    }

    /// Receive a UDP packet for a listener.
    fn receive_udp_from_listen(
        &mut self,
        _listener: UdpListenerId,
        _src_ip: I::Addr,
        _dst_ip: I::Addr,
        _src_port: Option<NonZeroU16>,
        _body: &[u8],
    ) {
        log_unimplemented!((), "UdpEventDispatcher::receive_udp_from_listen: not implemented");
    }
}

impl<I: Ip, D: EventDispatcher> StateContext<UdpState<I>> for Context<D> {
    fn get_state_with(&self, _id: ()) -> &UdpState<I> {
        #[specialize_ip]
        fn get_state<I: Ip, D: EventDispatcher>(ctx: &Context<D>) -> &UdpState<I> {
            #[ipv4]
            return &ctx.state().transport.udpv4;
            #[ipv6]
            return &ctx.state().transport.udpv6;
        }

        get_state(self)
    }

    fn get_state_mut_with(&mut self, _id: ()) -> &mut UdpState<I> {
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
    fn receive_udp_from_conn(&mut self, _conn: UdpConnId, _body: &[u8]) {
        log_unimplemented!((), "UdpEventDispatcher::receive_udp_from_conn: not implemented");
    }

    /// Receive a UDP packet for a listener.
    fn receive_udp_from_listen<A: IpAddress>(
        &mut self,
        _listener: UdpListenerId,
        _src_ip: A,
        _dst_ip: A,
        _src_port: Option<NonZeroU16>,
        _body: &[u8],
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
    dst_ip: SpecifiedAddr<A>,
    mut buffer: B,
) -> Result<(), B> {
    trace!("received udp packet: {:x?}", buffer.as_mut());
    let packet = if let Ok(packet) =
        buffer.parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(src_ip, dst_ip.get()))
    {
        packet
    } else {
        // TODO(joshlf): Do something with ICMP here?
        return Ok(());
    };

    let state = ctx.get_state();

    if let Some(conn) = SpecifiedAddr::new(src_ip)
        .and_then(|src_ip| Conn::from_packet(src_ip, dst_ip, &packet))
        .and_then(|conn| state.conn_state.conns.get_id_by_addr(&conn))
    {
        ctx.receive_udp_from_conn(UdpConnId(conn), packet.body());
        Ok(())
    } else if let Some(listener) = state
        .conn_state
        .listeners
        .get_by_addr(&Listener::from_packet(dst_ip, &packet))
        .map(UdpListenerId::new_specified)
        .or_else(|| {
            state
                .conn_state
                .wildcard_listeners
                .get_by_addr(&packet.dst_port())
                .map(UdpListenerId::new_wildcard)
        })
    {
        ctx.receive_udp_from_listen(
            listener,
            src_ip,
            dst_ip.get(),
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
// TODO(rheacock): remove `allow(dead_code)` when this is used.
#[allow(dead_code)]
pub(crate) fn send_udp_conn<I: Ip, B: BufferMut, C: BufferUdpContext<I, B>>(
    ctx: &mut C,
    conn: UdpConnId,
    body: B,
) {
    let state = ctx.get_state();
    let Conn { local_addr, local_port, remote_addr, remote_port } = *state
        .conn_state
        .conns
        .get_conn_by_id(conn.0)
        .expect("transport::udp::send_udp_conn: no such conn");

    // TODO(rheacock): Handle an error result here.
    let _ = ctx.send_frame(
        IpPacketFromArgs::new(local_addr, remote_addr, IpProto::Udp),
        body.encapsulate(UdpPacketBuilder::new(
            local_addr.into_addr(),
            remote_addr.into_addr(),
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
// TODO(rheacock): remove `allow(dead_code)` when this is used.
#[allow(dead_code)]
pub(crate) fn send_udp_listener<A: IpAddress, B: BufferMut, C: BufferUdpContext<A::Version, B>>(
    ctx: &mut C,
    listener: UdpListenerId,
    local_addr: SpecifiedAddr<A>,
    remote_addr: SpecifiedAddr<A>,
    remote_port: NonZeroU16,
    body: B,
) {
    if !ctx.is_local_addr(local_addr.get()) {
        // TODO(joshlf): Return error.
        panic!("transport::udp::send_udp::listener: invalid local addr");
    }

    let state = ctx.get_state();

    let local_port = match listener.listener_type {
        ListenerType::Specified => {
            state.conn_state.listeners.get_by_listener(listener.id).and_then(|addrs| {
                // We found the listener. Make sure at least one of the addresses
                // associated with it is the local_addr the caller passed.
                addrs
                    .iter()
                    .find_map(|addr| if addr.addr == local_addr { Some(addr.port) } else { None })
            })
        }
        ListenerType::Wildcard => {
            state.conn_state.wildcard_listeners.get_by_listener(listener.id).map(|ports| ports[0])
        }
    }
    .unwrap_or_else(|| {
        // TODO(brunodalbo) return an error rather than panicking
        panic!("Listener not found");
    });

    // TODO(rheacock): Handle an error result here.
    let _ = ctx.send_frame(
        IpPacketFromArgs::new(local_addr, remote_addr, IpProto::Udp),
        body.encapsulate(UdpPacketBuilder::new(
            local_addr.into_addr(),
            remote_addr.into_addr(),
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
pub fn connect_udp<A: IpAddress, C: UdpContext<A::Version>>(
    ctx: &mut C,
    local_addr: Option<SpecifiedAddr<A>>,
    local_port: Option<NonZeroU16>,
    remote_addr: SpecifiedAddr<A>,
    remote_port: NonZeroU16,
) -> UdpConnId {
    let default_local = if let Some(local) = ctx.local_address_for_remote(remote_addr) {
        local
    } else {
        // TODO(joshlf): There's no route to the remote, so return an error.
        panic!("connect_udp: no route to host");
    };

    let local_addr = local_addr.unwrap_or(default_local);
    let local_port = if let Some(local_port) = local_port {
        local_port
    } else {
        // TODO(brunodalbo): If a local port could not be allocated, return an
        // error instead of panicking.
        try_alloc_local_port(ctx, &ProtocolFlowId::new(local_addr, remote_addr, remote_port))
            .expect("connect_udp: failed to allocate local port")
    };

    let c = Conn { local_addr, local_port, remote_addr, remote_port };
    let listener = Listener { addr: local_addr, port: local_port };
    let state = ctx.get_state_mut();
    if state.conn_state.conns.get_id_by_addr(&c).is_some()
        || state.conn_state.listeners.get_by_addr(&listener).is_some()
    {
        // TODO(joshlf): Return error
        panic!("UDP connection in use");
    }
    UdpConnId(state.conn_state.conns.insert(c.clone(), c))
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
// TODO(rheacock): remove `allow(dead_code)` when this is used.
#[allow(dead_code)]
pub(crate) fn listen_udp<A: IpAddress, C: UdpContext<A::Version>>(
    ctx: &mut C,
    addrs: Vec<SpecifiedAddr<A>>,
    port: NonZeroU16,
) -> UdpListenerId {
    let state = ctx.get_state_mut();
    if addrs.is_empty() {
        if state.conn_state.wildcard_listeners.get_by_addr(&port).is_some() {
            // TODO(joshlf): Return error
            panic!("UDP listener address in use");
        }
        // TODO(joshlf): Check for connections bound to this IP:port.
        UdpListenerId::new_wildcard(state.conn_state.wildcard_listeners.insert(vec![port]))
    } else {
        for addr in &addrs {
            let listener = Listener { addr: *addr, port };
            if state.conn_state.listeners.get_by_addr(&listener).is_some() {
                // TODO(joshlf): Return error
                panic!("UDP listener address in use");
            }
        }
        UdpListenerId::new_specified(
            state
                .conn_state
                .listeners
                .insert(addrs.into_iter().map(|addr| Listener { addr, port }).collect()),
        )
    }
}

#[cfg(test)]
mod tests {
    use net_types::ip::{Ipv4, Ipv6};
    use packet::serialize::Buf;
    use rand_xorshift::XorShiftRng;
    use specialize_ip_macro::ip_test;

    use super::*;
    use crate::ip::IpProto;
    use crate::testutil::{
        get_other_ip_address, new_fake_crypto_rng, set_logger_for_test, FakeCryptoRng,
    };

    /// The listener data sent through a [`DummyUdpContext`].
    struct ListenData<A: IpAddress> {
        listener: UdpListenerId,
        src_ip: A,
        dst_ip: A,
        src_port: Option<NonZeroU16>,
        body: Vec<u8>,
    }

    /// The UDP connection data sent through a [`DummyUdpContext`].
    struct ConnData {
        conn: UdpConnId,
        body: Vec<u8>,
    }

    struct DummyUdpContext<I: Ip> {
        state: UdpState<I>,
        listen_data: Vec<ListenData<I::Addr>>,
        conn_data: Vec<ConnData>,
        rng: FakeCryptoRng<XorShiftRng>,
    }

    impl<I: Ip> Default for DummyUdpContext<I> {
        fn default() -> Self {
            DummyUdpContext {
                state: Default::default(),
                listen_data: Default::default(),
                conn_data: Default::default(),
                rng: new_fake_crypto_rng(0),
            }
        }
    }

    type DummyContext<I> = crate::context::testutil::DummyContext<
        DummyUdpContext<I>,
        (),
        IpPacketFromArgs<<I as Ip>::Addr>,
    >;

    impl<I: Ip> TransportIpContext<I> for DummyContext<I> {
        fn is_local_addr(&self, addr: <I as Ip>::Addr) -> bool {
            local_addr::<I>().into_addr() == addr
        }

        fn local_address_for_remote(
            &self,
            _remote: SpecifiedAddr<<I as Ip>::Addr>,
        ) -> Option<SpecifiedAddr<<I as Ip>::Addr>> {
            Some(local_addr::<I>())
        }
    }

    impl<I: Ip> StateContext<UdpState<I>> for DummyContext<I> {
        fn get_state_with(&self, _id: ()) -> &UdpState<I> {
            &self.get_ref().state
        }

        fn get_state_mut_with(&mut self, _id: ()) -> &mut UdpState<I> {
            &mut self.get_mut().state
        }
    }

    impl<I: Ip> RngContext for DummyContext<I> {
        type Rng = FakeCryptoRng<XorShiftRng>;

        fn rng(&mut self) -> &mut Self::Rng {
            &mut self.get_mut().rng
        }
    }

    impl<I: Ip> UdpContext<I> for DummyContext<I> {}
    impl<I: Ip, B: BufferMut> BufferUdpContext<I, B> for DummyContext<I> {
        fn receive_udp_from_conn(&mut self, conn: UdpConnId, body: &[u8]) {
            self.get_mut().conn_data.push(ConnData { conn, body: body.to_owned() })
        }

        fn receive_udp_from_listen(
            &mut self,
            listener: UdpListenerId,
            src_ip: <I as Ip>::Addr,
            dst_ip: <I as Ip>::Addr,
            src_port: Option<NonZeroU16>,
            body: &[u8],
        ) {
            self.get_mut().listen_data.push(ListenData {
                listener,
                src_ip,
                dst_ip,
                src_port,
                body: body.to_owned(),
            })
        }
    }

    fn local_addr<I: Ip>() -> SpecifiedAddr<I::Addr> {
        get_other_ip_address::<I::Addr>(1)
    }

    fn remote_addr<I: Ip>() -> SpecifiedAddr<I::Addr> {
        get_other_ip_address::<I::Addr>(2)
    }

    /// Helper function to inject an UDP packet with the provided parameters.
    fn receive_udp_packet<I: Ip>(
        ctx: &mut DummyContext<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: NonZeroU16,
        dst_port: NonZeroU16,
        body: &[u8],
    ) {
        let builder = UdpPacketBuilder::new(src_ip, dst_ip, Some(src_port), dst_port);
        let buffer =
            Buf::new(body.to_owned(), ..).encapsulate(builder).serialize_vec_outer().unwrap();
        receive_ip_packet(ctx, src_ip, SpecifiedAddr::new(dst_ip).unwrap(), buffer)
            .expect("Receive IP packet succeeds");
    }

    /// Helper function to test UDP listeners over different IP versions.
    ///
    /// Tests that a listener can be created, that the context receives
    /// packet notifications for that listener, and that we can send data using
    /// that listener.
    #[ip_test]
    fn test_listen_udp<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();
        // Create a listener on local port 100, bound to the local IP:
        let listener =
            listen_udp::<I::Addr, _>(&mut ctx, vec![local_ip], NonZeroU16::new(100).unwrap());
        assert_eq!(listener.listener_type, ListenerType::Specified);

        // Inject a packet and check that the context receives it:
        let body = [1, 2, 3, 4, 5];
        receive_udp_packet(
            &mut ctx,
            remote_ip.into_addr(),
            local_ip.into_addr(),
            NonZeroU16::new(200).unwrap(),
            NonZeroU16::new(100).unwrap(),
            &body[..],
        );

        let listen_data = &ctx.get_ref().listen_data;
        assert_eq!(listen_data.len(), 1);
        let pkt = &listen_data[0];
        assert_eq!(pkt.listener, listener);
        assert_eq!(pkt.src_ip, remote_ip.into_addr());
        assert_eq!(pkt.dst_ip, local_ip.into_addr());
        assert_eq!(pkt.src_port.unwrap().get(), 200);
        assert_eq!(pkt.body, &body[..]);

        // Now send a packet using the listener reference and check that it came
        // out as expected:
        send_udp_listener(
            &mut ctx,
            listener,
            local_ip,
            remote_ip,
            NonZeroU16::new(200).unwrap(),
            Buf::new(body.to_owned(), ..),
        );
        let frames = ctx.frames();
        assert_eq!(frames.len(), 1);
        let (meta, frame_body) = &frames[0];
        assert_eq!(meta.src_ip, local_ip);
        assert_eq!(meta.dst_ip, remote_ip);
        assert_eq!(meta.proto, IpProto::Udp);
        let mut buf = &frame_body[..];
        let packet = UdpPacket::parse(
            &mut buf,
            UdpParseArgs::new(meta.src_ip.into_addr(), meta.dst_ip.into_addr()),
        )
        .expect("Parsed sent UDP packet");
        assert_eq!(packet.src_port().unwrap().get(), 100);
        assert_eq!(packet.dst_port().get(), 200);
        assert_eq!(packet.body(), &body[..]);
    }

    /// Helper function to test that UDP packets without a connection are
    /// dropped.
    ///
    /// Tests that receiving a UDP packet on a port over which there isn't a
    /// listener causes the packet to be dropped correctly.
    #[ip_test]
    fn test_udp_drop<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();

        let body = [1, 2, 3, 4, 5];
        receive_udp_packet(
            &mut ctx,
            remote_ip.into_addr(),
            local_ip.into_addr(),
            NonZeroU16::new(200).unwrap(),
            NonZeroU16::new(100).unwrap(),
            &body[..],
        );
        assert_eq!(ctx.get_ref().listen_data.len(), 0);
        assert_eq!(ctx.get_ref().conn_data.len(), 0);
    }

    /// Helper function to test that udp connections can be created and data can
    /// be transmitted over it.
    ///
    /// Only tests with specified local port and address bounds.
    #[ip_test]
    fn test_udp_conn_basic<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();
        // create a UDP connection with a specified local port and local ip:
        let conn = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            Some(NonZeroU16::new(100).unwrap()),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
        );

        // Inject a UDP packet and see if we receive it on the context:
        let body = [1, 2, 3, 4, 5];
        receive_udp_packet(
            &mut ctx,
            remote_ip.into_addr(),
            local_ip.into_addr(),
            NonZeroU16::new(200).unwrap(),
            NonZeroU16::new(100).unwrap(),
            &body[..],
        );

        let conn_data = &ctx.get_ref().conn_data;
        assert_eq!(conn_data.len(), 1);
        let pkt = &conn_data[0];
        assert_eq!(pkt.conn, conn);
        assert_eq!(pkt.body, &body[..]);

        // Now try to send something over this new connection:
        send_udp_conn(&mut ctx, conn, Buf::new(body.to_owned(), ..));
        let frames = ctx.frames();
        assert_eq!(frames.len(), 1);
        let (meta, frame_body) = &frames[0];
        assert_eq!(meta.src_ip, local_ip);
        assert_eq!(meta.dst_ip, remote_ip);
        assert_eq!(meta.proto, IpProto::Udp);
        let mut buf = &frame_body[..];
        let packet = UdpPacket::parse(
            &mut buf,
            UdpParseArgs::new(meta.src_ip.into_addr(), meta.dst_ip.into_addr()),
        )
        .expect("Parsed sent UDP packet");
        assert_eq!(packet.src_port().unwrap().get(), 100);
        assert_eq!(packet.dst_port().get(), 200);
        assert_eq!(packet.body(), &body[..]);
    }

    /// Tests that if we have multiple listeners and connections, demuxing the
    /// flows is performed correctly.
    #[ip_test]
    fn test_udp_demux<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let remote_ip_a = get_other_ip_address::<I::Addr>(70);
        let remote_ip_b = get_other_ip_address::<I::Addr>(72);
        let local_port_a = NonZeroU16::new(100).unwrap();
        let local_port_b = NonZeroU16::new(101).unwrap();
        let local_port_c = NonZeroU16::new(102).unwrap();
        let local_port_d = NonZeroU16::new(103).unwrap();
        let remote_port_a = NonZeroU16::new(200).unwrap();
        // Create some UDP connections and listeners:
        let conn1 = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            Some(local_port_d),
            remote_ip_a,
            remote_port_a,
        );
        // conn2 has just a remote addr different than conn1
        let conn2 = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            Some(local_port_d),
            remote_ip_b,
            remote_port_a,
        );
        let list1 = listen_udp::<I::Addr, _>(&mut ctx, vec![local_ip], local_port_a);
        let list2 = listen_udp::<I::Addr, _>(&mut ctx, vec![local_ip], local_port_b);
        let wildcard_list = listen_udp::<I::Addr, _>(&mut ctx, vec![], local_port_c);

        // now inject UDP packets that each of the created connections should
        // receive:
        let body_conn1 = [1, 1, 1, 1];
        receive_udp_packet(
            &mut ctx,
            remote_ip_a.into_addr(),
            local_ip.into_addr(),
            remote_port_a,
            local_port_d,
            &body_conn1[..],
        );
        let body_conn2 = [2, 2, 2, 2];
        receive_udp_packet(
            &mut ctx,
            remote_ip_b.into_addr(),
            local_ip.into_addr(),
            remote_port_a,
            local_port_d,
            &body_conn2[..],
        );
        let body_list1 = [3, 3, 3, 3];
        receive_udp_packet(
            &mut ctx,
            remote_ip_a.into_addr(),
            local_ip.into_addr(),
            remote_port_a,
            local_port_a,
            &body_list1[..],
        );
        let body_list2 = [4, 4, 4, 4];
        receive_udp_packet(
            &mut ctx,
            remote_ip_a.into_addr(),
            local_ip.into_addr(),
            remote_port_a,
            local_port_b,
            &body_list2[..],
        );
        let body_wildcard_list = [5, 5, 5, 5];
        receive_udp_packet(
            &mut ctx,
            remote_ip_a.into_addr(),
            local_ip.into_addr(),
            remote_port_a,
            local_port_c,
            &body_wildcard_list[..],
        );
        // check that we got everything in order:
        let conn_packets = &ctx.get_ref().conn_data;
        assert_eq!(conn_packets.len(), 2);
        let pkt = &conn_packets[0];
        assert_eq!(pkt.conn, conn1);
        assert_eq!(pkt.body, &body_conn1[..]);
        let pkt = &conn_packets[1];
        assert_eq!(pkt.conn, conn2);
        assert_eq!(pkt.body, &body_conn2[..]);

        let list_packets = &ctx.get_ref().listen_data;
        assert_eq!(list_packets.len(), 3);
        let pkt = &list_packets[0];
        assert_eq!(pkt.listener, list1);
        assert_eq!(pkt.src_ip, remote_ip_a.into_addr());
        assert_eq!(pkt.dst_ip, local_ip.into_addr());
        assert_eq!(pkt.src_port.unwrap(), remote_port_a);
        assert_eq!(pkt.body, &body_list1[..]);

        let pkt = &list_packets[1];
        assert_eq!(pkt.listener, list2);
        assert_eq!(pkt.src_ip, remote_ip_a.into_addr());
        assert_eq!(pkt.dst_ip, local_ip.into_addr());
        assert_eq!(pkt.src_port.unwrap(), remote_port_a);
        assert_eq!(pkt.body, &body_list2[..]);

        let pkt = &list_packets[2];
        assert_eq!(pkt.listener, wildcard_list);
        assert_eq!(pkt.src_ip, remote_ip_a.into_addr());
        assert_eq!(pkt.dst_ip, local_ip.into_addr());
        assert_eq!(pkt.src_port.unwrap(), remote_port_a);
        assert_eq!(pkt.body, &body_wildcard_list[..]);
    }

    /// Tests UDP wildcard listeners for different IP versions.
    #[ip_test]
    fn test_wildcard_listeners<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();
        let local_ip_a = get_other_ip_address::<I::Addr>(1);
        let local_ip_b = get_other_ip_address::<I::Addr>(2);
        let remote_ip_a = get_other_ip_address::<I::Addr>(70);
        let remote_ip_b = get_other_ip_address::<I::Addr>(72);
        let listener_port = NonZeroU16::new(100).unwrap();
        let remote_port = NonZeroU16::new(200).unwrap();
        let listener = listen_udp::<I::Addr, _>(&mut ctx, vec![], listener_port);
        assert_eq!(listener.listener_type, ListenerType::Wildcard);

        let body = [1, 2, 3, 4, 5];
        receive_udp_packet(
            &mut ctx,
            remote_ip_a.into_addr(),
            local_ip_a.into_addr(),
            remote_port,
            listener_port,
            &body[..],
        );
        // receive into a different local ip:
        receive_udp_packet(
            &mut ctx,
            remote_ip_b.into_addr(),
            local_ip_b.into_addr(),
            remote_port,
            listener_port,
            &body[..],
        );

        // check that we received both packets for the listener:
        let listen_packets = &ctx.get_ref().listen_data;
        assert_eq!(listen_packets.len(), 2);
        let pkt = &listen_packets[0];
        assert_eq!(pkt.listener, listener);
        assert_eq!(pkt.src_ip, remote_ip_a.into_addr());
        assert_eq!(pkt.dst_ip, local_ip_a.into_addr());
        assert_eq!(pkt.src_port.unwrap(), remote_port);
        assert_eq!(pkt.body, &body[..]);
        let pkt = &listen_packets[1];
        assert_eq!(pkt.listener, listener);
        assert_eq!(pkt.src_ip, remote_ip_b.into_addr());
        assert_eq!(pkt.dst_ip, local_ip_b.into_addr());
        assert_eq!(pkt.src_port.unwrap(), remote_port);
        assert_eq!(pkt.body, &body[..]);
    }

    /// Tests establishing a UDP connection without providing a local IP
    #[ip_test]
    fn test_conn_unspecified_local_ip<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();
        let local_port = NonZeroU16::new(100).unwrap();
        let remote_port = NonZeroU16::new(200).unwrap();
        let conn = connect_udp::<I::Addr, _>(
            &mut ctx,
            None,
            Some(local_port),
            remote_addr::<I>(),
            remote_port,
        );
        let connid = ctx.get_ref().state.conn_state.conns.get_conn_by_id(conn.into()).unwrap();

        assert_eq!(connid.local_addr, local_addr::<I>());
        assert_eq!(connid.local_port, local_port);
        assert_eq!(connid.remote_addr, remote_addr::<I>());
        assert_eq!(connid.remote_port, remote_port);
    }

    /// Tests local port allocation for [`connect_udp`].
    ///
    /// Tests that calling [`connect_udp`] causes a valid local port to be
    /// allocated when no local port is passed.
    #[ip_test]
    fn test_udp_local_port_alloc<I: Ip>() {
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let ip_a = get_other_ip_address::<I::Addr>(100);
        let ip_b = get_other_ip_address::<I::Addr>(200);

        let conn_a = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            None,
            ip_a,
            NonZeroU16::new(1010).unwrap(),
        );
        let conn_b = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            None,
            ip_b,
            NonZeroU16::new(1010).unwrap(),
        );
        let conn_c = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            None,
            ip_a,
            NonZeroU16::new(2020).unwrap(),
        );
        let conn_d = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            None,
            ip_a,
            NonZeroU16::new(1010).unwrap(),
        );
        let conns = &ctx.get_ref().state.conn_state.conns;
        let valid_range = &UdpConnectionState::<I>::EPHEMERAL_RANGE;
        let port_a = conns.get_conn_by_id(conn_a.into()).unwrap().local_port.get();
        assert!(valid_range.contains(&port_a));
        let port_b = conns.get_conn_by_id(conn_b.into()).unwrap().local_port.get();
        assert!(valid_range.contains(&port_b));
        assert_ne!(port_a, port_b);
        let port_c = conns.get_conn_by_id(conn_c.into()).unwrap().local_port.get();
        assert!(valid_range.contains(&port_c));
        assert_ne!(port_a, port_c);
        let port_d = conns.get_conn_by_id(conn_d.into()).unwrap().local_port.get();
        assert!(valid_range.contains(&port_d));
        assert_ne!(port_a, port_d);
    }
}
