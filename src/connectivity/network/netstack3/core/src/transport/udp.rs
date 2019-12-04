// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The User Datagram Protocol (UDP).

use std::collections::HashSet;
use std::num::{NonZeroU16, NonZeroUsize};
use std::ops::RangeInclusive;

use failure::Fail;
use log::trace;
use net_types::ip::{Ip, IpAddress};
use net_types::{SpecifiedAddr, Witness};
use packet::{BufferMut, ParsablePacket, Serializer};
use specialize_ip_macro::specialize_ip;
use zerocopy::ByteSlice;

use crate::algorithm::{PortAlloc, PortAllocImpl, ProtocolFlowId};
use crate::context::{RngContext, RngContextExt, StateContext};
use crate::data_structures::IdMapCollectionKey;
use crate::error::{ConnectError, LocalAddressError, NetstackError, RemoteAddressError};
use crate::ip::{
    BufferTransportIpContext, IpPacketFromArgs, IpProto, IpVersionMarker, TransportIpContext,
};
use crate::transport::{ConnAddrMap, ListenerAddrMap};
use crate::wire::udp::{UdpPacket, UdpPacketBuilder, UdpParseArgs};
use crate::{BufferDispatcher, Context, EventDispatcher};

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

impl<I: Ip> UdpConnectionState<I> {
    /// Collects the currently used local ports into a [`HashSet`].
    ///
    /// If `addrs` is empty, `collect_used_local_ports` returns all the local
    /// ports currently in use, otherwise it returns all the local ports in use
    /// for the addresses in `addrs`.
    fn collect_used_local_ports<'a>(
        &self,
        addrs: impl ExactSizeIterator<Item = &'a SpecifiedAddr<I::Addr>> + Clone,
    ) -> HashSet<NonZeroU16> {
        let mut ports = HashSet::new();
        ports.extend(self.wildcard_listeners.addr_to_listener.keys());
        if addrs.len() == 0 {
            // for wildcard addresses, collect ALL local ports
            ports.extend(self.listeners.addr_to_listener.keys().map(|l| l.port));
            ports.extend(self.conns.addr_to_id.keys().map(|c| c.local_port));
        } else {
            // if `addrs` is not empty, just collect the ones that use the same
            // local addresses.
            ports.extend(self.listeners.addr_to_listener.keys().filter_map(|l| {
                if addrs.clone().any(|a| a == &l.addr) {
                    Some(l.port)
                } else {
                    None
                }
            }));
            ports.extend(self.conns.addr_to_id.keys().filter_map(|c| {
                if addrs.clone().any(|a| a == &c.local_addr) {
                    Some(c.local_port)
                } else {
                    None
                }
            }));
        }
        ports
    }

    /// Checks whether the provided port is available to be used for a listener.
    ///
    /// If `addr` is `None`, `is_listen_port_available` will only return `true`
    /// if *no* connections or listeners bound to any addresses are using the
    /// provided `port`.
    fn is_listen_port_available(
        &self,
        addr: Option<SpecifiedAddr<I::Addr>>,
        port: NonZeroU16,
    ) -> bool {
        self.wildcard_listeners.get_by_addr(&port).is_none()
            && addr
                .map(|addr| {
                    self.listeners.get_by_addr(&Listener { addr, port }).is_none()
                        && !self
                            .conns
                            .addr_to_id
                            .keys()
                            .any(|c| c.local_addr == addr && c.local_port == port)
                })
                .unwrap_or_else(|| {
                    !(self.listeners.addr_to_listener.keys().any(|l| l.port == port)
                        || self.conns.addr_to_id.keys().any(|c| c.local_port == port))
                })
    }
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

