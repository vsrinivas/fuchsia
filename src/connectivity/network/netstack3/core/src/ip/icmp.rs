// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Control Message Protocol (ICMP).

use byteorder::{ByteOrder, NetworkEndian};
use log::{debug, trace};
use net_types::ip::{Ip, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use net_types::{MulticastAddress, SpecifiedAddr, Witness};
use packet::{BufferMut, ParseBuffer, Serializer, TruncateDirection, TruncatingSerializer};
use zerocopy::ByteSlice;

use crate::context::{CounterContext, InstantContext, StateContext};
use crate::data_structures::token_bucket::TokenBucket;
use crate::device::ndp::NdpPacketHandler;
use crate::device::{DeviceId, FrameDestination};
use crate::error::{ConnectError, ExistsError, NoRouteError};
use crate::ip::forwarding::ForwardingTable;
use crate::ip::{
    mld::MldHandler,
    path_mtu::PmtuHandler,
    socket::{
        BufferIpSocketContext, IpSock, IpSocket, IpSocketContext, SendError, UnroutableBehavior,
    },
    IpDeviceIdContext, IpExt, IpProto, IpVersionMarker, IPV6_MIN_MTU,
};
use crate::transport::ConnAddrMap;
use crate::wire::icmp::{
    self as wire_icmp, peek_message_type, IcmpDestUnreachable, IcmpEchoRequest, IcmpMessage,
    IcmpMessageType, IcmpPacket, IcmpPacketBuilder, IcmpPacketRaw, IcmpParseArgs, IcmpTimeExceeded,
    IcmpUnusedCode, Icmpv4DestUnreachableCode, Icmpv4Packet, Icmpv4ParameterProblem,
    Icmpv4ParameterProblemCode, Icmpv4RedirectCode, Icmpv4TimeExceededCode,
    Icmpv6DestUnreachableCode, Icmpv6Packet, Icmpv6PacketTooBig, Icmpv6ParameterProblem,
    Icmpv6ParameterProblemCode, Icmpv6TimeExceededCode, MessageBody, OriginalPacket,
};
use crate::wire::ipv4::{Ipv4Header, Ipv4PacketRaw};
use crate::wire::ipv6::{Ipv6Header, Ipv6PacketRaw, UndefinedBodyBoundsError};
use crate::{BufferDispatcher, Context, EventDispatcher};

/// The default number of ICMP error messages to send per second.
///
/// Beyond this rate, error messages will be silently dropped.
pub const DEFAULT_ERRORS_PER_SECOND: u64 = 2 << 16;

/// An ICMPv4 error type and code.
///
/// Each enum variant corresponds to a particular error type, and contains the
/// possible codes for that error type.
#[derive(Copy, Clone, Debug, PartialEq)]
#[allow(missing_docs)]
pub enum Icmpv4ErrorCode {
    DestUnreachable(Icmpv4DestUnreachableCode),
    Redirect(Icmpv4RedirectCode),
    TimeExceeded(Icmpv4TimeExceededCode),
    ParameterProblem(Icmpv4ParameterProblemCode),
}

/// An ICMPv6 error type and code.
///
/// Each enum variant corresponds to a particular error type, and contains the
/// possible codes for that error type.
#[derive(Copy, Clone, Debug, PartialEq)]
#[allow(missing_docs)]
pub enum Icmpv6ErrorCode {
    DestUnreachable(Icmpv6DestUnreachableCode),
    PacketTooBig,
    TimeExceeded(Icmpv6TimeExceededCode),
    ParameterProblem(Icmpv6ParameterProblemCode),
}

/// A builder for ICMPv4 state.
#[derive(Copy, Clone)]
pub struct Icmpv4StateBuilder {
    send_timestamp_reply: bool,
    errors_per_second: u64,
}

impl Default for Icmpv4StateBuilder {
    fn default() -> Icmpv4StateBuilder {
        Icmpv4StateBuilder {
            send_timestamp_reply: false,
            errors_per_second: DEFAULT_ERRORS_PER_SECOND,
        }
    }
}

impl Icmpv4StateBuilder {
    /// Enable or disable replying to ICMPv4 Timestamp Request messages with
    /// Timestamp Reply messages (default: disabled).
    ///
    /// Enabling this can introduce a very minor vulnerability in which an
    /// attacker can learn the system clock's time, which in turn can aid in
    /// attacks against time-based authentication systems.
    pub fn send_timestamp_reply(&mut self, send_timestamp_reply: bool) -> &mut Self {
        self.send_timestamp_reply = send_timestamp_reply;
        self
    }

    /// Configure the number of ICMPv4 error messages to send per second
    /// (default: [`DEFAULT_ERRORS_PER_SECOND`]).
    ///
    /// The number of ICMPv4 error messages sent by the stack will be rate
    /// limited to the given number of errors per second. Any messages that
    /// exceed this rate will be silently dropped.
    pub fn errors_per_second(&mut self, errors_per_second: u64) -> &mut Self {
        self.errors_per_second = errors_per_second;
        self
    }

    pub(crate) fn build<Instant, S>(self) -> Icmpv4State<Instant, S> {
        Icmpv4State {
            conns: ConnAddrMap::default(),
            error_send_bucket: TokenBucket::new(self.errors_per_second),
            send_timestamp_reply: self.send_timestamp_reply,
        }
    }
}

/// The state associated with the ICMPv4 protocol.
pub(crate) struct Icmpv4State<Instant, S> {
    conns: ConnAddrMap<IcmpAddr<Ipv4Addr>, IcmpConn<S>>,
    error_send_bucket: TokenBucket<Instant>,
    send_timestamp_reply: bool,
}

// Used by `receive_icmp_echo_reply`.
impl<Instant, S> AsRef<ConnAddrMap<IcmpAddr<Ipv4Addr>, IcmpConn<S>>> for Icmpv4State<Instant, S> {
    fn as_ref(&self) -> &ConnAddrMap<IcmpAddr<Ipv4Addr>, IcmpConn<S>> {
        &self.conns
    }
}

// Used by `send_icmpv4_echo_request_inner`.
impl<Instant, S> AsMut<ConnAddrMap<IcmpAddr<Ipv4Addr>, IcmpConn<S>>> for Icmpv4State<Instant, S> {
    fn as_mut(&mut self) -> &mut ConnAddrMap<IcmpAddr<Ipv4Addr>, IcmpConn<S>> {
        &mut self.conns
    }
}

/// A builder for ICMPv6 state.
#[derive(Copy, Clone)]
pub struct Icmpv6StateBuilder {
    errors_per_second: u64,
}

impl Default for Icmpv6StateBuilder {
    fn default() -> Icmpv6StateBuilder {
        Icmpv6StateBuilder { errors_per_second: DEFAULT_ERRORS_PER_SECOND }
    }
}

impl Icmpv6StateBuilder {
    /// Configure the number of ICMPv6 error messages to send per second
    /// (default: [`DEFAULT_ERRORS_PER_SECOND`]).
    ///
    /// The number of ICMPv6 error messages sent by the stack will be rate
    /// limited to the given number of errors per second. Any messages that
    /// exceed this rate will be silently dropped.
    ///
    /// This rate limit is required by [RFC 4443 Section 2.4] (f).
    ///
    /// [RFC 4443 Section 2.4]: https://tools.ietf.org/html/rfc4443#section-2.4
    pub fn errors_per_second(&mut self, errors_per_second: u64) -> &mut Self {
        self.errors_per_second = errors_per_second;
        self
    }

    pub(crate) fn build<Instant, S>(self) -> Icmpv6State<Instant, S> {
        Icmpv6State {
            conns: ConnAddrMap::default(),
            error_send_bucket: TokenBucket::new(self.errors_per_second),
        }
    }
}

/// The state associated with the ICMPv6 protocol.
pub(crate) struct Icmpv6State<Instant, S> {
    conns: ConnAddrMap<IcmpAddr<Ipv6Addr>, IcmpConn<S>>,
    error_send_bucket: TokenBucket<Instant>,
}

// Used by `receive_icmp_echo_reply`.
impl<Instant, S> AsRef<ConnAddrMap<IcmpAddr<Ipv6Addr>, IcmpConn<S>>> for Icmpv6State<Instant, S> {
    fn as_ref(&self) -> &ConnAddrMap<IcmpAddr<Ipv6Addr>, IcmpConn<S>> {
        &self.conns
    }
}

// Used by `send_icmpv6_echo_request_inner`.
impl<Instant, S> AsMut<ConnAddrMap<IcmpAddr<Ipv6Addr>, IcmpConn<S>>> for Icmpv6State<Instant, S> {
    fn as_mut(&mut self) -> &mut ConnAddrMap<IcmpAddr<Ipv6Addr>, IcmpConn<S>> {
        &mut self.conns
    }
}

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
struct IcmpAddr<A: IpAddress> {
    local_addr: SpecifiedAddr<A>,
    remote_addr: SpecifiedAddr<A>,
    icmp_id: u16,
}

#[derive(Clone)]
struct IcmpConn<S> {
    icmp_id: u16,
    ip: S,
}

impl<'a, A: IpAddress, S: IpSocket<A::Version>> From<&'a IcmpConn<S>> for IcmpAddr<A> {
    fn from(conn: &'a IcmpConn<S>) -> IcmpAddr<A> {
        IcmpAddr {
            local_addr: *conn.ip.local_ip(),
            remote_addr: *conn.ip.remote_ip(),
            icmp_id: conn.icmp_id,
        }
    }
}

/// The ID identifying an ICMP connection.
///
/// When a new ICMP connection is added, it is given a unique `IcmpConnId`.
/// These are opaque `usize`s which are intentionally allocated as densely as
/// possible around 0, making it possible to store any associated data in a
/// `Vec` indexed by the ID. `IcmpConnId` implements `Into<usize>`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct IcmpConnId<I: Ip>(usize, IpVersionMarker<I>);

impl<I: Ip> IcmpConnId<I> {
    fn new(id: usize) -> IcmpConnId<I> {
        IcmpConnId(id, IpVersionMarker::default())
    }
}

impl<I: Ip> From<IcmpConnId<I>> for usize {
    fn from(id: IcmpConnId<I>) -> usize {
        id.0
    }
}

/// Apply an update to all IPv4 sockets that this module is responsible for
/// - namely, those contained in ICMPv4 sockets.
pub(super) fn apply_ipv4_socket_update<C: Icmpv4SocketContext>(
    ctx: &mut C,
    update: <C::IpSocket as IpSocket<Ipv4>>::Update,
) {
    let (state, meta) = ctx.get_state_and_update_meta();
    // We have to collect into a `Vec` here because the iterator borrows `ctx`,
    // which we need mutable access to in order to report the closures.
    let closed_conns = state
        .conns
        .update_retain(|conn| match conn.ip.apply_update(&update, meta) {
            Ok(()) => true,
            Err(NoRouteError) => false,
        })
        .collect::<Vec<_>>();
    for (id, _conn) in closed_conns {
        ctx.close_icmp_connection(IcmpConnId::new(id), NoRouteError);
    }
}

/// Apply an update to all IPv6 sockets that this module is responsible for
/// - namely, those contained in ICMPv6 sockets.
pub(super) fn apply_ipv6_socket_update<C: Icmpv6SocketContext>(
    ctx: &mut C,
    update: <C::IpSocket as IpSocket<Ipv6>>::Update,
) {
    let (state, meta) = ctx.get_state_and_update_meta();
    // We have to collect into a `Vec` here because the iterator borrows `ctx`,
    // which we need mutable access to in order to report the closures.
    let closed_conns = state
        .conns
        .update_retain(|conn| match conn.ip.apply_update(&update, meta) {
            Ok(()) => true,
            Err(NoRouteError) => false,
        })
        .collect::<Vec<_>>();
    for (id, _conn) in closed_conns {
        ctx.close_icmp_connection(IcmpConnId::new(id), NoRouteError);
    }
}

/// An extension trait adding extra ICMP-related functionality to IP versions.
pub trait IcmpIpExt: wire_icmp::IcmpIpExt {
    /// The type of error code for this version of ICMP - [`Icmpv4ErrorCode`] or
    /// [`Icmpv6ErrorCode`].
    type ErrorCode;
}

impl IcmpIpExt for Ipv4 {
    type ErrorCode = Icmpv4ErrorCode;
}

impl IcmpIpExt for Ipv6 {
    type ErrorCode = Icmpv6ErrorCode;
}

/// An event dispatcher for the ICMP layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait IcmpEventDispatcher<I: IcmpIpExt> {
    /// Receive an ICMP error message related to a previously-sent ICMP echo
    /// request.
    ///
    /// `seq_num` is the sequence number of the original echo request that
    /// triggered the error, and `err` is the specific error identified by the
    /// incoming ICMP error message.
    fn receive_icmp_error(&mut self, _conn: IcmpConnId<I>, _seq_num: u16, _err: I::ErrorCode) {
        log_unimplemented!((), "IcmpEventDispatcher::receive_icmp_error: not implemented");
    }

    /// Close an ICMP connection because it is no longer routable.
    ///
    /// `close_icmp_connection` is called when a change to routing state has
    /// made an ICMP socket no longer routable. After the call has returned, the
    /// core will completely remove the socket, and any future calls referencing
    /// it will either panic (because of an unrecognized [`IcmpConnId`]) or
    /// incorrectly refer to a different, newly-opened ICMP connection.
    fn close_icmp_connection(&mut self, conn: IcmpConnId<I>, err: NoRouteError);
}

/// An event dispatcher for the ICMP layer when a buffer is required.
///
/// `BufferIcmpEventDispatcher` extends [`IcmpEventDispatcher`] by adding
/// methods that require a buffer.
pub trait BufferIcmpEventDispatcher<I: IcmpIpExt, B: BufferMut>: IcmpEventDispatcher<I> {
    /// Receive an ICMP echo reply.
    fn receive_icmp_echo_reply(&mut self, _conn: IcmpConnId<I>, _seq_num: u16, _data: B) {
        log_unimplemented!((), "IcmpEventDispatcher::receive_icmp_echo_reply: not implemented");
    }
}

/// The execution context for ICMP sockets.
///
/// `IcmpSocketContext` provides support for receiving ICMP echo replies for
/// both ICMP(v4) and ICMPv6 sockets.
pub(crate) trait IcmpSocketContext<I: IcmpIpExt>:
    IpDeviceIdContext + CounterContext + InstantContext + IpSocketContext<I>
{
    /// Receive an ICMP error message related to a previously-sent ICMP echo
    /// request.
    ///
    /// `seq_num` is the sequence number of the original echo request that
    /// triggered the error, and `err` is the specific error identified by the
    /// incoming ICMP error message.
    fn receive_icmp_error(&mut self, _conn: IcmpConnId<I>, _seq_num: u16, _err: I::ErrorCode) {
        log_unimplemented!((), "IcmpSocketContext::receive_icmp_error: not implemented");
    }

    /// Close an ICMP connection because it is no longer routable.
    ///
    /// `close_icmp_connection` is called when a change to routing state has
    /// made an ICMP socket no longer routable. After the call has returned, the
    /// core will completely remove the socket, and any future calls referencing
    /// it will either panic (because of an unrecognized [`IcmpConnId`]) or
    /// incorrectly refer to a different, newly-opened ICMP connection.
    fn close_icmp_connection(&mut self, conn: IcmpConnId<I>, err: NoRouteError);
}

/// The execution context for ICMP sockets when a buffer is required.
///
/// `BufferIcmpSocketContext` extends [`IcmpSocketContext`], adding methods that
/// require a buffer type parameter.
pub(crate) trait BufferIcmpSocketContext<I: IcmpIpExt, B: BufferMut>:
    IcmpSocketContext<I> + BufferIpSocketContext<I, B>
{
    /// Receive an ICMP echo reply.
    ///
    /// If `I` is `Ipv4`, then this is an ICMP(v4) echo reply, and if it's
    /// `Ipv6`, then this is an ICMPv6 echo reply.
    fn receive_icmp_echo_reply(&mut self, _conn: IcmpConnId<I>, _seq_num: u16, _data: B) {
        log_unimplemented!((), "IcmpSocketContext::receive_icmp_echo_reply: not implemented");
    }
}

