// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Protocol, versions 4 and 6.

#[macro_use]
pub(crate) mod path_mtu;

mod forwarding;
pub(crate) mod gmp;
pub mod icmp;
mod ipv6;
pub(crate) mod reassembly;
pub(crate) mod socket;
mod types;

// It's ok to `pub use` rather `pub(crate) use` here because the items in
// `types` which are themselves `pub(crate)` will still not be allowed to be
// re-exported from the root.
pub use self::types::*;

use alloc::vec::Vec;
use core::fmt::{Debug, Display};
use core::num::NonZeroU8;

use log::{debug, trace};
use net_types::ip::{AddrSubnet, Ip, IpAddress, IpVersion, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, Subnet};
use net_types::{MulticastAddr, SpecifiedAddr, Witness};
use packet::{Buf, BufferMut, Either, ParseMetadata, Serializer};
use packet_formats::error::IpParseError;
use packet_formats::icmp::{Icmpv4ParameterProblem, Icmpv6ParameterProblem};
use packet_formats::ip::{IpExt, IpPacket, IpPacketBuilder, IpProto};
use packet_formats::ipv4::Ipv4Packet;
use packet_formats::ipv6::Ipv6Packet;
use specialize_ip_macro::{specialize_ip, specialize_ip_address};

use crate::context::{CounterContext, FrameContext, StateContext, TimerContext, TimerHandler};
use crate::device::{DeviceId, FrameDestination};
use crate::error::{ExistsError, NotFoundError};
use crate::ip::forwarding::{Destination, ForwardingTable};
use crate::ip::gmp::igmp::IgmpPacketHandler;
use crate::ip::icmp::{
    send_icmpv4_parameter_problem, send_icmpv6_parameter_problem, BufferIcmpContext,
    BufferIcmpEventDispatcher, IcmpContext, IcmpEventDispatcher, IcmpIpExt, IcmpIpTransportContext,
    Icmpv4ErrorCode, Icmpv4State, Icmpv4StateBuilder, Icmpv6ErrorCode, Icmpv6State,
    Icmpv6StateBuilder,
};
use crate::ip::ipv6::Ipv6PacketAction;
use crate::ip::path_mtu::{IpLayerPathMtuCache, PmtuTimerId};
use crate::ip::reassembly::{
    process_fragment, reassemble_packet, FragmentCacheKey, FragmentProcessingState,
    IpLayerFragmentCache,
};
use crate::ip::socket::{IpSock, IpSockUpdate};
use crate::{BufferDispatcher, Context, EventDispatcher, StackState, TimerId, TimerIdInner};

/// Default IPv4 TTL.
// TODO(joshlf): Use `new` instead of `new_unchecked` once `new` is a const fn.
const DEFAULT_TTL: NonZeroU8 = unsafe { NonZeroU8::new_unchecked(64) };

/// The metadata for sending an IP packet from a particular source address.
///
/// `IpPacketFromArgs` is used as the metadata for the [`FrameContext`] bound
/// required by [`BufferTransportIpContext`]. It allows sending an IP packet
/// from a particular source address.
// TODO(rheacock): remove `allow(dead_code)` when this is used.
#[allow(dead_code)]
pub struct IpPacketFromArgs<A: IpAddress> {
    pub(crate) src_ip: SpecifiedAddr<A>,
    pub(crate) dst_ip: SpecifiedAddr<A>,
    pub(crate) proto: IpProto,
}

impl<A: IpAddress> IpPacketFromArgs<A> {
    /// Constructs a new `IpPacketFromArgs`.
    pub(crate) fn new(
        src_ip: SpecifiedAddr<A>,
        dst_ip: SpecifiedAddr<A>,
        proto: IpProto,
    ) -> IpPacketFromArgs<A> {
        IpPacketFromArgs { src_ip, dst_ip, proto }
    }
}

/// An error encountered when receiving a transport-layer packet.
#[derive(Debug)]
pub(crate) struct TransportReceiveError {
    inner: TransportReceiveErrorInner,
}

impl TransportReceiveError {
    // NOTE: We don't expose a constructor for the "protocol unsupported" case.
    // This ensures that the only way that we send a "protocol unsupported"
    // error is if the implementation of `IpTransportContext` provided for a
    // given protocol number is `()`. That's because `()` is the only type whose
    // `receive_ip_packet` function is implemented in this module, and thus it's
    // the only type that is able to construct a "protocol unsupported"
    // `TransportReceiveError`.

    /// Constructs a new `TransportReceiveError` to indicate an unreachable
    /// port.
    pub(crate) fn new_port_unreachable() -> TransportReceiveError {
        TransportReceiveError { inner: TransportReceiveErrorInner::PortUnreachable }
    }
}

#[derive(Debug)]
enum TransportReceiveErrorInner {
    ProtocolUnsupported,
    PortUnreachable,
}

/// The execution context provided by a transport layer protocol to the IP
/// layer.
///
/// An implementation for `()` is provided which indicates that a particular
/// transport layer protocol is unsupported.
pub(crate) trait IpTransportContext<I: IcmpIpExt, C: ?Sized> {
    /// Receive an ICMP error message.
    ///
    /// All arguments beginning with `original_` are fields from the IP packet
    /// that triggered the error. The `original_body` is provided here so that
    /// the error can be associated with a transport-layer socket.
    ///
    /// While ICMPv4 error messages are supposed to contain the first 8 bytes of
    /// the body of the offending packet, and ICMPv6 error messages are supposed
    /// to contain as much of the offending packet as possible without violating
    /// the IPv6 minimum MTU, the caller does NOT guarantee that either of these
    /// hold. It is `receive_icmp_error`'s responsibility to handle any length
    /// of `original_body`, and to perform any necessary validation.
    fn receive_icmp_error(
        ctx: &mut C,
        original_src_ip: Option<SpecifiedAddr<I::Addr>>,
        original_dst_ip: SpecifiedAddr<I::Addr>,
        original_body: &[u8],
        err: I::ErrorCode,
    );
}

/// The execution context provided by a transport layer protocol to the IP layer
/// when a buffer is required.
pub(crate) trait BufferIpTransportContext<I: IcmpIpExt, B: BufferMut, C: IpDeviceIdContext + ?Sized>:
    IpTransportContext<I, C>
{
    /// Receive a transport layer packet in an IP packet.
    ///
    /// In the event of an unreachable port, `receive_ip_packet` returns the
    /// buffer in its original state (with the transport packet un-parsed) in
    /// the `Err` variant.
    fn receive_ip_packet(
        ctx: &mut C,
        device: Option<C::DeviceId>,
        src_ip: I::Addr,
        dst_ip: SpecifiedAddr<I::Addr>,
        buffer: B,
    ) -> Result<(), (B, TransportReceiveError)>;
}

impl<I: IcmpIpExt, C: ?Sized> IpTransportContext<I, C> for () {
    fn receive_icmp_error(
        _ctx: &mut C,
        _original_src_ip: Option<SpecifiedAddr<I::Addr>>,
        _original_dst_ip: SpecifiedAddr<I::Addr>,
        _original_body: &[u8],
        err: I::ErrorCode,
    ) {
        trace!("IpTransportContext::receive_icmp_error: Received ICMP error message ({:?}) for unsupported IP protocol", err);
    }
}

impl<I: IcmpIpExt, B: BufferMut, C: IpDeviceIdContext + ?Sized> BufferIpTransportContext<I, B, C>
    for ()
{
    fn receive_ip_packet(
        _ctx: &mut C,
        _device: Option<C::DeviceId>,
        _src_ip: I::Addr,
        _dst_ip: SpecifiedAddr<I::Addr>,
        buffer: B,
    ) -> Result<(), (B, TransportReceiveError)> {
        Err((
            buffer,
            TransportReceiveError { inner: TransportReceiveErrorInner::ProtocolUnsupported },
        ))
    }
}

/// The execution context provided by the IP layer to transport layer protocols.
pub trait TransportIpContext<I: Ip>: IpDeviceIdContext {
    /// Is this one of our local addresses?
    ///
    /// `is_local_addr` returns whether `addr` is the address associated with
    /// one of our local interfaces.
    fn is_local_addr(&self, addr: I::Addr) -> bool;

    /// Get the local address of the interface that will be used to route to a
    /// remote address.
    ///
    /// `local_address_for_remote` looks up the route to `remote`. If one is
    /// found, it returns the IP address of the interface specified by the
    /// route, or `None` if the interface has no IP address.
    fn local_address_for_remote(
        &self,
        remote: SpecifiedAddr<I::Addr>,
    ) -> Option<SpecifiedAddr<I::Addr>>;
}

/// The execution context provided by the IP layer to transport layer protocols
/// when a buffer is provided.
///
/// `BufferTransportIpContext` is like [`TransportIpContext`], except that it
/// also requires that the context be capable of receiving frames in buffers of
/// type `B`. This is used when a buffer of type `B` is provided to IP (in
/// particular, in the [`FrameContext`] implementation), and allows any
/// generated link-layer frames to reuse that buffer rather than needing to
/// always allocate a new one.
pub trait BufferTransportIpContext<I: Ip, B: BufferMut>:
    TransportIpContext<I> + FrameContext<B, IpPacketFromArgs<I::Addr>>
{
}

impl<
        I: Ip,
        B: BufferMut,
        C: TransportIpContext<I> + FrameContext<B, IpPacketFromArgs<I::Addr>>,
    > BufferTransportIpContext<I, B> for C
{
}

// TODO(joshlf): With all 256 protocol numbers (minus reserved ones) given their
// own associated type in both traits, running `cargo check` on a 2018 MacBook
// Pro takes over a minute. Eventually - and before we formally publish this as
// a library - we should identify the bottleneck in the compiler and optimize
// it. For the time being, however, we only support protocol numbers that we
// actually use (TCP and UDP).

/// The execution context for IPv6's transport layer.
///
/// `Ipv6TransportLayerContext` defines the [`IpTransportContext`] for each IPv6
/// protocol number. The procol numbers 1 (ICMP) and 2 (IGMP) are used by the
/// stack itself, and cannot be overridden.
pub(crate) trait Ipv4TransportLayerContext {
    type Proto6: IpTransportContext<Ipv4, Self>;
    type Proto17: IpTransportContext<Ipv4, Self>;
}

/// The execution context for IPv6's transport layer.
///
/// `Ipv6TransportLayerContext` defines the [`IpTransportContext`] for
/// each IPv6 protocol number. The procol numbers 0 (Hop-by-Hop Options), 58
/// (ICMPv6), 59 (No Next Header), and 60 (Destination Options) are used by the
/// stack itself, and cannot be overridden.
pub(crate) trait Ipv6TransportLayerContext {
    type Proto6: IpTransportContext<Ipv6, Self>;
    type Proto17: IpTransportContext<Ipv6, Self>;
}

impl<D: EventDispatcher> Ipv4TransportLayerContext for Context<D> {
    type Proto6 = ();
    type Proto17 = crate::transport::udp::UdpIpTransportContext;
}

impl<D: EventDispatcher> Ipv6TransportLayerContext for Context<D> {
    type Proto6 = ();
    type Proto17 = crate::transport::udp::UdpIpTransportContext;
}

impl<A: IpAddress, B: BufferMut, D: BufferDispatcher<B>> FrameContext<B, IpPacketFromArgs<A>>
    for Context<D>
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        meta: IpPacketFromArgs<A>,
        body: S,
    ) -> Result<(), S> {
        // TODO(brunodalbo) this lookup is not considering the source ip in
        // `meta`, doesn't look totally correct.
        let route = if let Some(r) = lookup_route(self, meta.dst_ip) {
            r
        } else {
            return Err(body);
        };
        send_ip_packet_from_device(
            self,
            route.device,
            meta.src_ip.get(),
            meta.dst_ip.get(),
            route.next_hop,
            meta.proto,
            body,
            None,
        )
    }
}

impl<I: Ip, D: EventDispatcher> TransportIpContext<I> for Context<D> {
    fn is_local_addr(&self, addr: I::Addr) -> bool {
        SpecifiedAddr::new(addr)
            .map(|addr| crate::device::is_local_addr(self, &addr))
            .unwrap_or(false)
    }

    fn local_address_for_remote(
        &self,
        remote: SpecifiedAddr<I::Addr>,
    ) -> Option<SpecifiedAddr<I::Addr>> {
        local_address_for_remote(self, remote)
    }
}

/// An execution context which provides a `DeviceId` type for various IP
/// internals to share.
///
/// This trait provides the associated `DeviceId` type, and is used by
/// [`IgmpContext`], [`MldContext`], and [`IcmpContext`]. It allows them to use
/// the same `DeviceId` type rather than each providing their own, which would
/// require lots of verbose type bounds when they need to be interoperable (such
/// as when ICMP delivers an MLD packet to the `mld` module for processing).
pub trait IpDeviceIdContext {
    type DeviceId: Copy + Display + Debug + Send + Sync + 'static;
}

/// A dummy device ID for use in testing.
///
/// `DummyDeviceId` is provided for use in implementing
/// `IpDeviceIdContext::DeviceId` in tests. Unlike `()`, it implements the
/// `Display` trait, which is a requirement of `IpDeviceIdContext::DeviceId`.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[cfg(test)]
pub(crate) struct DummyDeviceId;

#[cfg(test)]
impl Display for DummyDeviceId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "DummyDeviceId")
    }
}

// Temporary blanket impl until we switch over entirely to the traits defined in
// the `context` module.
impl<D: EventDispatcher> IpDeviceIdContext for Context<D> {
    type DeviceId = DeviceId;
}

/// A builder for IPv4 state.
#[derive(Copy, Clone)]
pub struct Ipv4StateBuilder {
    forward: bool,
    icmp: Icmpv4StateBuilder,
}

impl Default for Ipv4StateBuilder {
    fn default() -> Ipv4StateBuilder {
        // NOTE: We implement `Default` manually even though this implementation
        // is equivalent to what `#[derive(Default)]` would produce in order to
        // make the default values explicit.
        Ipv4StateBuilder { forward: false, icmp: Icmpv4StateBuilder::default() }
    }
}

