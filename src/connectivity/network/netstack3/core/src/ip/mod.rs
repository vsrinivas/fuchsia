// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Protocol, versions 4 and 6.

mod forwarding;
mod gmp;
pub mod icmp;
mod igmp;
mod ipv6;
mod mld;
pub(crate) mod path_mtu;
pub(crate) mod reassembly;
mod types;

// It's ok to `pub use` rather `pub(crate) use` here because the items in
// `types` which are themselves `pub(crate)` will still not be allowed to be
// re-exported from the root.
pub use self::types::*;

use std::fmt::{Debug, Display};
use std::mem;

use log::{debug, trace};
use net_types::ip::{AddrSubnet, Ip, IpAddress, IpVersion, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, Subnet};
use net_types::MulticastAddr;
use packet::{Buf, BufferMut, Either, EmptyBuf, ParsablePacket, ParseMetadata, Serializer};
use specialize_ip_macro::{specialize_ip, specialize_ip_address};

use crate::context::{CounterContext, FrameContext, StateContext, TimerContext};
use crate::data_structures::IdMap;
use crate::device::{DeviceId, FrameDestination};
use crate::error::{ExistsError, IpParseError, NotFoundError};
use crate::ip::forwarding::{Destination, ForwardingTable};
use crate::ip::icmp::{
    send_icmpv4_parameter_problem, send_icmpv6_parameter_problem, should_send_icmpv4_error,
    should_send_icmpv6_error, IcmpContext, IcmpEventDispatcher, IcmpState, IcmpStateBuilder,
    Icmpv4State, Icmpv6State,
};
use crate::ip::igmp::{IgmpContext, IgmpInterface, IgmpPacketMetadata, IgmpTimerId};
use crate::ip::ipv6::Ipv6PacketAction;
use crate::ip::mld::{MldContext, MldFrameMetadata, MldInterface, MldReportDelay};
use crate::ip::path_mtu::{handle_pmtu_timer, IpLayerPathMtuCache, PmtuTimerId};
use crate::ip::reassembly::{
    handle_reassembly_timer, process_fragment, reassemble_packet, FragmentCacheKey,
    FragmentProcessingState, IpLayerFragmentCache,
};
use crate::wire::icmp::{IcmpIpExt, Icmpv4ParameterProblem, Icmpv6ParameterProblem};
use crate::wire::ipv4::{Ipv4PacketBuilder, Ipv4PacketBuilderWithOptions};
use crate::{BufferDispatcher, Context, EventDispatcher, StackState, TimerId, TimerIdInner};

/// Default IPv4 TTL.
const DEFAULT_TTL: u8 = 64;

/// Minimum MTU required by all IPv6 devices.
pub(crate) const IPV6_MIN_MTU: u32 = 1280;

// The ipv4 multicast address for all routers.
const IPV4_ALL_ROUTERS: Ipv4Addr = Ipv4Addr::new([224, 0, 0, 2]);
// The ipv6 multicast address for all routers.
const IPV6_ALL_ROUTERS: Ipv6Addr =
    Ipv6Addr::new([0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2]);

/// The metadata for sending an IP packet from a particular source address.
///
/// `IpPacketFromArgs` is used as the metadata for the [`FrameContext`] bound
/// required by [`BufferTransportIpContext`]. It allows sending an IP packet
/// from a particular source address.
pub struct IpPacketFromArgs<A: IpAddress> {
    pub(crate) src_ip: A,
    pub(crate) dst_ip: A,
    pub(crate) proto: IpProto,
}

impl<A: IpAddress> IpPacketFromArgs<A> {
    /// Constructs a new `IpPacketFromArgs`.
    pub(crate) fn new(src_ip: A, dst_ip: A, proto: IpProto) -> IpPacketFromArgs<A> {
        IpPacketFromArgs { src_ip, dst_ip, proto }
    }
}

/// The execution context provided by the IP layer to transport layer protocols.
pub trait TransportIpContext<I: Ip> {
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
    fn local_address_for_remote(&self, remote: I::Addr) -> Option<I::Addr>;
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

impl<A: IpAddress, B: BufferMut, D: BufferDispatcher<B>> FrameContext<B, IpPacketFromArgs<A>>
    for Context<D>
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        meta: IpPacketFromArgs<A>,
        body: S,
    ) -> Result<(), S> {
        send_ip_packet_from(self, meta.src_ip, meta.dst_ip, meta.proto, body)
    }
}

impl<I: Ip, D: EventDispatcher> TransportIpContext<I> for Context<D> {
    fn is_local_addr(&self, addr: I::Addr) -> bool {
        is_local_addr(self, addr)
    }

    fn local_address_for_remote(&self, remote: I::Addr) -> Option<I::Addr> {
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
pub(crate) trait IpDeviceIdContext {
    type DeviceId: Copy + Display + Debug + Send + Sync + 'static;
}

// Temporary blanket impl until we switch over entirely to the traits defined in
// the `context` module.
impl<D: EventDispatcher> IpDeviceIdContext for Context<D> {
    type DeviceId = DeviceId;
}

/// A builder for IP layer state.
#[derive(Clone)]
pub struct IpStateBuilder {
    forward_v4: bool,
    forward_v6: bool,
    icmp: IcmpStateBuilder,
}

impl Default for IpStateBuilder {
    fn default() -> IpStateBuilder {
        IpStateBuilder { forward_v4: false, forward_v6: false, icmp: IcmpStateBuilder::default() }
    }
}

impl IpStateBuilder {
    /// Enable or disable IP packet forwarding (default: disabled).
    ///
    /// If `forward` is true, then an incoming IP packet whose destination
    /// address identifies a remote host will be forwarded to that host.
    pub fn forward(&mut self, forward: bool) -> &mut Self {
        self.forward_v4 = forward;
        self.forward_v6 = forward;
        self
    }

    /// Get the builder for the ICMP state.
    pub fn icmp_builder(&mut self) -> &mut IcmpStateBuilder {
        &mut self.icmp
    }

    pub(crate) fn build<D: EventDispatcher>(self) -> IpLayerState<D> {
        IpLayerState {
            v4: IpLayerStateInner {
                forward: self.forward_v4,
                table: ForwardingTable::default(),
                fragment_cache: IpLayerFragmentCache::new(),
                path_mtu: IpLayerPathMtuCache::new(),
            },
            v6: IpLayerStateInner {
                forward: self.forward_v6,
                table: ForwardingTable::default(),
                fragment_cache: IpLayerFragmentCache::new(),
                path_mtu: IpLayerPathMtuCache::new(),
            },
            icmp: self.icmp.build(),
            igmp: IdMap::default(),
            mld: IdMap::default(),
        }
    }
}

/// The state associated with the IP layer.
pub(crate) struct IpLayerState<D: EventDispatcher> {
    v4: IpLayerStateInner<Ipv4, D>,
    v6: IpLayerStateInner<Ipv6, D>,
    icmp: IcmpState,
    igmp: IdMap<IgmpInterface<D::Instant>>,
    mld: IdMap<MldInterface<D::Instant>>,
}

impl<D: EventDispatcher> IpLayerState<D> {
    /// Get the IGMP state associated with the device immutably.
    pub(crate) fn get_igmp_state(&self, device_id: usize) -> &IgmpInterface<D::Instant> {
        self.igmp.get(device_id).unwrap()
    }

    /// Get the IGMP state associated with the device mutably.
    pub(crate) fn get_igmp_state_mut(
        &mut self,
        device_id: usize,
    ) -> &mut IgmpInterface<D::Instant> {
        self.igmp.entry(device_id).or_insert(IgmpInterface::default())
    }

    /// Get the MLD state associated with the device immutably.
    pub(crate) fn get_mld_state(&self, device_id: usize) -> &MldInterface<D::Instant> {
        self.mld.get(device_id).unwrap()
    }

    /// Get the MLD state associated with the device mutably.
    pub(crate) fn get_mld_state_mut(&mut self, device_id: usize) -> &mut MldInterface<D::Instant> {
        self.mld.entry(device_id).or_insert(MldInterface::default())
    }
}

struct IpLayerStateInner<I: Ip, D: EventDispatcher> {
    forward: bool,
    table: ForwardingTable<I>,
    fragment_cache: IpLayerFragmentCache<I>,
    path_mtu: IpLayerPathMtuCache<I, D::Instant>,
}

#[specialize_ip]
fn get_state_inner<I: Ip, D: EventDispatcher>(state: &StackState<D>) -> &IpLayerStateInner<I, D> {
    #[ipv4]
    return &state.ip.v4;
    #[ipv6]
    return &state.ip.v6;
}

#[specialize_ip]
fn get_state_inner_mut<I: Ip, D: EventDispatcher>(
    state: &mut StackState<D>,
) -> &mut IpLayerStateInner<I, D> {
    #[ipv4]
    return &mut state.ip.v4;
    #[ipv6]
    return &mut state.ip.v6;
}

impl<D: EventDispatcher> StateContext<DeviceId, IgmpInterface<D::Instant>> for Context<D> {
    fn get_state(&self, device: DeviceId) -> &IgmpInterface<D::Instant> {
        self.state().ip.get_igmp_state(device.id())
    }

    fn get_state_mut(&mut self, device: DeviceId) -> &mut IgmpInterface<D::Instant> {
        self.state_mut().ip.get_igmp_state_mut(device.id())
    }
}

impl<D: EventDispatcher> StateContext<DeviceId, MldInterface<D::Instant>> for Context<D> {
    fn get_state(&self, device: DeviceId) -> &MldInterface<D::Instant> {
        self.state().ip.get_mld_state(device.id())
    }

    fn get_state_mut(&mut self, device: DeviceId) -> &mut MldInterface<D::Instant> {
        self.state_mut().ip.get_mld_state_mut(device.id())
    }
}

impl<I: Ip, D: EventDispatcher> StateContext<(), IpLayerFragmentCache<I>> for Context<D> {
    fn get_state(&self, _id: ()) -> &IpLayerFragmentCache<I> {
        &get_state_inner(self.state()).fragment_cache
    }

    fn get_state_mut(&mut self, _id: ()) -> &mut IpLayerFragmentCache<I> {
        &mut get_state_inner_mut(self.state_mut()).fragment_cache
    }
}

impl<I: Ip, D: EventDispatcher> StateContext<(), IpLayerPathMtuCache<I, D::Instant>>
    for Context<D>
{
    fn get_state(&self, _id: ()) -> &IpLayerPathMtuCache<I, D::Instant> {
        &get_state_inner(self.state()).path_mtu
    }

    fn get_state_mut(&mut self, _id: ()) -> &mut IpLayerPathMtuCache<I, D::Instant> {
        &mut get_state_inner_mut(self.state_mut()).path_mtu
    }
}

/// An event dispatcher for the IP layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait IpLayerEventDispatcher<B: BufferMut>: IcmpEventDispatcher<B> {}

/// The identifier for timer events in the IP layer.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub(crate) enum IpLayerTimerId {
    /// A timer event for IPv4 packet reassembly timeouts.
    ReassemblyTimeoutv4(FragmentCacheKey<Ipv4Addr>),
    /// A timer event for IPv6 packet reassembly timeouts.
    ReassemblyTimeoutv6(FragmentCacheKey<Ipv6Addr>),
    PmtuTimeout(IpVersion),
    /// Timer for IGMP protocol
    IgmpTimer(IgmpTimerId<DeviceId>),
    MldTimer(MldReportDelay<DeviceId>),
}

impl IpLayerTimerId {
    fn new_igmp_timer_id(id: IgmpTimerId<DeviceId>) -> TimerId {
        TimerId(TimerIdInner::IpLayer(IpLayerTimerId::IgmpTimer(id)))
    }