/// The execution context for ICMPv4 sockets.
///
/// `Icmpv4SocketContext` extends [`IcmpSocketContext`] with ICMPv4-specific
/// functionality.
pub(crate) trait Icmpv4SocketContext:
    IpDeviceIdContext
    + IcmpSocketContext<Ipv4>
    + StateContext<
        Icmpv4State<<Self as InstantContext>::Instant, <Self as IpSocketContext<Ipv4>>::IpSocket>,
    >
{
    /// Get the [`Icmpv4State`] and the metadata needed to apply an IP socket
    /// update at the same time.
    fn get_state_and_update_meta(
        &mut self,
    ) -> (
        &mut Icmpv4State<Self::Instant, Self::IpSocket>,
        &<Self::IpSocket as IpSocket<Ipv4>>::UpdateMeta,
    );
}

/// The execution context for ICMPv6 sockets.
///
/// `Icmpv6SocketContext` extends [`IcmpSocketContext`] with ICMPv6-specific
/// functionality.
pub(crate) trait Icmpv6SocketContext:
    IpDeviceIdContext
    + IcmpSocketContext<Ipv6>
    + StateContext<
        Icmpv6State<<Self as InstantContext>::Instant, <Self as IpSocketContext<Ipv6>>::IpSocket>,
    >
{
    /// Get the [`Icmpv6State`] and the metadata needed to apply an IP socket
    /// update at the same time.
    fn get_state_and_update_meta(
        &mut self,
    ) -> (
        &mut Icmpv6State<Self::Instant, Self::IpSocket>,
        &<Self::IpSocket as IpSocket<Ipv6>>::UpdateMeta,
    );
}

impl<D: EventDispatcher> IcmpSocketContext<Ipv4> for Context<D> {
    fn receive_icmp_error(&mut self, conn: IcmpConnId<Ipv4>, seq_num: u16, err: Icmpv4ErrorCode) {
        self.dispatcher_mut().receive_icmp_error(conn, seq_num, err);
    }

    fn close_icmp_connection(&mut self, conn: IcmpConnId<Ipv4>, err: NoRouteError) {
        self.dispatcher_mut().close_icmp_connection(conn, err);
    }
}

impl<D: EventDispatcher> IcmpSocketContext<Ipv6> for Context<D> {
    fn receive_icmp_error(&mut self, conn: IcmpConnId<Ipv6>, seq_num: u16, err: Icmpv6ErrorCode) {
        self.dispatcher_mut().receive_icmp_error(conn, seq_num, err);
    }

    fn close_icmp_connection(&mut self, conn: IcmpConnId<Ipv6>, err: NoRouteError) {
        self.dispatcher_mut().close_icmp_connection(conn, err);
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> BufferIcmpSocketContext<Ipv4, B> for Context<D> {
    fn receive_icmp_echo_reply(&mut self, conn: IcmpConnId<Ipv4>, seq_num: u16, data: B) {
        self.dispatcher_mut().receive_icmp_echo_reply(conn, seq_num, data);
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> BufferIcmpSocketContext<Ipv6, B> for Context<D> {
    fn receive_icmp_echo_reply(&mut self, conn: IcmpConnId<Ipv6>, seq_num: u16, data: B) {
        self.dispatcher_mut().receive_icmp_echo_reply(conn, seq_num, data);
    }
}

impl<D: EventDispatcher> Icmpv4SocketContext for Context<D> {
    fn get_state_and_update_meta(
        &mut self,
    ) -> (
        &mut Icmpv4State<Self::Instant, IpSock<Ipv4, DeviceId>>,
        &ForwardingTable<Ipv4, Self::DeviceId>,
    ) {
        let state = self.state_mut();
        (&mut state.ipv4.icmp, &state.ipv4.inner.table)
    }
}

impl<D: EventDispatcher> Icmpv6SocketContext for Context<D> {
    fn get_state_and_update_meta(
        &mut self,
    ) -> (
        &mut Icmpv6State<Self::Instant, IpSock<Ipv6, DeviceId>>,
        &ForwardingTable<Ipv6, Self::DeviceId>,
    ) {
        let state = self.state_mut();
        (&mut state.ipv6.icmp, &state.ipv6.inner.table)
    }
}

/// The execution context shared by ICMP(v4) and ICMPv6 for the internal
/// operations of the IP stack.
///
/// Unlike [`IcmpSocketContext`], `IcmpContext` is not exposed outside of this
/// crate.
pub(crate) trait IcmpContext<I: IcmpIpExt, B: BufferMut>:
    BufferIcmpSocketContext<I, B> + CounterContext + InstantContext
{
    /// Send an ICMP reply to a remote host.
    ///
    /// `send_icmp_reply` sends a reply to a non-error message (e.g., "echo
    /// request" or "timestamp request" messages). It takes the ingress device,
    /// source IP, and destination IP of the packet *being responded to*. It
    /// uses ICMP-specific logic to figure out whether and how to send an ICMP
    /// reply.
    ///
    /// `dst_ip` must not be the unspecified address, but this is guaranteed
    /// statically because packets destined to the unspecified address are never
    /// delivered locally. `src_ip` must not be the unspecified address, as we
    /// are replying to this packet, and the unspecified address is not
    /// routable.
    ///
    /// `get_body` returns a `Serializer` with the bytes of the ICMP packet,
    /// and, when called, is given the source IP address chosen for the outbound
    /// packet. This allows `get_body` to properly compute the ICMP checksum,
    /// which relies on both the source and destination IP addresses of the IP
    /// packet it's encapsulated in.
    fn send_icmp_reply<S: Serializer<Buffer = B>, F: FnOnce(SpecifiedAddr<I::Addr>) -> S>(
        &mut self,
        device: Option<Self::DeviceId>,
        src_ip: SpecifiedAddr<I::Addr>,
        dst_ip: SpecifiedAddr<I::Addr>,
        get_body: F,
    ) -> Result<(), S>;

    /// Send an ICMP error message to a remote host.
    ///
    /// `send_icmp_error_message` sends an ICMP error message. It takes the
    /// ingress device, source IP, and destination IP of the packet that
    /// generated the error. It uses ICMP-specific logic to figure out whether
    /// and how to send an ICMP error message. `ip_mtu` is an optional MTU size
    /// for the final IP packet generated by this ICMP response. `src_ip` must
    /// not be the unspecified address, as we are replying to this packet, and
    /// the unspecified address is not routable (RFCs 792 and 4443 also disallow
    /// sending error messages in response to packets with an unspecified source
    /// address, probably for exactly this reason).
    ///
    /// `send_icmp_error_message` is responsible for calling
    /// [`should_send_icmpv4_error`] or [`should_send_icmpv6_error`]. If those
    /// return `false`, then it must not send the message regardless of whatever
    /// other logic is used.
    ///
    /// If the encapsulated error message is an ICMPv6 Packet Too Big Message or
    /// a Parameter Problem Message, Code 2 reporting an unrecognized IPv6
    /// option that has the Option Type highest-order two bits set to 10,
    /// `allow_dst_multicast` must be set to `true`. See
    /// [`should_send_icmpv6_error`] for more details.
    ///
    /// get_body` returns a `Serializer` with the bytes of the ICMP packet, and,
    /// when called, is given the source IP address chosen for the outbound
    /// packet. This allows `get_body` to properly compute the ICMP checksum,
    /// which relies on both the source and destination IP addresses of the IP
    /// packet it's encapsulated in.
    fn send_icmp_error_message<S: Serializer<Buffer = B>, F: FnOnce(SpecifiedAddr<I::Addr>) -> S>(
        &mut self,
        device: Self::DeviceId,
        frame_dst: FrameDestination,
        src_ip: SpecifiedAddr<I::Addr>,
        dst_ip: I::Addr,
        get_body: F,
        ip_mtu: Option<u32>,
        allow_dst_multicast: bool,
    ) -> Result<(), S>;
}

/// The execution context for ICMP(v4).
pub(crate) trait Icmpv4Context<B: BufferMut>:
    IcmpContext<Ipv4, B>
    + StateContext<
        Icmpv4State<<Self as InstantContext>::Instant, <Self as IpSocketContext<Ipv4>>::IpSocket>,
    > + Icmpv4SocketContext
{
    // TODO(joshlf): If we end up needing to respond to these messages with new
    // outbound packets, then perhaps it'd be worth passing the original buffer
    // so that it can be reused?

    /// Receive an ICMP(v4) error message.
    ///
    /// `original_packet` is the packet that triggered the error. Some of its
    /// contents - including at least 8 bytes of the packet's body - are
    /// encapsulated in the error message, and provided here so that the error
    /// can be associated with a transport-layer socket.
    fn receive_icmpv4_error(&mut self, original_packet: Ipv4PacketRaw<&[u8]>, err: Icmpv4ErrorCode);
}

/// The execution context for ICMPv6.
pub(crate) trait Icmpv6Context<B: BufferMut>:
    IcmpContext<Ipv6, B>
    + StateContext<
        Icmpv6State<<Self as InstantContext>::Instant, <Self as IpSocketContext<Ipv6>>::IpSocket>,
    > + Icmpv6SocketContext
{
    /// Receive an ICMPv6 error message.
    ///
    /// `original_packet` is the packet that triggered the error. Some of its
    /// contents - as much as possible without making the encapsulating ICMP
    /// error larger than the IPv6 minimum MTU - are encapsulated in the error
    /// message, and provided here so that the error can be associated with a
    /// transport-layer socket.
    fn receive_icmpv6_error(&mut self, original_packet: Ipv6PacketRaw<&[u8]>, err: Icmpv6ErrorCode);
}

/// Attempt to send an ICMP or ICMPv6 error message, applying a rate limit.
///
/// `try_send_error!($ctx, $e)` attempts to consume a token from the token
/// bucket at `$ctx.get_state_mut().error_send_bucket`. If it succeeds, it
/// invokes the expression `$e`, and otherwise does nothing. It assumes that the
/// type of `$e` is `Result<(), _>` and, in the case that the rate limit is
/// exceeded and it does not invoke `$e`, returns `Ok(())`.
///
/// [RFC 4443 Section 2.4] (f) requires that we MUST limit the rate of outbound
/// ICMPv6 error messages. To our knowledge, there is no similar requirement for
/// ICMPv4, but the same rationale applies, so we do it for ICMPv4 as well.
///
/// [RFC 4443 Section 2.4]: https://tools.ietf.org/html/rfc4443#section-2.4
macro_rules! try_send_error {
    ($ctx:expr, $e:expr) => {{
        // TODO(joshlf): Figure out a way to avoid querying for the current time
        // unconditionally. See the documentation on the `CachedInstantContext`
        // type for more information.
        let instant_ctx = crate::context::new_cached_instant_context($ctx);
        if $ctx.get_state_mut().error_send_bucket.try_take(&instant_ctx) {
            $e
        } else {
            trace!("ip::icmp::try_send_error!: dropping rate-limited ICMP error message");
            Ok(())
        }
    }};
}

/// Receive an ICMP(v4) packet.
pub(crate) fn receive_icmpv4_packet<B: BufferMut, C: Icmpv4Context<B> + PmtuHandler<Ipv4>>(
    ctx: &mut C,
    device: Option<C::DeviceId>,
    src_ip: Ipv4Addr,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
    mut buffer: B,
) {
    trace!("receive_icmpv4_packet({}, {})", src_ip, dst_ip);
    let packet = match buffer.parse_with::<_, Icmpv4Packet<_>>(IcmpParseArgs::new(src_ip, dst_ip)) {
        Ok(packet) => packet,
        Err(_) => return, // TODO(joshlf): Do something else here?
    };

    match packet {
        Icmpv4Packet::EchoRequest(echo_request) => {
            ctx.increment_counter("receive_icmpv4_packet::echo_request");

            if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
                let req = *echo_request.message();
                let code = echo_request.code();
                let (local_ip, remote_ip) = (dst_ip, src_ip);
                // TODO(joshlf): Do something if send_icmp_reply returns an
                // error?
                let _ = ctx.send_icmp_reply(device, remote_ip, local_ip, |src_ip| {
                    buffer.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                        src_ip,
                        remote_ip,
                        code,
                        req.reply(),
                    ))
                });
            } else {
                trace!("receive_icmpv4_packet: Received echo request with an unspecified source address");
            }
        }
        Icmpv4Packet::EchoReply(echo_reply) => {
            ctx.increment_counter("receive_icmpv4_packet::echo_reply");
            trace!("receive_icmpv4_packet: Received an EchoReply message");
            let id = echo_reply.message().id();
            let seq = echo_reply.message().seq();
            receive_icmp_echo_reply::<_, _, _, Icmpv4State<_, _>, _>(
                ctx, src_ip, dst_ip, id, seq, buffer,
            );
        }
        Icmpv4Packet::TimestampRequest(timestamp_request) => {
            ctx.increment_counter("receive_icmpv4_packet::timestamp_request");
            if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
                if StateContext::<Icmpv4State<_, _>>::get_state(ctx).send_timestamp_reply {
                    trace!("receive_icmpv4_packet: Responding to Timestamp Request message");
                    // We're supposed to respond with the time that we processed
                    // this message as measured in milliseconds since midnight
                    // UT. However, that would require that we knew the local
                    // time zone and had a way to convert
                    // `InstantContext::Instant` to a `u32` value. We can't do
                    // that, and probably don't want to introduce all of the
                    // machinery necessary just to support this one use case.
                    // Luckily, RFC 792 page 17 provides us with an out:
                    //
                    //   If the time is not available in miliseconds [sic] or
                    //   cannot be provided with respect to midnight UT then any
                    //   time can be inserted in a timestamp provided the high
                    //   order bit of the timestamp is also set to indicate this
                    //   non-standard value.
                    //
                    // Thus, we provide a zero timestamp with the high order bit
                    // set.
                    const NOW: u32 = 0x80000000;
                    let reply = timestamp_request.message().reply(NOW, NOW);
                    let (local_ip, remote_ip) = (dst_ip, src_ip);
                    // We don't actually want to use any of the _contents_ of
                    // the buffer, but we would like to reuse it as scratch
                    // space. Eventually, `IcmpPacketBuilder` will implement
                    // `InnerPacketBuilder` for messages without bodies, but
                    // until that happens, we need to give it an empty buffer.
                    buffer.shrink_front_to(0);
                    // TODO(joshlf): Do something if send_icmp_reply returns an
                    // error?
                    let _ = ctx.send_icmp_reply(device, remote_ip, local_ip, |src_ip| {
                        buffer.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                            src_ip,
                            remote_ip,
                            IcmpUnusedCode,
                            reply,
                        ))
                    });
                } else {
                    trace!("receive_icmpv4_packet: Silently ignoring Timestamp Request message");
                }
            } else {
                trace!("receive_icmpv4_packet: Received timestamp request with an unspecified source address");
            }
        }
        Icmpv4Packet::TimestampReply(_) => {
            // TODO(joshlf): Support sending Timestamp Requests and receiving
            // Timestamp Replies?
            debug!("receive_icmpv4_packet: Received unsolicited Timestamp Reply message");
        }
        Icmpv4Packet::DestUnreachable(dest_unreachable) => {
            ctx.increment_counter("receive_icmpv4_packet::dest_unreachable");
            trace!("receive_icmpv4_packet: Received a Destination Unreachable message");

            if dest_unreachable.code() == Icmpv4DestUnreachableCode::FragmentationRequired {
                if let Some(next_hop_mtu) = dest_unreachable.message().next_hop_mtu() {
                    // We are updating the path MTU from the destination address
                    // of this `packet` (which is an IP address on this node) to
                    // some remote (identified by the source address of this
                    // `packet`).
                    //
                    // `update_pmtu_if_less` may return an error, but it will
                    // only happen if the Dest Unreachable message's mtu field
                    // had a value that was less than the IPv4 minimum mtu
                    // (which as per IPv4 RFC 791, must not happen).
                    ctx.update_pmtu_if_less(dst_ip.get(), src_ip, u32::from(next_hop_mtu.get()));
                } else {
                    // If the Next-Hop MTU from an incoming ICMP message is `0`,
                    // then we assume the source node of the ICMP message does
                    // not implement RFC 1191 and therefore does not actually
                    // use the Next-Hop MTU field and still considers it as an
                    // unused field.
                    //
                    // In this case, the only information we have is the size of
                    // the original IP packet that was too big (the original
                    // packet header should be included in the ICMP response).
                    // Here we will simply reduce our PMTU estimate to a value
                    // less than the total length of the original packet. See
                    // RFC 1191 Section 5.
                    //
                    // `update_pmtu_next_lower` may return an error, but it will
                    // only happen if no valid lower value exists from the
                    // original packet's length. It is safe to silently ignore
                    // the error when we have no valid lower PMTU value as the
                    // node from `src_ip` would not be IP RFC compliant and we
                    // expect this to be very rare (for IPv4, the lowest MTU
                    // value for a link can be 68 bytes).
                    let original_packet_buf = dest_unreachable.body().bytes();
                    if original_packet_buf.len() >= 4 {
                        // We need the first 4 bytes as the total length field
                        // is at bytes 2/3 of the original packet buffer.
                        let total_len = NetworkEndian::read_u16(&original_packet_buf[2..4]);

                        trace!("receive_icmpv4_packet: Next-Hop MTU is 0 so using the next best PMTU value from {}", total_len);

                        ctx.update_pmtu_next_lower(dst_ip.get(), src_ip, u32::from(total_len));
                    } else {
                        // Ok to silently ignore as RFC 792 requires nodes to
                        // send the original IP packet header + 64 bytes of the
                        // original IP packet's body so the node itself is
                        // already violating the RFC.
                        trace!("receive_icmpv4_packet: Original packet buf is too small to get original packet len so ignoring");
                    }
                }
            }

            receive_icmpv4_error(
                ctx,
                &dest_unreachable,
                Icmpv4ErrorCode::DestUnreachable(dest_unreachable.code()),
            );
        }
        Icmpv4Packet::TimeExceeded(time_exceeded) => {
            ctx.increment_counter("receive_icmpv4_packet::time_exceeded");
            trace!("receive_icmpv4_packet: Received a Time Exceeded message");

            receive_icmpv4_error(
                ctx,
                &time_exceeded,
                Icmpv4ErrorCode::TimeExceeded(time_exceeded.code()),
            );
        }
        Icmpv4Packet::Redirect(_) => log_unimplemented!((), "receive_icmpv4_packet::redirect"),
        Icmpv4Packet::ParameterProblem(parameter_problem) => {
            ctx.increment_counter("receive_icmpv4_packet::parameter_problem");
            trace!("receive_icmpv4_packet: Received a Parameter Problem message");

            receive_icmpv4_error(
                ctx,
                &parameter_problem,
                Icmpv4ErrorCode::ParameterProblem(parameter_problem.code()),
            );
        }
    }
}