impl Ipv4StateBuilder {
    /// Enable or disable IP packet forwarding (default: disabled).
    ///
    /// If `forward` is true, then an incoming IP packet whose destination
    /// address identifies a remote host will be forwarded to that host.
    pub fn forward(&mut self, forward: bool) -> &mut Self {
        self.forward = forward;
        self
    }

    /// Get the builder for the ICMPv4 state.
    pub fn icmpv4_builder(&mut self) -> &mut Icmpv4StateBuilder {
        &mut self.icmp
    }

    pub(crate) fn build<Instant: crate::Instant, D>(self) -> Ipv4State<Instant, D> {
        Ipv4State {
            inner: IpStateInner {
                forward: self.forward,
                table: ForwardingTable::default(),
                fragment_cache: IpLayerFragmentCache::new(),
                path_mtu: IpLayerPathMtuCache::new(),
            },
            icmp: self.icmp.build(),
        }
    }
}

/// A builder for IPv6 state.
#[derive(Copy, Clone)]
pub struct Ipv6StateBuilder {
    forward: bool,
    icmp: Icmpv6StateBuilder,
}

impl Default for Ipv6StateBuilder {
    fn default() -> Ipv6StateBuilder {
        // NOTE: We implement `Default` manually even though this implementation
        // is equivalent to what `#[derive(Default)]` would produce in order to
        // make the default values explicit.
        Ipv6StateBuilder { forward: false, icmp: Icmpv6StateBuilder::default() }
    }
}

impl Ipv6StateBuilder {
    /// Enable or disable IPv6 packet forwarding (default: disabled).
    ///
    /// If `forward` is true, then an incoming IPv6 packet whose destination
    /// address identifies a remote host will be forwarded to that host.
    pub fn forward(&mut self, forward: bool) -> &mut Self {
        self.forward = forward;
        self
    }

    /// Get the builder for the ICMPv6 state.
    pub fn icmpv6_builder(&mut self) -> &mut Icmpv6StateBuilder {
        &mut self.icmp
    }

    pub(crate) fn build<Instant: crate::Instant, D>(self) -> Ipv6State<Instant, D> {
        Ipv6State {
            inner: IpStateInner {
                forward: self.forward,
                table: ForwardingTable::default(),
                fragment_cache: IpLayerFragmentCache::new(),
                path_mtu: IpLayerPathMtuCache::new(),
            },
            icmp: self.icmp.build(),
        }
    }
}

pub(crate) struct Ipv4State<Instant: crate::Instant, D> {
    inner: IpStateInner<Ipv4, Instant>,
    icmp: Icmpv4State<Instant, IpSock<Ipv4, D>>,
}

pub(crate) struct Ipv6State<Instant: crate::Instant, D> {
    inner: IpStateInner<Ipv6, Instant>,
    icmp: Icmpv6State<Instant, IpSock<Ipv6, D>>,
}

struct IpStateInner<I: Ip, Instant: crate::Instant> {
    forward: bool,
    table: ForwardingTable<I, DeviceId>,
    fragment_cache: IpLayerFragmentCache<I>,
    path_mtu: IpLayerPathMtuCache<I, Instant>,
}

#[specialize_ip]
fn get_state_inner<I: Ip, D: EventDispatcher>(
    state: &StackState<D>,
) -> &IpStateInner<I, D::Instant> {
    #[ipv4]
    return &state.ipv4.inner;
    #[ipv6]
    return &state.ipv6.inner;
}

#[specialize_ip]
fn get_state_inner_mut<I: Ip, D: EventDispatcher>(
    state: &mut StackState<D>,
) -> &mut IpStateInner<I, D::Instant> {
    #[ipv4]
    return &mut state.ipv4.inner;
    #[ipv6]
    return &mut state.ipv6.inner;
}

impl<I: Ip, D: EventDispatcher> StateContext<IpLayerFragmentCache<I>> for Context<D> {
    fn get_state_with(&self, _id: ()) -> &IpLayerFragmentCache<I> {
        &get_state_inner(self.state()).fragment_cache
    }

    fn get_state_mut_with(&mut self, _id: ()) -> &mut IpLayerFragmentCache<I> {
        &mut get_state_inner_mut(self.state_mut()).fragment_cache
    }
}

impl<I: Ip, D: EventDispatcher> StateContext<IpLayerPathMtuCache<I, D::Instant>> for Context<D> {
    fn get_state_with(&self, _id: ()) -> &IpLayerPathMtuCache<I, D::Instant> {
        &get_state_inner(self.state()).path_mtu
    }

    fn get_state_mut_with(&mut self, _id: ()) -> &mut IpLayerPathMtuCache<I, D::Instant> {
        &mut get_state_inner_mut(self.state_mut()).path_mtu
    }
}

/// An event dispatcher for the IP layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait IpLayerEventDispatcher<B: BufferMut>:
    BufferIcmpEventDispatcher<Ipv4, B>
    + BufferIcmpEventDispatcher<Ipv6, B>
    + IcmpEventDispatcher<Ipv4>
    + IcmpEventDispatcher<Ipv6>
{
}

/// The identifier for timer events in the IP layer.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub(crate) enum IpLayerTimerId {
    /// A timer event for IPv4 packet reassembly timeouts.
    ReassemblyTimeoutv4(FragmentCacheKey<Ipv4Addr>),
    /// A timer event for IPv6 packet reassembly timeouts.
    ReassemblyTimeoutv6(FragmentCacheKey<Ipv6Addr>),
    PmtuTimeout(IpVersion),
}

impl IpLayerTimerId {
    #[specialize_ip_address]
    fn new_reassembly_timeout_timer_id<A: IpAddress>(key: FragmentCacheKey<A>) -> TimerId {
        #[ipv4addr]
        let id = IpLayerTimerId::ReassemblyTimeoutv4(key);
        #[ipv6addr]
        let id = IpLayerTimerId::ReassemblyTimeoutv6(key);

        TimerId(TimerIdInner::IpLayer(id))
    }

    fn new_pmtu_timeout_timer_id<I: Ip>() -> TimerId {
        TimerId(TimerIdInner::IpLayer(IpLayerTimerId::PmtuTimeout(I::VERSION)))
    }
}

/// Handle a timer event firing in the IP layer.
pub(crate) fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: IpLayerTimerId) {
    match id {
        IpLayerTimerId::ReassemblyTimeoutv4(key) => ctx.handle_timer(key),
        IpLayerTimerId::ReassemblyTimeoutv6(key) => ctx.handle_timer(key),
        IpLayerTimerId::PmtuTimeout(IpVersion::V4) => {
            ctx.handle_timer(PmtuTimerId::<Ipv4>::default())
        }
        IpLayerTimerId::PmtuTimeout(IpVersion::V6) => {
            ctx.handle_timer(PmtuTimerId::<Ipv6>::default())
        }
    }
}

impl<A: IpAddress, D: EventDispatcher> TimerContext<FragmentCacheKey<A>> for Context<D> {
    fn schedule_timer_instant(
        &mut self,
        time: Self::Instant,
        key: FragmentCacheKey<A>,
    ) -> Option<Self::Instant> {
        self.dispatcher_mut()
            .schedule_timeout_instant(time, IpLayerTimerId::new_reassembly_timeout_timer_id(key))
    }

    fn cancel_timer(&mut self, key: FragmentCacheKey<A>) -> Option<Self::Instant> {
        self.dispatcher_mut().cancel_timeout(IpLayerTimerId::new_reassembly_timeout_timer_id(key))
    }

    // TODO(rheacock): the compiler thinks that `f` doesn't have to be mutable, but it does. Thus
    // we `allow(unused)` here.
    #[allow(unused)]
    fn cancel_timers_with<F: FnMut(&FragmentCacheKey<A>) -> bool>(&mut self, f: F) {
        #[specialize_ip_address]
        fn cancel_timers_with_inner<
            A: IpAddress,
            D: EventDispatcher,
            F: FnMut(&FragmentCacheKey<A>) -> bool,
        >(
            ctx: &mut Context<D>,
            mut f: F,
        ) {
            ctx.dispatcher_mut().cancel_timeouts_with(|id| match id {
                #[ipv4addr]
                TimerId(TimerIdInner::IpLayer(IpLayerTimerId::ReassemblyTimeoutv4(key))) => f(key),
                #[ipv6addr]
                TimerId(TimerIdInner::IpLayer(IpLayerTimerId::ReassemblyTimeoutv6(key))) => f(key),
                _ => false,
            });
        }

        cancel_timers_with_inner(self, f);
    }

    fn scheduled_instant(&self, key: FragmentCacheKey<A>) -> Option<Self::Instant> {
        self.dispatcher().scheduled_instant(IpLayerTimerId::new_reassembly_timeout_timer_id(key))
    }
}

impl<I: Ip, D: EventDispatcher> TimerContext<PmtuTimerId<I>> for Context<D> {
    fn schedule_timer_instant(
        &mut self,
        time: Self::Instant,
        _id: PmtuTimerId<I>,
    ) -> Option<Self::Instant> {
        self.dispatcher_mut()
            .schedule_timeout_instant(time, IpLayerTimerId::new_pmtu_timeout_timer_id::<I>())
    }

    fn cancel_timer(&mut self, _id: PmtuTimerId<I>) -> Option<Self::Instant> {
        self.dispatcher_mut().cancel_timeout(IpLayerTimerId::new_pmtu_timeout_timer_id::<I>())
    }

    fn cancel_timers_with<F: FnMut(&PmtuTimerId<I>) -> bool>(&mut self, _f: F) {
        self.dispatcher_mut()
            .cancel_timeouts_with(|id| id == &IpLayerTimerId::new_pmtu_timeout_timer_id::<I>());
    }

    fn scheduled_instant(&self, _id: PmtuTimerId<I>) -> Option<Self::Instant> {
        self.dispatcher().scheduled_instant(IpLayerTimerId::new_pmtu_timeout_timer_id::<I>())
    }
}

// TODO(joshlf): Once we support multiple extension headers in IPv6, we will
// need to verify that the callers of this function are still sound. In
// particular, they may accidentally pass a parse_metadata argument which
// corresponds to a single extension header rather than all of the IPv6 headers.

/// Dispatch a received IPv4 packet to the appropriate protocol.
///
/// `device` is the device the packet was received on. `parse_metadata` is the
/// parse metadata associated with parsing the IP headers. It is used to undo
/// that parsing. Both `device` and `parse_metadata` are required in order to
/// send ICMP messages in response to unrecognized protocols or ports. If either
/// of `device` or `parse_metadata` is `None`, the caller promises that the
/// protocol and port are recognized.
///
/// # Panics
///
/// `dispatch_receive_ipv4_packet` panics if the protocol is unrecognized and
/// `parse_metadata` is `None`. If an IGMP message is received but it is not
/// coming from a device, i.e., `device` given is `None`,
/// `dispatch_receive_ip_packet` will also panic.
fn dispatch_receive_ipv4_packet<B: BufferMut, D: BufferDispatcher<B>>(
    ctx: &mut Context<D>,
    device: Option<DeviceId>,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
    proto: IpProto,
    buffer: B,
    parse_metadata: Option<ParseMetadata>,
) {
    increment_counter!(ctx, "dispatch_receive_ipv4_packet");

    macro_rules! mtch {
        ($($cond:pat => $ty:ident),*) => {
            match proto {
                IpProto::Icmp => <IcmpIpTransportContext as BufferIpTransportContext<Ipv4, _, _>>
                            ::receive_ip_packet(ctx, device, src_ip, dst_ip, buffer),
                IpProto::Igmp => {
                    IgmpPacketHandler::<(), _, _>::receive_igmp_packet(
                        ctx,
                        device.expect("IGMP messages should come from a device"),
                        src_ip,
                        dst_ip,
                        buffer,
                    );
                    Ok(())
                }
                $($cond => <<Context<D> as Ipv4TransportLayerContext>::$ty as BufferIpTransportContext<Ipv4, _, _>>
                            ::receive_ip_packet(ctx, device, src_ip, dst_ip, buffer),)*
                // TODO(joshlf): Once all IP protocol numbers are covered,
                // remove this default case.
                _ => Err((
                    buffer,
                    TransportReceiveError { inner: TransportReceiveErrorInner::ProtocolUnsupported },
                )),
            }
        };
    }

    #[rustfmt::skip]
    let res = mtch!(IpProto::Tcp => Proto6, IpProto::Udp => Proto17);

    if let Err((mut buffer, err)) = res {
        // All branches promise to return the buffer in the same state it was in
        // when they were executed. Thus, all we have to do is undo the parsing
        // of the IP packet header, and the buffer will be back to containing
        // the entire original IP packet.
        let meta = parse_metadata.unwrap();
        buffer.undo_parse(meta);

        match err.inner {
            TransportReceiveErrorInner::ProtocolUnsupported => {
                icmp::send_icmpv4_protocol_unreachable(
                    ctx,
                    device.unwrap(),
                    frame_dst,
                    src_ip,
                    dst_ip,
                    buffer,
                    meta.header_len(),
                );
            }
            TransportReceiveErrorInner::PortUnreachable => {
                // TODO(joshlf): What if we're called from a loopback handler,
                // and device and parse_metadata are None? In other words, what
                // happens if we attempt to send to a loopback port which is
                // unreachable? We will eventually need to restructure the
                // control flow here to handle that case.
                icmp::send_icmpv4_port_unreachable(
                    ctx,
                    device.unwrap(),
                    frame_dst,
                    src_ip,
                    dst_ip,
                    buffer,
                    meta.header_len(),
                );
            }
        }
    }
}