    fn new_mld_timer_id(id: MldReportDelay<DeviceId>) -> TimerId {
        TimerId(TimerIdInner::IpLayer(IpLayerTimerId::MldTimer(id)))
    }

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
        IpLayerTimerId::ReassemblyTimeoutv4(key) => handle_reassembly_timer::<Ipv4, _>(ctx, key),
        IpLayerTimerId::ReassemblyTimeoutv6(key) => handle_reassembly_timer::<Ipv6, _>(ctx, key),
        IpLayerTimerId::PmtuTimeout(IpVersion::V4) => handle_pmtu_timer::<Ipv4, _>(ctx),
        IpLayerTimerId::PmtuTimeout(IpVersion::V6) => handle_pmtu_timer::<Ipv6, _>(ctx),
        IpLayerTimerId::IgmpTimer(timer) => crate::ip::igmp::handle_timeout(ctx, timer),
        IpLayerTimerId::MldTimer(timer) => crate::ip::mld::handle_timeout(ctx, timer),
    }
}

impl<D: EventDispatcher> TimerContext<IgmpTimerId<DeviceId>> for Context<D> {
    fn schedule_timer_instant(
        &mut self,
        time: Self::Instant,
        id: IgmpTimerId<DeviceId>,
    ) -> Option<Self::Instant> {
        self.dispatcher_mut().schedule_timeout_instant(time, IpLayerTimerId::new_igmp_timer_id(id))
    }

    fn cancel_timer(&mut self, id: IgmpTimerId<DeviceId>) -> Option<Self::Instant> {
        self.dispatcher_mut().cancel_timeout(IpLayerTimerId::new_igmp_timer_id(id))
    }

    fn cancel_timers_with<F: FnMut(&IgmpTimerId<DeviceId>) -> bool>(&mut self, mut f: F) {
        self.dispatcher_mut().cancel_timeouts_with(|id| match id {
            TimerId(TimerIdInner::IpLayer(IpLayerTimerId::IgmpTimer(id))) => f(id),
            _ => false,
        })
    }
}

impl<D: EventDispatcher> TimerContext<MldReportDelay<DeviceId>> for Context<D> {
    fn schedule_timer_instant(
        &mut self,
        time: Self::Instant,
        id: MldReportDelay<DeviceId>,
    ) -> Option<Self::Instant> {
        self.dispatcher_mut().schedule_timeout_instant(time, IpLayerTimerId::new_mld_timer_id(id))
    }

    fn cancel_timer(&mut self, id: MldReportDelay<DeviceId>) -> Option<Self::Instant> {
        self.dispatcher_mut().cancel_timeout(IpLayerTimerId::new_mld_timer_id(id))
    }