/// Receive an ICMPv6 packet.
pub(crate) fn receive_icmpv6_packet<
    B: BufferMut,
    C: Icmpv6Context<B>
        + PmtuHandler<Ipv6>
        + MldHandler
        + NdpPacketHandler<<C as IpDeviceIdContext>::DeviceId>,
>(
    ctx: &mut C,
    device: Option<C::DeviceId>,
    src_ip: Ipv6Addr,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
    mut buffer: B,
) {
    trace!("receive_icmpv6_packet({}, {})", src_ip, dst_ip);

    let packet = match buffer.parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, dst_ip)) {
        Ok(packet) => packet,
        Err(_) => return, // TODO(joshlf): Do something else here?
    };

    match packet {
        Icmpv6Packet::EchoRequest(echo_request) => {
            ctx.increment_counter("receive_icmpv6_packet::echo_request");

            if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
                let req = *echo_request.message();
                let code = echo_request.code();
                let (local_ip, remote_ip) = (dst_ip, src_ip);
                // TODO(joshlf): Do something if send_icmp_reply returns an
                // error?
                let _ = ctx.send_icmp_reply(device, remote_ip, local_ip, |src_ip| {
                    buffer.encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                        src_ip,
                        remote_ip,
                        code,
                        req.reply(),
                    ))
                });
            } else {
                trace!("receive_icmpv6_packet: Received echo request with an unspecified source address");
            }
        }
        Icmpv6Packet::EchoReply(echo_reply) => {
            ctx.increment_counter("receive_icmpv6_packet::echo_reply");
            trace!("receive_icmpv6_packet: Received an EchoReply message");
            let id = echo_reply.message().id();
            let seq = echo_reply.message().seq();
            receive_icmp_echo_reply::<_, _, _, Icmpv6State<_, _>, _>(
                ctx, src_ip, dst_ip, id, seq, buffer,
            );
        }
        Icmpv6Packet::Ndp(packet) => ctx.receive_ndp_packet(
            device.expect("received NDP packet from localhost"),
            src_ip,
            dst_ip,
            packet,
        ),
        Icmpv6Packet::PacketTooBig(packet_too_big) => {
            ctx.increment_counter("receive_icmpv6_packet::packet_too_big");
            trace!("receive_icmpv6_packet: Received a Packet Too Big message");
            // We are updating the path MTU from the destination address of this
            // `packet` (which is an IP address on this node) to some remote
            // (identified by the source address of this `packet`).
            //
            // `update_pmtu_if_less` may return an error, but it will only
            // happen if the Packet Too Big message's mtu field had a value that
            // was less than the IPv6 minimum mtu (which as per IPv6 RFC 8200,
            // must not happen).
            ctx.update_pmtu_if_less(dst_ip.get(), src_ip, packet_too_big.message().mtu());
            receive_icmpv6_error(ctx, &packet_too_big, Icmpv6ErrorCode::PacketTooBig);
        }
        Icmpv6Packet::Mld(packet) => ctx.receive_mld_packet(
            device.expect("MLD messages must come from a device"),
            src_ip,
            dst_ip,
            packet,
        ),
        Icmpv6Packet::DestUnreachable(dest_unreachable) => receive_icmpv6_error(
            ctx,
            &dest_unreachable,
            Icmpv6ErrorCode::DestUnreachable(dest_unreachable.code()),
        ),
        Icmpv6Packet::TimeExceeded(time_exceeded) => receive_icmpv6_error(
            ctx,
            &time_exceeded,
            Icmpv6ErrorCode::TimeExceeded(time_exceeded.code()),
        ),
        Icmpv6Packet::ParameterProblem(parameter_problem) => receive_icmpv6_error(
            ctx,
            &parameter_problem,
            Icmpv6ErrorCode::ParameterProblem(parameter_problem.code()),
        ),
    }
}

/// Receive an ICMP(v4) error message.
///
/// `receive_icmpv4_error` handles an incoming ICMP error message by parsing the
/// original IPv4 packet and then delegating to the context.
fn receive_icmpv4_error<
    B: BufferMut,
    C: Icmpv4Context<B>,
    BB: ByteSlice,
    M: IcmpMessage<Ipv4, BB, Body = OriginalPacket<BB>>,
>(
    ctx: &mut C,
    packet: &IcmpPacket<Ipv4, BB, M>,
    err: Icmpv4ErrorCode,
) {
    packet.with_original_packet(|res| match res {
        Ok(original_packet) => Icmpv4Context::receive_icmpv4_error(ctx, original_packet, err),
        Err(_) => debug!(
            "receive_icmpv4_error: Got ICMP error message with unparsable original IPv4 packet"
        ),
    })
}

/// Receive an ICMPv6 error message.
///
/// `receive_icmpv6_error` handles an incoming ICMPv6 error message by parsing
/// the original IPv6 packet and then delegating to the context.
fn receive_icmpv6_error<
    B: BufferMut,
    C: Icmpv6Context<B>,
    BB: ByteSlice,
    M: IcmpMessage<Ipv6, BB, Body = OriginalPacket<BB>>,
>(
    ctx: &mut C,
    packet: &IcmpPacket<Ipv6, BB, M>,
    err: Icmpv6ErrorCode,
) {
    packet.with_original_packet(|res| match res {
        Ok(original_packet) => Icmpv6Context::receive_icmpv6_error(ctx, original_packet, err),
        Err(_) => debug!(
            "receive_icmpv6_error: Got ICMPv6 error message with unparsable original IPv6 packet"
        ),
    })
}

/// Send an ICMP(v4) message in response to receiving a packet destined for an
/// unsupported IPv4 protocol.
///
/// `send_icmpv4_protocol_unreachable` sends the appropriate ICMP message in
/// response to receiving an IP packet from `src_ip` to `dst_ip` identifying an
/// unsupported protocol - in particular, a "destination unreachable" message
/// with a "protocol unreachable" code.
///
/// `original_packet` contains the contents of the entire original packet,
/// including the IP header. `header_len` is the length of the header including
/// all options.
pub(crate) fn send_icmpv4_protocol_unreachable<B: BufferMut, C: Icmpv4Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_protocol_unreachable");

    send_icmpv4_dest_unreachable(
        ctx,
        device,
        frame_dst,
        src_ip,
        dst_ip.into_addr(),
        Icmpv4DestUnreachableCode::DestProtocolUnreachable,
        original_packet,
        header_len,
    );
}

/// Send an ICMPv6 message in response to receiving a packet destined for an
/// unsupported Next Header.
///
/// `send_icmpv6_protocol_unreachable` is like
/// [`send_icmpv4_protocol_unreachable`], but for ICMPv6. It sends an ICMPv6
/// "parameter problem" message with an "unrecognized next header type" code.
///
/// `header_len` is the length of all IPv6 headers (including extension headers)
/// *before* the payload with the problematic Next Header type.
pub(crate) fn send_icmpv6_protocol_unreachable<B: BufferMut, C: Icmpv6Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv6_protocol_unreachable");

    send_icmpv6_parameter_problem(
        ctx,
        device,
        frame_dst,
        src_ip,
        dst_ip.into_addr(),
        Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
        // Per RFC 4443, the pointer refers to the first byte of the packet
        // whose Next Header field was unrecognized. It is measured as an offset
        // from the beginning of the first IPv6 header. E.g., a pointer of 40
        // (the length of a single IPv6 header) would indicate that the Next
        // Header field from that header - and hence of the first encapsulated
        // packet - was unrecognized.
        //
        // NOTE: Since header_len is a usize, this could theoretically be a
        // lossy conversion. However, all that means in practice is that, if a
        // remote host somehow managed to get us to process a frame with a 4GB
        // IP header and send an ICMP response, the pointer value would be
        // wrong. It's not worth wasting special logic to avoid generating a
        // malformed packet in a case that will almost certainly never happen.
        Icmpv6ParameterProblem::new(header_len as u32),
        original_packet,
        false,
    );
}

/// Send an ICMP(v4) message in response to receiving a packet destined for an
/// unreachable local transport-layer port.
///
/// `send_icmpv4_port_unreachable` sends the appropriate ICMP message in
/// response to receiving an IP packet from `src_ip` to `dst_ip` with an
/// unreachable local transport-layer port. In particular, this is an ICMP
/// "destination unreachable" message with a "port unreachable" code.
///
/// `original_packet` contains the contents of the entire original packet,
/// including the IP packet header. `header_len` is the length of the entire
/// header, including options.
pub(crate) fn send_icmpv4_port_unreachable<B: BufferMut, C: Icmpv4Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_port_unreachable");

    send_icmpv4_dest_unreachable(
        ctx,
        device,
        frame_dst,
        src_ip,
        dst_ip.into_addr(),
        Icmpv4DestUnreachableCode::DestPortUnreachable,
        original_packet,
        header_len,
    );
}

/// Send an ICMPv6 message in response to receiving a packet destined for an
/// unreachable local transport-layer port.
///
/// `send_icmpv6_port_unreachable` is like [`send_icmpv4_port_unreachable`], but
/// for ICMPv6.
///
/// `original_packet` contains the contents of the entire original packet,
/// including extension headers.
pub(crate) fn send_icmpv6_port_unreachable<B: BufferMut, C: Icmpv6Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
    original_packet: B,
) {
    ctx.increment_counter("send_icmpv6_port_unreachable");

    send_icmpv6_dest_unreachable(
        ctx,
        device,
        frame_dst,
        src_ip,
        dst_ip.into_addr(),
        Icmpv6DestUnreachableCode::PortUnreachable,
        original_packet,
    );
}

/// Send an ICMP(v4) message in response to receiving a packet destined for an
/// unreachable network.
///
/// `send_icmpv4_net_unreachable` sends the appropriate ICMP message in response
/// to receiving an IP packet from `src_ip` to an unreachable `dst_ip`. In
/// particular, this is an ICMP "destination unreachable" message with a "net
/// unreachable" code.
///
/// `original_packet` contains the contents of the entire original packet -
/// including all IP headers. `header_len` is the length of the IPv4 header. It
/// is ignored for IPv6.
pub(crate) fn send_icmpv4_net_unreachable<B: BufferMut, C: Icmpv4Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_net_unreachable");

    // Check whether we MUST NOT send an ICMP error message
    // because the original packet was itself an ICMP error message.
    if is_icmp_error_message::<Ipv4>(proto, &original_packet.as_ref()[header_len..]) {
        return;
    }

    send_icmpv4_dest_unreachable(
        ctx,
        device,
        frame_dst,
        src_ip,
        dst_ip,
        Icmpv4DestUnreachableCode::DestNetworkUnreachable,
        original_packet,
        header_len,
    );
}

/// Send an ICMPv6 message in response to receiving a packet destined for an
/// unreachable network.
///
/// `send_icmpv6_net_unreachable` is like [`send_icmpv4_net_unreachable`], but
/// for ICMPv6. It sends an ICMPv6 "destination unreachable" message with a "no
/// route to destination" code.
///
/// `original_packet` contains the contents of the entire original packet
/// including extension headers. `header_len` is the length of the IP header and
/// all extension headers.
pub(crate) fn send_icmpv6_net_unreachable<B: BufferMut, C: Icmpv6Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv6_net_unreachable");

    // Check whether we MUST NOT send an ICMP error message
    // because the original packet was itself an ICMP error message.
    if is_icmp_error_message::<Ipv6>(proto, &original_packet.as_ref()[header_len..]) {
        return;
    }

    send_icmpv6_dest_unreachable(
        ctx,
        device,
        frame_dst,
        src_ip,
        dst_ip,
        Icmpv6DestUnreachableCode::NoRoute,
        original_packet,
    );
}

/// Send an ICMP(v4) message in response to receiving a packet whose TTL has
/// expired.
///
/// `send_icmpv4_ttl_expired` sends the appropriate ICMP in response to
/// receiving an IP packet from `src_ip` to `dst_ip` whose TTL has expired. In
/// particular, this is an ICMP "time exceeded" message with a "time to live
/// exceeded in transit" code.
///
/// `original_packet` contains the contents of the entire original packet,
/// including the header. `header_len` is the length of the IP header including
/// options.
pub(crate) fn send_icmpv4_ttl_expired<B: BufferMut, C: Icmpv4Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    proto: IpProto,
    mut original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_ttl_expired");

    if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
        // Check whether we MUST NOT send an ICMP error message because the
        // original packet was itself an ICMP error message.
        if is_icmp_error_message::<Ipv4>(proto, &original_packet.as_ref()[header_len..]) {
            return;
        }

        // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
        // body.
        original_packet.shrink_back_to(header_len + 64);
        // TODO(joshlf): Do something if send_icmp_error_message returns an
        // error?
        let _ = try_send_error!(
            ctx,
            ctx.send_icmp_error_message(
                device,
                frame_dst,
                src_ip,
                dst_ip,
                |local_ip| {
                    original_packet.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                        local_ip,
                        src_ip,
                        Icmpv4TimeExceededCode::TtlExpired,
                        IcmpTimeExceeded::default(),
                    ))
                },
                None,
                false,
            )
        );
    }
}