/// Dispatch a received IPv6 packet to the appropriate protocol.
///
/// `dispatch_receive_ipv6_packet` has the same semantics as
/// `dispatch_receive_ipv4_packet`, but for IPv6.
fn dispatch_receive_ipv6_packet<B: BufferMut, D: BufferDispatcher<B>>(
    ctx: &mut Context<D>,
    device: Option<DeviceId>,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
    proto: IpProto,
    buffer: B,
    parse_metadata: Option<ParseMetadata>,
) {
    increment_counter!(ctx, "dispatch_receive_ipv6_packet");

    macro_rules! mtch {
        ($($cond:pat => $ty:ident),*) => {
            match proto {
                IpProto::Icmpv6 => <IcmpIpTransportContext as BufferIpTransportContext<Ipv6, _, _>>
                            ::receive_ip_packet(ctx, device, src_ip, dst_ip, buffer),
                // A value of `IpProto::NoNextHeader` tells us that there is no
                // header whatsoever following the last lower-level header so we
                // stop processing here.
                IpProto::NoNextHeader => Ok(()),
                $($cond => <<Context<D> as Ipv4TransportLayerContext>::$ty as BufferIpTransportContext<Ipv6, _, _>>
                            ::receive_ip_packet(ctx, device, src_ip, dst_ip, buffer),)*
                // TODO(joshlf): Once all IP Next Header numbers are covered,
                // remove this default case.
                _ => Err((
                    buffer,
                    TransportReceiveError { inner: TransportReceiveErrorInner::ProtocolUnsupported },
                )),
            }
        };
    }

    #[rustfmt::skip]
    let res = mtch!(IpProto::Tcp => Proto6, IpProto::Udp => Proto17);

    if let Err((mut buffer, err)) = res {
        // All branches promise to return the buffer in the same state it was in
        // when they were executed. Thus, all we have to do is undo the parsing
        // of the IP packet header, and the buffer will be back to containing
        // the entire original IP packet.
        let meta = parse_metadata.unwrap();
        buffer.undo_parse(meta);

        match err.inner {
            TransportReceiveErrorInner::ProtocolUnsupported => {
                icmp::send_icmpv6_protocol_unreachable(
                    ctx,
                    device.unwrap(),
                    frame_dst,
                    src_ip,
                    dst_ip,
                    buffer,
                    meta.header_len(),
                );
            }
            TransportReceiveErrorInner::PortUnreachable => {
                // TODO(joshlf): What if we're called from a loopback handler,
                // and device and parse_metadata are None? In other words, what
                // happens if we attempt to send to a loopback port which is
                // unreachable? We will eventually need to restructure the
                // control flow here to handle that case.
                icmp::send_icmpv6_port_unreachable(
                    ctx,
                    device.unwrap(),
                    frame_dst,
                    src_ip,
                    dst_ip,
                    buffer,
                );
            }
        }
    }
}

/// Drop a packet and undo the effects of parsing it.
///
/// `drop_packet_and_undo_parse!` takes a `$packet` and a `$buffer` which the
/// packet was parsed from. It saves the results of the `src_ip()`, `dst_ip()`,
/// `proto()`, and `parse_metadata()` methods. It drops `$packet` and uses the
/// result of `parse_metadata()` to undo the effects of parsing the packet.
/// Finally, it returns the source IP, destination IP, protocol, and parse
/// metadata.
macro_rules! drop_packet_and_undo_parse {
    ($packet:expr, $buffer:expr) => {{
        let (src_ip, dst_ip, proto, meta) = $packet.into_metadata();
        $buffer.undo_parse(meta);
        (src_ip, dst_ip, proto, meta)
    }};
}

/// Process a fragment and reassemble if required.
///
/// Attempts to process a potential fragment packet and reassemble if we
/// are ready to do so. If the packet isn't fragmented, or a packet was
/// reassembled, attempt to dispatch the packet.
macro_rules! process_fragment {
    ($ctx:expr, $dispatch:ident, $device:expr, $frame_dst:expr, $buffer:expr, $packet:expr, $dst_ip:expr, $ip:ident) => {{
        match process_fragment::<$ip, _, &mut [u8]>($ctx, $packet) {
            // Handle the packet right away since reassembly is not needed.
            FragmentProcessingState::NotNeeded(packet) => {
                trace!("receive_ip_packet: not fragmented");
                // TODO(joshlf):
                // - Check for already-expired TTL?
                let (src_ip, _, proto, meta) = packet.into_metadata();
                $dispatch(
                    $ctx,
                    Some($device),
                    $frame_dst,
                    src_ip,
                    $dst_ip,
                    proto,
                    $buffer,
                    Some(meta),
                );
            }
            // Ready to reassemble a packet.
            FragmentProcessingState::Ready { key, packet_len } => {
                trace!("receive_ip_packet: fragmented, ready for reassembly");
                // Allocate a buffer of `packet_len` bytes.
                let mut buffer = Buf::new(alloc::vec![0; packet_len], ..);

                // Attempt to reassemble the packet.
                match reassemble_packet::<$ip, _, _, _>($ctx, &key, buffer.buffer_view_mut()) {
                    // Successfully reassembled the packet, handle it.
                    Ok(packet) => {
                        trace!("receive_ip_packet: fragmented, reassembled packet: {:?}", packet);
                        // TODO(joshlf):
                        // - Check for already-expired TTL?
                        let (src_ip, _, proto, meta) = packet.into_metadata();
                        $dispatch::<Buf<Vec<u8>>, _>(
                            $ctx,
                            Some($device),
                            $frame_dst,
                            src_ip,
                            $dst_ip,
                            proto,
                            buffer,
                            Some(meta),
                        );
                    }
                    // TODO(ghanan): Handle reassembly errors, remove
                    // `allow(unreachable_patterns)` when complete.
                    _ => return,
                    #[allow(unreachable_patterns)]
                    Err(e) => {
                        trace!("receive_ip_packet: fragmented, failed to reassemble: {:?}", e);
                    }
                }
            }
            // Cannot proceed since we need more fragments before we
            // can reassemble a packet.
            FragmentProcessingState::NeedMoreFragments => {
                trace!("receive_ip_packet: fragmented, need more before reassembly")
            }
            // TODO(ghanan): Handle invalid fragments.
            FragmentProcessingState::InvalidFragment => {
                trace!("receive_ip_packet: fragmented, invalid")
            }
            FragmentProcessingState::OutOfMemory => {
                trace!("receive_ip_packet: fragmented, dropped because OOM")
            }
        };
    }};
}

// TODO(joshlf): Can we turn `try_parse_ip_packet` into a function? So far, I've
// been unable to get the borrow checker to accept it.

/// Try to parse an IP packet from a buffer.
///
/// If parsing fails, return the buffer to its original state so that its
/// contents can be used to send an ICMP error message. When invoked, the macro
/// expands to an expression whose type is `Result<P, P::Error>`, where `P` is
/// the parsed packet type.
macro_rules! try_parse_ip_packet {
    ($buffer:expr) => {{
        let p_len = $buffer.prefix_len();
        let s_len = $buffer.suffix_len();

        let result = $buffer.parse_mut();

        if let Err(err) = result {
            // Revert `buffer` to it's original state.
            let n_p_len = $buffer.prefix_len();
            let n_s_len = $buffer.suffix_len();

            if p_len > n_p_len {
                $buffer.grow_front(p_len - n_p_len);
            }

            if s_len > n_s_len {
                $buffer.grow_back(s_len - n_s_len);
            }

            Err(err)
        } else {
            result
        }
    }};
}

/// Receive an IP packet from a device.
///
/// `receive_ip_packet` calls [`receive_ipv4_packet`] or [`receive_ipv6_packet`]
/// depending on the type parameter, `I`. It is used only in testing.
#[cfg(test)]
pub(crate) fn receive_ip_packet<B: BufferMut, D: BufferDispatcher<B>, I: Ip>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    buffer: B,
) {
    match I::VERSION {
        IpVersion::V4 => receive_ipv4_packet(ctx, device, frame_dst, buffer),
        IpVersion::V6 => receive_ipv6_packet(ctx, device, frame_dst, buffer),
    }
}

/// Receive an IPv4 packet from a device.
///
/// `frame_dst` specifies whether this packet was received in a broadcast or
/// unicast link-layer frame.
pub(crate) fn receive_ipv4_packet<B: BufferMut, D: BufferDispatcher<B>>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    mut buffer: B,
) {
    increment_counter!(ctx, "receive_ipv4_packet");
    trace!("receive_ip_packet({})", device);

    let mut packet: Ipv4Packet<_> = match try_parse_ip_packet!(buffer) {
        Ok(packet) => packet,
        // Conditionally send an ICMP response if we encountered a parameter
        // problem error when parsing an IPv4 packet. Note, we do not always
        // send back an ICMP response as it can be used as an attack vector for
        // DDoS attacks. We only send back an ICMP response if the RFC requires
        // that we MUST send one, as noted by `must_send_icmp` and `action`.
        Err(IpParseError::ParameterProblem {
            src_ip,
            dst_ip,
            code,
            pointer,
            must_send_icmp,
            header_len,
            action,
        }) if action.should_send_icmp(&dst_ip) && must_send_icmp => {
            // This should never return `true` for IPv4.
            assert!(!action.should_send_icmp_to_multicast());
            send_icmpv4_parameter_problem(
                ctx,
                device,
                frame_dst,
                src_ip,
                dst_ip,
                code,
                Icmpv4ParameterProblem::new(pointer),
                buffer,
                header_len,
            );
            return;
        }
        _ => return, // TODO(joshlf): Do something with ICMP here?
    };

    // TODO(ghanan): Act upon options.

    if Ipv4::LOOPBACK_SUBNET.contains(&packet.dst_ip()) {
        // A packet from outside this host was sent with the destination IP of
        // the loopback address, which is illegal. Loopback traffic is handled
        // explicitly in send_ip_packet.
        //
        // TODO(joshlf): Do something with ICMP here?
        debug!("got packet from remote host for loopback address {}", packet.dst_ip());
    } else if let Some((dst_ip, true)) = SpecifiedAddr::new(packet.dst_ip())
        .map(|dst_ip| (dst_ip, deliver_ipv4(ctx, device, dst_ip)))
    {
        trace!("receive_ipv4_packet: delivering locally");

        // Process a potential IPv4 fragment if the destination is this host.
        //
        // We process IPv4 packet reassembly here because, for IPv4, the
        // fragment data is in the header itself so we can handle it right away.
        //
        // Note, the `process_fragment` function (which is called by the
        // `process_fragment!` macro) could panic if the packet does not have
        // fragment data. However, we are guaranteed that it will not panic
        // because the fragment data is in the fixed header so it is always
        // present (even if the fragment data has values that implies that the
        // packet is not fragmented).
        process_fragment!(
            ctx,
            dispatch_receive_ipv4_packet,
            device,
            frame_dst,
            buffer,
            packet,
            dst_ip,
            Ipv4
        );
    } else {
        match forward(ctx, device, packet.dst_ip()) {
            ForwardDestination::Destination(dest) => {
                let ttl = packet.ttl();
                if ttl > 1 {
                    trace!("receive_ipv4_packet: forwarding");

                    packet.set_ttl(ttl - 1);
                    drop_packet_and_undo_parse!(packet, buffer);
                    if crate::device::send_ip_frame(ctx, dest.device, dest.next_hop, buffer)
                        .is_err()
                    {
                        debug!("failed to forward IPv4 packet: MTU exceeded");
                    }
                } else {
                    debug!("received IPv4 packet dropped due to expired TTL");

                    // TTL is 0 or would become 0 after decrement; see "TTL" section,
                    // https://tools.ietf.org/html/rfc791#page-14
                    let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
                    icmp::send_icmpv4_ttl_expired(
                        ctx,
                        device,
                        frame_dst,
                        src_ip,
                        dst_ip,
                        proto,
                        buffer,
                        meta.header_len(),
                    );
                }
            }
            ForwardDestination::NoRouteToHost => {
                let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
                debug!("received IPv4 packet with no known route to destination {}", dst_ip);

                icmp::send_icmpv4_net_unreachable(
                    ctx,
                    device,
                    frame_dst,
                    src_ip,
                    dst_ip,
                    proto,
                    buffer,
                    meta.header_len(),
                );
            }
            ForwardDestination::ForwardingDisabled => {
                // RFC 1122 § 3.2.1.3 "A host MUST silently discard an incoming
                // datagram that is not destined for the host." If forwarding is
                // disabled (we are not acting as a router) we follow this host
                // rule.
                debug!(
                    "received IPv4 packet to non-local destination {} with forwarding disabled",
                    packet.dst_ip()
                );
            }
        }
    }
}