/// Helper function to allocate a listen port.
///
/// Finds a random ephemeral port that is not in the provided `used_ports` set.
fn try_alloc_listen_port<I: Ip, C: UdpContext<I>>(
    ctx: &mut C,
    used_ports: &HashSet<NonZeroU16>,
) -> Option<NonZeroU16> {
    let mut port = UdpConnectionState::<I>::rand_ephemeral(ctx.rng());
    for _ in UdpConnectionState::<I>::EPHEMERAL_RANGE {
        // we can unwrap here because we know that the EPHEMERAL_RANGE doesn't
        // include 0.
        let tryport = NonZeroU16::new(port.get()).unwrap();
        if !used_ports.contains(&tryport) {
            return Some(tryport);
        }
        port.next();
    }
    None
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

impl<'a, A: IpAddress> From<&'a Conn<A>> for Conn<A> {
    fn from(c: &'a Conn<A>) -> Self {
        c.clone()
    }
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

/// Information associated with a UDP connection
pub struct UdpConnInfo<A: IpAddress> {
    /// The local address associated with a UDP connection.
    pub local_addr: SpecifiedAddr<A>,
    /// The local port associated with a UDP connection.
    pub local_port: NonZeroU16,
    /// The remote address associated with a UDP connection.
    pub remote_addr: SpecifiedAddr<A>,
    /// The remote port associated with a UDP connection.
    pub remote_port: NonZeroU16,
}

impl<A: IpAddress> From<Conn<A>> for UdpConnInfo<A> {
    fn from(c: Conn<A>) -> Self {
        let Conn { local_addr, local_port, remote_addr, remote_port } = c;
        Self { local_addr, local_port, remote_addr, remote_port }
    }
}

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
struct Listener<A: IpAddress> {
    addr: SpecifiedAddr<A>,
    port: NonZeroU16,
}

/// Information associated with a UDP listener
pub struct UdpListenerInfo<A: IpAddress> {
    /// The local address associated with a UDP listener, or `None` for any
    /// address.
    pub local_addr: Option<SpecifiedAddr<A>>,
    /// The local port associated with a UDP listener.
    pub local_port: NonZeroU16,
}

impl<A: IpAddress> From<Listener<A>> for UdpListenerInfo<A> {
    fn from(l: Listener<A>) -> Self {
        Self { local_addr: Some(l.addr), local_port: l.port }
    }
}

impl<A: IpAddress> From<NonZeroU16> for UdpListenerInfo<A> {
    fn from(local_port: NonZeroU16) -> Self {
        Self { local_addr: None, local_port }
    }
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
pub struct UdpConnId<I: Ip>(usize, IpVersionMarker<I>);

impl<I: Ip> UdpConnId<I> {
    fn new(id: usize) -> UdpConnId<I> {
        UdpConnId(id, IpVersionMarker::default())
    }
}

impl<I: Ip> From<UdpConnId<I>> for usize {
    fn from(id: UdpConnId<I>) -> usize {
        id.0
    }
}

impl<I: Ip> IdMapCollectionKey for UdpConnId<I> {
    const VARIANT_COUNT: usize = 1;

    fn get_variant(&self) -> usize {
        0
    }

    fn get_id(&self) -> usize {
        self.0
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
pub struct UdpListenerId<I: Ip> {
    id: usize,
    listener_type: ListenerType,
    _marker: IpVersionMarker<I>,
}

impl<I: Ip> UdpListenerId<I> {
    fn new_specified(id: usize) -> Self {
        UdpListenerId {
            id,
            listener_type: ListenerType::Specified,
            _marker: IpVersionMarker::default(),
        }
    }

    fn new_wildcard(id: usize) -> Self {
        UdpListenerId {
            id,
            listener_type: ListenerType::Wildcard,
            _marker: IpVersionMarker::default(),
        }
    }
}

impl<I: Ip> IdMapCollectionKey for UdpListenerId<I> {
    const VARIANT_COUNT: usize = 2;
    fn get_variant(&self) -> usize {
        match self.listener_type {
            ListenerType::Specified => 0,
            ListenerType::Wildcard => 1,
        }
    }
    fn get_id(&self) -> usize {
        self.id
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
    fn receive_udp_from_conn(
        &mut self,
        _conn: UdpConnId<I>,
        _src_ip: I::Addr,
        _src_port: NonZeroU16,
        _body: &[u8],
    ) {
        log_unimplemented!((), "UdpEventDispatcher::receive_udp_from_conn: not implemented");
    }

    /// Receive a UDP packet for a listener.
    fn receive_udp_from_listen(
        &mut self,
        _listener: UdpListenerId<I>,
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

impl<I: Ip, B: BufferMut, D: BufferDispatcher<B> + UdpEventDispatcher<I>> BufferUdpContext<I, B>
    for Context<D>
{
    fn receive_udp_from_conn(
        &mut self,
        conn: UdpConnId<I>,
        src_ip: I::Addr,
        src_port: NonZeroU16,
        body: &[u8],
    ) {
        self.dispatcher_mut().receive_udp_from_conn(conn, src_ip, src_port, body);
    }

    fn receive_udp_from_listen(
        &mut self,
        listener: UdpListenerId<I>,
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
pub trait UdpEventDispatcher<I: Ip> {
    /// Receive a UDP packet for a connection.
    fn receive_udp_from_conn(
        &mut self,
        _conn: UdpConnId<I>,
        _src_ip: I::Addr,
        _src_port: NonZeroU16,
        _body: &[u8],
    ) {
        log_unimplemented!((), "UdpEventDispatcher::receive_udp_from_conn: not implemented");
    }

    /// Receive a UDP packet for a listener.
    fn receive_udp_from_listen(
        &mut self,
        _listener: UdpListenerId<I>,
        _src_ip: I::Addr,
        _dst_ip: I::Addr,
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
    trace!("received UDP packet: {:x?}", buffer.as_mut());
    let packet = if let Ok(packet) =
        buffer.parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(src_ip, dst_ip.get()))
    {
        packet
    } else {
        // TODO(joshlf): Do something with ICMP here?
        return Ok(());
    };

    let state = ctx.get_state();

    if let Some((conn_id, conn)) = SpecifiedAddr::new(src_ip)
        .and_then(|src_ip| Conn::from_packet(src_ip, dst_ip, &packet))
        .and_then(|conn| state.conn_state.conns.get_id_by_addr(&conn).map(|id| (id, conn)))
    {
        ctx.receive_udp_from_conn(
            UdpConnId::new(conn_id),
            conn.remote_addr.get(),
            conn.remote_port,
            packet.body(),
        );
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

/// Sends a single UDP frame without creating a connection or listener.
///
/// `send_udp` is equivalent to creating a UDP connection with [`connect_udp`]
/// with the same arguments provided to `send_udp`, sending `body` over the
/// created connection and, finally, destroying the connection.
///
/// `send_udp` fails if the selected 4-tuple conflicts with any existing socket.
// TODO(brunodalbo) we may need more arguments here to express REUSEADDR and
// BIND_TO_DEVICE options
pub fn send_udp<A: IpAddress, B: BufferMut, C: BufferUdpContext<A::Version, B>>(
    ctx: &mut C,
    local_addr: Option<SpecifiedAddr<A>>,
    local_port: Option<NonZeroU16>,
    remote_addr: SpecifiedAddr<A>,
    remote_port: NonZeroU16,
    body: B,
) -> crate::error::Result<()> {
    // TODO(brunodalbo) this can be faster if we just perform the checks but
    // don't actually create a UDP connection.
    let tmp_conn = connect_udp(ctx, local_addr, local_port, remote_addr, remote_port)
        .map_err(|e| NetstackError::Connect(e))?;

    // Not using `?` here since we need to `remove_udp_conn` even in the case of failure.
    let ret = send_udp_conn(ctx, tmp_conn, body).map_err(NetstackError::SendUdp);
    remove_udp_conn(ctx, tmp_conn);

    ret
}

/// Send a UDP packet on an existing connection.
pub fn send_udp_conn<I: Ip, B: BufferMut, C: BufferUdpContext<I, B>>(
    ctx: &mut C,
    conn: UdpConnId<I>,
    body: B,
) -> Result<(), SendError> {
    let state = ctx.get_state();
    let Conn { local_addr, local_port, remote_addr, remote_port } = *state
        .conn_state
        .conns
        .get_conn_by_id(conn.0)
        .expect("transport::udp::send_udp_conn: no such conn");

    ctx.send_frame(
        IpPacketFromArgs::new(local_addr, remote_addr, IpProto::Udp),
        body.encapsulate(UdpPacketBuilder::new(
            local_addr.into_addr(),
            remote_addr.into_addr(),
            Some(local_port),
            remote_port,
        )),
    )
    .map_err(Into::into)
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
pub fn send_udp_listener<A: IpAddress, B: BufferMut, C: BufferUdpContext<A::Version, B>>(
    ctx: &mut C,
    listener: UdpListenerId<A::Version>,
    local_addr: Option<SpecifiedAddr<A>>,
    remote_addr: SpecifiedAddr<A>,
    remote_port: NonZeroU16,
    body: B,
) -> Result<(), SendError> {
    let local_addr = match local_addr {
        Some(a) => a,
        // TODO(brunodalbo) this may cause problems when we don't match the
        // bound listener addresses, we should revisit whether that check is
        // actually necessary.
        // Also, if the local address is a multicast address this function
        // should probably fail and `send_udp` must be used instead
        None => ctx
            .local_address_for_remote(remote_addr)
            .ok_or(SendError::Remote(RemoteAddressError::NoRoute))?,
    };
    if !ctx.is_local_addr(local_addr.get()) {
        return Err(SendError::Local(LocalAddressError::CannotBindToAddress));
    }

    let state = ctx.get_state();

    let local_port = match listener.listener_type {
        ListenerType::Specified => {
            let addrs = state
                .conn_state
                .listeners
                .get_by_listener(listener.id)
                .expect("specified listener not found");
            // We found the listener. Make sure at least one of the addresses
            // associated with it is the local_addr the caller passed.
            addrs
                .iter()
                .find_map(|addr| if addr.addr == local_addr { Some(addr.port) } else { None })
                .ok_or(SendError::Local(LocalAddressError::AddressMismatch))?
        }
        ListenerType::Wildcard => {
            let ports = state
                .conn_state
                .wildcard_listeners
                .get_by_listener(listener.id)
                .expect("wildcard listener not found");
            ports[0]
        }
    };

    ctx.send_frame(
        IpPacketFromArgs::new(local_addr, remote_addr, IpProto::Udp),
        body.encapsulate(UdpPacketBuilder::new(
            local_addr.into_addr(),
            remote_addr.into_addr(),
            Some(local_port),
            remote_port,
        )),
    )?;

    Ok(())
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
pub fn connect_udp<A: IpAddress, C: UdpContext<A::Version>>(
    ctx: &mut C,
    local_addr: Option<SpecifiedAddr<A>>,
    local_port: Option<NonZeroU16>,
    remote_addr: SpecifiedAddr<A>,
    remote_port: NonZeroU16,
) -> Result<UdpConnId<A::Version>, ConnectError> {
    let default_local = ctx
        .local_address_for_remote(remote_addr)
        .ok_or(ConnectError::Remote(RemoteAddressError::NoRoute))?;

    let local_addr = local_addr.unwrap_or(default_local);

    if !ctx.is_local_addr(local_addr.get()) {
        return Err(ConnectError::Local(LocalAddressError::CannotBindToAddress));
    }
    let local_port = if let Some(local_port) = local_port {
        local_port
    } else {
        try_alloc_local_port(ctx, &ProtocolFlowId::new(local_addr, remote_addr, remote_port))
            .ok_or(ConnectError::Local(LocalAddressError::FailedToAllocateLocalPort))?
    };

    let c = Conn { local_addr, local_port, remote_addr, remote_port };
    let listener = Listener { addr: local_addr, port: local_port };
    let state = ctx.get_state_mut();
    if state.conn_state.conns.get_id_by_addr(&c).is_some()
        || state.conn_state.listeners.get_by_addr(&listener).is_some()
    {
        return Err(ConnectError::ConnectionInUse);
    }
    Ok(UdpConnId::new(state.conn_state.conns.insert(c.clone(), c)))
}

/// Removes a previously registered UDP connection.
///
/// `remove_udp_conn` removes a previously registered UDP connection indexed by
/// the [`UpConnId`] `id`. It returns the [`UdpConnInfo`] information that was
/// associated with that UDP connection.
///
/// # Panics
///
/// `remove_udp_conn` panics if `id` is not a valid `UdpConnId`.
pub fn remove_udp_conn<I: Ip, C: UdpContext<I>>(
    ctx: &mut C,
    id: UdpConnId<I>,
) -> UdpConnInfo<I::Addr> {
    let state = ctx.get_state_mut();
    state.conn_state.conns.remove_by_id(id.into()).expect("UDP connection not found").into()
}

/// Gets the [`UdpConnInfo`] associated with the UDP connection referenced by [`id`].
///
/// # Panics
///
/// `get_udp_conn_info` panics if `id` is not a valid `UdpConnId`.
pub fn get_udp_conn_info<I: Ip, C: UdpContext<I>>(
    ctx: &C,
    id: UdpConnId<I>,
) -> UdpConnInfo<I::Addr> {
    ctx.get_state()
        .conn_state
        .conns
        .get_conn_by_id(id.into())
        .expect("UDP connection not found")
        .clone()
        .into()
}

/// Listen on for incoming UDP packets.
///
/// `listen_udp` registers `listener` as a listener for incoming UDP packets on
/// the given `port`. If `addr` is `None`, the listener is a "wildcard
/// listener", and is bound to all local addresses. See the `transport` module
/// documentation for more details.
///
/// If `addr` is `Some``, and `addr` is already bound on the given port (either
/// by a listener or a connection), `listen_udp` will fail. If `addr` is `None`,
/// and a wildcard listener is already bound to the given port, `listen_udp`
/// will fail.
///
/// # Panics
///
/// `listen_udp` panics if `listener` is already in use.
pub fn listen_udp<A: IpAddress, C: UdpContext<A::Version>>(
    ctx: &mut C,
    addr: Option<SpecifiedAddr<A>>,
    port: Option<NonZeroU16>,
) -> UdpListenerId<A::Version> {
    let port = if let Some(port) = port {
        if !ctx.get_state().conn_state.is_listen_port_available(addr, port) {
            // TODO(brunodalbo) return error
            panic!("UDP listen port already in use")
        }
        port
    } else {
        let used_ports =
            ctx.get_state_mut().conn_state.collect_used_local_ports(addr.as_ref().into_iter());
        // TODO(brunodalbo) return error
        try_alloc_listen_port(ctx, &used_ports).expect("UDP failed to alloc local port")
    };
    match addr {
        None => {
            let state = ctx.get_state_mut();
            UdpListenerId::new_wildcard(state.conn_state.wildcard_listeners.insert(vec![port]))
        }
        Some(addr) => {
            if !ctx.is_local_addr(addr.get()) {
                // TODO(brunodalbo) return error
                panic!("UDP can't bind to address");
            }
            let state = ctx.get_state_mut();
            UdpListenerId::new_specified(
                state.conn_state.listeners.insert(vec![Listener { addr, port }]),
            )
        }
    }
}

/// Removes a previously registered UDP listener.
///
/// `remove_udp_listener` removes a previously registered UDP listener indexed
/// by the [`UdpListenerId`] `id`. It returns the [`UdpListenerInfo`]
/// information that was associated with that UDP listener.
///
/// # Panics
///
/// `remove_listener` panics if `id` is not a valid `UdpListenerId`.
pub fn remove_udp_listener<I: Ip, C: UdpContext<I>>(
    ctx: &mut C,
    id: UdpListenerId<I>,
) -> UdpListenerInfo<I::Addr> {
    let state = ctx.get_state_mut();
    match id.listener_type {
        ListenerType::Specified => state
            .conn_state
            .listeners
            .remove_by_listener(id.id)
            .expect("Invalid UDP listener ID")
            // NOTE(brunodalbo) ListenerAddrMap keeps vecs internally, but we
            // always only add a single address, so unwrap the first one
            .first()
            .expect("Unexpected empty UDP listener")
            .clone()
            .into(),
        ListenerType::Wildcard => state
            .conn_state
            .wildcard_listeners
            .remove_by_listener(id.id)
            .expect("Invalid UDP listener ID")
            // NOTE(brunodalbo) ListenerAddrMap keeps vecs internally, but we
            // always only add a single address, so unwrap the first one
            .first()
            .expect("Unexpected empty UDP listener")
            .clone()
            .into(),
    }
}

/// Gets the [`UdpListenerInfo`] associated with the UDP listener referenced by
/// [`id`].
///
/// # Panics
///
/// `get_udp_conn_info` panics if `id` is not a valid `UdpListenerId`.
pub fn get_udp_listener_info<I: Ip, C: UdpContext<I>>(
    ctx: &C,
    id: UdpListenerId<I>,
) -> UdpListenerInfo<I::Addr> {
    let state = ctx.get_state();
    match id.listener_type {
        ListenerType::Specified => state
            .conn_state
            .listeners
            .get_by_listener(id.id)
            .expect("UDP listener not found")
            // NOTE(brunodalbo) ListenerAddrMap keeps vecs internally, but we
            // always only add a single address, so unwrap the first one
            .first()
            .map(|l| l.clone().into())
            .expect("Unexpected empty UDP listener"),
        ListenerType::Wildcard => state
            .conn_state
            .wildcard_listeners
            .get_by_listener(id.id)
            .expect("UDP listener not found")
            // NOTE(brunodalbo) ListenerAddrMap keeps vecs internally, but we
            // always only add a single address, so unwrap the first one
            .first()
            .map(|l| l.clone().into())
            .expect("Unexpected empty UDP listener"),
    }
}

/// Error type for send errors.
#[derive(Fail, Debug, PartialEq)]
pub enum SendError {
    // TODO(maufflick): Flesh this type out when the underlying error information becomes
    // available (and probably remove this "unknown" error).
    /// Failed to send for an unknown reason.
    #[fail(display = "send failed")]
    Unknown,

    #[fail(display = "{}", _0)]
    /// Errors related to the local address.
    Local(#[cause] LocalAddressError),

    #[fail(display = "{}", _0)]
    /// Errors related to the remote address.
    Remote(#[cause] RemoteAddressError),
}

// This conversion from a non-error type into an error isn't ideal.
// TODO(maufflick): This will be unnecessary/require changes when send_frame returns a proper error.
impl<S: Serializer> From<S> for SendError {
    fn from(_s: S) -> SendError {
        // TODO(maufflick): Include useful information about the underlying error once propagated.
        SendError::Unknown
    }
}

#[cfg(test)]
mod tests {
    use net_types::ip::{Ipv4, Ipv6};
    use packet::serialize::Buf;
    use specialize_ip_macro::ip_test;

    use super::*;
    use crate::ip::IpProto;
    use crate::testutil::{get_other_ip_address, set_logger_for_test};

    /// The listener data sent through a [`DummyUdpContext`].
    struct ListenData<I: Ip> {
        listener: UdpListenerId<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: Vec<u8>,
    }

    /// The UDP connection data sent through a [`DummyUdpContext`].
    struct ConnData<I: Ip> {
        conn: UdpConnId<I>,
        body: Vec<u8>,
    }

    struct DummyUdpContext<I: Ip> {
        state: UdpState<I>,
        listen_data: Vec<ListenData<I>>,
        conn_data: Vec<ConnData<I>>,
        extra_local_addrs: Vec<I::Addr>,
        treat_address_unroutable: Option<Box<dyn Fn(&<I as Ip>::Addr) -> bool>>,
    }

    impl<I: Ip> Default for DummyUdpContext<I> {
        fn default() -> Self {
            DummyUdpContext {
                state: Default::default(),
                listen_data: Default::default(),
                conn_data: Default::default(),
                extra_local_addrs: Vec::new(),
                treat_address_unroutable: None,
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
                || self.get_ref().extra_local_addrs.contains(&addr)
        }

        fn local_address_for_remote(
            &self,
            remote: SpecifiedAddr<<I as Ip>::Addr>,
        ) -> Option<SpecifiedAddr<<I as Ip>::Addr>> {
            if let Some(treat_address_unroutable) = &self.get_ref().treat_address_unroutable {
                if treat_address_unroutable(&remote) {
                    return None;
                }
            }
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

    impl<I: Ip> UdpContext<I> for DummyContext<I> {}
    impl<I: Ip, B: BufferMut> BufferUdpContext<I, B> for DummyContext<I> {
        fn receive_udp_from_conn(
            &mut self,
            conn: UdpConnId<I>,
            _src_ip: <I as Ip>::Addr,
            _src_port: NonZeroU16,
            body: &[u8],
        ) {
            self.get_mut().conn_data.push(ConnData { conn, body: body.to_owned() })
        }

        fn receive_udp_from_listen(
            &mut self,
            listener: UdpListenerId<I>,
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

    /// Tests UDP listeners over different IP versions.
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
        let listener = listen_udp::<I::Addr, _>(&mut ctx, Some(local_ip), NonZeroU16::new(100));
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

        // Send a packet providing a local ip:
        send_udp_listener(
            &mut ctx,
            listener,
            Some(local_ip),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
            Buf::new(body.to_owned(), ..),
        )
        .expect("send_udp_listener failed");
        // And send a packet that doesn't:
        send_udp_listener(
            &mut ctx,
            listener,
            None,
            remote_ip,
            NonZeroU16::new(200).unwrap(),
            Buf::new(body.to_owned(), ..),
        )
        .expect("send_udp_listener failed");
        let frames = ctx.frames();
        assert_eq!(frames.len(), 2);
        let check_frame = |(meta, frame_body): &(IpPacketFromArgs<I::Addr>, Vec<u8>)| {
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
        };
        check_frame(&frames[0]);
        check_frame(&frames[1]);
    }

    /// Tests that UDP packets without a connection are dropped.
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

    /// Tests that UDP connections can be created and data can be transmitted over it.
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
        )
        .expect("connect_udp failed");

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
        send_udp_conn(&mut ctx, conn, Buf::new(body.to_owned(), ..))
            .expect("send_udp_conn returned an error");

        let frames = ctx.frames();
        assert_eq!(frames.len(), 1);

        // check first frame:
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

    /// Tests that UDP connections fail with an appropriate error for non-routable remote
    /// addresses.
    #[ip_test]
    fn test_udp_conn_unroutable<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();
        // set dummy context callback to treat all addresses as unroutable.
        ctx.get_mut().treat_address_unroutable = Some(Box::new(|_address| true));
        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();
        // create a UDP connection with a specified local port and local ip:
        let conn_err = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            Some(NonZeroU16::new(100).unwrap()),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
        )
        .unwrap_err();

        assert_eq!(conn_err, ConnectError::Remote(RemoteAddressError::NoRoute));
    }

    /// Tests that UDP connections fail with an appropriate error when local address is non-local.
    #[ip_test]
    fn test_udp_conn_cannot_bind<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();

        // use remote address to trigger ConnectError::CannotBindToAddress.
        let local_ip = remote_addr::<I>();
        let remote_ip = remote_addr::<I>();
        // create a UDP connection with a specified local port and local ip:
        let conn_err = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            Some(NonZeroU16::new(100).unwrap()),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
        )
        .unwrap_err();

        assert_eq!(conn_err, ConnectError::Local(LocalAddressError::CannotBindToAddress));
    }

    /// Tests that UDP connections fail with an appropriate error when local ports are exhausted.
    #[ip_test]
    fn test_udp_conn_exhausted<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();

        // exhaust local ports to trigger FailedToAllocateLocalPort error.
        for port_num in UdpConnectionState::<I>::EPHEMERAL_RANGE {
            let local_ip1 = local_addr::<I>();
            ctx.get_state_mut().conn_state.listeners.insert(vec![Listener {
                addr: local_ip1,
                port: NonZeroU16::new(port_num).unwrap(),
            }]);
        }

        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();
        let conn_err = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            None,
            remote_ip,
            NonZeroU16::new(100).unwrap(),
        )
        .unwrap_err();

        assert_eq!(conn_err, ConnectError::Local(LocalAddressError::FailedToAllocateLocalPort));
    }

    /// Tests that UDP connections fail with an appropriate error when the connection is in use.
    #[ip_test]
    fn test_udp_conn_in_use<I: Ip>() {
        set_logger_for_test();
        let mut ctx = DummyContext::<I>::default();

        // use remote address to trigger ConnectError::CannotBindToAddress.
        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();

        let local_port = NonZeroU16::new(100).unwrap();

        // Tie up the connection so the second call to `connect_udp` fails.
        let _ = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            Some(local_port),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
        )
        .expect("Initial call to connect_udp was expected to succeed");

        // create a UDP connection with a specified local port and local ip:
        let conn_err = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            Some(local_port),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
        )
        .unwrap_err();

        assert_eq!(conn_err, ConnectError::ConnectionInUse);
    }

    #[ip_test]
    fn test_send_udp<I: Ip>() {
        set_logger_for_test();

        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();

        // UDP connection count should be zero before and after `send_udp` call.
        assert_eq!(ctx.get_state().conn_state.conns.addr_to_id.keys().len(), 0);

        let body = [1, 2, 3, 4, 5];
        // Try to send something with send_udp
        send_udp(
            &mut ctx,
            Some(local_ip),
            NonZeroU16::new(100),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
            Buf::new(body.to_vec(), ..),
        )
        .expect("send_udp failed");

        // UDP connection count should be zero before and after `send_udp` call.
        assert_eq!(ctx.get_state().conn_state.conns.addr_to_id.keys().len(), 0);
        let frames = ctx.frames();
        assert_eq!(frames.len(), 1);

        // check first frame:
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

    /// Tests that `send_udp` propogates errors.
    #[ip_test]
    fn test_send_udp_errors<I: Ip>() {
        set_logger_for_test();

        let mut ctx = DummyContext::<I>::default();

        // use invalid local ip to force a CannotBindToAddress error.
        let local_ip = remote_addr::<I>();
        let remote_ip = remote_addr::<I>();

        let body = [1, 2, 3, 4, 5];
        // Try to send something with send_udp
        let send_error = send_udp(
            &mut ctx,
            Some(local_ip),
            NonZeroU16::new(100),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
            Buf::new(body.to_vec(), ..),
        )
        .expect_err("send_udp unexpectedly succeeded");

        assert_eq!(
            send_error,
            NetstackError::Connect(ConnectError::Local(LocalAddressError::CannotBindToAddress))
        );
    }

    /// Tests that `send_udp` cleans up after errors.
    #[ip_test]
    fn test_send_udp_errors_cleanup<I: Ip>() {
        set_logger_for_test();

        let mut ctx = DummyContext::<I>::default();

        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();

        // UDP connection count should be zero before and after `send_udp` call.
        assert_eq!(ctx.get_state().conn_state.conns.addr_to_id.keys().len(), 0);

        // Instruct the dummy frame context to throw errors.
        let frames: &mut crate::context::testutil::DummyFrameContext<IpPacketFromArgs<I::Addr>> =
            ctx.as_mut();
        frames.set_should_error_for_frame(|_frame_meta| true);

        let body = [1, 2, 3, 4, 5];
        // Try to send something with send_udp
        let send_error = send_udp(
            &mut ctx,
            Some(local_ip),
            NonZeroU16::new(100),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
            Buf::new(body.to_vec(), ..),
        )
        .expect_err("send_udp unexpectedly succeeded");

        assert_eq!(send_error, NetstackError::SendUdp(SendError::Unknown));

        // UDP connection count should be zero before and after `send_udp` call (even in the case
        // of errors).
        assert_eq!(ctx.get_state().conn_state.conns.addr_to_id.keys().len(), 0);
    }

    /// Tests that UDP send failures are propagated as errors.
    ///
    /// Only tests with specified local port and address bounds.
    #[ip_test]
    fn test_send_udp_conn_failure<I: Ip>() {
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
        )
        .expect("connect_udp failed");

        // Instruct the dummy frame context to throw errors.
        let frames: &mut crate::context::testutil::DummyFrameContext<IpPacketFromArgs<I::Addr>> =
            ctx.as_mut();
        frames.set_should_error_for_frame(|_frame_meta| true);

        // Now try to send something over this new connection:
        let send_err = send_udp_conn(&mut ctx, conn, Buf::new(vec![], ..)).unwrap_err();
        assert_eq!(send_err, SendError::Unknown);
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
        )
        .expect("connect_udp failed");
        // conn2 has just a remote addr different than conn1
        let conn2 = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            Some(local_port_d),
            remote_ip_b,
            remote_port_a,
        )
        .expect("connect_udp failed");
        let list1 = listen_udp::<I::Addr, _>(&mut ctx, Some(local_ip), Some(local_port_a));
        let list2 = listen_udp::<I::Addr, _>(&mut ctx, Some(local_ip), Some(local_port_b));
        let wildcard_list = listen_udp::<I::Addr, _>(&mut ctx, None, Some(local_port_c));

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
        let listener = listen_udp::<I::Addr, _>(&mut ctx, None, Some(listener_port));
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
        )
        .expect("connect_udp failed");
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
        )
        .expect("connect_udp failed");
        let conn_b = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            None,
            ip_b,
            NonZeroU16::new(1010).unwrap(),
        )
        .expect("connect_udp failed");
        let conn_c = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            None,
            ip_a,
            NonZeroU16::new(2020).unwrap(),
        )
        .expect("connect_udp failed");
        let conn_d = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            None,
            ip_a,
            NonZeroU16::new(1010).unwrap(),
        )
        .expect("connect_udp failed");
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

    /// Tests [`UdpConnectionState::collect_used_local_ports`]
    #[ip_test]
    fn test_udp_collect_local_ports<I: Ip>() {
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let local_ip_2 = get_other_ip_address::<I::Addr>(10);
        let remote_ip = remote_addr::<I>();
        ctx.get_mut().extra_local_addrs.push(local_ip_2.get());

        let pa = NonZeroU16::new(10).unwrap();
        let pb = NonZeroU16::new(11).unwrap();
        let pc = NonZeroU16::new(12).unwrap();
        let pd = NonZeroU16::new(13).unwrap();
        let pe = NonZeroU16::new(14).unwrap();
        let pf = NonZeroU16::new(15).unwrap();
        let remote_port = NonZeroU16::new(100).unwrap();

        // create some listeners and connections:
        // wildcard listeners:
        listen_udp::<I::Addr, _>(&mut ctx, None, Some(pa));
        listen_udp::<I::Addr, _>(&mut ctx, None, Some(pb));
        // specified address listeners:
        listen_udp::<I::Addr, _>(&mut ctx, Some(local_ip), Some(pc));
        listen_udp::<I::Addr, _>(&mut ctx, Some(local_ip_2), Some(pd));
        // connections:
        connect_udp::<I::Addr, _>(&mut ctx, Some(local_ip), Some(pe), remote_ip, remote_port)
            .expect("connect_udp failed");
        connect_udp::<I::Addr, _>(&mut ctx, Some(local_ip_2), Some(pf), remote_ip, remote_port)
            .expect("connect_udp failed");

        let conn_state = &ctx.get_state().conn_state;

        // collect all used local ports:
        assert_eq!(
            conn_state.collect_used_local_ports(None.into_iter()),
            [pa, pb, pc, pd, pe, pf].iter().copied().collect()
        );
        // collect all local ports for local_ip:
        assert_eq!(
            conn_state.collect_used_local_ports(Some(local_ip).iter()),
            [pa, pb, pc, pe].iter().copied().collect()
        );
        // collect all local ports for local_ip_2:
        assert_eq!(
            conn_state.collect_used_local_ports(Some(local_ip_2).iter()),
            [pa, pb, pd, pf].iter().copied().collect()
        );
        // collect all local ports for local_ip and local_ip_2:
        assert_eq!(
            conn_state.collect_used_local_ports(vec![local_ip, local_ip_2].iter()),
            [pa, pb, pc, pd, pe, pf].iter().copied().collect()
        );
    }

    /// Tests local port allocation for [`listen_udp`].
    ///
    /// Tests that calling [`listen_udp`] causes a valid local port to be
    /// allocated when no local port is passed.
    #[ip_test]
    fn test_udp_listen_port_alloc<I: Ip>() {
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();

        let wildcard_list = listen_udp::<I::Addr, _>(&mut ctx, None, None);
        let specified_list = listen_udp::<I::Addr, _>(&mut ctx, Some(local_ip), None);

        let wildcard_port = ctx
            .get_state()
            .conn_state
            .wildcard_listeners
            .get_by_listener(wildcard_list.id)
            .unwrap()
            .first()
            .unwrap()
            .clone();
        let specified_port = ctx
            .get_state()
            .conn_state
            .listeners
            .get_by_listener(specified_list.id)
            .unwrap()
            .first()
            .unwrap()
            .port;
        assert!(UdpConnectionState::<I>::EPHEMERAL_RANGE.contains(&wildcard_port.get()));
        assert!(UdpConnectionState::<I>::EPHEMERAL_RANGE.contains(&specified_port.get()));
        assert_ne!(wildcard_port, specified_port);
    }

    /// Tests [`remove_udp_conn`]
    #[ip_test]
    fn test_remove_udp_conn<I: Ip>() {
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();
        let local_port = NonZeroU16::new(100).unwrap();
        let remote_port = NonZeroU16::new(200).unwrap();
        let conn = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            Some(local_port),
            remote_ip,
            remote_port,
        )
        .expect("connect_udp failed");
        let info = remove_udp_conn(&mut ctx, conn);
        // assert that the info gotten back matches what was expected:
        assert_eq!(info.local_addr, local_ip);
        assert_eq!(info.local_port, local_port);
        assert_eq!(info.remote_addr, remote_ip);
        assert_eq!(info.remote_port, remote_port);

        // assert that that connection id was removed from the connections
        // state:
        assert!(ctx.get_state().conn_state.conns.get_conn_by_id(conn.0).is_none());
    }

    /// Tests [`remove_udp_listener`]
    #[ip_test]
    fn test_remove_udp_listener<I: Ip>() {
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let local_port = NonZeroU16::new(100).unwrap();

        // test removing a specified listener:
        let list = listen_udp::<I::Addr, _>(&mut ctx, Some(local_ip), Some(local_port));
        let info = remove_udp_listener(&mut ctx, list);
        assert_eq!(info.local_addr.unwrap(), local_ip);
        assert_eq!(info.local_port, local_port);
        assert!(ctx.get_state().conn_state.listeners.get_by_listener(list.id).is_none());

        // test removing a wildcard listener:
        let list = listen_udp::<I::Addr, _>(&mut ctx, None, Some(local_port));
        let info = remove_udp_listener(&mut ctx, list);
        assert!(info.local_addr.is_none());
        assert_eq!(info.local_port, local_port);
        assert!(ctx.get_state().conn_state.wildcard_listeners.get_by_listener(list.id).is_none());
    }

    #[ip_test]
    fn test_get_conn_info<I: Ip>() {
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();
        let remote_ip = remote_addr::<I>();
        // create a UDP connection with a specified local port and local ip:
        let conn = connect_udp::<I::Addr, _>(
            &mut ctx,
            Some(local_ip),
            NonZeroU16::new(100),
            remote_ip,
            NonZeroU16::new(200).unwrap(),
        )
        .expect("connect_udp failed");
        let info = get_udp_conn_info(&ctx, conn);
        assert_eq!(info.local_addr, local_ip);
        assert_eq!(info.local_port.get(), 100);
        assert_eq!(info.remote_addr, remote_ip);
        assert_eq!(info.remote_port.get(), 200);
    }

    #[ip_test]
    fn test_get_listener_info<I: Ip>() {
        let mut ctx = DummyContext::<I>::default();
        let local_ip = local_addr::<I>();

        // check getting info on specified listener:
        let list = listen_udp::<I::Addr, _>(&mut ctx, Some(local_ip), NonZeroU16::new(100));
        let info = get_udp_listener_info(&ctx, list);
        assert_eq!(info.local_addr.unwrap(), local_ip);
        assert_eq!(info.local_port.get(), 100);

        // check getting info on wildcard listener:
        let list = listen_udp::<I::Addr, _>(&mut ctx, None, NonZeroU16::new(200));
        let info = get_udp_listener_info(&ctx, list);
        assert!(info.local_addr.is_none());
        assert_eq!(info.local_port.get(), 200);
    }
}