/// Send an ICMPv6 message in response to receiving a packet whose hop limit has
/// expired.
///
/// `send_icmpv6_ttl_expired` is like [`send_icmpv4_ttl_expired`], but for
/// ICMPv6. It sends an ICMPv6 "time exceeded" message with a "hop limit
/// exceeded in transit" code.
///
/// `original_packet` contains the contents of the entire original packet
/// including extension headers. `header_len` is the length of the IP header and
/// all extension headers.
pub(crate) fn send_icmpv6_ttl_expired<B: BufferMut, C: Icmpv6Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv6_ttl_expired");

    if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
        // Check whether we MUST NOT send an ICMP error message because the
        // original packet was itself an ICMP error message.
        if is_icmp_error_message::<Ipv6>(proto, &original_packet.as_ref()[header_len..]) {
            return;
        }

        // TODO(joshlf): Do something if send_icmp_error_message returns an
        // error?
        let _ = try_send_error!(
            ctx,
            ctx.send_icmp_error_message(
                device,
                frame_dst,
                src_ip,
                dst_ip,
                |local_ip| {
                    let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                        local_ip,
                        src_ip,
                        Icmpv6TimeExceededCode::HopLimitExceeded,
                        IcmpTimeExceeded::default(),
                    );

                    // Per RFC 4443, body contains as much of the original body
                    // as possible without exceeding IPv6 minimum MTU.
                    TruncatingSerializer::new(original_packet, TruncateDirection::DiscardBack)
                        .encapsulate(icmp_builder)
                },
                Some(IPV6_MIN_MTU),
                false,
            )
        );
    }
}

// TODO(joshlf): Test send_icmpv6_packet_too_big once we support dummy IPv6 test
// setups.

/// Send an ICMPv6 message in response to receiving a packet whose size exceeds
/// the MTU of the next hop interface.
///
/// `send_icmpv6_packet_too_big` sends an ICMPv6 "packet too big" message in
/// response to receiving an IP packet from `src_ip` to `dst_ip` whose size
/// exceeds the `mtu` of the next hop interface.
pub(crate) fn send_icmpv6_packet_too_big<B: BufferMut, C: Icmpv6Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    proto: IpProto,
    mtu: u32,
    original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv6_packet_too_big");

    if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
        // Check whether we MUST NOT send an ICMP error message because the
        // original packet was itself an ICMP error message.
        if is_icmp_error_message::<Ipv6>(proto, &original_packet.as_ref()[header_len..]) {
            return;
        }

        let _ = try_send_error!(
            ctx,
            ctx.send_icmp_error_message(
                device,
                frame_dst,
                src_ip,
                dst_ip,
                |local_ip| {
                    let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                        local_ip,
                        src_ip,
                        IcmpUnusedCode,
                        Icmpv6PacketTooBig::new(mtu),
                    );

                    // Per RFC 4443, body contains as much of the original body
                    // as possible without exceeding IPv6 minimum MTU.
                    //
                    // The final IP packet must fit within the MTU, so we shrink
                    // the original packet to the MTU minus the IPv6 and ICMP
                    // header sizes.
                    TruncatingSerializer::new(original_packet, TruncateDirection::DiscardBack)
                        .encapsulate(icmp_builder)
                },
                Some(IPV6_MIN_MTU),
                // Note, here we explicitly let `should_send_icmpv6_error` allow
                // a multicast destination (link-layer or destination IP) as RFC
                // 4443 Section 2.4.e explicitly allows sending an ICMP response
                // if the original packet was sent to a multicast IP or link
                // layer if the ICMP response message will be a Packet Too Big
                // Message.
                true,
            )
        );
    }
}

pub(crate) fn send_icmpv4_parameter_problem<B: BufferMut, C: Icmpv4Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    code: Icmpv4ParameterProblemCode,
    parameter_problem: Icmpv4ParameterProblem,
    mut original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_parameter_problem");

    if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
        // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
        // body.
        original_packet.shrink_back_to(header_len + 64);
        // TODO(joshlf): Do something if send_icmp_error_message returns an
        // error?
        let _ = try_send_error!(
            ctx,
            ctx.send_icmp_error_message(
                device,
                frame_dst,
                src_ip,
                dst_ip,
                |local_ip| {
                    original_packet.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                        local_ip,
                        src_ip,
                        code,
                        parameter_problem,
                    ))
                },
                None,
                false,
            )
        );
    } else {
        trace!("send_icmpv4_parameter_problem: Can't send ICMP error response to the unspecified address");
    }
}

/// Send an ICMPv6 Parameter Problem error message.
///
/// If the error message is Code 2 reporting an unrecognized IPv6 option that has the Option
/// Type highest-order two bits set to 10, `allow_dst_multicast` must be set to `true`. See
/// [`should_send_icmpv6_error`] for more details.
///
/// # Panics
///
/// Panics if `allow_multicast_addr` is set to `true`, but this Parameter Problem's code is not
/// 2 (Unrecognized IPv6 Option).
pub(crate) fn send_icmpv6_parameter_problem<B: BufferMut, C: Icmpv6Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    code: Icmpv6ParameterProblemCode,
    parameter_problem: Icmpv6ParameterProblem,
    original_packet: B,
    allow_multicast_dst: bool,
) {
    // Only allow the `allow_multicast_dst` parameter to be set if the code is the unrecognized
    // IPv6 option as that is one of the few exceptions where we can send an ICMP packet in response
    // to a packet that was destined for a multicast address.
    assert!(!allow_multicast_dst || code == Icmpv6ParameterProblemCode::UnrecognizedIpv6Option);

    ctx.increment_counter("send_icmpv6_parameter_problem");

    if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
        // TODO(joshlf): Do something if send_icmp_error_message returns an
        // error?
        let _ = try_send_error!(
            ctx,
            ctx.send_icmp_error_message(
                device,
                frame_dst,
                src_ip,
                dst_ip,
                |local_ip| {
                    let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                        local_ip,
                        src_ip,
                        code,
                        parameter_problem,
                    );

                    // Per RFC 4443, body contains as much of the original body
                    // as possible without exceeding IPv6 minimum MTU.
                    TruncatingSerializer::new(original_packet, TruncateDirection::DiscardBack)
                        .encapsulate(icmp_builder)
                },
                Some(IPV6_MIN_MTU),
                allow_multicast_dst,
            )
        );
    }
}

fn send_icmpv4_dest_unreachable<B: BufferMut, C: Icmpv4Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    code: Icmpv4DestUnreachableCode,
    mut original_packet: B,
    header_len: usize,
) {
    ctx.increment_counter("send_icmpv4_dest_unreachable");

    if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
        // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
        // body.
        original_packet.shrink_back_to(header_len + 64);
        // TODO(joshlf): Do something if send_icmp_error_message returns an
        // error?
        let _ = try_send_error!(
            ctx,
            ctx.send_icmp_error_message(
                device,
                frame_dst,
                src_ip,
                dst_ip,
                |local_ip| {
                    original_packet.encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                        local_ip,
                        src_ip,
                        code,
                        IcmpDestUnreachable::default(),
                    ))
                },
                None,
                false,
            )
        );
    }
}

fn send_icmpv6_dest_unreachable<B: BufferMut, C: Icmpv6Context<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    code: Icmpv6DestUnreachableCode,
    original_packet: B,
) {
    if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
        // TODO(joshlf): Do something if send_icmp_error_message returns an
        // error?
        let _ = try_send_error!(
            ctx,
            ctx.send_icmp_error_message(
                device,
                frame_dst,
                src_ip,
                dst_ip,
                |local_ip| {
                    let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                        local_ip,
                        src_ip,
                        code,
                        IcmpDestUnreachable::default(),
                    );

                    // Per RFC 4443, body contains as much of the original body
                    // as possible without exceeding IPv6 minimum MTU.
                    TruncatingSerializer::new(original_packet, TruncateDirection::DiscardBack)
                        .encapsulate(icmp_builder)
                },
                Some(IPV6_MIN_MTU),
                false,
            )
        );
    }
}

/// Should we send an ICMP(v4) response?
///
/// `should_send_icmpv4_error` implements the logic described in RFC 1122
/// Section 3.2.2. It decides whether, upon receiving an incoming packet with
/// the given parameters, we should send an ICMP response or not. In particular,
/// we do not send an ICMP response if we've received:
/// - a packet destined to a broadcast or multicast address
/// - a packet sent in a link-layer broadcast
/// - a non-initial fragment
/// - a packet whose source address does not define a single host (a
///   zero/unspecified address, a loopback address, a broadcast address, a
///   multicast address, or a Class E address)
///
/// Note that `should_send_icmpv4_error` does NOT check whether the incoming
/// packet contained an ICMP error message. This is because that check is
/// unnecessary for some ICMP error conditions. The ICMP error message check can
/// be performed separately with `is_icmp_error_message`.
pub(crate) fn should_send_icmpv4_error(
    frame_dst: FrameDestination,
    src_ip: SpecifiedAddr<Ipv4Addr>,
    dst_ip: Ipv4Addr,
) -> bool {
    // NOTE: We do not explicitly implement the "unspecified address" check, as
    // it is enforced by the types of the arguments.

    // TODO(joshlf): Implement the rest of the rules:
    // - a packet destined to a subnet broadcast address
    // - a non-initial fragment
    // - a packet whose source address is a subnet broadcast address

    // NOTE: The FrameDestination type has variants for unicast, multicast, and
    // broadcast. One implication of the fact that we only check for broadcast
    // here (in compliance with the RFC) is that we could, in one very unlikely
    // edge case, respond with an ICMP error message to an IP packet which was
    // sent in a link-layer multicast frame. In particular, that can happen if
    // we subscribe to a multicast IP group and, as a result, subscribe to the
    // corresponding multicast MAC address, and we receive a unicast IP packet
    // in a multicast link-layer frame destined to that MAC address.
    //
    // TODO(joshlf): Should we filter incoming multicast IP traffic to make sure
    // that it matches the multicast MAC address of the frame it was
    // encapsulated in?
    !(dst_ip.is_multicast()
        || dst_ip.is_global_broadcast()
        || frame_dst.is_broadcast()
        || src_ip.is_loopback()
        || src_ip.is_global_broadcast()
        || src_ip.is_multicast()
        || src_ip.is_class_e())
}

/// Should we send an ICMPv6 response?
///
/// `should_send_icmpv6_error` implements the logic described in RFC 4443
/// Section 2.4.e. It decides whether, upon receiving an incoming packet with
/// the given parameters, we should send an ICMP response or not. In particular,
/// we do not send an ICMP response if we've received:
/// - a packet destined to a multicast address
///   - Two exceptions to this rules:
///     1) the Packet Too Big Message to allow Path MTU discovery to work for
///        IPv6 multicast
///     2) the Parameter Problem Message, Code 2 reporting an unrecognized IPv6
///        option that has the Option Type highest-order two bits set to 10
/// - a packet sent as a link-layer multicast or broadcast
///   - same exceptions apply here as well.
/// - a packet whose source address does not define a single host (a
///   zero/unspecified address, a loopback address, or a multicast address)
///
/// If an ICMP response will be a Packet Too Big Message or a Parameter Problem
/// Message, Code 2 reporting an unrecognized IPv6 option that has the Option
/// Type highest-order two bits set to 10, `allow_dst_multicast` must be set to
/// `true` so this function will allow the exception mentioned above.
///
/// Note that `should_send_icmpv6_error` does NOT check whether the incoming
/// packet contained an ICMP error message. This is because that check is
/// unnecessary for some ICMP error conditions. The ICMP error message check can
/// be performed separately with `is_icmp_error_message`.
pub(crate) fn should_send_icmpv6_error(
    frame_dst: FrameDestination,
    src_ip: SpecifiedAddr<Ipv6Addr>,
    dst_ip: Ipv6Addr,
    allow_dst_multicast: bool,
) -> bool {
    // NOTE: We do not explicitly implement the "unspecified address" check, as
    // it is enforced by the types of the arguments.
    !((!allow_dst_multicast
        && (dst_ip.is_multicast() || frame_dst.is_multicast() || frame_dst.is_broadcast()))
        || src_ip.is_loopback()
        || src_ip.is_multicast())
}

/// Determine whether or not an IP packet body contains an ICMP error message
/// for the purposes of determining whether or not to send an ICMP response.
///
/// `is_icmp_error_message` checks whether `proto` is ICMP(v4) for IPv4 or
/// ICMPv6 for IPv6 and, if so, attempts to parse `buf` as an ICMP packet in
/// order to determine whether it is an error message or not. If parsing fails,
/// it conservatively assumes that it is an error packet in order to avoid
/// violating the MUST NOT directives of RFC 1122 Section 3.2.2 and [RFC 4443
/// Section 2.4.e].
///
/// [RFC 4443 Section 2.4.e]: https://tools.ietf.org/html/rfc4443#section-2.4
fn is_icmp_error_message<I: IcmpIpExt>(proto: IpProto, buf: &[u8]) -> bool {
    proto == I::IP_PROTO
        && peek_message_type::<I::IcmpMessageType>(buf).map(IcmpMessageType::is_err).unwrap_or(true)
}

/// Common logic for receiving an ICMP echo reply.
fn receive_icmp_echo_reply<
    I: IcmpIpExt,
    S,
    B: BufferMut,
    State: AsRef<ConnAddrMap<IcmpAddr<I::Addr>, IcmpConn<S>>>,
    C: IcmpContext<I, B> + StateContext<State>,
>(
    ctx: &mut C,
    src_ip: I::Addr,
    dst_ip: SpecifiedAddr<I::Addr>,
    id: u16,
    seq: u16,
    body: B,
) {
    if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
        if let Some(conn) = ctx.get_state().as_ref().get_id_by_addr(&IcmpAddr {
            local_addr: dst_ip,
            remote_addr: src_ip,
            icmp_id: id,
        }) {
            trace!("receive_icmp_echo_reply: Received echo reply for local socket");
            ctx.receive_icmp_echo_reply(IcmpConnId::new(conn), seq, body);
        } else {
            // NOTE(brunodalbo): Neither the ICMPv4 or ICMPv6 RFCs explicitly
            // state what to do in case we receive an "unsolicited" echo reply.
            // We only expose the replies if we have a registered connection for
            // the IcmpAddr of the incoming reply for now. Given that a reply
            // should only be sent in response to a request, an ICMP
            // unreachable-type message is probably not appropriate for
            // unsolicited replies.
            trace!("receive_icmp_echo_reply: Received echo reply with no local socket");
        }
    } else {
        trace!("receive_icmp_echo_reply: Received echo reply with an unspecified source address");
    }
}

/// Receive an ICMP(v4) error related to an ICMP socket.
pub(crate) fn receive_icmpv4_socket_error<B: BufferMut, C: Icmpv4Context<B>>(
    ctx: &mut C,
    original_packet: Ipv4PacketRaw<&[u8]>,
    err: Icmpv4ErrorCode,
) {
    ctx.increment_counter("receive_icmpv4_socket_error");
    trace!("receive_icmpv4_socket_error({:?})", err);

    let mut body = original_packet.body().into_inner();
    let echo_request =
        if let Ok(echo_request) = body.parse::<IcmpPacketRaw<Ipv4, _, IcmpEchoRequest>>() {
            echo_request
        } else {
            // NOTE: This might just mean that the error message was in response
            // to a packet that we sent that wasn't an echo request, so we just
            // silently ignore it.
            return;
        };

    if let (Some(src_ip), Some(dst_ip)) =
        (SpecifiedAddr::new(original_packet.src_ip()), SpecifiedAddr::new(original_packet.dst_ip()))
    {
        let id = echo_request.message().id();
        if let Some(conn) = ctx.get_state().as_ref().get_id_by_addr(&IcmpAddr {
            local_addr: src_ip,
            remote_addr: dst_ip,
            icmp_id: id,
        }) {
            let seq = echo_request.message().seq();
            IcmpSocketContext::receive_icmp_error(ctx, IcmpConnId::new(conn), seq, err);
        } else {
            trace!("receive_icmpv4_error: Got ICMPv4 error message for no-longer existent ICMP echo socket");
        }
    } else {
        trace!("receive_icmpv4_error: Got ICMPv4 error message for IPv4 packet with an unspecified destination IP address");
    }
}