/// Receive an IPv6 packet from a device.
///
/// `frame_dst` specifies whether this packet was received in a broadcast or
/// unicast link-layer frame.
pub(crate) fn receive_ipv6_packet<B: BufferMut, D: BufferDispatcher<B>>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    mut buffer: B,
) {
    increment_counter!(ctx, "receive_ipv6_packet");
    trace!("receive_ipv6_packet({})", device);

    let mut packet: Ipv6Packet<_> = match try_parse_ip_packet!(buffer) {
        Ok(packet) => packet,
        // Conditionally send an ICMP response if we encountered a parameter
        // problem error when parsing an IPv4 packet. Note, we do not always
        // send back an ICMP response as it can be used as an attack vector for
        // DDoS attacks. We only send back an ICMP response if the RFC requires
        // that we MUST send one, as noted by `must_send_icmp` and `action`.
        Err(IpParseError::ParameterProblem {
            src_ip,
            dst_ip,
            code,
            pointer,
            must_send_icmp,
            header_len: _,
            action,
        }) if action.should_send_icmp(&dst_ip) && must_send_icmp => {
            send_icmpv6_parameter_problem(
                ctx,
                device,
                frame_dst,
                src_ip,
                dst_ip,
                code,
                Icmpv6ParameterProblem::new(pointer),
                buffer,
                action.should_send_icmp_to_multicast(),
            );
            return;
        }
        _ => return, // TODO(joshlf): Do something with ICMP here?
    };

    trace!("receive_ipv6_packet: parsed packet: {:?}", packet);

    // TODO(ghanan): Act upon extension headers.

    if Ipv6::LOOPBACK_SUBNET.contains(&packet.dst_ip()) {
        // A packet from outside this host was sent with the destination IP of
        // the loopback address, which is illegal. Loopback traffic is handled
        // explicitly in send_ip_packet.
        //
        // TODO(joshlf): Do something with ICMP here?
        debug!("got packet from remote host for loopback address {}", packet.dst_ip());
    } else if let Some((dst_ip, true)) = SpecifiedAddr::new(packet.dst_ip())
        .map(|dst_ip| (dst_ip, deliver_ipv6(ctx, device, dst_ip)))
    {
        trace!("receive_ipv6_packet: delivering locally");

        // Process a potential IPv6 fragment if the destination is this host.
        //
        // We need to process extension headers in the order they appear in the
        // header. With some extension headers, we do not proceed to the next
        // header, and do some action immediately. For example, say we have an
        // IPv6 packet with two extension headers (routing extension header
        // before a fragment extension header). Until we get to the final
        // destination node in the routing header, we would need to reroute the
        // packet to the next destination without reassembling. Once the packet
        // gets to the last destination in the routing header, that node will
        // process the fragment extension header and handle reassembly.
        //
        // We also need to make sure the destination address is not actually a
        // tentative address we are performing NDP's Duplicate Address Detection
        // on.
        //
        // As per RFC 4862 section 5.4:
        //
        //   An address on which the Duplicate Address Detection procedure is
        //   applied is said to be tentative until the procedure has completed
        //   successfully. A tentative address is not considered "assigned to an
        //   interface" in the traditional sense.  That is, the interface must
        //   accept Neighbor Solicitation and Advertisement messages containing
        //   the tentative address in the Target Address field, but processes
        //   such packets differently from those whose Target Address matches an
        //   address assigned to the interface. Other packets addressed to the
        //   tentative address should be silently discarded. Note that the
        //   "other packets" include Neighbor Solicitation and Advertisement
        //   messages that have the tentative (i.e., unicast) address as the IP
        //   destination address and contain the tentative address in the Target
        //   Address field.  Such a case should not happen in normal operation,
        //   though, since these messages are multicasted in the Duplicate
        //   Address Detection procedure.
        //
        // That is, we accept no packets destined to a tentative address. NS and
        // NA packets should be addressed to a multicast address that we would
        // have joined during DAD so that we can receive those packets.
        //
        // Here we check to see if the packet's destination address is the IPv6
        // address assigned to the device. If it is, we check to see if it is
        // tentative. If the destination address is not the address assigned to
        // the device, or it is not tentative, we are okay to proceed; else, we
        // drop the packet.
        if crate::device::is_addr_tentative_on_device(ctx, &dst_ip, device) {
            // Silently drop as per RFC 4862 section 5.4
            trace!(
                "receive_ipv6_packet: Dropping packet as it is destined for a tentative address"
            );
            return;
        }

        // Handle extension headers first.
        match ipv6::handle_extension_headers(ctx, device, frame_dst, &packet, true) {
            Ipv6PacketAction::_Discard => {
                trace!("receive_ipv6_packet: handled IPv6 extension headers: discarding packet");
            }
            Ipv6PacketAction::Continue => {
                trace!("receive_ipv6_packet: handled IPv6 extension headers: dispatching packet");

                // TODO(joshlf):
                // - Do something with ICMP if we don't have a handler for that
                //   protocol?
                // - Check for already-expired TTL?
                let (src_ip, _, proto, meta) = packet.into_metadata();
                dispatch_receive_ipv6_packet(
                    ctx,
                    Some(device),
                    frame_dst,
                    src_ip,
                    dst_ip,
                    proto,
                    buffer,
                    Some(meta),
                );
            }
            Ipv6PacketAction::ProcessFragment => {
                trace!(
                    "receive_ipv6_packet: handled IPv6 extension headers: handling fragmented packet"
                );

                // Note, the `process_fragment` function (which is called by the
                // `process_fragment!` macro) could panic if the packet does not
                // have fragment data. However, we are guaranteed that it will
                // not panic for an IPv6 packet because the fragment data is in
                // an (optional) fragment extension header which we attempt to
                // handle by calling `ipv6::handle_extension_headers`. We will
                // only end up here if its return value is
                // `Ipv6PacketAction::ProcessFragment` which is only posisble
                // when the packet has the fragment extension header (even if
                // the fragment data has values that implies that the packet is
                // not fragmented).
                //
                // TODO(ghanan): Handle extension headers again since there
                //               could be some more in a reassembled packet
                //               (after the fragment header).
                process_fragment!(
                    ctx,
                    dispatch_receive_ipv6_packet,
                    device,
                    frame_dst,
                    buffer,
                    packet,
                    dst_ip,
                    Ipv6
                );
            }
        }
    } else {
        match forward(ctx, device, packet.dst_ip()) {
            ForwardDestination::Destination(dest) => {
                let ttl = packet.ttl();
                if ttl > 1 {
                    trace!("receive_ipv6_packet: forwarding");

                    // Handle extension headers first.
                    match ipv6::handle_extension_headers(ctx, device, frame_dst, &packet, false) {
                        Ipv6PacketAction::_Discard => {
                            trace!("receive_ipv6_packet: handled IPv6 extension headers: discarding packet");
                            return;
                        }
                        Ipv6PacketAction::Continue => {
                            trace!("receive_ipv6_packet: handled IPv6 extension headers: forwarding packet");
                        }
                        Ipv6PacketAction::ProcessFragment => unreachable!("When forwarding packets, we should only ever look at the hop by hop options extension header (if present)"),
                    }

                    packet.set_ttl(ttl - 1);
                    let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
                    if let Err(buffer) =
                        crate::device::send_ip_frame(ctx, dest.device, dest.next_hop, buffer)
                    {
                        debug!("failed to forward IPv6 packet: MTU exceeded");
                        trace!("receive_ipv6_packet: Sending ICMPv6 Packet Too Big");
                        // TODO(joshlf): Increment the TTL since we just decremented it.
                        // The fact that we don't do this is technically a violation of
                        // the ICMP spec (we're not encapsulating the original packet
                        // that caused the issue, but a slightly modified version of
                        // it), but it's not that big of a deal because it won't affect
                        // the sender's ability to figure out the minimum path MTU. This
                        // may break other logic, though, so we should still fix it
                        // eventually.
                        let mtu = crate::device::get_mtu(ctx, device);
                        crate::ip::icmp::send_icmpv6_packet_too_big(
                            ctx,
                            device,
                            frame_dst,
                            src_ip,
                            dst_ip,
                            proto,
                            mtu,
                            buffer,
                            meta.header_len(),
                        );
                    }
                } else {
                    debug!("received IPv6 packet dropped due to expired Hop Limit");

                    // Hop Limit is 0 or would become 0 after decrement; see RFC 2460
                    // Section 3.
                    let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
                    icmp::send_icmpv6_ttl_expired(
                        ctx,
                        device,
                        frame_dst,
                        src_ip,
                        dst_ip,
                        proto,
                        buffer,
                        meta.header_len(),
                    );
                }
            }
            ForwardDestination::NoRouteToHost => {
                let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
                debug!("received IPv6 packet with no known route to destination {}", dst_ip);

                icmp::send_icmpv6_net_unreachable(
                    ctx,
                    device,
                    frame_dst,
                    src_ip,
                    dst_ip,
                    proto,
                    buffer,
                    meta.header_len(),
                );
            }
            ForwardDestination::ForwardingDisabled => {
                // TODO(fxbug.dev/21182): Check to make sure the behavior here is the
                // same as for IPv4.
                //
                // When forwarding is disabled, we silently drop packets not
                // destined for this host, based on RFC 1122 § 3.2.1.3, however
                // this RFC predates IPv6, so the v6 behavior could be
                // different.
                debug!(
                    "received IPv6 packet to non-local destination {} with forwarding disabled",
                    packet.dst_ip()
                );
            }
        }
    }
}

/// Get the local address of the interface that will be used to route to a
/// remote address.
///
/// `local_address_for_remote` looks up the route to `remote`. If one is found,
/// it returns the IP address of the interface specified by the route, or `None`
/// if the interface has no IP address.
pub(crate) fn local_address_for_remote<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
    remote: SpecifiedAddr<A>,
) -> Option<SpecifiedAddr<A>> {
    let route = lookup_route(ctx, remote)?;
    crate::device::get_ip_addr_subnet(ctx, route.device).map(|a| a.addr())
}

// Should we deliver this IPv4 packet locally?
// deliver returns true if:
// - dst_ip is equal to the address set on the device
// - dst_ip is equal to the broadcast address of the subnet set on the device
// - dst_ip is equal to the global broadcast address
// - dst_ip is equal to a mutlicast group that `device` joined
fn deliver_ipv4<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
) -> bool {
    // TODO(joshlf):
    // - This implements a strict host model (in which we only accept packets
    //   which are addressed to the device over which they were received). This
    //   is the easiest to implement for the time being, but we should actually
    //   put real thought into what our host model should be (fxbug.dev/20852).
    let dst_ip = dst_ip.get();

    crate::device::get_assigned_ip_addr_subnets::<_, Ipv4Addr>(ctx, device)
        .map(AddrSubnet::into_addr_subnet)
        .any(|(addr, subnet)| dst_ip == addr.get() || dst_ip == subnet.broadcast())
        || dst_ip.is_global_broadcast()
        || MulticastAddr::new(dst_ip)
            .map_or(false, |a| crate::device::is_in_ip_multicast(ctx, device, a))
}

// Should we deliver this IPv6 packet locally?
// deliver returns true if:
// - dst_ip is equal to the address set on the device
// - dst_ip is equal to the broadcast address of the subnet set on the device
// - dst_ip is equal to a mutlicast group that `device` joined
fn deliver_ipv6<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
) -> bool {
    // TODO(brunodalbo):
    // Along with the host model described above, we need to be able to have
    // multiple IPs per interface, it becomes imperative for IPv6.
    crate::device::get_ip_addr_state(ctx, device, &dst_ip).is_some()
        || dst_ip == Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.into_specified()
        || MulticastAddr::new(dst_ip.get())
            .map_or(false, |a| crate::device::is_in_ip_multicast(ctx, device, a))
}

/// Either a reason a packet should not be forwarded, or the destination it
/// should be forwarded to.
enum ForwardDestination<A: IpAddress> {
    /// The packet should not be forwarded because the netstack is not configured
    /// for forwarding for the requested IP version, either globally or for the
    /// inbound device.
    ForwardingDisabled,
    /// The packet should not be forwarded because no route to the specified host
    /// could be found.
    NoRouteToHost,
    /// The packet should be forwarded to the given destination.
    Destination(Destination<A, DeviceId>),
}

// Should we forward this packet, and if so, to whom?
fn forward<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: DeviceId,
    dst_ip: A,
) -> ForwardDestination<A> {
    trace!("ip::forward: destination ip = {:?}", dst_ip);

    // Is this netstack configured to foward packets not destined for it?
    if !crate::ip::is_routing_enabled::<_, A::Version>(ctx) {
        trace!("ip::forward: can't forward because netstack not configured to do so");
        return ForwardDestination::ForwardingDisabled;
    }

    // Does the interface the packet arrived on have routing enabled?
    if !crate::device::is_routing_enabled::<_, A::Version>(ctx, device_id) {
        trace!("ip::forward: can't forward because packet arrived on an interface without routing enabled; device = {:?}", device_id);
        return ForwardDestination::ForwardingDisabled;
    }

    match SpecifiedAddr::new(dst_ip).map_or(None, |dst_ip| lookup_route(ctx, dst_ip)) {
        Some(dest) => {
            trace!("ip::forward: found a valid route to {:?} -> {:?}", dst_ip, dest);
            ForwardDestination::Destination(dest)
        }
        None => {
            trace!("ip::forward: can't forward because no valid route exists to {:?}", dst_ip);
            ForwardDestination::NoRouteToHost
        }
    }
}

// Look up the route to a host.
pub(crate) fn lookup_route<A: IpAddress, D: EventDispatcher>(
    ctx: &Context<D>,
    dst_ip: SpecifiedAddr<A>,
) -> Option<Destination<A, DeviceId>> {
    get_state_inner::<A::Version, _>(ctx.state()).table.lookup(dst_ip)
}

/// Add a route to the forwarding table, returning `Err` if the subnet
/// is already in the table.
#[specialize_ip_address]
pub(crate) fn add_route<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
    next_hop: SpecifiedAddr<A>,
) -> Result<(), ExistsError> {
    let res =
        get_state_inner_mut::<A::Version, _>(ctx.state_mut()).table.add_route(subnet, next_hop);

    if res.is_ok() {
        #[ipv4addr]
        crate::ip::socket::apply_ipv4_socket_update(ctx, IpSockUpdate::new());
        #[ipv6addr]
        crate::ip::socket::apply_ipv6_socket_update(ctx, IpSockUpdate::new());
    }

    res
}

/// Add a device route to the forwarding table, returning `Err` if the
/// subnet is already in the table.
#[specialize_ip_address]
pub(crate) fn add_device_route<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
    device: DeviceId,
) -> Result<(), ExistsError> {
    let res = get_state_inner_mut::<A::Version, _>(ctx.state_mut())
        .table
        .add_device_route(subnet, device);

    if res.is_ok() {
        #[ipv4addr]
        crate::ip::socket::apply_ipv4_socket_update(ctx, IpSockUpdate::new());
        #[ipv6addr]
        crate::ip::socket::apply_ipv6_socket_update(ctx, IpSockUpdate::new());
    }

    res
}

/// Delete a route from the forwarding table, returning `Err` if no
/// route was found to be deleted.
#[specialize_ip_address]
pub(crate) fn del_device_route<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
) -> Result<(), NotFoundError> {
    let res = get_state_inner_mut::<A::Version, _>(ctx.state_mut()).table.del_route(subnet);

    if res.is_ok() {
        #[ipv4addr]
        crate::ip::socket::apply_ipv4_socket_update(ctx, IpSockUpdate::new());
        #[ipv6addr]
        crate::ip::socket::apply_ipv6_socket_update(ctx, IpSockUpdate::new());
    }

    res
}