    fn cancel_timers_with<F: FnMut(&MldReportDelay<DeviceId>) -> bool>(&mut self, mut f: F) {
        self.dispatcher_mut().cancel_timeouts_with(|id| match id {
            TimerId(TimerIdInner::IpLayer(IpLayerTimerId::MldTimer(id))) => f(id),
            _ => false,
        })
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

    fn cancel_timers_with<F: FnMut(&PmtuTimerId<I>) -> bool>(&mut self, f: F) {
        self.dispatcher_mut()
            .cancel_timeouts_with(|id| id == &IpLayerTimerId::new_pmtu_timeout_timer_id::<I>());
    }
}

// TODO(joshlf): Once we support multiple extension headers in IPv6, we will
// need to verify that the callers of this function are still sound. In
// particular, they may accidentally pass a parse_metadata argument which
// corresponds to a single extension header rather than all of the IPv6 headers.

/// Dispatch a received IP packet to the appropriate protocol.
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
/// `dispatch_receive_ip_packet` panics if the protocol is unrecognized and
/// `parse_metadata` is `None`.
/// If an IGMP message is received but it is not coming from a device, i.e.,
/// `device` given is `None`, `dispatch_receive_ip_packet` will also panic.
#[specialize_ip_address]
fn dispatch_receive_ip_packet<B: BufferMut, D: BufferDispatcher<B>, A: IpAddress>(
    ctx: &mut Context<D>,
    device: Option<DeviceId>,
    frame_dst: FrameDestination,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    mut buffer: B,
    parse_metadata: Option<ParseMetadata>,
) {
    increment_counter!(ctx, "dispatch_receive_ip_packet");

    let res = match proto {
        // IPv4-only.
        #[ipv4addr]
        IpProto::Icmp => {
            icmp::receive_icmpv4_packet(ctx, device, src_ip, dst_ip, buffer);
            Ok(())
        }

        // IPv4-only.
        #[ipv4addr]
        IpProto::Igmp => {
            igmp::receive_igmp_packet(
                ctx,
                device.expect("IGMP messages should come from a device"),
                src_ip,
                dst_ip,
                buffer,
            );
            Ok(())
        }

        // IPv6-only.
        #[ipv6addr]
        IpProto::Icmpv6 => {
            icmp::receive_icmpv6_packet(ctx, device, src_ip, dst_ip, buffer);
            Ok(())
        }

        // A value of `IpProto::NoNextHeader` tells us that there is no header whatsoever
        // following the last lower-level header so we stop processing here.
        //
        // IPv6-only.
        #[ipv6addr]
        IpProto::NoNextHeader => Ok(()),

        // IPv4 and IPv6.
        IpProto::Udp => crate::transport::udp::receive_ip_packet(ctx, src_ip, dst_ip, buffer),

        _ => {
            // Undo the parsing of the IP packet header so that the buffer
            // contains the entire original IP packet.
            let meta = parse_metadata.unwrap();
            buffer.undo_parse(meta);
            #[ipv4addr]
            icmp::send_icmpv4_protocol_unreachable(
                ctx,
                device.unwrap(),
                frame_dst,
                src_ip,
                dst_ip,
                proto,
                buffer,
                meta.header_len(),
            );
            #[ipv6addr]
            icmp::send_icmpv6_protocol_unreachable(
                ctx,
                device.unwrap(),
                frame_dst,
                src_ip,
                dst_ip,
                proto,
                buffer,
                meta.header_len(),
            );
            Ok(())
        }
    };

    if let Err(mut buffer) = res {
        // TODO(joshlf): What if we're called from a loopback handler, and
        // device and parse_metadata are None? In other words, what happens if
        // we attempt to send to a loopback port which is unreachable? We will
        // eventually need to restructure the control flow here to handle that
        // case.

        // udp::receive_ip_packet promises to return the buffer in the same
        // state it was in when they were called. Thus, all we have to do is
        // undo the parsing of the IP packet header, and the buffer will be back
        // to containing the entire original IP packet.
        let meta = parse_metadata.unwrap();
        buffer.undo_parse(meta);
        #[ipv4addr]
        icmp::send_icmpv4_port_unreachable(
            ctx,
            device.unwrap(),
            frame_dst,
            src_ip,
            dst_ip,
            buffer,
            meta.header_len(),
        );

        #[ipv6addr]
        icmp::send_icmpv6_port_unreachable(ctx, device.unwrap(), frame_dst, src_ip, dst_ip, buffer);
    }
}

/// Drop a packet and extract some of the fields.
///
/// `drop_packet!` saves the results of the `src_ip()`, `dst_ip()`, `proto()`,
/// and `parse_metadata()` methods, drops the packet, and returns them. This
/// exposes the buffer that the packet was borrowed from so that it can be used
/// directly again.
macro_rules! drop_packet {
    ($packet:expr) => {{
        let src_ip = $packet.src_ip();
        let dst_ip = $packet.dst_ip();
        let proto = $packet.proto();
        let meta = $packet.parse_metadata();
        mem::drop($packet);
        (src_ip, dst_ip, proto, meta)
    }};
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
        let (src_ip, dst_ip, proto, meta) = drop_packet!($packet);
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
    ($ctx:expr, $device:expr, $frame_dst:expr, $buffer:expr, $packet:expr, $ip:ident) => {{
        match process_fragment::<$ip, _, &mut [u8]>($ctx, $packet) {
            // Handle the packet right away since reassembly is not needed.
            FragmentProcessingState::NotNeeded(packet) => {
                trace!("receive_ip_packet: not fragmented");
                // TODO(joshlf):
                // - Check for already-expired TTL?
                let (src_ip, dst_ip, proto, meta) = drop_packet!(packet);
                dispatch_receive_ip_packet(
                    $ctx,
                    Some($device),
                    $frame_dst,
                    src_ip,
                    dst_ip,
                    proto,
                    $buffer,
                    Some(meta),
                );
            }
            // Ready to reassemble a packet.
            FragmentProcessingState::Ready { key, packet_len } => {
                trace!("receive_ip_packet: fragmented, ready for reassembly");
                // Allocate a buffer of `packet_len` bytes.
                let mut buffer = Buf::new(vec![0; packet_len], ..);

                // Attempt to reassemble the packet.
                match reassemble_packet::<$ip, _, _, _>($ctx, &key, buffer.buffer_view_mut()) {
                    // Successfully reassembled the packet, handle it.
                    Ok(packet) => {
                        trace!("receive_ip_packet: fragmented, reassembled packet: {:?}", packet);
                        // TODO(joshlf):
                        // - Check for already-expired TTL?
                        let (src_ip, dst_ip, proto, meta) = drop_packet!(packet);
                        dispatch_receive_ip_packet::<Buf<Vec<u8>>, _, _>(
                            $ctx,
                            Some($device),
                            $frame_dst,
                            src_ip,
                            dst_ip,
                            proto,
                            buffer,
                            Some(meta),
                        );
                    }
                    // TODO(ghanan): Handle reassembly errors.
                    _ => return,
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
        };
    }};
}

/// Receive an IP packet from a device.
///
/// `frame_dst` specifies whether this packet was received in a broadcast or
/// unicast link-layer frame.
#[specialize_ip]
pub(crate) fn receive_ip_packet<B: BufferMut, D: BufferDispatcher<B>, I: Ip>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    mut buffer: B,
) {
    increment_counter!(ctx, "receive_ip_packet");

    trace!("receive_ip_packet({})", device);

    // Snapshot of `buffer`'s current state to revert to if parsing fails.
    // TODO(ghanan): Remove manual restoration of `buffer`'s state once
    //               the packet crate guarantees that if parsing fails,
    //               the buffer's state is left as it was before parsing.
    let p_len = buffer.prefix_len();
    let s_len = buffer.suffix_len();

    let result = buffer.parse_mut::<<I as IpExtByteSlice<&mut [u8]>>::Packet>();

    if result.is_err() {
        let err = result.unwrap_err();

        // Revert `buffer` to it's original state.
        let n_p_len = buffer.prefix_len();
        let n_s_len = buffer.suffix_len();

        if p_len > n_p_len {
            buffer.grow_front(p_len - n_p_len);
        }

        if s_len > n_s_len {
            buffer.grow_back(s_len - n_s_len);
        }

        handle_parse_error(ctx, device, frame_dst, buffer, err);

        return;
    }

    // This will never panic because we check if `result` was an error earlier and return
    // before reaching here. That is, it is guaranteed that when we reach here, `result`
    // is not an error.
    let mut packet = result.unwrap();

    trace!("receive_ip_packet: parsed packet: {:?}", packet);

    // TODO(ghanan): For IPv4 packets, act upon options and for IPv6 packets,
    //               act upon extension headers.

    if I::LOOPBACK_SUBNET.contains(&packet.dst_ip()) {
        // A packet from outside this host was sent with the destination IP of
        // the loopback address, which is illegal. Loopback traffic is handled
        // explicitly in send_ip_packet.
        //
        // TODO(joshlf): Do something with ICMP here?
        debug!("got packet from remote host for loopback address {}", packet.dst_ip());
    } else if deliver(ctx, device, packet.dst_ip()) {
        trace!("receive_ip_packet: delivering locally");

        // Process a potential IPv4 fragment if the destination is this host.
        //
        // We process IPv4 packet reassembly here because, for IPv4, the fragment data
        // is in the header itself so we can handle it right away. For IPv6, it is in
        // an extension header and where it appears in the extension header determines
        // whether or not a node processes it.
        //
        // For IPv6, we need to process extension headers in the order they appear in
        // the header. With some extension headers, we do not proceed to the next header,
        // and do some action immediately. For example, say we have an IPv6 packet with
        // two extension headers (routing extension header before a fragment extension
        // header). Until we get to the final destination node in the routing header, we
        // would need to reroute the packet to the next destination without reassembling.
        // Once the packet gets to the last destination in the routing header, that node
        // will process the fragment extension header and handle reassembly.
        //
        // Note, the `process_fragment` function (which is called by the `process_fragment!`
        // macro) could panic if the packet does not have fragment data. However, we are
        // guaranteed that it will not panic for an IPv4 packet because the fragment data
        // is in the fixed header so it is always present (even if the fragment data has
        // values that implies that the packet is not fragmented).
        #[ipv4]
        process_fragment!(ctx, device, frame_dst, buffer, packet, Ipv4);

        #[ipv6]
        {
            // For IPv6, we need to make sure the destination address is not actually a
            // tentative address we are performing NDP's Duplicate Address Detection on.
            //
            // As per RFC 4862 section 5.4:
            // An address on which the Duplicate Address Detection procedure is
            // applied is said to be tentative until the procedure has completed
            // successfully.  A tentative address is not considered "assigned to an
            // interface" in the traditional sense.  That is, the interface must
            // accept Neighbor Solicitation and Advertisement messages containing
            // the tentative address in the Target Address field, but processes such
            // packets differently from those whose Target Address matches an
            // address assigned to the interface.  Other packets addressed to the
            // tentative address should be silently discarded.  Note that the "other
            // packets" include Neighbor Solicitation and Advertisement messages
            // that have the tentative (i.e., unicast) address as the IP destination
            // address and contain the tentative address in the Target Address
            // field.  Such a case should not happen in normal operation, though,
            // since these messages are multicasted in the Duplicate Address
            // Detection procedure.
            //
            // That is, we accept no packets destined to a tentative address. NS and NA
            // packets should be addressed to a multicast address that we would have
            // joined during DAD so that we can receive those packets.
            //
            // Here we check to see if the packet's destination address is the IPv6 address
            // assigned to the device. If it is, we check to see if it is tentative. If the
            // destination address is not the address assigned to the device, or it is not
            // tentative, we are okay to proceed; else, we drop the packet.
            if crate::device::is_addr_tentative_on_device(ctx, packet.dst_ip(), device) {
                // Silently drop as per RFC 4862 section 5.4
                trace!(
                    "receive_ip_packet: Dropping packet as it is destined for a tentative address"
                );
                return;
            }

            // Handle extension headers first.
            match ipv6::handle_extension_headers(ctx, device, frame_dst, &packet, true) {
                Ipv6PacketAction::Discard => {
                    trace!("receive_ip_packet: handled IPv6 extension headers: discarding packet");
                }
                Ipv6PacketAction::Continue => {
                    trace!("receive_ip_packet: handled IPv6 extension headers: dispatching packet");

                    // TODO(joshlf):
                    // - Do something with ICMP if we don't have a handler for that protocol?
                    // - Check for already-expired TTL?
                    let (src_ip, dst_ip, proto, meta) = drop_packet!(packet);
                    dispatch_receive_ip_packet(
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
                        "receive_ip_packet: handled IPv6 extension headers: handling fragmented packet"
                    );

                    // Note, the `process_fragment` function (which is called by the
                    // `process_fragment!` macro) could panic if the packet does not
                    // have fragment data. However, we are guaranteed that it will not
                    // panic for an IPv6 packet because the fragment data is in an
                    // (optional) fragment extension header which we attempt to handle
                    // by calling `ipv6::handle_extension_headers`. We will only end up
                    // here if its return value is `Ipv6PacketAction::ProcessFragment`
                    // which is only posisble when the packet has the fragment extension
                    // header (even if the fragment data has values that implies that
                    // the packet is not fragmented).
                    //
                    // TODO(ghanan): Handle extension headers again since there could be
                    //               some more in a reassembled packet (after the
                    //               fragment header).
                    process_fragment!(ctx, device, frame_dst, buffer, packet, Ipv6);
                }
            }
        }
    } else if let Some(dest) = forward(ctx, device, packet.dst_ip()) {
        let ttl = packet.ttl();
        if ttl > 1 {
            trace!("receive_ip_packet: forwarding");

            #[ipv6]
            // Handle extension headers first.
            match ipv6::handle_extension_headers(ctx, device, frame_dst, &packet, false) {
                Ipv6PacketAction::Discard => {
                    trace!("receive_ip_packet: handled IPv6 extension headers: discarding packet");
                    return;
                }
                Ipv6PacketAction::Continue => {
                    trace!("receive_ip_packet: handled IPv6 extension headers: forwarding packet");
                }
                Ipv6PacketAction::ProcessFragment => unreachable!("When forwarding packets, we should only ever look at the hop by hop options extension header (if present)"),
            }

            packet.set_ttl(ttl - 1);
            let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
            if let Err(buffer) =
                crate::device::send_ip_frame(ctx, dest.device, dest.next_hop, buffer)
            {
                #[specialize_ip_address]
                fn send_packet_too_big<B: BufferMut, D: BufferDispatcher<B>, A: IpAddress>(
                    ctx: &mut Context<D>,
                    device: DeviceId,
                    frame_dst: FrameDestination,
                    src_ip: A,
                    dst_ip: A,
                    proto: IpProto,
                    mtu: u32,
                    original_packet: B,
                    header_len: usize,
                ) {
                    #[ipv6addr]
                    crate::ip::icmp::send_icmpv6_packet_too_big(
                        ctx,
                        device,
                        frame_dst,
                        src_ip,
                        dst_ip,
                        proto,
                        mtu,
                        original_packet,
                        header_len,
                    );
                }

                debug!("failed to forward IP packet: MTU exceeded");
                trace!("receive_ip_packet: Sending ICMPv6 Packet Too Big");
                // TODO(joshlf): Increment the TTL since we just decremented it.
                // The fact that we don't do this is technically a violation of
                // the ICMP spec (we're not encapsulating the original packet
                // that caused the issue, but a slightly modified version of
                // it), but it's not that big of a deal because it won't affect
                // the sender's ability to figure out the minimum path MTU. This
                // may break other logic, though, so we should still fix it
                // eventually.
                let mtu = crate::device::get_mtu(ctx.state(), device);
                send_packet_too_big(
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
            debug!("received IP packet dropped due to expired TTL");

            // TTL is 0 or would become 0 after decrement; see "TTL" section,
            // https://tools.ietf.org/html/rfc791#page-14
            let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
            #[ipv4]
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
            #[ipv6]
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
    } else {
        let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
        debug!("received IP packet with no known route to destination {}", dst_ip);

        #[ipv4]
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

        #[ipv6]
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
}

/// Get the local address of the interface that will be used to route to a
/// remote address.
///
/// `local_address_for_remote` looks up the route to `remote`. If one is found,
/// it returns the IP address of the interface specified by the route, or `None`
/// if the interface has no IP address.
pub(crate) fn local_address_for_remote<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
    remote: A,
) -> Option<A> {
    let route = lookup_route(ctx, remote)?;
    crate::device::get_ip_addr_subnet(ctx, route.device).map(AddrSubnet::into_addr)
}

/// Handle an IP parsing error, `err`.
#[specialize_ip]
fn handle_parse_error<B: BufferMut, D: BufferDispatcher<B>, I: Ip>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    original_packet: B,
    err: IpParseError<I>,
) {
    match err {
        // Conditionally send an ICMP response if we encountered a parameter
        // problem error when parsing an IP packet. Note, we do not always send
        // back an ICMP response as it can be used as an attack vector for DDoS
        // attacks. We only send back an ICMP response if the RFC requires
        // that we MUST send one, as noted by `must_send_icmp` and `action`.
        IpParseError::ParameterProblem {
            src_ip,
            dst_ip,
            code,
            pointer,
            must_send_icmp,
            header_len,
            action,
        } => {
            if action.should_send_icmp(&dst_ip) && must_send_icmp {
                #[ipv4]
                {
                    // This should never return `true` for IPv4.
                    assert!(!action.should_send_icmp_to_multicast());

                    if should_send_icmpv4_error(frame_dst, src_ip, dst_ip) {
                        send_icmpv4_parameter_problem(
                            ctx,
                            device,
                            src_ip,
                            dst_ip,
                            code,
                            Icmpv4ParameterProblem::new(pointer),
                            original_packet,
                            header_len,
                        );
                    }
                }

                #[ipv6]
                {
                    // Some IPv6 parsing errors may require us to send an
                    // ICMP response even if the original packet's destination
                    // was a multicast (as defined by RFC 4443 section 2.4.e).
                    // `action.should_send_icmp_to_multicast()` should return
                    // `true` if such an exception applies.
                    if should_send_icmpv6_error(
                        frame_dst,
                        src_ip,
                        dst_ip,
                        action.should_send_icmp_to_multicast(),
                    ) {
                        send_icmpv6_parameter_problem(
                            ctx,
                            device,
                            src_ip,
                            dst_ip,
                            code,
                            Icmpv6ParameterProblem::new(pointer),
                            original_packet,
                            header_len,
                        );
                    }
                }
            }
        }
        // TODO(joshlf): Do something with ICMP here? If not, then just turn
        // this match into an if let.
        _ => {}
    }
}

// Should we deliver this packet locally?
// deliver returns true if:
// - dst_ip is equal to the address set on the device
// - dst_ip is equal to the broadcast address of the subnet set on the device
// - dst_ip is equal to the global broadcast address
// - dst_ip is equal to a mutlicast group that `device` joined
#[specialize_ip_address]
fn deliver<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device: DeviceId,
    dst_ip: A,
) -> bool {
    // TODO(joshlf):
    // - This implements a strict host model (in which we only accept packets
    //   which are addressed to the device over which they were received). This
    //   is the easiest to implement for the time being, but we should actually
    //   put real thought into what our host model should be (NET-1011).
    #[ipv4addr]
    #[allow(clippy::or_fun_call)] // for the .unwrap_or(dst_ip.is_global_broadcast())
    return crate::device::get_ip_addr_subnet(ctx, device)
        .map(AddrSubnet::into_addr_subnet)
        .map(|(addr, subnet)| dst_ip == addr || dst_ip == subnet.broadcast())
        .unwrap_or(dst_ip.is_global_broadcast())
        || MulticastAddr::new(dst_ip)
            .map(|a| crate::device::is_in_ip_multicast(ctx, device, a))
            .unwrap_or(false);

    // TODO(brunodalbo):
    // Along with the host model described above, we need to be able to have
    // multiple IPs per interface, it becomes imperative for IPv6.
    //
    // Also, only when we're a router we should accept
    // Ipv6::ALL_ROUTERS_LINK_LOCAL.
    #[ipv6addr]
    return crate::device::get_ip_addr_subnet(ctx, device)
        .map(AddrSubnet::into_addr_subnet)
        .map(|(addr, _): (Ipv6Addr, _)| dst_ip == addr)
        .unwrap_or(false)
        || crate::device::get_ipv6_link_local_addr(ctx, device).get() == dst_ip
        || dst_ip == Ipv6::ALL_NODES_LINK_LOCAL_ADDRESS
        || MulticastAddr::new(dst_ip)
            .map(|a| crate::device::is_in_ip_multicast(ctx, device, a))
            .unwrap_or(false);
}

// Should we forward this packet, and if so, to whom?
fn forward<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: DeviceId,
    dst_ip: A,
) -> Option<Destination<A>> {
    trace!("ip::forward: destination ip = {:?}", dst_ip);

    // Is this netstack configured to foward packets not destined for it?
    if !crate::ip::is_forwarding_enabled::<_, A::Version>(ctx) {
        trace!("ip::forward: can't forward because netstack not configured to do so");
        return None;
    }

    // Does the interface the packet arrived on have routing enabled?
    if !crate::device::is_forwarding_enabled::<_, A::Version>(ctx, device_id) {
        trace!("ip::forward: can't forward because packet arrived on an interface without routing enabled; device = {:?}", device_id);
        return None;
    }

    match lookup_route(ctx, dst_ip) {
        Some(dest) => {
            trace!("ip::forward: found a valid route to {:?} -> {:?}", dst_ip, dest);
            Some(dest)
        }
        _ => {
            trace!("ip::forward: can't forward because no valid route exists to {:?}", dst_ip);
            None
        }
    }
}

// Look up the route to a host.
pub(crate) fn lookup_route<A: IpAddress, D: EventDispatcher>(
    ctx: &Context<D>,
    dst_ip: A,
) -> Option<Destination<A>> {
    get_state_inner::<A::Version, _>(ctx.state()).table.lookup(dst_ip)
}

/// Add a route to the forwarding table, returning `Err` if the subnet
/// is already in the table.
pub(crate) fn add_route<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
    next_hop: A,
) -> Result<(), ExistsError> {
    get_state_inner_mut::<A::Version, _>(ctx.state_mut()).table.add_route(subnet, next_hop)
}

/// Add a device route to the forwarding table, returning `Err` if the
/// subnet is already in the table.
pub(crate) fn add_device_route<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
    device: DeviceId,
) -> Result<(), ExistsError> {
    get_state_inner_mut::<A::Version, _>(ctx.state_mut()).table.add_device_route(subnet, device)
}