/// Receive an ICMPv6 error related to an ICMP socket.
pub(crate) fn receive_icmpv6_socket_error<B: BufferMut, C: Icmpv6Context<B>>(
    ctx: &mut C,
    original_packet: Ipv6PacketRaw<&[u8]>,
    err: Icmpv6ErrorCode,
) {
    ctx.increment_counter("receive_icmpv6_socket_error");
    trace!("receive_icmpv6_socket_error({:?})", err);

    let mut body = match original_packet.body() {
        Ok(body) => body.into_inner(),
        Err(UndefinedBodyBoundsError) => {
            trace!("receive_icmpv6_socket: could not extract original packet's body");
            // There's nothing we can do in this case, so we just return.
            return;
        }
    };

    let echo_request =
        if let Ok(echo_request) = body.parse::<IcmpPacketRaw<Ipv6, _, IcmpEchoRequest>>() {
            echo_request
        } else {
            // NOTE: This might just mean that the error message was in response
            // to a packet that we sent that wasn't an echo request, so we just
            // silently ignore it.
            return;
        };

    if let (Some(src_ip), Some(dst_ip)) =
        (SpecifiedAddr::new(original_packet.src_ip()), SpecifiedAddr::new(original_packet.dst_ip()))
    {
        let id = echo_request.message().id();
        if let Some(conn) = ctx.get_state().as_ref().get_id_by_addr(&IcmpAddr {
            local_addr: src_ip,
            remote_addr: dst_ip,
            icmp_id: id,
        }) {
            let seq = echo_request.message().seq();
            IcmpSocketContext::receive_icmp_error(ctx, IcmpConnId::new(conn), seq, err);
        } else {
            trace!("receive_icmpv6_socket_error: Got ICMPv6 error message for no-longer existant ICMP echo socket");
        }
    } else {
        trace!("receive_icmpv6_socket_error: Got ICMPv6 error message for IPv6 packet with an unspecified destination IP address");
    }
}

/// Send an ICMPv4 echo request on an existing connection.
///
/// # Panics
///
/// `send_icmpv4_echo_request` panics if `conn` is not associated with an ICMPv4
/// connection.
pub fn send_icmpv4_echo_request<B: BufferMut, D: BufferDispatcher<B>>(
    ctx: &mut Context<D>,
    conn: IcmpConnId<Ipv4>,
    seq_num: u16,
    body: B,
) -> Result<(), SendError> {
    send_icmpv4_echo_request_inner(ctx, conn, seq_num, body)
}

fn send_icmpv4_echo_request_inner<
    B: BufferMut,
    C: BufferIcmpSocketContext<Ipv4, B>
        + StateContext<
            Icmpv4State<<C as InstantContext>::Instant, <C as IpSocketContext<Ipv4>>::IpSocket>,
        >,
>(
    ctx: &mut C,
    conn: IcmpConnId<Ipv4>,
    seq_num: u16,
    body: B,
) -> Result<(), SendError> {
    send_icmp_echo_request_inner(ctx, conn, seq_num, body)
}

/// Send an ICMPv6 echo request on an existing connection.
///
/// # Panics
///
/// `send_icmpv6_echo_request` panics if `conn` is not associated with an ICMPv6
/// connection.
pub fn send_icmpv6_echo_request<B: BufferMut, D: BufferDispatcher<B>>(
    ctx: &mut Context<D>,
    conn: IcmpConnId<Ipv6>,
    seq_num: u16,
    body: B,
) -> Result<(), SendError> {
    send_icmpv6_echo_request_inner(ctx, conn, seq_num, body)
}

fn send_icmpv6_echo_request_inner<
    B: BufferMut,
    C: BufferIcmpSocketContext<Ipv6, B>
        + StateContext<
            Icmpv6State<<C as InstantContext>::Instant, <C as IpSocketContext<Ipv6>>::IpSocket>,
        >,
>(
    ctx: &mut C,
    conn: IcmpConnId<Ipv6>,
    seq_num: u16,
    body: B,
) -> Result<(), SendError> {
    send_icmp_echo_request_inner(ctx, conn, seq_num, body)
}

fn send_icmp_echo_request_inner<
    I: IcmpIpExt,
    B: BufferMut,
    S: AsMut<ConnAddrMap<IcmpAddr<I::Addr>, IcmpConn<<C as IpSocketContext<I>>::IpSocket>>>,
    C: BufferIcmpSocketContext<I, B> + StateContext<S>,
>(
    ctx: &mut C,
    conn: IcmpConnId<I>,
    seq_num: u16,
    body: B,
) -> Result<(), SendError>
where
    IcmpEchoRequest: for<'a> IcmpMessage<I, &'a [u8], Code = IcmpUnusedCode>,
{
    // TODO(joshlf): Come up with a better approach to the lifetimes issues than
    // cloning the entire socket.
    let conn = ctx
        .get_state_mut()
        .as_mut()
        .get_conn_by_id(conn.0)
        .expect("icmp::send_icmp_echo_request_inner: no such conn")
        .clone();
    ctx.send_ip_packet(
        &conn.ip,
        body.encapsulate(IcmpPacketBuilder::<I, &[u8], _>::new(
            conn.ip.local_ip().get(),
            conn.ip.remote_ip().get(),
            IcmpUnusedCode,
            IcmpEchoRequest::new(conn.icmp_id, seq_num),
        )),
    )
    .map_err(|(_body, err)| err)
}

/// Creates a new ICMPv4 connection.
///
/// Creates a new ICMPv4 connection with the provided parameters `local_addr`,
/// `remote_addr` and `icmp_id`, and returns its newly-allocated ID. If
/// `local_addr` is `None`, one will be chosen automatically.
///
/// If a connection with the conflicting parameters already exists, the call
/// fails and returns an [`NetstackError`].
pub fn new_icmpv4_connection<D: EventDispatcher>(
    ctx: &mut Context<D>,
    local_addr: Option<SpecifiedAddr<Ipv4Addr>>,
    remote_addr: SpecifiedAddr<Ipv4Addr>,
    icmp_id: u16,
) -> Result<IcmpConnId<Ipv4>, ConnectError> {
    new_icmpv4_connection_inner(ctx, local_addr, remote_addr, icmp_id)
}

// TODO(joshlf): Make this the external function (replacing the existing
// `new_icmpv4_connection`) once the ICMP context traits are part of the public
// API.
fn new_icmpv4_connection_inner<C: Icmpv4SocketContext>(
    ctx: &mut C,
    local_addr: Option<SpecifiedAddr<Ipv4Addr>>,
    remote_addr: SpecifiedAddr<Ipv4Addr>,
    icmp_id: u16,
) -> Result<IcmpConnId<Ipv4>, ConnectError> {
    let ip =
        ctx.new_ip_socket(local_addr, remote_addr, IpProto::Icmp, None, UnroutableBehavior::Close)?;
    Ok(new_icmp_connection_inner(&mut ctx.get_state_mut().conns, remote_addr, icmp_id, ip)?)
}

/// Creates a new ICMPv4 connection.
///
/// Creates a new ICMPv4 connection with the provided parameters `local_addr`,
/// `remote_addr` and `icmp_id`, and returns its newly-allocated ID. If
/// `local_addr` is `None`, one will be chosen automatically.
///
/// If a connection with the conflicting parameters already exists, the call
/// fails and returns an [`NetstackError`].
pub fn new_icmpv6_connection<D: EventDispatcher>(
    ctx: &mut Context<D>,
    local_addr: Option<SpecifiedAddr<Ipv6Addr>>,
    remote_addr: SpecifiedAddr<Ipv6Addr>,
    icmp_id: u16,
) -> Result<IcmpConnId<Ipv6>, ConnectError> {
    new_icmpv6_connection_inner(ctx, local_addr, remote_addr, icmp_id)
}

// TODO(joshlf): Make this the external function (replacing the existing
// `new_icmpv6_connection`) once the ICMP context traits are part of the public
// API.
fn new_icmpv6_connection_inner<C: Icmpv6SocketContext>(
    ctx: &mut C,
    local_addr: Option<SpecifiedAddr<Ipv6Addr>>,
    remote_addr: SpecifiedAddr<Ipv6Addr>,
    icmp_id: u16,
) -> Result<IcmpConnId<Ipv6>, ConnectError> {
    let ip = ctx.new_ip_socket(
        local_addr,
        remote_addr,
        IpProto::Icmpv6,
        None,
        UnroutableBehavior::Close,
    )?;
    Ok(new_icmp_connection_inner(&mut ctx.get_state_mut().conns, remote_addr, icmp_id, ip)?)
}

fn new_icmp_connection_inner<I: IcmpIpExt + IpExt, S: IpSocket<I>>(
    conns: &mut ConnAddrMap<IcmpAddr<I::Addr>, IcmpConn<S>>,
    remote_addr: SpecifiedAddr<I::Addr>,
    icmp_id: u16,
    ip: S,
) -> Result<IcmpConnId<I>, ExistsError> {
    let addr = IcmpAddr { local_addr: *ip.local_ip(), remote_addr, icmp_id };
    if conns.get_id_by_addr(&addr).is_some() {
        return Err(ExistsError);
    }
    Ok(IcmpConnId::new(conns.insert(addr, IcmpConn { icmp_id, ip })))
}

#[cfg(test)]
mod tests {
    use std::fmt::Debug;
    #[cfg(feature = "udp-icmp-port-unreachable")]
    use std::num::NonZeroU16;
    use std::time::Duration;

    use net_types::{
        ip::{Ip, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
        LinkLocalAddr,
    };
    use packet::{Buf, Serializer};
    use specialize_ip_macro::{ip_test, specialize_ip};

    use super::*;
    use crate::context::{
        testutil::{DummyContext, DummyInstant},
        FrameContext, TimerContext,
    };
    use crate::device::{set_routing_enabled, DeviceId, FrameDestination};
    use crate::ip::mld::{MldContext, MldFrameMetadata, MldInterface, MldReportDelay};
    use crate::ip::path_mtu::testutil::DummyPmtuState;
    use crate::ip::socket::testutil::{DummyIpSocket, DummyIpSocketContext};
    use crate::ip::{receive_ipv4_packet, DummyDeviceId, IpExt};
    use crate::testutil::{
        DummyEventDispatcher, DummyEventDispatcherBuilder, DUMMY_CONFIG_V4, DUMMY_CONFIG_V6,
    };
    use crate::wire::icmp::{
        IcmpEchoRequest, IcmpMessage, IcmpPacket, IcmpUnusedCode, Icmpv4TimestampRequest,
        MessageBody, NdpPacket,
    };
    #[cfg(feature = "udp-icmp-port-unreachable")]
    use crate::wire::udp::UdpPacketBuilder;

    //
    // Tests that require an entire IP stack.
    //

    /// Test that receiving a particular IP packet results in a particular ICMP
    /// response.
    ///
    /// Test that receiving an IP packet from remote host
    /// `DUMMY_CONFIG.remote_ip` to host `dst_ip` with `ttl` and `proto` results
    /// in all of the counters in `assert_counters` being triggered at least
    /// once, and exactly one ICMP packet being sent in response with
    /// `expect_message` and `expect_code`.
    ///
    /// The state is initialized to `testutil::DUMMY_CONFIG` when testing.
    #[allow(clippy::too_many_arguments)]
    fn test_receive_ip_packet<
        C: PartialEq + Debug,
        M: for<'a> IcmpMessage<Ipv4, &'a [u8], Code = C> + PartialEq + Debug,
        F: for<'a> Fn(&IcmpPacket<Ipv4, &'a [u8], M>),
    >(
        body: &mut [u8],
        dst_ip: Ipv4Addr,
        ttl: u8,
        proto: IpProto,
        assert_counters: &[&str],
        expect_message: M,
        expect_code: C,
        f: F,
    ) {
        crate::testutil::set_logger_for_test();
        let buffer = Buf::new(body, ..)
            .encapsulate(<Ipv4 as IpExt>::PacketBuilder::new(
                DUMMY_CONFIG_V4.remote_ip,
                dst_ip,
                ttl,
                proto,
            ))
            .serialize_vec_outer()
            .unwrap();

        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V4)
            .build::<DummyEventDispatcher>();

        // currently only used by test_receive_timestamp
        ctx.state_mut().ipv4.icmp.send_timestamp_reply = true;
        let device = DeviceId::new_ethernet(0);
        // currently only used by test_ttl_exceeded
        ctx.state_mut().ipv4.inner.forward = true;
        set_routing_enabled::<_, Ipv4>(&mut ctx, device, true);
        receive_ipv4_packet(&mut ctx, device, FrameDestination::Unicast, buffer);

        for counter in assert_counters {
            assert!(*ctx.state().test_counters.get(counter) > 0, "counter at zero: {}", counter);
        }

        assert_eq!(ctx.dispatcher().frames_sent().len(), 1);
        let (src_mac, dst_mac, src_ip, dst_ip, _, message, code) =
            crate::testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, M, _>(
                &ctx.dispatcher().frames_sent()[0].1,
                f,
            )
            .unwrap();