/// Return all the routes for the provided `IpAddress` type
pub(crate) fn iter_all_routes<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
) -> core::slice::Iter<Entry<A, DeviceId>> {
    get_state_inner::<A::Version, _>(ctx.state()).table.iter_installed()
}

/// Send an IPv4 packet to a remote host.
///
/// `send_ipv4_packet` accepts a destination IP address, a protocol, and a
/// callback. It computes the routing information, and invokes the callback with
/// the computed destination address. The callback returns a
/// `SerializationRequest`, which is serialized in a new IP packet and sent.
pub(crate) fn send_ipv4_packet<
    B: BufferMut,
    D: BufferDispatcher<B>,
    S: Serializer<Buffer = B>,
    F: FnOnce(SpecifiedAddr<Ipv4Addr>) -> S,
>(
    ctx: &mut Context<D>,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
    proto: IpProto,
    get_body: F,
) -> Result<(), S> {
    trace!("send_ipv4_packet({}, {})", dst_ip, proto);
    increment_counter!(ctx, "send_ipv4_packet");
    if Ipv4::LOOPBACK_SUBNET.contains(&dst_ip) {
        increment_counter!(ctx, "send_ipv4_packet::loopback");

        // TODO(joshlf): Currently, we have no way of representing the loopback
        // device as a DeviceId. We will need to fix that eventually.

        // TODO(joshlf): Currently, we serialize using the normal Serializer
        // functionality. I wonder if, in the case of delivering to loopback, we
        // can do something more efficient?
        let buffer =
            get_body(Ipv4::LOOPBACK_ADDRESS).serialize_vec_outer().map_err(|(_, ser)| ser)?;
        // TODO(joshlf): Respond with some kind of error if we don't have a
        // handler for that protocol? Maybe simulate what would have happened
        // (w.r.t ICMP) if this were a remote host?

        // NOTE(joshlf): By passing a DeviceId and ParseMetadata of None here,
        // we are promising that the protocol will be recognized (and the call
        // will panic if it's not). This is OK because the fact that we're
        // sending a packet with this protocol means we are able to process that
        // protocol.
        //
        // NOTE(joshlf): By doing that, we are also promising that the port will
        // be recognized. That is NOT OK, and the call will panic in that case.
        // TODO(joshlf): Fix this.

        // The reason for this match is so that, in each case, we are guaranteed
        // that the buffer type is a supported buffer type for our event
        // dispatcher. If we were to just pass the original buffer itself, then
        // in addition to supporting `B`, the event dispatcher would need to
        // support `Either<B, Vec<u8>>`, but then `Either<B, Vec<u8>>` would
        // become `B` from the perspective of `dispatch_receive_ip_packet`, so
        // it would need to support `Either<Either<B, Vec<u8>>, Vec<u8>>`, and
        // so on. This match allows us to break that type recursion.
        match buffer {
            Either::A(buffer) => dispatch_receive_ipv4_packet(
                ctx,
                None,
                FrameDestination::Unicast,
                Ipv4::LOOPBACK_ADDRESS.into_addr(),
                dst_ip,
                proto,
                buffer,
                None,
            ),
            Either::B(buffer) => dispatch_receive_ipv4_packet::<Buf<Vec<u8>>, _>(
                ctx,
                None,
                FrameDestination::Unicast,
                Ipv4::LOOPBACK_ADDRESS.into_addr(),
                dst_ip,
                proto,
                buffer,
                None,
            ),
        }
    } else if let Some(dest) = lookup_route(ctx, dst_ip) {
        // TODO(joshlf): Are we sure that a device route can never be set for a
        // device without an IP address? At the least, this is not currently
        // enforced anywhere, and is a DoS vector.
        let src_ip: SpecifiedAddr<Ipv4Addr> = crate::device::get_ip_addr_subnet(ctx, dest.device)
            .expect("IP device route set for device without IP address")
            .addr();
        send_ip_packet_from_device(
            ctx,
            dest.device,
            src_ip.into_addr(),
            dst_ip.into_addr(),
            dest.next_hop,
            proto,
            get_body(src_ip),
            None,
        )?;
    } else {
        debug!("No route to host");
        // TODO(joshlf): No route to host
    }

    Ok(())
}

/// Send an IPv6 packet to a remote host.
///
/// `send_ipv6_packet` accepts a destination IP address, a protocol, and a
/// callback. It computes the routing information, and invokes the callback with
/// the computed destination address. The callback returns a
/// `SerializationRequest`, which is serialized in a new IP packet and sent.
pub(crate) fn send_ipv6_packet<
    B: BufferMut,
    D: BufferDispatcher<B>,
    S: Serializer<Buffer = B>,
    F: FnOnce(SpecifiedAddr<Ipv6Addr>) -> S,
>(
    ctx: &mut Context<D>,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
    proto: IpProto,
    get_body: F,
) -> Result<(), S> {
    trace!("send_ipv6_packet({}, {})", dst_ip, proto);
    increment_counter!(ctx, "send_ipv6_packet");
    if Ipv6::LOOPBACK_SUBNET.contains(&dst_ip) {
        increment_counter!(ctx, "send_ipv6_packet::loopback");

        // TODO(joshlf): Currently, we have no way of representing the loopback
        // device as a DeviceId. We will need to fix that eventually.

        // TODO(joshlf): Currently, we serialize using the normal Serializer
        // functionality. I wonder if, in the case of delivering to loopback, we
        // can do something more efficient?
        let buffer =
            get_body(Ipv6::LOOPBACK_ADDRESS).serialize_vec_outer().map_err(|(_, ser)| ser)?;
        // TODO(joshlf): Respond with some kind of error if we don't have a
        // handler for that protocol? Maybe simulate what would have happened
        // (w.r.t ICMP) if this were a remote host?

        // NOTE(joshlf): By passing a DeviceId and ParseMetadata of None here,
        // we are promising that the protocol will be recognized (and the call
        // will panic if it's not). This is OK because the fact that we're
        // sending a packet with this protocol means we are able to process that
        // protocol.
        //
        // NOTE(joshlf): By doing that, we are also promising that the port will
        // be recognized. That is NOT OK, and the call will panic in that case.
        // TODO(joshlf): Fix this.

        // The reason for this match is so that, in each case, we are guaranteed
        // that the buffer type is a supported buffer type for our event
        // dispatcher. If we were to just pass the original buffer itself, then
        // in addition to supporting `B`, the event dispatcher would need to
        // support `Either<B, Vec<u8>>`, but then `Either<B, Vec<u8>>` would
        // become `B` from the perspective of `dispatch_receive_ip_packet`, so
        // it would need to support `Either<Either<B, Vec<u8>>, Vec<u8>>`, and
        // so on. This match allows us to break that type recursion.
        match buffer {
            Either::A(buffer) => dispatch_receive_ipv6_packet(
                ctx,
                None,
                FrameDestination::Unicast,
                Ipv6::LOOPBACK_ADDRESS.into_addr(),
                dst_ip,
                proto,
                buffer,
                None,
            ),
            Either::B(buffer) => dispatch_receive_ipv6_packet::<Buf<Vec<u8>>, _>(
                ctx,
                None,
                FrameDestination::Unicast,
                Ipv6::LOOPBACK_ADDRESS.into_addr(),
                dst_ip,
                proto,
                buffer,
                None,
            ),
        }
    } else if let Some(dest) = lookup_route(ctx, dst_ip) {
        // TODO(joshlf): Are we sure that a device route can never be set for a
        // device without an IP address? At the least, this is not currently
        // enforced anywhere, and is a DoS vector.
        let src_ip: SpecifiedAddr<Ipv6Addr> = crate::device::get_ip_addr_subnet(ctx, dest.device)
            .expect("IP device route set for device without IP address")
            .addr();
        send_ip_packet_from_device(
            ctx,
            dest.device,
            src_ip.into_addr(),
            dst_ip.into_addr(),
            dest.next_hop,
            proto,
            get_body(src_ip),
            None,
        )?;
    } else {
        debug!("No route to host");
        // TODO(joshlf): No route to host
    }

    Ok(())
}

/// Send an IP packet to a remote host over a specific device.
///
/// `send_ip_packet_from_device` accepts a device, a source and destination IP
/// address, a next hop IP address, and a `SerializationRequest`. It computes
/// the routing information and serializes the request in a new IP packet and
/// sends it. `mtu` will optionally impose an MTU constraint on the whole IP
/// packet. This is useful for cases where some packets are being sent out
/// which must not exceed some size (ICMPv6 Error Responses).
///
/// # Panics
///
/// Since `send_ip_packet_from_device` specifies a physical device, it cannot
/// send to or from a loopback IP address. If either `src_ip` or `dst_ip` are in
/// the loopback subnet, `send_ip_packet_from_device` will panic.
pub(crate) fn send_ip_packet_from_device<B: BufferMut, D: BufferDispatcher<B>, A, S>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: A,
    dst_ip: A,
    next_hop: SpecifiedAddr<A>,
    proto: IpProto,
    body: S,
    mtu: Option<u32>,
) -> Result<(), S>
where
    A: IpAddress,
    S: Serializer<Buffer = B>,
{
    assert!(!A::Version::LOOPBACK_SUBNET.contains(&src_ip));
    assert!(!A::Version::LOOPBACK_SUBNET.contains(&dst_ip));

    // Tentative addresses are not considered bound to an interface in the traditional sense,
    // therefore, no packet should have a source IP set to a tentative address.
    debug_assert!(!SpecifiedAddr::new(src_ip).map_or(false, |src_ip| {
        crate::device::is_addr_tentative_on_device(ctx, &src_ip, device)
    }));

    let builder = <A::Version as IpExt>::PacketBuilder::new(
        src_ip,
        dst_ip,
        get_hop_limit::<_, A::Version>(ctx, device),
        proto,
    );
    let body = body.encapsulate(builder);

    if let Some(mtu) = mtu {
        let body = body.with_mtu(mtu as usize);
        crate::device::send_ip_frame(ctx, device, next_hop, body)
            .map_err(|ser| ser.into_inner().into_inner())
    } else {
        crate::device::send_ip_frame(ctx, device, next_hop, body).map_err(|ser| ser.into_inner())
    }
}

impl<D: EventDispatcher> StateContext<Icmpv4State<D::Instant, IpSock<Ipv4, DeviceId>>>
    for Context<D>
{
    fn get_state_with(&self, _id: ()) -> &Icmpv4State<D::Instant, IpSock<Ipv4, DeviceId>> {
        &self.state().ipv4.icmp
    }

    fn get_state_mut_with(
        &mut self,
        _id: (),
    ) -> &mut Icmpv4State<D::Instant, IpSock<Ipv4, DeviceId>> {
        &mut self.state_mut().ipv4.icmp
    }
}

impl<D: EventDispatcher> StateContext<Icmpv6State<D::Instant, IpSock<Ipv6, DeviceId>>>
    for Context<D>
{
    fn get_state_with(&self, _id: ()) -> &Icmpv6State<D::Instant, IpSock<Ipv6, DeviceId>> {
        &self.state().ipv6.icmp
    }

    fn get_state_mut_with(
        &mut self,
        _id: (),
    ) -> &mut Icmpv6State<D::Instant, IpSock<Ipv6, DeviceId>> {
        &mut self.state_mut().ipv6.icmp
    }
}

impl<D: EventDispatcher> IcmpContext<Ipv4> for Context<D> {
    fn receive_icmp_error(
        &mut self,
        original_src_ip: Option<SpecifiedAddr<Ipv4Addr>>,
        original_dst_ip: SpecifiedAddr<Ipv4Addr>,
        original_proto: IpProto,
        original_body: &[u8],
        err: Icmpv4ErrorCode,
    ) {
        self.increment_counter("IcmpContext<Ipv4>::receive_icmp_error");
        trace!("IcmpContext<Ipv4>::receive_icmp_error({:?})", err);

        macro_rules! mtch {
            ($($cond:pat => $ty:ident),*) => {
                match original_proto {
                    IpProto::Icmp => <IcmpIpTransportContext as IpTransportContext<Ipv4, _>>
                                ::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),
                    $($cond => <<Context<D> as Ipv4TransportLayerContext>::$ty as IpTransportContext<Ipv4, _>>
                                ::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),)*
                    // TODO(joshlf): Once all IP protocol numbers are covered,
                    // remove this default case.
                    _ => <() as IpTransportContext<Ipv4, _>>::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),
                }
            };
        }

        #[rustfmt::skip]
        mtch!(IpProto::Tcp => Proto6, IpProto::Udp => Proto17);
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> BufferIcmpContext<Ipv4, B> for Context<D> {
    fn send_icmp_reply<S: Serializer<Buffer = B>, F: FnOnce(SpecifiedAddr<Ipv4Addr>) -> S>(
        &mut self,
        device: Option<DeviceId>,
        src_ip: SpecifiedAddr<Ipv4Addr>,
        dst_ip: SpecifiedAddr<Ipv4Addr>,
        get_body: F,
    ) -> Result<(), S> {
        trace!("send_icmp_reply({:?}, {}, {})", device, src_ip, dst_ip);
        self.increment_counter("send_icmp_reply");

        // TODO(joshlf): Use `dst_ip` for anything?
        send_ipv4_packet(self, src_ip, IpProto::Icmp, get_body)
    }

    fn send_icmp_error_message<
        S: Serializer<Buffer = B>,
        F: FnOnce(SpecifiedAddr<Ipv4Addr>) -> S,
    >(
        &mut self,
        device: DeviceId,
        frame_dst: FrameDestination,
        src_ip: SpecifiedAddr<Ipv4Addr>,
        dst_ip: Ipv4Addr,
        get_body: F,
        ip_mtu: Option<u32>,
        _allow_dst_multicast: bool,
    ) -> Result<(), S> {
        trace!("send_icmp_error_message({}, {}, {}, {:?})", device, src_ip, dst_ip, ip_mtu);
        self.increment_counter("send_icmp_error_message");

        if !crate::ip::icmp::should_send_icmpv4_error(frame_dst, src_ip, dst_ip) {
            return Ok(());
        }

        if let Some((device, local_ip, next_hop)) =
            get_icmp_error_message_destination(self, device, src_ip, dst_ip)
        {
            send_ip_packet_from_device(
                self,
                device,
                local_ip.into_addr(),
                src_ip.into_addr(),
                next_hop,
                IpProto::Icmp,
                get_body(local_ip),
                ip_mtu,
            )?;
        }

        Ok(())
    }
}