/// Delete a route from the forwarding table, returning `Err` if no
/// route was found to be deleted.
pub(crate) fn del_device_route<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
) -> Result<(), NotFoundError> {
    get_state_inner_mut::<A::Version, _>(ctx.state_mut()).table.del_route(subnet)
}

/// Return all the routes for the provided `IpAddress` type
pub(crate) fn iter_all_routes<D: EventDispatcher, A: IpAddress>(
    ctx: &Context<D>,
) -> std::slice::Iter<Entry<A>> {
    get_state_inner::<A::Version, _>(ctx.state()).table.iter_installed()
}

/// Is this one of our local addresses?
///
/// `is_local_addr` returns whether `addr` is the address associated with one of
/// our local interfaces.
pub(crate) fn is_local_addr<D: EventDispatcher, A: IpAddress>(ctx: &Context<D>, addr: A) -> bool {
    log_unimplemented!(false, "ip::is_local_addr: not implemented")
}

/// Send an IP packet to a remote host.
///
/// `send_ip_packet` accepts a destination IP address, a protocol, and a
/// callback. It computes the routing information, and invokes the callback with
/// the computed destination address. The callback returns a
/// `SerializationRequest`, which is serialized in a new IP packet and sent.
pub(crate) fn send_ip_packet<B: BufferMut, D: BufferDispatcher<B>, A, S, F>(
    ctx: &mut Context<D>,
    dst_ip: A,
    proto: IpProto,
    get_body: F,
) -> Result<(), S>
where
    A: IpAddress,
    S: Serializer<Buffer = B>,
    F: FnOnce(A) -> S,
{
    trace!("send_ip_packet({}, {})", dst_ip, proto);
    increment_counter!(ctx, "send_ip_packet");
    if A::Version::LOOPBACK_SUBNET.contains(&dst_ip) {
        increment_counter!(ctx, "send_ip_packet::loopback");

        // TODO(joshlf): Currently, we have no way of representing the loopback
        // device as a DeviceId. We will need to fix that eventually.

        // TODO(joshlf): Currently, we serialize using the normal Serializer
        // functionality. I wonder if, in the case of delivering to loopback, we
        // can do something more efficient?
        let mut buffer =
            get_body(A::Version::LOOPBACK_ADDRESS).serialize_vec_outer().map_err(|(_, ser)| ser)?;
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
            Either::A(buffer) => dispatch_receive_ip_packet(
                ctx,
                None,
                FrameDestination::Unicast,
                A::Version::LOOPBACK_ADDRESS,
                dst_ip,
                proto,
                buffer,
                None,
            ),
            Either::B(buffer) => dispatch_receive_ip_packet::<Buf<Vec<u8>>, _, _>(
                ctx,
                None,
                FrameDestination::Unicast,
                A::Version::LOOPBACK_ADDRESS,
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
        let src_ip = crate::device::get_ip_addr_subnet(ctx, dest.device)
            .expect("IP device route set for device without IP address")
            .addr();
        send_ip_packet_from_device(
            ctx,
            dest.device,
            src_ip,
            dst_ip,
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

/// Send an IP packet to a remote host from a specific source address.
///
/// `send_ip_packet_from` accepts a source and destination IP address and a
/// `SerializationRequest`. It computes the routing information and serializes
/// the request in a new IP packet and sends it.
///
/// `send_ip_packet_from` computes a route to the destination with the
/// restriction that the packet must originate from the source address, and must
/// egress over the interface associated with that source address. If this
/// restriction cannot be met, a "no route to host" error is returned.
pub(crate) fn send_ip_packet_from<B: BufferMut, D: BufferDispatcher<B>, A, S>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    body: S,
) -> Result<(), S>
where
    A: IpAddress,
    S: Serializer<Buffer = B>,
{
    // TODO(joshlf): Figure out how to compute a route with the restrictions
    // mentioned in the doc comment.
    log_unimplemented!(Ok(()), "ip::send_ip_packet_from: not implemented")
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
    next_hop: A,
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
    // therefore, no packet should have a source ip set to a tentative address.
    debug_assert!(!crate::device::is_addr_tentative_on_device(ctx, src_ip, device));

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

impl<D: EventDispatcher> FrameContext<EmptyBuf, IgmpPacketMetadata<DeviceId>> for Context<D> {
    fn send_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        meta: IgmpPacketMetadata<DeviceId>,
        body: S,
    ) -> Result<(), S> {
        send_igmp_packet(self, meta.device, meta.src_ip, meta.dst_ip, body)
    }
}

impl<D: EventDispatcher> FrameContext<EmptyBuf, MldFrameMetadata<DeviceId>> for Context<D> {
    fn send_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        meta: MldFrameMetadata<DeviceId>,
        body: S,
    ) -> Result<(), S> {
        crate::device::send_ip_frame(self, meta.device, meta.local_ip, body)
    }
}

impl<D: EventDispatcher> IgmpContext for Context<D> {
    fn get_ip_addr_subnet(&self, device: DeviceId) -> Option<AddrSubnet<Ipv4Addr>> {
        crate::device::get_ip_addr_subnet(self, device)
    }
}

impl<D: EventDispatcher> MldContext for Context<D> {
    fn get_ip_addr_subnet(&self, device: DeviceId) -> Option<AddrSubnet<Ipv6Addr>> {
        crate::device::get_ip_addr_subnet(self, device)
    }
}

/// Send an IGMP packet.
///
/// IGMP packet must have a TTL of 1 and have RouterAlert set.
fn send_igmp_packet<B: BufferMut, D: BufferDispatcher<B>, S>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    body: S,
) -> Result<(), S>
where
    S: Serializer<Buffer = B>,
{
    let ipv4 = Ipv4PacketBuilder::new(src_ip, dst_ip, 1, IpProto::Igmp);
    let with_options = match Ipv4PacketBuilderWithOptions::new(
        ipv4,
        &[Ipv4Option { copied: true, data: crate::ip::Ipv4OptionData::RouterAlert { data: 0 } }],
    ) {
        None => return Err(body),
        Some(builder) => builder,
    };

    let body = body.encapsulate(with_options);
    crate::device::send_ip_frame(ctx, device, dst_ip, body).map_err(|ser| ser.into_inner())
}

impl<D: EventDispatcher> StateContext<(), Icmpv4State> for Context<D> {
    fn get_state(&self, _id: ()) -> &Icmpv4State {
        &self.state().ip.icmp.v4
    }

    fn get_state_mut(&mut self, _id: ()) -> &mut Icmpv4State {
        &mut self.state_mut().ip.icmp.v4
    }
}

impl<D: EventDispatcher> StateContext<(), Icmpv6State> for Context<D> {
    fn get_state(&self, _id: ()) -> &Icmpv6State {
        &self.state().ip.icmp.v6
    }

    fn get_state_mut(&mut self, _id: ()) -> &mut Icmpv6State {
        &mut self.state_mut().ip.icmp.v6
    }
}

impl<I: IcmpIpExt, B: BufferMut, D: BufferDispatcher<B>> IcmpContext<I, B> for Context<D> {
    fn send_icmp_reply<S: Serializer<Buffer = B>, F: FnOnce(I::Addr) -> S>(
        &mut self,
        device: Option<DeviceId>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        get_body: F,
    ) -> Result<(), S> {
        trace!("send_icmp_reply({:?}, {}, {})", device, src_ip, dst_ip);
        self.increment_counter("send_icmp_reply");

        // TODO(joshlf): Use `dst_ip` for anything?
        send_ip_packet(self, src_ip, I::IP_PROTO, get_body)
    }

    fn send_icmp_error_message<S: Serializer<Buffer = B>, F: FnOnce(I::Addr) -> S>(
        &mut self,
        device: DeviceId,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        get_body: F,
        ip_mtu: Option<u32>,
    ) -> Result<(), S> {
        trace!("send_icmp_error_message({}, {}, {}, {:?})", device, src_ip, dst_ip, ip_mtu);
        self.increment_counter("send_icmp_error_message");

        // TODO(joshlf): Come up with rules for when to send ICMP responses.
        // E.g., should we send a response over a different device than the
        // device that the original packet ingressed over? We'll probably want
        // to consult BCP 38 (aka RFC 2827) and RFC 3704.

        if let Some(route) = lookup_route(self, src_ip) {
            if let Some(local_ip) =
                crate::device::get_ip_addr_subnet(self, route.device).map(AddrSubnet::into_addr)
            {
                send_ip_packet_from_device(
                    self,
                    route.device,
                    local_ip,
                    src_ip,
                    route.next_hop,
                    I::IP_PROTO,
                    get_body(local_ip),
                    ip_mtu,
                )?;
            } else {
                log_unimplemented!(
                    (),
                    "Sending ICMP over unnumbered device {} is unimplemented",
                    route.device
                );

                // TODO(joshlf): We need a general-purpose mechanism for
                // choosing a source address in cases where we're a) acting as a
                // router (and thus sending packets with our own source address,
                // but not as a result of any local application behavior) and,
                // b) sending over an unnumbered device (one without any
                // configured IP address). ICMP is the notable use case. Most
                // likely, we will want to pick the IP address of a different
                // local device. See for an explanation of why we might have
                // this setup:
                // https://www.cisco.com/c/en/us/support/docs/ip/hot-standby-router-protocol-hsrp/13786-20.html#unnumbered_iface
            }
        } else {
            debug!("Can't send ICMP response to {}: no route to host", src_ip);
        }

        Ok(())
    }
}

/// Is `ctx` configured to forward packets?
#[specialize_ip]
pub(crate) fn is_forwarding_enabled<D: EventDispatcher, I: Ip>(ctx: &Context<D>) -> bool {
    get_state_inner::<I, _>(ctx.state()).forward
}

/// Get the hop limit for new IP packets that will be sent out from `device`.
#[specialize_ip]
fn get_hop_limit<D: EventDispatcher, I: Ip>(ctx: &Context<D>, device: DeviceId) -> u8 {
    // TODO(ghanan): Should IPv4 packets use the same TTL value
    //               as IPv6 packets? Currently for the IPv6 case,
    //               we get the default hop limit from the device
    //               state which can be updated by NDP's Router
    //               Advertisement.

    #[ipv4]
    return DEFAULT_TTL;

    // This value can be updated by NDP's Router Advertisements.
    #[ipv6]
    return crate::device::get_ipv6_hop_limit(ctx, device).get();
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::convert::TryFrom;
    use std::num::NonZeroU16;

    use byteorder::{ByteOrder, NetworkEndian};
    use net_types::ip::{Ipv4Addr, Ipv6Addr};
    use packet::{Buf, ParseBuffer};
    use rand::Rng;

    use crate::device::{set_forwarding_enabled, FrameDestination};
    use crate::ip::path_mtu::{get_pmtu, min_mtu};
    use crate::testutil::*;
    use crate::wire::ethernet::EthernetFrame;
    use crate::wire::icmp::{
        IcmpDestUnreachable, IcmpEchoRequest, IcmpPacketBuilder, IcmpParseArgs, IcmpUnusedCode,
        Icmpv4DestUnreachableCode, Icmpv6Packet, Icmpv6PacketTooBig, Icmpv6ParameterProblemCode,
        MessageBody,
    };
    use crate::wire::ipv4::Ipv4PacketBuilder;
    use crate::wire::ipv6::ext_hdrs::ExtensionHeaderOptionAction;
    use crate::wire::ipv6::Ipv6PacketBuilder;
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
    ) {
        // Check the ICMP that bob attempted to send to alice
        let device_frames = ctx.dispatcher.frames_sent().clone();
        assert!(!device_frames.is_empty());
        let mut buffer = Buf::new(device_frames[0].1.as_slice(), ..);
        let frame = buffer.parse::<EthernetFrame<_>>().unwrap();
        let packet = buffer.parse::<<Ipv6 as IpExtByteSlice<&[u8]>>::Packet>().unwrap();
        let (src_ip, dst_ip, proto, _) = drop_packet!(packet);
        assert_eq!(dst_ip, DUMMY_CONFIG_V6.remote_ip);
        assert_eq!(src_ip, DUMMY_CONFIG_V6.local_ip);
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
            Ipv4Addr::new([192, 168, 0, 100]),
            DUMMY_CONFIG_V4.local_ip,
            10,
            IpProto::Udp,
        )
    }

    /// Process an IP fragment depending on the `Ip` `process_ip_fragment` is
    /// specialized with.
    #[specialize_ip]
    fn process_ip_fragment<I: Ip, D: EventDispatcher>(
        ctx: &mut Context<D>,
        device: DeviceId,
        fragment_id: u16,
        fragment_offset: u8,
        fragment_count: u8,
    ) {
        #[ipv4]
        process_ipv4_fragment(ctx, device, fragment_id, fragment_offset, fragment_count);

        #[ipv6]
        process_ipv6_fragment(ctx, device, fragment_id, fragment_offset, fragment_count);
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
        let mut buffer =
            Buf::new(body, ..).encapsulate(builder).serialize_vec_outer().unwrap().into_inner();
        receive_ip_packet::<_, _, Ipv4>(ctx, device, FrameDestination::Unicast, buffer);
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
        bytes[8..24]
            .copy_from_slice(&[16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31]);
        bytes[24..40].copy_from_slice(DUMMY_CONFIG_V6.local_ip.bytes());
        bytes[40] = IpProto::Udp.into();
        bytes[42] = fragment_offset >> 5;
        bytes[43] = ((fragment_offset & 0x1F) << 3) | if m_flag { 1 } else { 0 };
        NetworkEndian::write_u32(&mut bytes[44..48], fragment_id as u32);
        bytes.extend(fragment_offset * 8..fragment_offset * 8 + 8);
        let payload_len = (bytes.len() - 40) as u16;
        NetworkEndian::write_u16(&mut bytes[4..6], payload_len);
        let mut buffer = Buf::new(bytes, ..);
        receive_ip_packet::<_, _, Ipv6>(ctx, device, FrameDestination::Unicast, buffer);
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
        let mut buf = Buf::new(bytes, ..);

        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, FrameDestination::Unicast, buf);

        assert_eq!(get_counter_val(&mut ctx, "send_icmpv4_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);
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
        let mut buf = Buf::new(bytes, ..);
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, FrameDestination::Unicast, buf);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::ErroneousHeaderField,
            42,
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

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::SkipAndContinue,
            false,
        );
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);

        //
        // Test with unrecognized option type set with
        // action = discard.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacket,
            false,
        );
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp
        // where dest addr is a unicast addr.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendICMP,
            false,
        );
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
        );

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp
        // where dest addr is a multicast addr.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendICMP,
            true,
        );
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
        );

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp if not multicast addr
        // where dest addr is a unicast addr.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast,
            false,
        );
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
        );

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp if not multicast addr
        // but dest addr is a multicast addr.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast,
            true,
        );
        // Do not expect an ICMP response for this packet
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);

        //
        // None of our tests should have sent an icmpv4 packet, or dispatched an ip packet
        // after the first.
        //

        assert_eq!(get_counter_val(&mut ctx, "send_icmpv4_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);
    }

    fn test_ip_packet_reassembly_not_needed<I: Ip>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(get_dummy_config::<I::Addr>())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);

        //
        // Test that a non fragmented packet gets dispatched right away.
        //

        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 0, 1);

        // Make sure the packet got dispatched.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);
    }

    #[test]
    fn test_ipv4_packet_reassembly_not_needed() {
        test_ip_packet_reassembly_not_needed::<Ipv4>();
    }

    #[test]
    fn test_ipv6_packet_reassembly_not_needed() {
        test_ip_packet_reassembly_not_needed::<Ipv6>();
    }

    fn test_ip_packet_reassembly<I: Ip>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(get_dummy_config::<I::Addr>())
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
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);

        // Process fragment #2
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 2, 3);

        // Make sure the packet finally got dispatched now that the final
        // fragment has been 'received'.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);
    }

    #[test]
    fn test_ipv4_packet_reassembly() {
        test_ip_packet_reassembly::<Ipv4>();
    }

    #[test]
    fn test_ipv6_packet_reassembly() {
        test_ip_packet_reassembly::<Ipv6>();
    }

    fn test_ip_packet_reassembly_with_packets_arriving_out_of_order<I: Ip>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(get_dummy_config::<I::Addr>())
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
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);

        // Process a packet that does not require reassembly (packet #2, fragment #0).
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_2, 0, 1);

        // Make packet #1 got dispatched since it didn't need reassembly.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);

        // Process packet #0, fragment #2
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_0, 2, 3);

        // Make sure no other packets got dispatched yet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);

        // Process packet #0, fragment #0
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_0, 0, 3);

        // Make sure that packet #0 finally got dispatched now that the final
        // fragment has been 'received'.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 2);

        // Process packet #1, fragment #1
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id_1, 1, 3);

        // Make sure the packet finally got dispatched now that the final
        // fragment has been 'received'.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 3);
    }

    #[test]
    fn test_ipv4_packet_reassembly_with_packets_arriving_out_of_order() {
        test_ip_packet_reassembly_with_packets_arriving_out_of_order::<Ipv4>();
    }

    #[test]
    fn test_ipv6_packet_reassembly_with_packets_arriving_out_of_order() {
        test_ip_packet_reassembly_with_packets_arriving_out_of_order::<Ipv6>();
    }

    fn test_ip_packet_reassembly_timeout<I: Ip>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(get_dummy_config::<I::Addr>())
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
        assert!(trigger_next_timer(&mut ctx));

        // Make sure no other times exist..
        assert_eq!(ctx.dispatcher.timer_events().count(), 0);

        // Process fragment #2
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 2, 3);

        // Make sure no packets got dispatched yet since even
        // though we technically received all the fragments, this fragment
        // (#2) arrived too late and the reassembly timeout was triggered,
        // causing the prior fragment data to be discarded.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);
    }

    #[test]
    fn test_ipv4_packet_reassembly_timeout() {
        test_ip_packet_reassembly_timeout::<Ipv4>();
    }

    #[test]
    fn test_ipv6_packet_reassembly_timeout() {
        test_ip_packet_reassembly_timeout::<Ipv6>();
    }

    fn test_ip_reassembly_only_at_destination_host<I: Ip>() {
        // Create a new network with two parties (alice & bob) and
        // enable IP packet forwarding for alice.
        let a = "alice";
        let b = "bob";
        let dummy_config = get_dummy_config::<I::Addr>();
        let mut state_builder = StackStateBuilder::default();
        state_builder.ip_builder().forward(true);
        let mut ndp_configs = crate::device::ndp::NdpConfigurations::default();
        ndp_configs.set_dup_addr_detect_transmits(None);
        ndp_configs.set_max_router_solicitations(None);
        state_builder.device_builder().set_default_ndp_configs(ndp_configs);
        let device = DeviceId::new_ethernet(0);
        let mut alice = DummyEventDispatcherBuilder::from_config(dummy_config.swap())
            .build_with(state_builder, DummyEventDispatcher::default());
        set_forwarding_enabled::<_, I>(&mut alice, device, true);
        let bob = DummyEventDispatcherBuilder::from_config(dummy_config).build();
        let contexts = vec![(a.clone(), alice), (b.clone(), bob)].into_iter();
        let mut net = DummyNetwork::new(contexts, move |net, device_id| {
            if *net == a {
                (b.clone(), device, None)
            } else {
                (a.clone(), device, None)
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
        assert_eq!(get_counter_val(&mut net.context("alice"), "dispatch_receive_ip_packet"), 0);
        assert_eq!(get_counter_val(&mut net.context("bob"), "dispatch_receive_ip_packet"), 0);

        // Process fragment #2
        process_ip_fragment::<I, _>(&mut net.context("alice"), device, fragment_id, 2, 3);
        assert!(!net.step().is_idle());

        // Make sure the packet finally got dispatched now that the final
        // fragment has been received by bob.
        assert_eq!(get_counter_val(&mut net.context("alice"), "dispatch_receive_ip_packet"), 0);
        assert_eq!(get_counter_val(&mut net.context("bob"), "dispatch_receive_ip_packet"), 1);

        // Make sure there are no more events.
        assert!(net.step().is_idle());
    }

    #[test]
    fn test_ipv4_reassembly_only_at_destination_host() {
        test_ip_reassembly_only_at_destination_host::<Ipv4>();
    }

    #[test]
    fn test_ipv6_reassembly_only_at_destination_host() {
        test_ip_reassembly_only_at_destination_host::<Ipv6>();
    }

    #[test]
    fn test_ipv6_packet_too_big() {
        //
        // Test sending an IPv6 Packet Too Big Error when receiving a packet that is
        // too big to be forwarded when it isn't destined for the node it arrived at.
        //

        let dummy_config = get_dummy_config::<Ipv6Addr>();
        let mut state_builder = StackStateBuilder::default();
        state_builder.ip_builder().forward(true);
        let mut ndp_configs = crate::device::ndp::NdpConfigurations::default();
        ndp_configs.set_dup_addr_detect_transmits(None);
        ndp_configs.set_max_router_solicitations(None);
        state_builder.device_builder().set_default_ndp_configs(ndp_configs);
        let mut dispatcher_builder = DummyEventDispatcherBuilder::from_config(dummy_config.clone());
        let extra_ip = Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 100]);
        let extra_mac = Mac::new([13, 14, 15, 16, 17, 18]);
        dispatcher_builder.add_ndp_table_entry(0, extra_ip, extra_mac);
        dispatcher_builder.add_ndp_table_entry(0, extra_mac.to_ipv6_link_local().get(), extra_mac);
        let mut ctx = dispatcher_builder.build_with(state_builder, DummyEventDispatcher::default());
        let device = DeviceId::new_ethernet(0);
        set_forwarding_enabled::<_, Ipv6>(&mut ctx, device, true);
        let frame_dst = FrameDestination::Unicast;

        // Construct an IPv6 packet that is too big for our MTU (MTU = 1280; body itself is 5000).
        // Note, the final packet will be larger because of IP header data.
        let mut rng = new_rng(70812476915813);
        let mut body: Vec<u8> = std::iter::repeat_with(|| rng.gen()).take(5000).collect();

        // Ip packet from some node destined to a remote on this network, arriving locally.
        let mut ipv6_packet_buf = Buf::new(body.clone(), ..)
            .encapsulate(Ipv6PacketBuilder::new(extra_ip, dummy_config.remote_ip, 64, IpProto::Udp))
            .serialize_vec_outer()
            .unwrap();
        // Receive the IP packet.
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, ipv6_packet_buf.clone());

        // Should not have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_packet_too_big"), 1);

        // Should have sent out one frame though.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);

        // Received packet should be a Packet Too Big ICMP error message.
        let mut buf = &ctx.dispatcher.frames_sent()[0].1[..];
        // The original packet's TTL gets decremented so we decrement here
        // to validate the rest of the icmp message body.
        let ipv6_packet_buf_mut: &mut [u8] = ipv6_packet_buf.as_mut();
        ipv6_packet_buf_mut[7] -= 1;
        let (_, _, _, _, message, code) =
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

    fn test_ip_update_pmtu<I: Ip>() {
        //
        // Test receiving a Packet Too Big (IPv6) or Dest Unreachable Fragmentation
        // Required (IPv4) which should update the PMTU if it is less than the current value.
        //

        let dummy_config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        //
        // Update PMTU from None
        //

        let new_mtu1 = min_mtu::<I>() + 100;

        // Create ICMP IP buf
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip,
            dummy_config.local_ip,
            u16::try_from(new_mtu1).unwrap(),
            None,
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);

        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu1
        );

        //
        // Don't update PMTU when current PMTU is less than reported MTU
        //

        let new_mtu2 = min_mtu::<I>() + 200;

        // Create IPv6 ICMPv6 packet too big packet with MTU larger than current PMTU.
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip,
            dummy_config.local_ip,
            u16::try_from(new_mtu2).unwrap(),
            None,
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 2);

        // The PMTU should not have updated to `new_mtu2`
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu1
        );

        //
        // Update PMTU when current PMTU is greater than the reported MTU
        //

        let new_mtu3 = min_mtu::<I>() + 50;

        // Create IPv6 ICMPv6 packet too big packet with MTU smaller than current PMTU.
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip,
            dummy_config.local_ip,
            u16::try_from(new_mtu3).unwrap(),
            None,
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 3);

        // The PMTU should have updated to 1900.
        assert_eq!(
            get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(),
            new_mtu3
        );
    }

    #[test]
    fn test_ipv4_update_pmtu() {
        test_ip_update_pmtu::<Ipv4>();
    }

    #[test]
    fn test_ipv6_update_pmtu() {
        test_ip_update_pmtu::<Ipv6>();
    }

    fn test_ip_update_pmtu_too_low<I: Ip>() {
        //
        // Test receiving a Packet Too Big (IPv6) or Dest Unreachable Fragmentation
        // Required (IPv4) which should not update the PMTU if it is less than the min mtu.
        //

        let dummy_config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        //
        // Update PMTU from None but with a mtu too low.
        //

        let new_mtu1 = min_mtu::<I>() - 1;

        // Create ICMP IP buf
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip,
            dummy_config.local_ip,
            u16::try_from(new_mtu1).unwrap(),
            None,
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);

        assert!(get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).is_none(),);
    }

    #[test]
    fn test_ipv4_update_pmtu_too_low() {
        test_ip_update_pmtu_too_low::<Ipv4>();
    }

    #[test]
    fn test_ipv6_update_pmtu_too_low() {
        test_ip_update_pmtu_too_low::<Ipv6>();
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

        let dummy_config = get_dummy_config::<Ipv4Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        //
        // Update from None
        //

        // Create ICMP IP buf w/ orig packet body len = 500; orig packet len = 520
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip,
            dummy_config.local_ip,
            0, // A 0 value indicates that the source of the
            // ICMP message does not implement RFC 1191.
            create_orig_packet_buf(dummy_config.local_ip, dummy_config.remote_ip, 500).into(),
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, Ipv4>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);

        // Should have decreased PMTU value to the next lower PMTU
        // plateau from `crate::ip::path_mtu::PMTU_PLATEAUS`.
        assert_eq!(get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(), 508);

        //
        // Don't Update when packet size is too small
        //

        // Create ICMP IP buf w/ orig packet body len = 1; orig packet len = 21
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip,
            dummy_config.local_ip,
            0,
            create_orig_packet_buf(dummy_config.local_ip, dummy_config.remote_ip, 1).into(),
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, Ipv4>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 2);

        // Should not have updated PMTU as there is no other valid
        // lower PMTU value.
        assert_eq!(get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(), 508);

        //
        // Update to lower PMTU estimate based on original packet size.
        //

        // Create ICMP IP buf w/ orig packet body len = 60; orig packet len = 80
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip,
            dummy_config.local_ip,
            0,
            create_orig_packet_buf(dummy_config.local_ip, dummy_config.remote_ip, 60).into(),
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, Ipv4>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 3);

        // Should have decreased PMTU value to the next lower PMTU
        // plateau from `crate::ip::path_mtu::PMTU_PLATEAUS`.
        assert_eq!(get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(), 68);

        //
        // Should not update PMTU because the next low PMTU from this original
        // packet size is higher than current PMTU.
        //

        // Create ICMP IP buf w/ orig packet body len = 290; orig packet len = 310
        let packet_buf = create_packet_too_big_buf(
            dummy_config.remote_ip,
            dummy_config.local_ip,
            0, // A 0 value indicates that the source of the
            // ICMP message does not implement RFC 1191.
            create_orig_packet_buf(dummy_config.local_ip, dummy_config.remote_ip, 290).into(),
        );

        // Receive the IP packet.
        receive_ip_packet::<_, _, Ipv4>(&mut ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 4);

        // Should not have updated the PMTU as the current PMTU is lower.
        assert_eq!(get_pmtu(&mut ctx, dummy_config.local_ip, dummy_config.remote_ip).unwrap(), 68);
    }

    #[test]
    fn test_invalid_icmpv4_in_ipv6() {
        let ip_config = get_dummy_config::<Ipv6Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(ip_config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(1);
        let frame_dst = FrameDestination::Unicast;

        let ic_config = get_dummy_config::<Ipv4Addr>();
        let icmp_builder = IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
            ic_config.remote_ip,
            ic_config.local_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(0, 0),
        );

        let ip_builder =
            Ipv6PacketBuilder::new(ip_config.remote_ip, ip_config.local_ip, 64, IpProto::Icmp);

        let mut buf = Buf::new(Vec::new(), ..)
            .encapsulate(icmp_builder)
            .encapsulate(ip_builder)
            .serialize_vec_outer()
            .unwrap();

        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);

        // Should not have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);

        // In IPv6, the next header value (ICMP(v4)) would have been considered
        // unrecognized so an ICMP parameter problem response SHOULD be sent, but
        // the netstack chooses to just drop the packet since we are not
        // required to send the ICMP response.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 0);
    }

    #[test]
    fn test_invalid_icmpv6_in_ipv4() {
        let ip_config = get_dummy_config::<Ipv4Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(ip_config.clone())
            .build::<DummyEventDispatcher>();
        // First possible device id.
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        let ic_config = get_dummy_config::<Ipv6Addr>();
        let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
            ic_config.remote_ip,
            ic_config.local_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(0, 0),
        );

        let ip_builder =
            Ipv4PacketBuilder::new(ip_config.remote_ip, ip_config.local_ip, 64, IpProto::Icmpv6);

        let mut buf = Buf::new(Vec::new(), ..)
            .encapsulate(icmp_builder)
            .encapsulate(ip_builder)
            .serialize_vec_outer()
            .unwrap();

        receive_ip_packet::<_, _, Ipv4>(&mut ctx, device, frame_dst, buf);

        // Should have dispatched the packet but resulted in an ICMP error.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv4_dest_unreachable"), 1);
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        let buf = &ctx.dispatcher.frames_sent()[0].1[..];
        let (_, _, _, _, msg, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, IcmpDestUnreachable, _>(
                buf,
                |_| {},
            )
            .unwrap();
        assert_eq!(code, Icmpv4DestUnreachableCode::DestProtocolUnreachable);
    }

    fn test_joining_leaving_ip_multicast_group<I: Ip>() {
        #[specialize_ip_address]
        fn get_multicast_addr<A: IpAddress>() -> A {
            #[ipv4addr]
            return Ipv4Addr::new([224, 0, 0, 1]);

            #[ipv6addr]
            return Ipv6Addr::new([255, 17, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
        }

        let config = get_dummy_config::<I::Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(config.clone())
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;
        let multi_addr = get_multicast_addr::<I::Addr>();
        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(<I as IpExt>::PacketBuilder::new(
                config.remote_ip,
                multi_addr,
                64,
                IpProto::Udp,
            ))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .into_inner();

        let multi_addr = MulticastAddr::new(multi_addr).unwrap();
        // Should not have dispatched the packet since we are not in the
        // multicast group `multi_addr`.
        assert!(!crate::device::is_in_ip_multicast(&ctx, device, multi_addr));
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);

        // Join the multicast group and receive the packet, we should dispatch it.
        crate::device::join_ip_multicast(&mut ctx, device, multi_addr);
        assert!(crate::device::is_in_ip_multicast(&ctx, device, multi_addr));
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);

        // Leave the multicast group and receive the packet, we should not dispatch it.
        crate::device::leave_ip_multicast(&mut ctx, device, multi_addr);
        assert!(!crate::device::is_in_ip_multicast(&ctx, device, multi_addr));
        receive_ip_packet::<_, _, I>(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);
    }

    #[test]
    fn test_joining_leaving_ipv4_multicast_groups() {
        test_joining_leaving_ip_multicast_group::<Ipv4>();
    }

    #[test]
    fn test_joining_leaving_ipv6_multicast_groups() {
        test_joining_leaving_ip_multicast_group::<Ipv6>();
    }

    #[test]
    #[should_panic]
    fn test_lookup_table_ipv4_loopback_panic() {
        test_lookup_table_address(get_dummy_config::<Ipv4Addr>(), Ipv4::LOOPBACK_ADDRESS);
    }

    #[test]
    #[should_panic]
    fn test_lookup_table_ipv6_loopback_panic() {
        test_lookup_table_address(get_dummy_config::<Ipv6Addr>(), Ipv6::LOOPBACK_ADDRESS);
    }

    #[test]
    fn test_lookup_table_ipv4_unspecified() {
        assert!(test_lookup_table_address(
            get_dummy_config::<Ipv4Addr>(),
            Ipv4::UNSPECIFIED_ADDRESS
        )
        .is_none());
    }

    #[test]
    fn test_lookup_table_ipv6_unspecified() {
        assert!(test_lookup_table_address(
            get_dummy_config::<Ipv6Addr>(),
            Ipv6::UNSPECIFIED_ADDRESS
        )
        .is_none());
    }

    fn test_lookup_table_address<A: IpAddress>(
        cfg: DummyEventDispatcherConfig<A>,
        ip_address: A,
    ) -> Option<Destination<A>> {
        let mut ctx =
            DummyEventDispatcherBuilder::from_config(cfg.clone()).build::<DummyEventDispatcher>();
        lookup_route(&ctx, ip_address)
    }

    #[test]
    fn test_no_dispatch_non_ndp_packets_during_ndp_dad() {
        // Here we make sure we are not dispatching packets destined to a tentative address
        // (that is performing NDP's Duplicate Address Detection (DAD)) -- IPv6 only.

        // We explicitly call `build_with` when building our context below because `build` will
        // set the default NDP parameter DUP_ADDR_DETECT_TRANSMITS to 0 (effectively disabling
        // DAD) so we use our own custom `StackStateBuilder` to set it to the default value
        // of `1` (see `DUP_ADDR_DETECT_TRANSMITS`).
        let config = get_dummy_config::<Ipv6Addr>();
        let mut ctx = DummyEventDispatcherBuilder::from_config(config.clone())
            .build_with(StackStateBuilder::default(), DummyEventDispatcher::default());
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(Ipv6PacketBuilder::new(
                config.remote_ip,
                config.local_ip,
                64,
                IpProto::Udp,
            ))
            .serialize_vec_outer()
            .unwrap()
            .into_inner();

        // Make sure all timers are done (initial DAD to complete on the interface).
        trigger_timers_until(&mut ctx, |_| false);

        // Received packet should have been dispatched.
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);

        // Set the new IP (this should trigger DAD).
        let ip = Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 10]);
        crate::device::set_ip_addr_subnet(&mut ctx, device, AddrSubnet::new(ip, 128).unwrap());

        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(Ipv6PacketBuilder::new(config.remote_ip, ip, 64, IpProto::Udp))
            .serialize_vec_outer()
            .unwrap()
            .into_inner();

        // Received packet should not have been dispatched.
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 1);

        // Make sure all timers are done (DAD to complete on the interface due to new IP).
        trigger_timers_until(&mut ctx, |_| false);

        // Received packet should have been dispatched.
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 2);
    }
}