        assert_eq!(src_mac, DUMMY_CONFIG_V4.local_mac);
        assert_eq!(dst_mac, DUMMY_CONFIG_V4.remote_mac);
        assert_eq!(src_ip, DUMMY_CONFIG_V4.local_ip.get());
        assert_eq!(dst_ip, DUMMY_CONFIG_V4.remote_ip.get());
        assert_eq!(message, expect_message);
        assert_eq!(code, expect_code);
    }

    #[test]
    fn test_receive_echo() {
        crate::testutil::set_logger_for_test();

        let req = IcmpEchoRequest::new(0, 0);
        let req_body = &[1, 2, 3, 4];
        let mut buffer = Buf::new(req_body.to_vec(), ..)
            .encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                DUMMY_CONFIG_V4.remote_ip,
                DUMMY_CONFIG_V4.local_ip,
                IcmpUnusedCode,
                req,
            ))
            .serialize_vec_outer()
            .unwrap();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG_V4.local_ip.get(),
            64,
            IpProto::Icmp,
            &["receive_icmpv4_packet::echo_request", "send_ipv4_packet"],
            req.reply(),
            IcmpUnusedCode,
            |packet| assert_eq!(packet.original_packet().bytes(), req_body),
        );
    }

    #[test]
    fn test_receive_timestamp() {
        crate::testutil::set_logger_for_test();

        let req = Icmpv4TimestampRequest::new(1, 2, 3);
        let mut buffer = Buf::new(vec![], ..)
            .encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                DUMMY_CONFIG_V4.remote_ip,
                DUMMY_CONFIG_V4.local_ip,
                IcmpUnusedCode,
                req,
            ))
            .serialize_vec_outer()
            .unwrap();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG_V4.local_ip.get(),
            64,
            IpProto::Icmp,
            &["receive_icmpv4_packet::timestamp_request", "send_ipv4_packet"],
            req.reply(0x80000000, 0x80000000),
            IcmpUnusedCode,
            |_| {},
        );
    }

    #[test]
    fn test_protocol_unreachable() {
        // Receive an IP packet for an unreachable protocol (255). Check to make
        // sure that we respond with the appropriate ICMP message.
        //
        // TODO(joshlf): Also perform the check for IPv6 once we support dummy
        // IPv6 networks.
        test_receive_ip_packet(
            &mut [0u8; 128],
            DUMMY_CONFIG_V4.local_ip.get(),
            64,
            IpProto::Other(255),
            &["send_icmpv4_protocol_unreachable", "send_icmp_error_message"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestProtocolUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    #[cfg(feature = "udp-icmp-port-unreachable")]
    fn test_port_unreachable() {
        // TODO(joshlf): Use TCP in addition to UDP since UDP only works with
        // the udp-icmp-port-unreachable feature enabled.

        // Receive an IP packet for an unreachable UDP port (1234). Check to
        // make sure that we respond with the appropriate ICMP message.
        //
        // TODO(joshlf):
        // - Also perform the check for IPv6 once we support dummy IPv6
        //   networks.
        // - Perform the same check for TCP once the logic is implemented
        let mut buf = [0u8; 128];
        let mut buffer = Buf::new(&mut buf[..], ..)
            .encapsulate(UdpPacketBuilder::new(
                DUMMY_CONFIG_V4.remote_ip.get(),
                DUMMY_CONFIG_V4.local_ip.get(),
                None,
                NonZeroU16::new(1234).unwrap(),
            ))
            .serialize_vec_outer();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG_V4.local_ip,
            64,
            IpProto::Udp,
            &["send_icmpv4_port_unreachable", "send_icmp_error_message"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestPortUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    fn test_net_unreachable() {
        // Receive an IP packet for an unreachable destination address. Check to
        // make sure that we respond with the appropriate ICMP message.
        //
        // TODO(joshlf): Also perform the check for IPv6 once we support dummy
        // IPv6 networks.
        test_receive_ip_packet(
            &mut [0u8; 128],
            Ipv4Addr::new([1, 2, 3, 4]),
            64,
            IpProto::Udp,
            &["send_icmpv4_net_unreachable", "send_icmp_error_message"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestNetworkUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    fn test_ttl_expired() {
        // Receive an IP packet with an expired TTL. Check to make sure that we
        // respond with the appropriate ICMP message.
        //
        // TODO(joshlf): Also perform the check for IPv6 once we support dummy
        // IPv6 networks.
        test_receive_ip_packet(
            &mut [0u8; 128],
            DUMMY_CONFIG_V4.remote_ip.get(),
            1,
            IpProto::Udp,
            &["send_icmpv4_ttl_expired", "send_icmp_error_message"],
            IcmpTimeExceeded::default(),
            Icmpv4TimeExceededCode::TtlExpired,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    fn test_should_send_icmpv4_error() {
        let src_ip = DUMMY_CONFIG_V4.local_ip;
        let dst_ip = DUMMY_CONFIG_V4.remote_ip.get();
        let frame_dst = FrameDestination::Unicast;
        let multicast_ip_1 = Ipv4Addr::new([224, 0, 0, 1]);
        let multicast_ip_2 = SpecifiedAddr::new(Ipv4Addr::new([224, 0, 0, 2])).unwrap();

        // Should Send.
        assert!(should_send_icmpv4_error(frame_dst, src_ip, dst_ip));

        // Should not send because destined for IP broadcast addr
        assert!(!should_send_icmpv4_error(frame_dst, src_ip, Ipv4::GLOBAL_BROADCAST_ADDRESS.get()));

        // Should not send because destined for multicast addr
        assert!(!should_send_icmpv4_error(frame_dst, src_ip, multicast_ip_1));

        // Should not send because Link Layer Broadcast.
        assert!(!should_send_icmpv4_error(FrameDestination::Broadcast, src_ip, dst_ip));

        // Should not send because from loopback addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4::LOOPBACK_ADDRESS, dst_ip));

        // Should not send because from global broadcast addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4::GLOBAL_BROADCAST_ADDRESS, dst_ip));

        // Should not send because from multicast addr
        assert!(!should_send_icmpv4_error(frame_dst, multicast_ip_2, dst_ip));

        // Should not send because from class E addr
        assert!(!should_send_icmpv4_error(
            frame_dst,
            SpecifiedAddr::new(Ipv4Addr::new([240, 0, 0, 1])).unwrap(),
            dst_ip
        ));
    }

    #[test]
    fn test_should_send_icmpv6_error() {
        let src_ip = DUMMY_CONFIG_V6.local_ip;
        let dst_ip = DUMMY_CONFIG_V6.remote_ip.get();
        let frame_dst = FrameDestination::Unicast;
        let multicast_ip_1 = Ipv6Addr::new([255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
        let multicast_ip_2 =
            SpecifiedAddr::new(Ipv6Addr::new([255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2]))
                .unwrap();

        // Should Send.
        assert!(should_send_icmpv6_error(frame_dst, src_ip, dst_ip, false));
        assert!(should_send_icmpv6_error(frame_dst, src_ip, dst_ip, true));

        // Should not send because destined for multicast addr,
        // unless exception applies
        assert!(!should_send_icmpv6_error(frame_dst, src_ip, multicast_ip_1, false));
        assert!(should_send_icmpv6_error(frame_dst, src_ip, multicast_ip_1, true));

        // Should not send because Link Layer Broadcast.,
        // unless exception applies
        assert!(!should_send_icmpv6_error(FrameDestination::Broadcast, src_ip, dst_ip, false));
        assert!(should_send_icmpv6_error(FrameDestination::Broadcast, src_ip, dst_ip, true));

        // Should not send because from loopback addr
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::LOOPBACK_ADDRESS, dst_ip, false));
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::LOOPBACK_ADDRESS, dst_ip, true));

        // Should not send because from multicast addr
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, dst_ip, false));
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, dst_ip, true));

        // Should not send becuase from multicast addr,
        // even though dest multicast exception applies
        assert!(!should_send_icmpv6_error(
            FrameDestination::Broadcast,
            multicast_ip_2,
            dst_ip,
            false
        ));
        assert!(!should_send_icmpv6_error(
            FrameDestination::Broadcast,
            multicast_ip_2,
            dst_ip,
            true
        ));
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, multicast_ip_1, false));
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, multicast_ip_1, true));
    }

    #[specialize_ip]
    #[ip_test]
    fn test_icmp_connections<I: Ip>() {
        crate::testutil::set_logger_for_test();
        #[ipv4]
        let recv_icmp_packet_name = "receive_icmpv4_packet";
        #[ipv6]
        let recv_icmp_packet_name = "receive_icmpv6_packet";

        let config = crate::testutil::get_dummy_config::<I::Addr>();
        let mut net =
            crate::testutil::new_dummy_network_from_config("alice", "bob", config.clone());

        let icmp_id = 13;

        #[ipv4]
        let conn = new_icmpv4_connection(
            net.context("alice"),
            Some(config.local_ip),
            config.remote_ip,
            icmp_id,
        )
        .unwrap();
        #[ipv6]
        let conn = new_icmpv6_connection(
            net.context("alice"),
            Some(config.local_ip),
            config.remote_ip,
            icmp_id,
        )
        .unwrap();

        let echo_body = vec![1, 2, 3, 4];

        #[ipv4]
        send_icmpv4_echo_request(net.context("alice"), conn, 7, Buf::new(echo_body.clone(), ..))
            .unwrap();
        #[ipv6]
        send_icmpv6_echo_request(net.context("alice"), conn, 7, Buf::new(echo_body.clone(), ..))
            .unwrap();

        net.run_until_idle().unwrap();
        assert_eq!(
            *net.context("bob")
                .state()
                .test_counters
                .get(&format!("{}::echo_request", recv_icmp_packet_name)),
            1
        );
        assert_eq!(
            *net.context("alice")
                .state()
                .test_counters
                .get(&format!("{}::echo_reply", recv_icmp_packet_name)),
            1
        );
        let replies = net.context("alice").dispatcher_mut().take_icmp_replies(conn);
        assert!(!replies.is_empty());
        let (seq, body) = &replies[0];
        assert_eq!(*seq, 7);
        assert_eq!(*body, echo_body);
    }

    //
    // Tests that only require an ICMP stack. Unlike the preceding tests, these
    // only test the ICMP stack and state, and mock everything else. We define
    // the `DummyIcmpv4Context` and `DummyIcmpv6Context` types, which we wrap in
    // a `DummyContext` to provide automatic implementations of a number of
    // required traits. The rest we implement manually.
    //

    // The arguments to `IcmpContext::send_icmp_reply`.
    #[derive(Debug, PartialEq)]
    struct SendIcmpReplyArgs<A: IpAddress> {
        device: Option<DummyDeviceId>,
        src_ip: SpecifiedAddr<A>,
        dst_ip: SpecifiedAddr<A>,
        body: Vec<u8>,
    }

    // The arguments to `IcmpContext::send_icmp_error_message`.
    #[derive(Debug, PartialEq)]
    struct SendIcmpErrorMessageArgs<A: IpAddress> {
        frame_dst: FrameDestination,
        src_ip: SpecifiedAddr<A>,
        dst_ip: A,
        body: Vec<u8>,
        ip_mtu: Option<u32>,
        allow_dst_multicast: bool,
        // Whether `should_send_icmpv{4,6}_error` returned true.
        sent: bool,
    }

    // The arguments to `BufferIcmpSocketContext::receive_icmp_echo_reply`.
    #[allow(unused)] // TODO(joshlf): Remove once we access these fields.
    struct ReceiveIcmpEchoReply<I: Ip> {
        conn: IcmpConnId<I>,
        seq_num: u16,
        data: Vec<u8>,
    }

    // The arguments to `IcmpSocketContext::close_icmp_connection`.
    #[allow(unused)] // TODO(joshlf): Remove once we access these fields.
    struct CloseIcmpConnectionArgs<I: Ip> {
        conn: IcmpConnId<I>,
        err: NoRouteError,
    }

    // The arguments to `IcmpSocketContext::receive_icmp_error`.
    #[derive(Debug, PartialEq)]
    struct ReceiveIcmpSocketErrorArgs<I: IcmpIpExt> {
        conn: IcmpConnId<I>,
        seq_num: u16,
        err: I::ErrorCode,
    }

    struct DummyIcmpContext<I: IcmpIpExt> {
        // All calls to `IcmpContext::send_icmp_reply`.
        send_icmp_reply: Vec<SendIcmpReplyArgs<I::Addr>>,
        // All calls to `IcmpContext::send_icmp_error_message`.
        send_icmp_error_message: Vec<SendIcmpErrorMessageArgs<I::Addr>>,
        // We store calls to `IcmpContext::receive_icmp_error` AND calls to
        // `IcmpSocketContext::receive_icmp_error`. Any call to
        // `IcmpContext::receive_icmp_error` with an IP proto of ICMP(v4|v6)
        // will be stored here and also passed to `receive_icmp_socket_error`,
        // which will in turn call `IcmpSocketContext::receive_icmp_error`.
        receive_icmp_echo_reply: Vec<ReceiveIcmpEchoReply<I>>,
        receive_icmp_error: Vec<I::ErrorCode>,
        receive_icmp_socket_error: Vec<ReceiveIcmpSocketErrorArgs<I>>,
        close_icmp_connection: Vec<CloseIcmpConnectionArgs<I>>,
        pmtu_state: DummyPmtuState<I::Addr>,
        socket_ctx: DummyIpSocketContext<I>,
    }

    impl Default for DummyIcmpContext<Ipv4> {
        fn default() -> DummyIcmpContext<Ipv4> {
            DummyIcmpContext::new(DummyIpSocketContext::new(DUMMY_CONFIG_V4.local_ip, true))
        }
    }

    impl Default for DummyIcmpContext<Ipv6> {
        fn default() -> DummyIcmpContext<Ipv6> {
            DummyIcmpContext::new(DummyIpSocketContext::new(DUMMY_CONFIG_V6.local_ip, true))
        }
    }

    impl<I: IcmpIpExt> DummyIcmpContext<I> {
        fn new(socket_ctx: DummyIpSocketContext<I>) -> DummyIcmpContext<I> {
            DummyIcmpContext {
                send_icmp_reply: Vec::new(),
                send_icmp_error_message: Vec::new(),
                receive_icmp_echo_reply: Vec::new(),
                receive_icmp_error: Vec::new(),
                receive_icmp_socket_error: Vec::new(),
                close_icmp_connection: Vec::new(),
                pmtu_state: DummyPmtuState::default(),
                socket_ctx,
            }
        }
    }

    struct DummyIcmpv4Context {
        inner: DummyIcmpContext<Ipv4>,
        icmp_state: Icmpv4State<DummyInstant, DummyIpSocket<Ipv4Addr>>,
    }

    struct DummyIcmpv6Context {
        inner: DummyIcmpContext<Ipv6>,
        icmp_state: Icmpv6State<DummyInstant, DummyIpSocket<Ipv6Addr>>,
    }

    impl Default for DummyIcmpv4Context {
        fn default() -> DummyIcmpv4Context {
            DummyIcmpv4Context {
                inner: DummyIcmpContext::default(),
                icmp_state: Icmpv4StateBuilder::default().build(),
            }
        }
    }

    impl Default for DummyIcmpv6Context {
        fn default() -> DummyIcmpv6Context {
            DummyIcmpv6Context {
                inner: DummyIcmpContext::default(),
                icmp_state: Icmpv6StateBuilder::default().build(),
            }
        }
    }

    impl Icmpv4SocketContext for Dummyv4Context {
        fn get_state_and_update_meta(
            &mut self,
        ) -> (&mut Icmpv4State<DummyInstant, DummyIpSocket<Ipv4Addr>>, &()) {
            (&mut self.get_mut().icmp_state, &())
        }
    }

    impl Icmpv6SocketContext for Dummyv6Context {
        fn get_state_and_update_meta(
            &mut self,
        ) -> (&mut Icmpv6State<DummyInstant, DummyIpSocket<Ipv6Addr>>, &()) {
            (&mut self.get_mut().icmp_state, &())
        }
    }

    /// Implement a number of traits and methods for the `$inner` and `$outer`
    /// context types.
    macro_rules! impl_context_traits {
        ($ip:ident, $inner:ident, $outer:ident, $state:ident, $should_send:expr) => {
            type $outer = DummyContext<$inner>;

            impl $inner {
                fn with_errors_per_second(errors_per_second: u64) -> $inner {
                    let mut ctx = $inner::default();
                    ctx.icmp_state.error_send_bucket = TokenBucket::new(errors_per_second);
                    ctx
                }
            }

            impl IpDeviceIdContext for $outer {
                type DeviceId = DummyDeviceId;
            }

            impl_pmtu_handler!($outer, $ip);

            impl AsMut<DummyPmtuState<<$ip as Ip>::Addr>> for $outer {
                fn as_mut(&mut self) -> &mut DummyPmtuState<<$ip as Ip>::Addr> {
                    &mut self.get_mut().inner.pmtu_state
                }
            }

            impl AsRef<DummyIpSocketContext<$ip>> for $inner {
                fn as_ref(&self) -> &DummyIpSocketContext<$ip> {
                    &self.inner.socket_ctx
                }
            }

            impl AsMut<DummyIpSocketContext<$ip>> for $inner {
                fn as_mut(&mut self) -> &mut DummyIpSocketContext<$ip> {
                    &mut self.inner.socket_ctx
                }
            }

            impl StateContext<$state<DummyInstant, DummyIpSocket<<$ip as Ip>::Addr>>> for $outer {
                fn get_state_with(
                    &self,
                    _id: (),
                ) -> &$state<DummyInstant, DummyIpSocket<<$ip as Ip>::Addr>> {
                    &self.get_ref().icmp_state
                }

                fn get_state_mut_with(
                    &mut self,
                    _id: (),
                ) -> &mut $state<DummyInstant, DummyIpSocket<<$ip as Ip>::Addr>> {
                    &mut self.get_mut().icmp_state
                }
            }

            impl IcmpSocketContext<$ip> for $outer {
                fn receive_icmp_error(
                    &mut self,
                    conn: IcmpConnId<$ip>,
                    seq_num: u16,
                    err: <$ip as IcmpIpExt>::ErrorCode,
                ) {
                    self.increment_counter("IcmpSocketContext::receive_icmp_error");
                    self.get_mut()
                        .inner
                        .receive_icmp_socket_error
                        .push(ReceiveIcmpSocketErrorArgs { conn, seq_num, err });
                }

                fn close_icmp_connection(&mut self, conn: IcmpConnId<$ip>, err: NoRouteError) {
                    self.get_mut()
                        .inner
                        .close_icmp_connection
                        .push(CloseIcmpConnectionArgs { conn, err });
                }
            }

            impl<B: BufferMut> BufferIcmpSocketContext<$ip, B> for $outer {
                fn receive_icmp_echo_reply(
                    &mut self,
                    conn: IcmpConnId<$ip>,
                    seq_num: u16,
                    data: B,
                ) {
                    self.get_mut().inner.receive_icmp_echo_reply.push(ReceiveIcmpEchoReply {
                        conn,
                        seq_num,
                        data: data.as_ref().to_vec(),
                    });
                }
            }

            impl<B: BufferMut> IcmpContext<$ip, B> for $outer {
                // TODO(rheacock): remove the `allow(unreachable_code`) once this is implemented.
                #[allow(unreachable_code)]
                fn send_icmp_reply<
                    S: Serializer<Buffer = B>,
                    F: FnOnce(SpecifiedAddr<<$ip as Ip>::Addr>) -> S,
                >(
                    &mut self,
                    device: Option<DummyDeviceId>,
                    src_ip: SpecifiedAddr<<$ip as Ip>::Addr>,
                    dst_ip: SpecifiedAddr<<$ip as Ip>::Addr>,
                    _get_body: F,
                ) -> Result<(), S> {
                    self.get_mut().inner.send_icmp_reply.push(SendIcmpReplyArgs {
                        device,
                        src_ip,
                        dst_ip,
                        body: unimplemented!(),
                    });
                    Ok(())
                }

                fn send_icmp_error_message<
                    S: Serializer<Buffer = B>,
                    F: FnOnce(SpecifiedAddr<<$ip as Ip>::Addr>) -> S,
                >(
                    &mut self,
                    _device: DummyDeviceId,
                    frame_dst: FrameDestination,
                    src_ip: SpecifiedAddr<<$ip as Ip>::Addr>,
                    dst_ip: <$ip as Ip>::Addr,
                    get_body: F,
                    ip_mtu: Option<u32>,
                    allow_dst_multicast: bool,
                ) -> Result<(), S> {
                    self.increment_counter("IcmpContext::send_icmp_error_message");
                    let sent = $should_send(frame_dst, src_ip, dst_ip, allow_dst_multicast);
                    self.get_mut().inner.send_icmp_error_message.push(SendIcmpErrorMessageArgs {
                        frame_dst,
                        src_ip,
                        dst_ip,
                        // Use `unwrap_or_else` like this rather than `unwrap`
                        // because the error variant of the return value of
                        // `serialize_vec_outer` doesn't implement `Debug`,
                        // which is required for `unwrap`.
                        body: get_body(SpecifiedAddr::new(dst_ip).unwrap())
                            .serialize_vec_outer()
                            .unwrap_or_else(|_| panic!("failed to serialize body"))
                            .as_ref()
                            .to_vec(),
                        ip_mtu,
                        allow_dst_multicast,
                        sent,
                    });
                    Ok(())
                }
            }
        };
    }

    impl_context_traits!(Ipv4, DummyIcmpv4Context, Dummyv4Context, Icmpv4State, |f, s, d, _| {
        should_send_icmpv4_error(f, s, d)
    });
    impl_context_traits!(Ipv6, DummyIcmpv6Context, Dummyv6Context, Icmpv6State, |f, s, d, a| {
        should_send_icmpv6_error(f, s, d, a)
    });

    impl<B: BufferMut> Icmpv4Context<B> for Dummyv4Context {
        fn receive_icmpv4_error(
            &mut self,
            original_packet: Ipv4PacketRaw<&[u8]>,
            err: Icmpv4ErrorCode,
        ) {
            self.increment_counter("Icmpv4Context::receive_icmpv4_error");
            self.get_mut().inner.receive_icmp_error.push(err);
            if original_packet.proto() == IpProto::Icmp {
                receive_icmpv4_socket_error::<B, _>(self, original_packet, err);
            }
        }
    }

    impl<B: BufferMut> Icmpv6Context<B> for Dummyv6Context {
        fn receive_icmpv6_error(
            &mut self,
            original_packet: Ipv6PacketRaw<&[u8]>,
            err: Icmpv6ErrorCode,
        ) {
            self.increment_counter("Icmpv6Context::receive_icmpv6_error");
            self.get_mut().inner.receive_icmp_error.push(err);
            if original_packet.next_header() == IpProto::Icmpv6 {
                receive_icmpv6_socket_error::<B, _>(self, original_packet, err);
            }
        }
    }

    impl NdpPacketHandler<DummyDeviceId> for Dummyv6Context {
        fn receive_ndp_packet<B: ByteSlice>(
            &mut self,
            _device: DummyDeviceId,
            _src_ip: Ipv6Addr,
            _dst_ip: SpecifiedAddr<Ipv6Addr>,
            _packet: NdpPacket<B>,
        ) {
            unimplemented!()
        }
    }

    impl MldContext for Dummyv6Context {
        fn get_ipv6_link_local_addr(
            &self,
            _device: DummyDeviceId,
        ) -> Option<LinkLocalAddr<Ipv6Addr>> {
            unimplemented!()
        }
    }

    impl<B: BufferMut> FrameContext<B, MldFrameMetadata<DummyDeviceId>> for Dummyv6Context {
        fn send_frame<S: Serializer<Buffer = B>>(
            &mut self,
            _metadata: MldFrameMetadata<DummyDeviceId>,
            _frame: S,
        ) -> Result<(), S> {
            unimplemented!()
        }
    }

    impl StateContext<MldInterface<DummyInstant>, DummyDeviceId> for Dummyv6Context {
        fn get_state_with(&self, _id: DummyDeviceId) -> &MldInterface<DummyInstant> {
            unimplemented!()
        }

        fn get_state_mut_with(&mut self, _id: DummyDeviceId) -> &mut MldInterface<DummyInstant> {
            unimplemented!()
        }
    }

    impl TimerContext<MldReportDelay<DummyDeviceId>> for Dummyv6Context {
        fn schedule_timer_instant(
            &mut self,
            _time: DummyInstant,
            _id: MldReportDelay<DummyDeviceId>,
        ) -> Option<DummyInstant> {
            unimplemented!()
        }

        fn cancel_timer(&mut self, _id: MldReportDelay<DummyDeviceId>) -> Option<DummyInstant> {
            unimplemented!()
        }

        fn cancel_timers_with<F: FnMut(&MldReportDelay<DummyDeviceId>) -> bool>(&mut self, _f: F) {
            unimplemented!()
        }

        fn scheduled_instant(&self, _id: MldReportDelay<DummyDeviceId>) -> Option<DummyInstant> {
            unimplemented!()
        }
    }

    #[test]
    fn test_receive_icmpv4_error() {
        // Chosen arbitrarily to be a) non-zero (it's easy to accidentally get
        // the value 0) and, b) different from each other.
        const ICMP_ID: u16 = 0x0F;
        const SEQ_NUM: u16 = 0xF0;

        /// Test receiving an ICMP error message.
        ///
        /// Test that receiving an ICMP error message with the given code and
        /// message contents, and containing the given original IPv4 packet,
        /// results in the counter values in `assert_counters`. After that
        /// assertion passes, `f` is called on the context so that the caller
        /// can perform whatever extra validation they want.
        ///
        /// The error message will be sent from `DUMMY_CONFIG_V4.remote_ip` to
        /// `DUMMY_CONFIG_V4.local_ip`. Before the message is sent, an ICMP
        /// socket will be established with the ID `ICMP_ID`, and
        /// `test_receive_icmpv4_error_helper` will assert that its `IcmpConnId`
        /// is 0. This allows the caller to craft the `original_packet` so that
        /// it should be delivered to this socket.
        fn test_receive_icmpv4_error_helper<
            C: Debug,
            M: for<'a> IcmpMessage<Ipv4, &'a [u8], Code = C> + Debug,
            F: Fn(&Dummyv4Context),
        >(
            original_packet: &mut [u8],
            code: C,
            msg: M,
            assert_counters: &[(&str, usize)],
            f: F,
        ) {
            crate::testutil::set_logger_for_test();

            let mut ctx = Dummyv4Context::default();
            // NOTE: This assertion is not a correctness requirement. It's just
            // that the rest of this test assumes that the new connection has ID
            // 0. If this assertion fails in the future, that isn't necessarily
            // evidence of a bug; we may just have to update this test to
            // accomodate whatever new ID allocation scheme is being used.
            assert_eq!(
                new_icmpv4_connection_inner(
                    &mut ctx,
                    Some(DUMMY_CONFIG_V4.local_ip),
                    DUMMY_CONFIG_V4.remote_ip,
                    ICMP_ID
                )
                .unwrap(),
                IcmpConnId::new(0)
            );

            receive_icmpv4_packet(
                &mut ctx,
                Some(DummyDeviceId),
                DUMMY_CONFIG_V4.remote_ip.get(),
                DUMMY_CONFIG_V4.local_ip,
                Buf::new(original_packet, ..)
                    .encapsulate(IcmpPacketBuilder::new(
                        DUMMY_CONFIG_V4.remote_ip,
                        DUMMY_CONFIG_V4.local_ip,
                        code,
                        msg,
                    ))
                    .serialize_vec_outer()
                    .unwrap(),
            );

            for (ctr, count) in assert_counters {
                assert_eq!(ctx.get_counter(ctr), *count, "wrong count for counter {}", ctr);
            }
            f(&ctx);
        }
        // Test that, when we receive various ICMPv4 error messages, we properly
        // pass them up to the IP layer and, sometimes, to the transport layer.

        // First, test with an original packet containing an ICMP message. Since
        // this test mock supports ICMP sockets, this error can be delivered all
        // the way up the stack.

        // A buffer containing an ICMP echo request with ID `ICMP_ID` and
        // sequence number `SEQ_NUM` from the local IP to the remote IP. Any
        // ICMP error message which contains this as its original packet should
        // be delivered to the socket created in
        // `test_receive_icmpv4_error_helper`.
        let mut buffer = Buf::new(&mut [], ..)
            .encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                DUMMY_CONFIG_V4.local_ip,
                DUMMY_CONFIG_V4.remote_ip,
                IcmpUnusedCode,
                IcmpEchoRequest::new(ICMP_ID, SEQ_NUM),
            ))
            .encapsulate(<Ipv4 as IpExt>::PacketBuilder::new(
                DUMMY_CONFIG_V4.local_ip,
                DUMMY_CONFIG_V4.remote_ip,
                64,
                IpProto::Icmp,
            ))
            .serialize_vec_outer()
            .unwrap();

        test_receive_icmpv4_error_helper(
            buffer.as_mut(),
            Icmpv4DestUnreachableCode::DestNetworkUnreachable,
            IcmpDestUnreachable::default(),
            &[
                ("Icmpv4Context::receive_icmpv4_error", 1),
                ("receive_icmpv4_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 1),
            ],
            |ctx| {
                let err = Icmpv4ErrorCode::DestUnreachable(
                    Icmpv4DestUnreachableCode::DestNetworkUnreachable,
                );
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(
                    ctx.get_ref().inner.receive_icmp_socket_error,
                    [ReceiveIcmpSocketErrorArgs {
                        conn: IcmpConnId::new(0),
                        seq_num: SEQ_NUM,
                        err
                    }]
                );
            },
        );

        test_receive_icmpv4_error_helper(
            buffer.as_mut(),
            Icmpv4TimeExceededCode::TtlExpired,
            IcmpTimeExceeded::default(),
            &[
                ("Icmpv4Context::receive_icmpv4_error", 1),
                ("receive_icmpv4_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 1),
            ],
            |ctx| {
                let err = Icmpv4ErrorCode::TimeExceeded(Icmpv4TimeExceededCode::TtlExpired);
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(
                    ctx.get_ref().inner.receive_icmp_socket_error,
                    [ReceiveIcmpSocketErrorArgs {
                        conn: IcmpConnId::new(0),
                        seq_num: SEQ_NUM,
                        err
                    }]
                );
            },
        );

        test_receive_icmpv4_error_helper(
            buffer.as_mut(),
            Icmpv4ParameterProblemCode::PointerIndicatesError,
            Icmpv4ParameterProblem::new(0),
            &[
                ("Icmpv4Context::receive_icmpv4_error", 1),
                ("receive_icmpv4_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 1),
            ],
            |ctx| {
                let err = Icmpv4ErrorCode::ParameterProblem(
                    Icmpv4ParameterProblemCode::PointerIndicatesError,
                );
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(
                    ctx.get_ref().inner.receive_icmp_socket_error,
                    [ReceiveIcmpSocketErrorArgs {
                        conn: IcmpConnId::new(0),
                        seq_num: SEQ_NUM,
                        err
                    }]
                );
            },
        );

        // Second, test with an original packet containing a malformed ICMP
        // packet (we accomplish this by leaving the IP packet's body empty). We
        // should process this packet in `receive_icmpv4_socket_error`, but we
        // should go no further - in particular, we should not call
        // `IcmpSocketContext::receive_icmp_error`.

        let mut buffer = Buf::new(&mut [], ..)
            .encapsulate(<Ipv4 as IpExt>::PacketBuilder::new(
                DUMMY_CONFIG_V4.local_ip,
                DUMMY_CONFIG_V4.remote_ip,
                64,
                IpProto::Icmp,
            ))
            .serialize_vec_outer()
            .unwrap();

        test_receive_icmpv4_error_helper(
            buffer.as_mut(),
            Icmpv4DestUnreachableCode::DestNetworkUnreachable,
            IcmpDestUnreachable::default(),
            &[
                ("Icmpv4Context::receive_icmpv4_error", 1),
                ("receive_icmpv4_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv4ErrorCode::DestUnreachable(
                    Icmpv4DestUnreachableCode::DestNetworkUnreachable,
                );
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        test_receive_icmpv4_error_helper(
            buffer.as_mut(),
            Icmpv4TimeExceededCode::TtlExpired,
            IcmpTimeExceeded::default(),
            &[
                ("Icmpv4Context::receive_icmpv4_error", 1),
                ("receive_icmpv4_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv4ErrorCode::TimeExceeded(Icmpv4TimeExceededCode::TtlExpired);
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        test_receive_icmpv4_error_helper(
            buffer.as_mut(),
            Icmpv4ParameterProblemCode::PointerIndicatesError,
            Icmpv4ParameterProblem::new(0),
            &[
                ("Icmpv4Context::receive_icmpv4_error", 1),
                ("receive_icmpv4_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv4ErrorCode::ParameterProblem(
                    Icmpv4ParameterProblemCode::PointerIndicatesError,
                );
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        // Third, test with an original packet containing a UDP packet. This
        // allows us to verify that protocol numbers are handled properly by
        // checking that `receive_icmpv4_socket_error` was NOT called.

        let mut buffer = Buf::new(&mut [], ..)
            .encapsulate(<Ipv4 as IpExt>::PacketBuilder::new(
                DUMMY_CONFIG_V4.local_ip,
                DUMMY_CONFIG_V4.remote_ip,
                64,
                IpProto::Udp,
            ))
            .serialize_vec_outer()
            .unwrap();

        test_receive_icmpv4_error_helper(
            buffer.as_mut(),
            Icmpv4DestUnreachableCode::DestNetworkUnreachable,
            IcmpDestUnreachable::default(),
            &[
                ("Icmpv4Context::receive_icmpv4_error", 1),
                ("receive_icmpv4_socket_error", 0),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv4ErrorCode::DestUnreachable(
                    Icmpv4DestUnreachableCode::DestNetworkUnreachable,
                );
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        test_receive_icmpv4_error_helper(
            buffer.as_mut(),
            Icmpv4TimeExceededCode::TtlExpired,
            IcmpTimeExceeded::default(),
            &[
                ("Icmpv4Context::receive_icmpv4_error", 1),
                ("receive_icmpv4_socket_error", 0),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv4ErrorCode::TimeExceeded(Icmpv4TimeExceededCode::TtlExpired);
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        test_receive_icmpv4_error_helper(
            buffer.as_mut(),
            Icmpv4ParameterProblemCode::PointerIndicatesError,
            Icmpv4ParameterProblem::new(0),
            &[
                ("Icmpv4Context::receive_icmpv4_error", 1),
                ("receive_icmpv4_socket_error", 0),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv4ErrorCode::ParameterProblem(
                    Icmpv4ParameterProblemCode::PointerIndicatesError,
                );
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );
    }

    #[test]
    fn test_receive_icmpv6_error() {
        // Chosen arbitrarily to be a) non-zero (it's easy to accidentally get
        // the value 0) and, b) different from each other.
        const ICMP_ID: u16 = 0x0F;
        const SEQ_NUM: u16 = 0xF0;

        /// Test receiving an ICMPv6 error message.
        ///
        /// Test that receiving an ICMP error message with the given code and
        /// message contents, and containing the given original IPv4 packet,
        /// results in the counter values in `assert_counters`. After that
        /// assertion passes, `f` is called on the context so that the caller
        /// can perform whatever extra validation they want.
        ///
        /// The error message will be sent from `DUMMY_CONFIG_V6.remote_ip` to
        /// `DUMMY_CONFIG_V6.local_ip`. Before the message is sent, an ICMP
        /// socket will be established with the ID `ICMP_ID`, and
        /// `test_receive_icmpv6_error_helper` will assert that its `IcmpConnId`
        /// is 0. This allows the caller to craft the `original_packet` so that
        /// it should be delivered to this socket.
        fn test_receive_icmpv6_error_helper<
            C: Debug,
            M: for<'a> IcmpMessage<Ipv6, &'a [u8], Code = C> + Debug,
            F: Fn(&Dummyv6Context),
        >(
            original_packet: &mut [u8],
            code: C,
            msg: M,
            assert_counters: &[(&str, usize)],
            f: F,
        ) {
            crate::testutil::set_logger_for_test();

            let mut ctx = Dummyv6Context::default();
            // NOTE: This assertion is not a correctness requirement. It's just
            // that the rest of this test assumes that the new connection has ID
            // 0. If this assertion fails in the future, that isn't necessarily
            // evidence of a bug; we may just have to update this test to
            // accomodate whatever new ID allocation scheme is being used.
            assert_eq!(
                new_icmpv6_connection_inner(
                    &mut ctx,
                    Some(DUMMY_CONFIG_V6.local_ip),
                    DUMMY_CONFIG_V6.remote_ip,
                    ICMP_ID
                )
                .unwrap(),
                IcmpConnId::new(0)
            );

            receive_icmpv6_packet(
                &mut ctx,
                Some(DummyDeviceId),
                DUMMY_CONFIG_V6.remote_ip.get(),
                DUMMY_CONFIG_V6.local_ip,
                Buf::new(original_packet, ..)
                    .encapsulate(IcmpPacketBuilder::new(
                        DUMMY_CONFIG_V6.remote_ip,
                        DUMMY_CONFIG_V6.local_ip,
                        code,
                        msg,
                    ))
                    .serialize_vec_outer()
                    .unwrap(),
            );

            for (ctr, count) in assert_counters {
                assert_eq!(ctx.get_counter(ctr), *count, "wrong count for counter {}", ctr);
            }
            f(&ctx);
        }
        // Test that, when we receive various ICMPv6 error messages, we properly
        // pass them up to the IP layer and, sometimes, to the transport layer.

        // First, test with an original packet containing an ICMPv6 message.
        // Since this test mock supports ICMPv6 sockets, this error can be
        // delivered all the way up the stack.

        // A buffer containing an ICMPv6 echo request with ID `ICMP_ID` and
        // sequence number `SEQ_NUM` from the local IP to the remote IP. Any
        // ICMPv6 error message which contains this as its original packet
        // should be delivered to the socket created in
        // `test_receive_icmpv6_error_helper`.
        let mut buffer = Buf::new(&mut [], ..)
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                DUMMY_CONFIG_V6.local_ip,
                DUMMY_CONFIG_V6.remote_ip,
                IcmpUnusedCode,
                IcmpEchoRequest::new(ICMP_ID, SEQ_NUM),
            ))
            .encapsulate(<Ipv6 as IpExt>::PacketBuilder::new(
                DUMMY_CONFIG_V6.local_ip,
                DUMMY_CONFIG_V6.remote_ip,
                64,
                IpProto::Icmpv6,
            ))
            .serialize_vec_outer()
            .unwrap();

        test_receive_icmpv6_error_helper(
            buffer.as_mut(),
            Icmpv6DestUnreachableCode::NoRoute,
            IcmpDestUnreachable::default(),
            &[
                ("Icmpv6Context::receive_icmpv6_error", 1),
                ("receive_icmpv6_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 1),
            ],
            |ctx| {
                let err = Icmpv6ErrorCode::DestUnreachable(Icmpv6DestUnreachableCode::NoRoute);
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(
                    ctx.get_ref().inner.receive_icmp_socket_error,
                    [ReceiveIcmpSocketErrorArgs {
                        conn: IcmpConnId::new(0),
                        seq_num: SEQ_NUM,
                        err
                    }]
                );
            },
        );

        test_receive_icmpv6_error_helper(
            buffer.as_mut(),
            Icmpv6TimeExceededCode::HopLimitExceeded,
            IcmpTimeExceeded::default(),
            &[
                ("Icmpv6Context::receive_icmpv6_error", 1),
                ("receive_icmpv6_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 1),
            ],
            |ctx| {
                let err = Icmpv6ErrorCode::TimeExceeded(Icmpv6TimeExceededCode::HopLimitExceeded);
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(
                    ctx.get_ref().inner.receive_icmp_socket_error,
                    [ReceiveIcmpSocketErrorArgs {
                        conn: IcmpConnId::new(0),
                        seq_num: SEQ_NUM,
                        err
                    }]
                );
            },
        );

        test_receive_icmpv6_error_helper(
            buffer.as_mut(),
            Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
            Icmpv6ParameterProblem::new(0),
            &[
                ("Icmpv6Context::receive_icmpv6_error", 1),
                ("receive_icmpv6_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 1),
            ],
            |ctx| {
                let err = Icmpv6ErrorCode::ParameterProblem(
                    Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                );
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(
                    ctx.get_ref().inner.receive_icmp_socket_error,
                    [ReceiveIcmpSocketErrorArgs {
                        conn: IcmpConnId::new(0),
                        seq_num: SEQ_NUM,
                        err
                    }]
                );
            },
        );

        // Second, test with an original packet containing a malformed ICMPv6
        // packet (we accomplish this by leaving the IP packet's body empty). We
        // should process this packet in `receive_icmpv6_socket_error`, but we
        // should go no further - in particular, we should not call
        // `IcmpSocketContext::receive_icmp_error`.

        let mut buffer = Buf::new(&mut [], ..)
            .encapsulate(<Ipv6 as IpExt>::PacketBuilder::new(
                DUMMY_CONFIG_V6.local_ip,
                DUMMY_CONFIG_V6.remote_ip,
                64,
                IpProto::Icmpv6,
            ))
            .serialize_vec_outer()
            .unwrap();

        test_receive_icmpv6_error_helper(
            buffer.as_mut(),
            Icmpv6DestUnreachableCode::NoRoute,
            IcmpDestUnreachable::default(),
            &[
                ("Icmpv6Context::receive_icmpv6_error", 1),
                ("receive_icmpv6_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv6ErrorCode::DestUnreachable(Icmpv6DestUnreachableCode::NoRoute);
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        test_receive_icmpv6_error_helper(
            buffer.as_mut(),
            Icmpv6TimeExceededCode::HopLimitExceeded,
            IcmpTimeExceeded::default(),
            &[
                ("Icmpv6Context::receive_icmpv6_error", 1),
                ("receive_icmpv6_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv6ErrorCode::TimeExceeded(Icmpv6TimeExceededCode::HopLimitExceeded);
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        test_receive_icmpv6_error_helper(
            buffer.as_mut(),
            Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
            Icmpv6ParameterProblem::new(0),
            &[
                ("Icmpv6Context::receive_icmpv6_error", 1),
                ("receive_icmpv6_socket_error", 1),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv6ErrorCode::ParameterProblem(
                    Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                );
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        // Third, test with an original packet containing a UDP packet. This
        // allows us to verify that protocol numbers are handled properly by
        // checking that `receive_icmpv6_socket_error` was NOT called.

        let mut buffer = Buf::new(&mut [], ..)
            .encapsulate(<Ipv6 as IpExt>::PacketBuilder::new(
                DUMMY_CONFIG_V6.local_ip,
                DUMMY_CONFIG_V6.remote_ip,
                64,
                IpProto::Udp,
            ))
            .serialize_vec_outer()
            .unwrap();

        test_receive_icmpv6_error_helper(
            buffer.as_mut(),
            Icmpv6DestUnreachableCode::NoRoute,
            IcmpDestUnreachable::default(),
            &[
                ("Icmpv6Context::receive_icmpv6_error", 1),
                ("receive_icmpv6_socket_error", 0),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv6ErrorCode::DestUnreachable(Icmpv6DestUnreachableCode::NoRoute);
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        test_receive_icmpv6_error_helper(
            buffer.as_mut(),
            Icmpv6TimeExceededCode::HopLimitExceeded,
            IcmpTimeExceeded::default(),
            &[
                ("Icmpv6Context::receive_icmpv6_error", 1),
                ("receive_icmpv6_socket_error", 0),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv6ErrorCode::TimeExceeded(Icmpv6TimeExceededCode::HopLimitExceeded);
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );

        test_receive_icmpv6_error_helper(
            buffer.as_mut(),
            Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
            Icmpv6ParameterProblem::new(0),
            &[
                ("Icmpv6Context::receive_icmpv6_error", 1),
                ("receive_icmpv6_socket_error", 0),
                ("IcmpSocketContext::receive_icmp_error", 0),
            ],
            |ctx| {
                let err = Icmpv6ErrorCode::ParameterProblem(
                    Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                );
                assert_eq!(ctx.get_ref().inner.receive_icmp_error, [err]);
                assert_eq!(ctx.get_ref().inner.receive_icmp_socket_error, []);
            },
        );
    }

    #[test]
    fn test_error_rate_limit() {
        crate::testutil::set_logger_for_test();

        /// Call `send_icmpv4_ttl_expired` with dummy values.
        fn send_icmpv4_ttl_expired_helper(ctx: &mut Dummyv4Context) {
            send_icmpv4_ttl_expired(
                ctx,
                DummyDeviceId,
                FrameDestination::Unicast,
                DUMMY_CONFIG_V4.remote_ip.get(),
                DUMMY_CONFIG_V4.local_ip.get(),
                IpProto::Udp,
                Buf::new(&mut [], ..),
                0,
            );
        }

        /// Call `send_icmpv4_parameter_problem` with dummy values.
        fn send_icmpv4_parameter_problem_helper(ctx: &mut Dummyv4Context) {
            send_icmpv4_parameter_problem(
                ctx,
                DummyDeviceId,
                FrameDestination::Unicast,
                DUMMY_CONFIG_V4.remote_ip.get(),
                DUMMY_CONFIG_V4.local_ip.get(),
                Icmpv4ParameterProblemCode::PointerIndicatesError,
                Icmpv4ParameterProblem::new(0),
                Buf::new(&mut [], ..),
                0,
            );
        }

        /// Call `send_icmpv4_dest_unreachable` with dummy values.
        fn send_icmpv4_dest_unreachable_helper(ctx: &mut Dummyv4Context) {
            send_icmpv4_dest_unreachable(
                ctx,
                DummyDeviceId,
                FrameDestination::Unicast,
                DUMMY_CONFIG_V4.remote_ip.get(),
                DUMMY_CONFIG_V4.local_ip.get(),
                Icmpv4DestUnreachableCode::DestNetworkUnreachable,
                Buf::new(&mut [], ..),
                0,
            );
        }

        /// Call `send_icmpv6_ttl_expired` with dummy values.
        fn send_icmpv6_ttl_expired_helper(ctx: &mut Dummyv6Context) {
            send_icmpv6_ttl_expired(
                ctx,
                DummyDeviceId,
                FrameDestination::Unicast,
                DUMMY_CONFIG_V6.remote_ip.get(),
                DUMMY_CONFIG_V6.local_ip.get(),
                IpProto::Udp,
                Buf::new(&mut [], ..),
                0,
            );
        }

        /// Call `send_icmpv6_packet_too_big` with dummy values.
        fn send_icmpv6_packet_too_big_helper(ctx: &mut Dummyv6Context) {
            send_icmpv6_packet_too_big(
                ctx,
                DummyDeviceId,
                FrameDestination::Unicast,
                DUMMY_CONFIG_V6.remote_ip.get(),
                DUMMY_CONFIG_V6.local_ip.get(),
                IpProto::Udp,
                0,
                Buf::new(&mut [], ..),
                0,
            );
        }

        /// Call `send_icmpv6_parameter_problem` with dummy values.
        fn send_icmpv6_parameter_problem_helper(ctx: &mut Dummyv6Context) {
            send_icmpv6_parameter_problem(
                ctx,
                DummyDeviceId,
                FrameDestination::Unicast,
                DUMMY_CONFIG_V6.remote_ip.get(),
                DUMMY_CONFIG_V6.local_ip.get(),
                Icmpv6ParameterProblemCode::ErroneousHeaderField,
                Icmpv6ParameterProblem::new(0),
                Buf::new(&mut [], ..),
                false,
            );
        }

        /// Call `send_icmpv6_dest_unreachable` with dummy values.
        fn send_icmpv6_dest_unreachable_helper(ctx: &mut Dummyv6Context) {
            send_icmpv6_dest_unreachable(
                ctx,
                DummyDeviceId,
                FrameDestination::Unicast,
                DUMMY_CONFIG_V6.remote_ip.get(),
                DUMMY_CONFIG_V6.local_ip.get(),
                Icmpv6DestUnreachableCode::NoRoute,
                Buf::new(&mut [], ..),
            );
        }

        // Run tests for each function that sends error messages to make sure
        // they're all properly rate limited.

        fn run_test<C, W: Fn(u64) -> DummyContext<C>, S: Fn(&mut DummyContext<C>)>(
            with_errors_per_second: W,
            send: S,
        ) {
            // Note that we could theoretically have more precise tests here
            // (e.g., a test that we send at the correct rate over the long
            // term), but those would amount to testing the `TokenBucket`
            // implementation, which has its own exhaustive tests. Instead, we
            // just have a few sanity checks to make sure that we're actually
            // invoking it when we expect to (as opposed to bypassing it
            // entirely or something).

            // Test that, if no time has elapsed, we can successfully send up to
            // `ERRORS_PER_SECOND` error messages, but no more.

            // Don't use `DEFAULT_ERRORS_PER_SECOND` because it's 2^16 and it
            // makes this test take a long time.
            const ERRORS_PER_SECOND: u64 = 64;

            let mut ctx = with_errors_per_second(ERRORS_PER_SECOND);

            for i in 0..ERRORS_PER_SECOND {
                send(&mut ctx);
                assert_eq!(ctx.get_counter("IcmpContext::send_icmp_error_message"), i as usize + 1);
            }

            assert_eq!(
                ctx.get_counter("IcmpContext::send_icmp_error_message"),
                ERRORS_PER_SECOND as usize
            );
            send(&mut ctx);
            assert_eq!(
                ctx.get_counter("IcmpContext::send_icmp_error_message"),
                ERRORS_PER_SECOND as usize
            );

            // Test that, if we set a rate of 0, we are not able to send any
            // error messages regardless of how much time has elapsed.

            let mut ctx = with_errors_per_second(0);
            send(&mut ctx);
            assert_eq!(ctx.get_counter("IcmpContext::send_icmp_error_message"), 0);
            ctx.sleep_skip_timers(Duration::from_secs(1));
            send(&mut ctx);
            assert_eq!(ctx.get_counter("IcmpContext::send_icmp_error_message"), 0);
            ctx.sleep_skip_timers(Duration::from_secs(1));
            send(&mut ctx);
            assert_eq!(ctx.get_counter("IcmpContext::send_icmp_error_message"), 0);
        }

        fn with_errors_per_second_v4(errors_per_second: u64) -> Dummyv4Context {
            Dummyv4Context::with_state(DummyIcmpv4Context::with_errors_per_second(
                errors_per_second,
            ))
        }
        run_test(with_errors_per_second_v4, send_icmpv4_ttl_expired_helper);
        run_test(with_errors_per_second_v4, send_icmpv4_parameter_problem_helper);
        run_test(with_errors_per_second_v4, send_icmpv4_dest_unreachable_helper);

        fn with_errors_per_second_v6(errors_per_second: u64) -> Dummyv6Context {
            Dummyv6Context::with_state(DummyIcmpv6Context::with_errors_per_second(
                errors_per_second,
            ))
        }

        run_test(with_errors_per_second_v6, send_icmpv6_ttl_expired_helper);
        run_test(with_errors_per_second_v6, send_icmpv6_packet_too_big_helper);
        run_test(with_errors_per_second_v6, send_icmpv6_parameter_problem_helper);
        run_test(with_errors_per_second_v6, send_icmpv6_dest_unreachable_helper);
    }
}