impl<D: EventDispatcher> IcmpContext<Ipv6> for Context<D> {
    fn receive_icmp_error(
        &mut self,
        original_src_ip: Option<SpecifiedAddr<Ipv6Addr>>,
        original_dst_ip: SpecifiedAddr<Ipv6Addr>,
        original_next_header: IpProto,
        original_body: &[u8],
        err: Icmpv6ErrorCode,
    ) {
        self.increment_counter("IcmpContext<Ipv6>::receive_icmp_error");
        trace!("IcmpContext<Ipv6>::receive_icmp_error({:?})", err);

        macro_rules! mtch {
            ($($cond:pat => $ty:ident),*) => {
                match original_next_header {
                    IpProto::Icmpv6 => <IcmpIpTransportContext as IpTransportContext<Ipv6, _>>
                    ::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),
                    $($cond => <<Context<D> as Ipv6TransportLayerContext>::$ty as IpTransportContext<Ipv6, _>>
                                ::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),)*
                    // TODO(joshlf): Once all IP protocol numbers are covered,
                    // remove this default case.
                    _ => <() as IpTransportContext<Ipv6, _>>::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),
                }
            };
        }

        #[rustfmt::skip]
        mtch!(IpProto::Tcp => Proto6, IpProto::Udp => Proto17);
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> BufferIcmpContext<Ipv6, B> for Context<D> {
    fn send_icmp_reply<S: Serializer<Buffer = B>, F: FnOnce(SpecifiedAddr<Ipv6Addr>) -> S>(
        &mut self,
        device: Option<DeviceId>,
        src_ip: SpecifiedAddr<Ipv6Addr>,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        get_body: F,
    ) -> Result<(), S> {
        trace!("send_icmp_reply({:?}, {}, {})", device, src_ip, dst_ip);
        self.increment_counter("send_icmp_reply");

        // TODO(joshlf): Use `dst_ip` for anything?
        send_ipv6_packet(self, src_ip, IpProto::Icmpv6, get_body)
    }

    fn send_icmp_error_message<
        S: Serializer<Buffer = B>,
        F: FnOnce(SpecifiedAddr<Ipv6Addr>) -> S,
    >(
        &mut self,
        device: DeviceId,
        frame_dst: FrameDestination,
        src_ip: SpecifiedAddr<Ipv6Addr>,
        dst_ip: Ipv6Addr,
        get_body: F,
        ip_mtu: Option<u32>,
        allow_dst_multicast: bool,
    ) -> Result<(), S> {
        trace!("send_icmp_error_message({}, {}, {}, {:?})", device, src_ip, dst_ip, ip_mtu);
        self.increment_counter("send_icmp_error_message");

        if !crate::ip::icmp::should_send_icmpv6_error(
            frame_dst,
            src_ip,
            dst_ip,
            allow_dst_multicast,
        ) {
            return Ok(());
        }

        if let Some((device, local_ip, next_hop)) =
            get_icmp_error_message_destination(self, device, src_ip, dst_ip)
        {
            send_ip_packet_from_device(
                self,
                device,
                local_ip.into_addr(),
                src_ip.into_addr(),
                next_hop,
                IpProto::Icmpv6,
                get_body(local_ip),
                ip_mtu,
            )?;
        }

        Ok(())
    }
}

/// Compute the device, source address, and next hop address for sending an ICMP
/// error message.
///
/// `device`, `src_ip`, and `dst_ip` are the device, source IP, and destination
/// IP of the original packet _being responded to_. If `Some(d, local_ip,
/// next_hop)` is returned, then a packet should be serialized with the source
/// address `local_ip` and the destination address `src_ip` and sent to the next
/// hop address `next_hop` on device `d`.
///
/// If `None` is returned, then an error message should not be sent. Note that
/// this does not call `crate::ip::icmp::should_send_icmpv{4,6}_error`; it is
/// the caller's responsibility to call that function and not send an error
/// message if it returns false.
fn get_icmp_error_message_destination<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
    _device: DeviceId,
    src_ip: SpecifiedAddr<A>,
    _dst_ip: A,
) -> Option<(DeviceId, SpecifiedAddr<A>, SpecifiedAddr<A>)> {
    // TODO(joshlf): Come up with rules for when to send ICMP error messages.
    // E.g., should we send a response over a different device than the device
    // that the original packet ingressed over? We'll probably want to consult
    // BCP 38 (aka RFC 2827) and RFC 3704.

    if let Some(route) = lookup_route(ctx, src_ip) {
        if let Some(local_ip) =
            crate::device::get_ip_addr_subnet(ctx, route.device).map(AddrSubnet::into_addr)
        {
            Some((route.device, local_ip, route.next_hop))
        } else {
            // TODO(joshlf): We need a general-purpose mechanism for choosing a
            // source address in cases where we're a) acting as a router (and
            // thus sending packets with our own source address, but not as a
            // result of any local application behavior) and, b) sending over an
            // unnumbered device (one without any configured IP address). ICMP
            // is the notable use case. Most likely, we will want to pick the IP
            // address of a different local device. See for an explanation of
            // why we might have this setup:
            // https://www.cisco.com/c/en/us/support/docs/ip/hot-standby-router-protocol-hsrp/13786-20.html#unnumbered_iface
            log_unimplemented!(
                None,
                "Sending ICMP over unnumbered device {} is unimplemented",
                route.device
            )
        }
    } else {
        debug!("Can't send ICMP response to {}: no route to host", src_ip);
        None
    }
}

/// Is `ctx` configured to route packets?
pub(crate) fn is_routing_enabled<D: EventDispatcher, I: Ip>(ctx: &Context<D>) -> bool {
    get_state_inner::<I, _>(ctx.state()).forward
}

/// Get the hop limit for new IP packets that will be sent out from `device`.
fn get_hop_limit<D: EventDispatcher, I: Ip>(ctx: &Context<D>, device: DeviceId) -> u8 {
    // TODO(ghanan): Should IPv4 packets use the same TTL value
    //               as IPv6 packets? Currently for the IPv6 case,
    //               we get the default hop limit from the device
    //               state which can be updated by NDP's Router
    //               Advertisement.

    match I::VERSION {
        IpVersion::V4 => DEFAULT_TTL.get(),
        // This value can be updated by NDP's Router Advertisements.
        IpVersion::V6 => crate::device::get_ipv6_hop_limit(ctx, device).get(),
    }
}

// Used in testing in other modules.
#[specialize_ip]
pub(crate) fn dispatch_receive_ip_packet_name<I: Ip>() -> &'static str {
    match I::VERSION {
        IpVersion::V4 => "dispatch_receive_ipv4_packet",
        IpVersion::V6 => "dispatch_receive_ipv6_packet",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::convert::TryFrom;
    use std::num::NonZeroU16;

    use byteorder::{ByteOrder, NetworkEndian};
    use net_types::ip::{Ipv4Addr, Ipv6Addr};
    use packet::{Buf, ParseBuffer};
    use packet_formats::ethernet::{
        EthernetFrame, EthernetFrameBuilder, EthernetFrameLengthCheck, EthernetIpExt,
    };
    use packet_formats::icmp::{
        IcmpDestUnreachable, IcmpEchoRequest, IcmpPacketBuilder, IcmpParseArgs, IcmpUnusedCode,
        Icmpv4DestUnreachableCode, Icmpv6Packet, Icmpv6PacketTooBig, Icmpv6ParameterProblemCode,
        MessageBody,
    };
    use packet_formats::ip::{IpExt, IpExtByteSlice, Ipv6ExtHdrType};
    use packet_formats::ipv4::Ipv4PacketBuilder;
    use packet_formats::ipv6::ext_hdrs::ExtensionHeaderOptionAction;
    use packet_formats::ipv6::Ipv6PacketBuilder;
    use packet_formats::testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame;
    use rand::Rng;
    use specialize_ip_macro::ip_test;

    use crate::device::{receive_frame, set_routing_enabled, FrameDestination};
    use crate::ip::path_mtu::get_pmtu;
    use crate::testutil::*;
    use crate::{DeviceId, Mac, StackStateBuilder};

    //
    // Some helper functions
    //

    /// Verify that an ICMP Parameter Problem packet was actually sent in response to
    /// a packet with an unrecognized IPv6 extension header option.
    ///
    /// `verify_icmp_for_unrecognized_ext_hdr_option` verifies that the next frame
    /// in `net` is an ICMP packet with code set to `code`, and pointer set to `pointer`.
    fn verify_icmp_for_unrecognized_ext_hdr_option(
        ctx: &mut Context<DummyEventDispatcher>,
        code: Icmpv6ParameterProblemCode,
        pointer: u32,
        offset: usize,
    ) {
        // Check the ICMP that bob attempted to send to alice
        let device_frames = ctx.dispatcher.frames_sent().clone();
        assert!(!device_frames.is_empty());
        let mut buffer = Buf::new(device_frames[offset].1.as_slice(), ..);
        let _frame =
            buffer.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
        let packet = buffer.parse::<<Ipv6 as IpExtByteSlice<&[u8]>>::Packet>().unwrap();
        let (src_ip, dst_ip, proto, _) = packet.into_metadata();
        assert_eq!(dst_ip, DUMMY_CONFIG_V6.remote_ip.get());
        assert_eq!(src_ip, DUMMY_CONFIG_V6.local_ip.get());
        assert_eq!(proto, IpProto::Icmpv6);
        let icmp =
            buffer.parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, dst_ip)).unwrap();
        if let Icmpv6Packet::ParameterProblem(icmp) = icmp {
            assert_eq!(icmp.code(), code);
            assert_eq!(icmp.message().pointer(), pointer);
        } else {
            panic!("Expected ICMPv6 Parameter Problem: {:?}", icmp);
        }
    }

    /// Populate a buffer `bytes` with data required to test unrecognized options.
    ///
    /// The unrecognized option type will be located at index 48. `bytes` must be
    /// at least 64 bytes long. If `to_multicast` is `true`, the destination address
    /// of the packet will be a multicast address.
    fn buf_for_unrecognized_ext_hdr_option_test(
        bytes: &mut [u8],
        action: ExtensionHeaderOptionAction,
        to_multicast: bool,
    ) -> Buf<&mut [u8]> {
        assert!(bytes.len() >= 64);

        let action: u8 = action.into();

        // Unrecognized Option type.
        let oty = 63 | (action << 6);

        #[rustfmt::skip]
        bytes[40..64].copy_from_slice(&[
            // Destination Options Extension Header
            IpProto::Udp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                        // Pad1
            1,   0,                   // Pad2
            1,   1, 0,                // Pad3
            oty, 6, 0, 0, 0, 0, 0, 0, // Unrecognized type w/ action = discard

            // Body
            1, 2, 3, 4, 5, 6, 7, 8
        ][..]);
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);

        let payload_len = (bytes.len() - 40) as u16;
        NetworkEndian::write_u16(&mut bytes[4..6], payload_len);

        bytes[6] = Ipv6ExtHdrType::DestinationOptions.into();
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(DUMMY_CONFIG_V6.remote_ip.bytes());

        if to_multicast {
            bytes[24..40].copy_from_slice(
                &[255, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32][..],
            );
        } else {
            bytes[24..40].copy_from_slice(DUMMY_CONFIG_V6.local_ip.bytes());
        }

        Buf::new(bytes, ..)
    }

    /// Create an IPv4 packet builder.
    fn get_ipv4_builder() -> Ipv4PacketBuilder {
        Ipv4PacketBuilder::new(
            DUMMY_CONFIG_V4.remote_ip,
            DUMMY_CONFIG_V4.local_ip,
            10,
            IpProto::Udp,
        )
    }

    /// Process an IP fragment depending on the `Ip` `process_ip_fragment` is
    /// specialized with.
    fn process_ip_fragment<I: Ip, D: EventDispatcher>(
        ctx: &mut Context<D>,
        device: DeviceId,
        fragment_id: u16,
        fragment_offset: u8,
        fragment_count: u8,
    ) {
        match I::VERSION {
            IpVersion::V4 => {
                process_ipv4_fragment(ctx, device, fragment_id, fragment_offset, fragment_count)
            }
            IpVersion::V6 => {
                process_ipv6_fragment(ctx, device, fragment_id, fragment_offset, fragment_count)
            }
        }
    }

    /// Generate and 'receive' an IPv4 fragment packet.
    ///
    /// `fragment_offset` is the fragment offset. `fragment_count` is the number
    /// of fragments for a packet. The generated packet will have a body of size
    /// 8 bytes.
    fn process_ipv4_fragment<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device: DeviceId,
        fragment_id: u16,
        fragment_offset: u8,
        fragment_count: u8,
    ) {
        assert!(fragment_offset < fragment_count);

        let m_flag = fragment_offset < (fragment_count - 1);

        let mut builder = get_ipv4_builder();
        builder.id(fragment_id);
        builder.fragment_offset(fragment_offset as u16);
        builder.mf_flag(m_flag);
        let mut body: Vec<u8> = Vec::new();
        body.extend(fragment_offset * 8..fragment_offset * 8 + 8);
        let buffer =
            Buf::new(body, ..).encapsulate(builder).serialize_vec_outer().unwrap().into_inner();
        receive_ipv4_packet(ctx, device, FrameDestination::Unicast, buffer);
    }

    /// Generate and 'receive' an IPv6 fragment packet.
    ///
    /// `fragment_offset` is the fragment offset. `fragment_count` is the number
    /// of fragments for a packet. The generated packet will have a body of size
    /// 8 bytes.
    fn process_ipv6_fragment<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device: DeviceId,
        fragment_id: u16,
        fragment_offset: u8,
        fragment_count: u8,
    ) {
        assert!(fragment_offset < fragment_count);

        let m_flag = fragment_offset < (fragment_count - 1);

        let mut bytes = vec![0; 48];
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);
        bytes[6] = Ipv6ExtHdrType::Fragment.into(); // Next Header
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(DUMMY_CONFIG_V6.remote_ip.bytes());
        bytes[24..40].copy_from_slice(DUMMY_CONFIG_V6.local_ip.bytes());
        bytes[40] = IpProto::Udp.into();
        bytes[42] = fragment_offset >> 5;
        bytes[43] = ((fragment_offset & 0x1F) << 3) | if m_flag { 1 } else { 0 };
        NetworkEndian::write_u32(&mut bytes[44..48], fragment_id as u32);
        bytes.extend(fragment_offset * 8..fragment_offset * 8 + 8);
        let payload_len = (bytes.len() - 40) as u16;
        NetworkEndian::write_u16(&mut bytes[4..6], payload_len);
        let buffer = Buf::new(bytes, ..);
        receive_ipv6_packet(ctx, device, FrameDestination::Unicast, buffer);
    }

    #[test]
    fn test_ipv6_icmp_parameter_problem_non_must() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);

        //
        // Test parsing an IPv6 packet with invalid next header value which
        // we SHOULD send an ICMP response for (but we don't since its not a
        // MUST).
        //

        #[rustfmt::skip]
        let bytes: &mut [u8] = &mut [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Body
            1, 2, 3, 4, 5,
        ][..];
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);
        let payload_len = (bytes.len() - 40) as u16;
        NetworkEndian::write_u16(&mut bytes[4..6], payload_len);
        bytes[6] = 255; // Invalid Next Header
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(DUMMY_CONFIG_V6.remote_ip.bytes());
        bytes[24..40].copy_from_slice(DUMMY_CONFIG_V6.local_ip.bytes());
        let buf = Buf::new(bytes, ..);

        receive_ipv6_packet(&mut ctx, device, FrameDestination::Unicast, buf);

        assert_eq!(get_counter_val(&mut ctx, "send_icmpv4_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv4_packet"), 0);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 0);
    }

    #[test]
    fn test_ipv6_icmp_parameter_problem_must() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);

        //
        // Test parsing an IPv6 packet where we MUST send an ICMP parameter problem
        // response (invalid routing type for a routing extension header).
        //

        #[rustfmt::skip]
        let bytes: &mut [u8] = &mut [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Routing Extension Header
            IpProto::Udp.into(),         // Next Header
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            255,                                // Routing Type (Invalid)
            1,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // Body
            1, 2, 3, 4, 5,
        ][..];
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);
        let payload_len = (bytes.len() - 40) as u16;
        NetworkEndian::write_u16(&mut bytes[4..6], payload_len);
        bytes[6] = Ipv6ExtHdrType::Routing.into();
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(DUMMY_CONFIG_V6.remote_ip.bytes());
        bytes[24..40].copy_from_slice(DUMMY_CONFIG_V6.local_ip.bytes());
        let buf = Buf::new(bytes, ..);
        receive_ipv6_packet(&mut ctx, device, FrameDestination::Unicast, buf);
        assert_eq!(ctx.dispatcher().frames_sent().len(), 1);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::ErroneousHeaderField,
            42,
            0,
        );
    }

    #[test]
    fn test_ipv6_unrecognized_ext_hdr_option() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let mut expected_icmps = 0;
        let mut bytes = [0; 64];
        let frame_dst = FrameDestination::Unicast;

        //
        // Test parsing an IPv6 packet where we MUST send an ICMP parameter problem
        // due to an unrecognized extension header option.
        //

        //
        // Test with unrecognized option type set with
        // action = skip & continue.
        //

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::SkipAndContinue,
            false,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 1);
        assert_eq!(ctx.dispatcher().frames_sent().len(), expected_icmps);

        //
        // Test with unrecognized option type set with
        // action = discard.
        //

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacket,
            false,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher().frames_sent().len(), expected_icmps);

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp
        // where dest addr is a unicast addr.
        //

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmp,
            false,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher().frames_sent().len(), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
            expected_icmps - 1,
        );

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp
        // where dest addr is a multicast addr.
        //

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmp,
            true,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher().frames_sent().len(), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
            expected_icmps - 1,
        );

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp if not multicast addr
        // where dest addr is a unicast addr.
        //

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast,
            false,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher().frames_sent().len(), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
            expected_icmps - 1,
        );

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp if not multicast addr
        // but dest addr is a multicast addr.
        //

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast,
            true,
        );
        // Do not expect an ICMP response for this packet
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher().frames_sent().len(), expected_icmps);

        //
        // None of our tests should have sent an icmpv4 packet, or dispatched an ip packet
        // after the first.
        //

        assert_eq!(get_counter_val(&mut ctx, "send_icmpv4_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 1);
    }

    #[ip_test]
    fn test_ip_packet_reassembly_not_needed<I: Ip + TestIpExt>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        //
        // Test that a non fragmented packet gets dispatched right away.
        //

        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 0, 1);

        // Make sure the packet got dispatched.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);
    }

    #[ip_test]
    fn test_ip_packet_reassembly<I: Ip + TestIpExt>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        //
        // Test that the received packet gets dispatched only after
        // receiving all the fragments.
        //

        // Process fragment #0
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 0, 3);

        // Process fragment #1
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 1, 3);

        // Make sure no packets got dispatched yet.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        // Process fragment #2
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 2, 3);

        // Make sure the packet finally got dispatched now that the final
        // fragment has been 'received'.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);
    }

    #[ip_test]
    fn test_ip_packet_reassembly_with_packets_arriving_out_of_order<I: Ip + TestIpExt>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let fragment_id_0 = 5;
        let fragment_id_1 = 10;
        let fragment_id_2 = 15;

        //
        // Test that received packets gets dispatched only after
        // receiving all the fragments with out of order arrival of
        // fragments.
        //

        // Process packet #0, fragment #1
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_0, 1, 3);

        // Process packet #1, fragment #2
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_1, 2, 3);

        // Process packet #1, fragment #0
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_1, 0, 3);

        // Make sure no packets got dispatched yet.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        // Process a packet that does not require reassembly (packet #2, fragment #0).
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_2, 0, 1);

        // Make packet #1 got dispatched since it didn't need reassembly.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        // Process packet #0, fragment #2
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_0, 2, 3);

        // Make sure no other packets got dispatched yet.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        // Process packet #0, fragment #0
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_0, 0, 3);

        // Make sure that packet #0 finally got dispatched now that the final
        // fragment has been 'received'.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 2);

        // Process packet #1, fragment #1
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_1, 1, 3);

        // Make sure the packet finally got dispatched now that the final
        // fragment has been 'received'.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 3);
    }

    #[ip_test]
    fn test_ip_packet_reassembly_timeout<I: Ip + TestIpExt>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        //
        // Test to make sure that packets must arrive within the reassembly
        // timeout.
        //

        // Process fragment #0
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 0, 3);

        // Make sure a timer got added.
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);

        // Process fragment #1
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 1, 3);

        // Trigger the timer (simulate a timeout for the fragmented packet)
        let key = FragmentCacheKey::<_>::new(
            I::DUMMY_CONFIG.remote_ip.get(),
            I::DUMMY_CONFIG.local_ip.get(),
            u32::from(fragment_id),
        );
        assert_eq!(
            trigger_next_timer(&mut ctx).unwrap(),
            IpLayerTimerId::new_reassembly_timeout_timer_id(key)
        );

        // Make sure no other times exist..
        assert_eq!(ctx.dispatcher.timer_events().count(), 0);

        // Process fragment #2
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 2, 3);

        // Make sure no packets got dispatched yet since even
        // though we technically received all the fragments, this fragment
        // (#2) arrived too late and the reassembly timeout was triggered,
        // causing the prior fragment data to be discarded.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 0);
    }

    #[ip_test]
    fn test_ip_reassembly_only_at_destination_host<I: Ip + TestIpExt>() {
        // Create a new network with two parties (alice & bob) and
        // enable IP packet routing for alice.
        let a = "alice";
        let b = "bob";
        let dummy_config = I::DUMMY_CONFIG;
        let mut state_builder = StackStateBuilder::default();
        state_builder.ipv4_builder().forward(true);
        state_builder.ipv6_builder().forward(true);
        let mut ndp_configs = crate::device::ndp::NdpConfigurations::default();
        ndp_configs.set_dup_addr_detect_transmits(None);
        ndp_configs.set_max_router_solicitations(None);
        state_builder.device_builder().set_default_ndp_configs(ndp_configs);
        let device = DeviceId::new_ethernet(0);
        let mut alice = DummyEventDispatcherBuilder::from_config(dummy_config.swap())
            .build_with(state_builder, DummyEventDispatcher::default());
        set_routing_enabled::<_, I>(&mut alice, device, true);
        let bob = DummyEventDispatcherBuilder::from_config(dummy_config).build();
        let contexts = vec![(a.clone(), alice), (b.clone(), bob)].into_iter();
        let mut net = DummyNetwork::new(contexts, move |net, _device_id| {
            if *net == a {
                vec![(b.clone(), device, None)]
            } else {
                vec![(a.clone(), device, None)]
            }
        });
        let fragment_id = 5;

        //
        // Test that packets only get reassembled and dispatched at the
        // destination. In this test, Alice is receiving packets from some source
        // that is actually destined for Bob. Alice should simply forward
        // the packets without attempting to process or reassemble the fragments.
        //

        // Process fragment #0
        process_ip_fragment::<I, _>(&mut net.context("alice"), device, fragment_id, 0, 3);
        // Make sure the packet got sent from alice to bob
        assert!(!net.step().is_idle());

        // Process fragment #1
        process_ip_fragment::<I, _>(&mut net.context("alice"), device, fragment_id, 1, 3);
        assert!(!net.step().is_idle());

        // Make sure no packets got dispatched yet.
        assert_eq!(
            get_counter_val(&mut net.context("alice"), dispatch_receive_ip_packet_name::<I>()),
            0
        );
        assert_eq!(
            get_counter_val(&mut net.context("bob"), dispatch_receive_ip_packet_name::<I>()),
            0
        );

        // Process fragment #2
        process_ip_fragment::<I, _>(&mut net.context("alice"), device, fragment_id, 2, 3);
        assert!(!net.step().is_idle());

        // Make sure the packet finally got dispatched now that the final
        // fragment has been received by bob.
        assert_eq!(
            get_counter_val(&mut net.context("alice"), dispatch_receive_ip_packet_name::<I>()),
            0
        );
        assert_eq!(
            get_counter_val(&mut net.context("bob"), dispatch_receive_ip_packet_name::<I>()),
            1
        );

        // Make sure there are no more events.
        assert!(net.step().is_idle());
    }

    #[test]
    fn test_ipv6_packet_too_big() {
        //
        // Test sending an IPv6 Packet Too Big Error when receiving a packet that is
        // too big to be forwarded when it isn't destined for the node it arrived at.
        //

        let dummy_config = Ipv6::DUMMY_CONFIG;
        let mut state_builder = StackStateBuilder::default();
        state_builder.ipv6_builder().forward(true);
        let mut ndp_configs = crate::device::ndp::NdpConfigurations::default();
        ndp_configs.set_dup_addr_detect_transmits(None);
        ndp_configs.set_max_router_solicitations(None);
        state_builder.device_builder().set_default_ndp_configs(ndp_configs);
        let mut dispatcher_builder = DummyEventDispatcherBuilder::from_config(dummy_config.clone());
        let extra_ip = Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 100]);
        let extra_mac = Mac::new([13, 14, 15, 16, 17, 18]);
        dispatcher_builder.add_ndp_table_entry(0, extra_ip, extra_mac);
        dispatcher_builder.add_ndp_table_entry(
            0,
            extra_mac.to_ipv6_link_local().addr().get(),
            extra_mac,
        );
        let mut ctx = dispatcher_builder.build_with(state_builder, DummyEventDispatcher::default());
        let device = DeviceId::new_ethernet(0);
        set_routing_enabled::<_, Ipv6>(&mut ctx, device, true);
        let frame_dst = FrameDestination::Unicast;

        // Construct an IPv6 packet that is too big for our MTU (MTU = 1280; body itself is 5000).
        // Note, the final packet will be larger because of IP header data.
        let mut rng = new_rng(70812476915813);
        let body: Vec<u8> = std::iter::repeat_with(|| rng.gen()).take(5000).collect();

        // Ip packet from some node destined to a remote on this network, arriving locally.
        let mut ipv6_packet_buf = Buf::new(body.clone(), ..)
            .encapsulate(Ipv6PacketBuilder::new(extra_ip, dummy_config.remote_ip, 64, IpProto::Udp))
            .serialize_vec_outer()
            .unwrap();
        // Receive the IP packet.
        receive_ipv6_packet(&mut ctx, device, frame_dst, ipv6_packet_buf.clone());

        // Should not have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 0);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_packet_too_big"), 1);

        // Should have sent out one frame though.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);

        // Received packet should be a Packet Too Big ICMP error message.
        let buf = &ctx.dispatcher.frames_sent()[0].1[..];
        // The original packet's TTL gets decremented so we decrement here
        // to validate the rest of the icmp message body.
        let ipv6_packet_buf_mut: &mut [u8] = ipv6_packet_buf.as_mut();
        ipv6_packet_buf_mut[7] -= 1;
        let (_, _, _, _, _, message, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, Icmpv6PacketTooBig, _>(
                buf,
                move |packet| {
                    // Size of the ICMP message body should be size of the
                    // MTU without IP and ICMP headers.
                    let expected_len = 1280 - 48;
                    let actual_body: &[u8] = ipv6_packet_buf.as_ref();
                    let actual_body = &actual_body[..expected_len];
                    assert_eq!(packet.body().len(), expected_len);
                    assert_eq!(packet.body().bytes(), actual_body);
                },
            )
            .unwrap();
        assert_eq!(code, IcmpUnusedCode);
        // MTU should match the mtu for the link.
        assert_eq!(message, Icmpv6PacketTooBig::new(1280));
    }

    #[specialize_ip_address]
    fn create_packet_too_big_buf<A: IpAddress>(
        src_ip: A,
        dst_ip: A,
        mtu: u16,
        body: Option<Buf<Vec<u8>>>,
    ) -> Buf<Vec<u8>> {
        let body = body.unwrap_or_else(|| Buf::new(Vec::new(), ..));

        #[ipv4addr]
        let ret = {
            let msg = match NonZeroU16::new(mtu) {
                Some(mtu) => IcmpDestUnreachable::new_for_frag_req(mtu),
                None => IcmpDestUnreachable::default(),
            };

            body.encapsulate(IcmpPacketBuilder::<Ipv4, &mut [u8], IcmpDestUnreachable>::new(
                dst_ip,
                src_ip,
                Icmpv4DestUnreachableCode::FragmentationRequired,
                msg,
            ))
            .encapsulate(Ipv4PacketBuilder::new(src_ip, dst_ip, 64, IpProto::Icmp))
            .serialize_vec_outer()
            .unwrap()
        };

        #[ipv6addr]
        let ret = body
            .encapsulate(IcmpPacketBuilder::<Ipv6, &mut [u8], Icmpv6PacketTooBig>::new(
                dst_ip,
                src_ip,
                IcmpUnusedCode,
                Icmpv6PacketTooBig::new(u32::from(mtu)),
            ))
            .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, 64, IpProto::Icmpv6))
            .serialize_vec_outer()
            .unwrap();

        ret.into_inner()
    }

    #[ip_test]
    fn test_ip_update_pmtu<I: Ip + TestIpExt>() {
        //
        // Test receiving a Packet Too Big (IPv6) or Dest Unreachable Fragmentation
        // Required (IPv4) which should update the PMTU if it is less than the current value.
        //

        let dummy_config = I::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        //
        // Update PMTU from None
        //

        let new_mtu1 = u32::from(I::MINIMUM_LINK_MTU) + 100;

        // Create ICMP IP buf
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip.get(),
            dummy_config.local_ip.get(),
            u16::try_from(new_mtu1).unwrap(),
            None,
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );

        //
        // Don't update PMTU when current PMTU is less than reported MTU
        //

        let new_mtu2 = u32::from(I::MINIMUM_LINK_MTU) + 200;

        // Create IPv6 ICMPv6 packet too big packet with MTU larger than current PMTU.
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip.get(),
            dummy_config.local_ip.get(),
            u16::try_from(new_mtu2).unwrap(),
            None,
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 2);

        // The PMTU should not have updated to `new_mtu2`
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu1
        );

        //
        // Update PMTU when current PMTU is greater than the reported MTU
        //

        let new_mtu3 = u32::from(I::MINIMUM_LINK_MTU) + 50;

        // Create IPv6 ICMPv6 packet too big packet with MTU smaller than current PMTU.
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip.get(),
            dummy_config.local_ip.get(),
            u16::try_from(new_mtu3).unwrap(),
            None,
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 3);

        // The PMTU should have updated to 1900.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            new_mtu3
        );
    }

    #[ip_test]
    fn test_ip_update_pmtu_too_low<I: Ip + TestIpExt>() {
        //
        // Test receiving a Packet Too Big (IPv6) or Dest Unreachable Fragmentation
        // Required (IPv4) which should not update the PMTU if it is less than the min mtu.
        //

        let dummy_config = I::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        //
        // Update PMTU from None but with a mtu too low.
        //

        let new_mtu1 = u32::from(I::MINIMUM_LINK_MTU) - 1;

        // Create ICMP IP buf
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip.get(),
            dummy_config.local_ip.get(),
            u16::try_from(new_mtu1).unwrap(),
            None,
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        assert!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).is_none(),
        );
    }

    /// Create buffer to be used as the ICMPv4 message body
    /// where the original packet's body  length is `body_len`.
    fn create_orig_packet_buf(src_ip: Ipv4Addr, dst_ip: Ipv4Addr, body_len: usize) -> Buf<Vec<u8>> {
        Buf::new(vec![0; body_len], ..)
            .encapsulate(Ipv4PacketBuilder::new(src_ip, dst_ip, 64, IpProto::Udp))
            .serialize_vec_outer()
            .unwrap()
            .into_inner()
    }

    #[test]
    fn test_ipv4_remote_no_rfc1191() {
        //
        // Test receiving an IPv4 Dest Unreachable Fragmentation
        // Required from a node that does not implement RFC 1191.
        //

        let dummy_config = Ipv4::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        //
        // Update from None
        //

        // Create ICMP IP buf w/ orig packet body len = 500; orig packet len = 520
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip.get(),
            dummy_config.local_ip.get(),
            0, // A 0 value indicates that the source of the
            // ICMP message does not implement RFC 1191.
            create_orig_packet_buf(dummy_config.local_ip.get(), dummy_config.remote_ip.get(), 500)
                .into(),
        );

        // Receive the IP packet.
        receive_ipv4_packet(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv4_packet"), 1);

        // Should have decreased PMTU value to the next lower PMTU
        // plateau from `crate::ip::path_mtu::PMTU_PLATEAUS`.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            508
        );

        //
        // Don't Update when packet size is too small
        //

        // Create ICMP IP buf w/ orig packet body len = 1; orig packet len = 21
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip.get(),
            dummy_config.local_ip.get(),
            0,
            create_orig_packet_buf(dummy_config.local_ip.get(), dummy_config.remote_ip.get(), 1)
                .into(),
        );

        // Receive the IP packet.
        receive_ipv4_packet(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv4_packet"), 2);

        // Should not have updated PMTU as there is no other valid
        // lower PMTU value.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            508
        );

        //
        // Update to lower PMTU estimate based on original packet size.
        //

        // Create ICMP IP buf w/ orig packet body len = 60; orig packet len = 80
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip.get(),
            dummy_config.local_ip.get(),
            0,
            create_orig_packet_buf(dummy_config.local_ip.get(), dummy_config.remote_ip.get(), 60)
                .into(),
        );

        // Receive the IP packet.
        receive_ipv4_packet(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv4_packet"), 3);

        // Should have decreased PMTU value to the next lower PMTU
        // plateau from `crate::ip::path_mtu::PMTU_PLATEAUS`.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            68
        );

        //
        // Should not update PMTU because the next low PMTU from this original
        // packet size is higher than current PMTU.
        //

        // Create ICMP IP buf w/ orig packet body len = 290; orig packet len = 310
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip.get(),
            dummy_config.local_ip.get(),
            0, // A 0 value indicates that the source of the
            // ICMP message does not implement RFC 1191.
            create_orig_packet_buf(dummy_config.local_ip.get(), dummy_config.remote_ip.get(), 290)
                .into(),
        );

        // Receive the IP packet.
        receive_ipv4_packet(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv4_packet"), 4);

        // Should not have updated the PMTU as the current PMTU is lower.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip.get(), dummy_config.remote_ip.get()).unwrap(),
            68
        );
    }

    #[test]
    fn test_invalid_icmpv4_in_ipv6() {
        let ip_config = Ipv6::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(ip_config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(1);
        let frame_dst = FrameDestination::Unicast;

        let ic_config = Ipv4::DUMMY_CONFIG;
        let icmp_builder = IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
            ic_config.remote_ip,
            ic_config.local_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(0, 0),
        );

        let ip_builder =
            Ipv6PacketBuilder::new(ip_config.remote_ip, ip_config.local_ip, 64, IpProto::Icmp);

        let buf = Buf::new(Vec::new(), ..)
            .encapsulate(icmp_builder)
            .encapsulate(ip_builder)
            .serialize_vec_outer()
            .unwrap();

        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);

        // Should not have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 0);

        // In IPv6, the next header value (ICMP(v4)) would have been considered
        // unrecognized so an ICMP parameter problem response SHOULD be sent, but
        // the netstack chooses to just drop the packet since we are not
        // required to send the ICMP response.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 0);
    }

    #[test]
    fn test_invalid_icmpv6_in_ipv4() {
        let ip_config = Ipv4::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(ip_config.clone())
            .build::<DummyEventDispatcher>();
        // First possible device id.
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        let ic_config = Ipv6::DUMMY_CONFIG;
        let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
            ic_config.remote_ip,
            ic_config.local_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(0, 0),
        );

        let ip_builder =
            Ipv4PacketBuilder::new(ip_config.remote_ip, ip_config.local_ip, 64, IpProto::Icmpv6);

        let buf = Buf::new(Vec::new(), ..)
            .encapsulate(icmp_builder)
            .encapsulate(ip_builder)
            .serialize_vec_outer()
            .unwrap();

        receive_ipv4_packet(&mut ctx, device, frame_dst, buf);

        // Should have dispatched the packet but resulted in an ICMP error.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv4_packet"), 1);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv4_dest_unreachable"), 1);
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        let buf = &ctx.dispatcher.frames_sent()[0].1[..];
        let (_, _, _, _, _, _, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, IcmpDestUnreachable, _>(
                buf,
                |_| {},
            )
            .unwrap();
        assert_eq!(code, Icmpv4DestUnreachableCode::DestProtocolUnreachable);
    }

    #[ip_test]
    fn test_joining_leaving_ip_multicast_group<I: Ip + TestIpExt + IpExt>() {
        #[specialize_ip_address]
        fn get_multicast_addr<A: IpAddress>() -> A {
            #[ipv4addr]
            return Ipv4Addr::new([224, 0, 0, 1]);

            #[ipv6addr]
            return Ipv6Addr::new([255, 17, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
        }

        //
        // Test receiving a packet destined to a multicast IP (and corresponding multicast MAC).
        //

        let config = I::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let multi_addr = get_multicast_addr::<I::Addr>();
        let dst_mac = Mac::from(&MulticastAddr::new(multi_addr).unwrap());
        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(I::PacketBuilder::new(
                config.remote_ip.get(),
                multi_addr,
                64,
                IpProto::Udp,
            ))
            .encapsulate(EthernetFrameBuilder::new(config.remote_mac, dst_mac, I::ETHER_TYPE))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .into_inner();

        let multi_addr = MulticastAddr::new(multi_addr).unwrap();
        // Should not have dispatched the packet since we are not in the
        // multicast group `multi_addr`.
        assert!(!crate::device::is_in_ip_multicast(&ctx, device, multi_addr));
        receive_frame(&mut ctx, device, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        // Join the multicast group and receive the packet, we should dispatch it.
        crate::device::join_ip_multicast(&mut ctx, device, multi_addr);
        assert!(crate::device::is_in_ip_multicast(&ctx, device, multi_addr));
        receive_frame(&mut ctx, device, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        // Leave the multicast group and receive the packet, we should not dispatch it.
        crate::device::leave_ip_multicast(&mut ctx, device, multi_addr);
        assert!(!crate::device::is_in_ip_multicast(&ctx, device, multi_addr));
        receive_frame(&mut ctx, device, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);
    }

    #[ip_test]
    #[should_panic(
        expected = "loopback addresses should be handled before consulting the forwarding table"
    )]
    fn test_lookup_table_address<I: Ip + TestIpExt>() {
        let cfg = I::DUMMY_CONFIG;
        let ip_address = I::LOOPBACK_ADDRESS;
        let ctx =
            DummyEventDispatcherBuilder::from_config(cfg.clone()).build::<DummyEventDispatcher>();
        lookup_route(&ctx, ip_address);
    }

    #[test]
    fn test_no_dispatch_non_ndp_packets_during_ndp_dad() {
        // Here we make sure we are not dispatching packets destined to a tentative address
        // (that is performing NDP's Duplicate Address Detection (DAD)) -- IPv6 only.

        // We explicitly call `build_with` when building our context below because `build` will
        // set the default NDP parameter DUP_ADDR_DETECT_TRANSMITS to 0 (effectively disabling
        // DAD) so we use our own custom `StackStateBuilder` to set it to the default value
        // of `1` (see `DUP_ADDR_DETECT_TRANSMITS`).
        let config = Ipv6::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::default()
            .build_with(StackStateBuilder::default(), DummyEventDispatcher::default());
        let device =
            ctx.state_mut().add_ethernet_device(config.local_mac, Ipv6::MINIMUM_LINK_MTU.into());
        crate::device::initialize_device(&mut ctx, device);

        let frame_dst = FrameDestination::Unicast;

        let ip = config.local_mac.to_ipv6_link_local().addr().get();

        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(Ipv6PacketBuilder::new(config.remote_ip, ip, 64, IpProto::Udp))
            .serialize_vec_outer()
            .unwrap()
            .into_inner();

        // Received packet should not have been dispatched.
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 0);

        // Make sure all timers are done (initial DAD to complete on the interface).
        trigger_timers_until(&mut ctx, |_| false);

        // Received packet should have been dispatched.
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 1);

        // Set the new IP (this should trigger DAD).
        let ip = config.local_ip.get();
        crate::device::add_ip_addr_subnet(&mut ctx, device, AddrSubnet::new(ip, 128).unwrap())
            .unwrap();

        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(Ipv6PacketBuilder::new(config.remote_ip, ip, 64, IpProto::Udp))
            .serialize_vec_outer()
            .unwrap()
            .into_inner();

        // Received packet should not have been dispatched.
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 1);

        // Make sure all timers are done (DAD to complete on the interface due to new IP).
        trigger_timers_until(&mut ctx, |_| false);

        // Received packet should have been dispatched.
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 2);
    }
}
