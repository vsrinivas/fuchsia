// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Protocol, versions 4 and 6.

#[macro_use]
pub(crate) mod path_mtu;

pub(crate) mod device;
pub(crate) mod forwarding;
pub(crate) mod gmp;
pub mod icmp;
mod integration;
mod ipv6;
pub(crate) mod reassembly;
pub(crate) mod socket;
mod types;

// It's ok to `pub use` rather `pub(crate) use` here because the items in
// `types` which are themselves `pub(crate)` will still not be allowed to be
// re-exported from the root.
pub use self::types::*;

use alloc::vec::Vec;
use core::{
    fmt::{self, Debug, Display, Formatter},
    num::NonZeroU8,
    slice::Iter,
};

use log::{debug, trace};
use net_types::{
    ip::{
        AddrSubnet, Ip, IpAddr, IpAddress, IpVersion, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr,
        Ipv6SourceAddr, Subnet,
    },
    SpecifiedAddr, UnicastAddr, Witness,
};
use nonzero_ext::nonzero;
use packet::{Buf, BufferMut, ParseMetadata, Serializer};
use packet_formats::{
    error::IpParseError,
    ip::{IpPacket, IpProto, Ipv4Proto, Ipv6Proto},
    ipv4::{Ipv4FragmentType, Ipv4Packet, Ipv4PacketBuilder},
    ipv6::{Ipv6Packet, Ipv6PacketBuilder},
};
use specialize_ip_macro::{specialize_ip, specialize_ip_address};

use crate::{
    context::{CounterContext, InstantContext, TimerContext},
    device::{DeviceId, FrameDestination},
    error::{ExistsError, NotFoundError},
    ip::{
        device::state::AddressState,
        forwarding::{Destination, ForwardingTable},
        gmp::igmp::IgmpPacketHandler,
        icmp::{
            BufferIcmpHandler, IcmpHandlerIpExt, IcmpIpExt, IcmpIpTransportContext, IcmpState,
            Icmpv4Error, Icmpv4ErrorCode, Icmpv4ErrorKind, Icmpv4State, Icmpv4StateBuilder,
            Icmpv6ErrorCode, Icmpv6ErrorKind, Icmpv6State, Icmpv6StateBuilder,
            InnerBufferIcmpContext, InnerIcmpContext, ShouldSendIcmpv4ErrorInfo,
            ShouldSendIcmpv6ErrorInfo,
        },
        ipv6::Ipv6PacketAction,
        path_mtu::{PmtuCache, PmtuHandler, PmtuTimerId},
        reassembly::{FragmentCacheKey, FragmentProcessingState, IpPacketFragmentCache},
        socket::{BufferIpSocketHandler, IpSock, IpSockUpdate, IpSocketHandler, Ipv4SocketContext},
    },
    BufferDispatcher, Ctx, EventDispatcher, Instant, StackState, TimerId, TimerIdInner,
};

/// Default IPv4 TTL.
const DEFAULT_TTL: NonZeroU8 = nonzero!(64u8);

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

/// An [`Ip`] extension trait adding functionality specific to the IP layer.
pub trait IpExt: packet_formats::ip::IpExt + IcmpIpExt {
    /// The type used to specify an IP packet's source address in a call to
    /// [`BufferIpTransportContext::receive_ip_packet`].
    ///
    /// For IPv4, this is `Ipv4Addr`. For IPv6, this is [`Ipv6SourceAddr`].
    type RecvSrcAddr: Into<Self::Addr>;
}

impl IpExt for Ipv4 {
    type RecvSrcAddr = Ipv4Addr;
}

impl IpExt for Ipv6 {
    type RecvSrcAddr = Ipv6SourceAddr;
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
pub(crate) trait BufferIpTransportContext<I: IpExt, B: BufferMut, C: IpDeviceIdContext<I> + ?Sized>:
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
        src_ip: I::RecvSrcAddr,
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

impl<I: IpExt, B: BufferMut, C: IpDeviceIdContext<I> + ?Sized> BufferIpTransportContext<I, B, C>
    for ()
{
    fn receive_ip_packet(
        _ctx: &mut C,
        _device: Option<C::DeviceId>,
        _src_ip: I::RecvSrcAddr,
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
pub trait TransportIpContext<I: IpExt>: IpDeviceIdContext<I> + IpSocketHandler<I> {
    /// Is this one of our local addresses, and is it in the assigned state?
    ///
    /// `is_assigned_local_addr` returns whether `addr` is the address
    /// associated with one of our local interfaces and, for IPv6, whether it is
    /// in the "assigned" state.
    fn is_assigned_local_addr(&self, addr: I::Addr) -> bool;
}

/// The execution context provided by the IP layer to transport layer protocols
/// when a buffer is provided.
///
/// `BufferTransportIpContext` is like [`TransportIpContext`], except that it
/// also requires that the context be capable of receiving frames in buffers of
/// type `B`. This is used when a buffer of type `B` is provided to IP, and
/// allows any generated link-layer frames to reuse that buffer rather than
/// needing to always allocate a new one.
pub trait BufferTransportIpContext<I: IpExt, B: BufferMut>:
    TransportIpContext<I> + BufferIpSocketHandler<I, B>
{
}

impl<I: IpExt, B: BufferMut, C: TransportIpContext<I> + BufferIpSocketHandler<I, B>>
    BufferTransportIpContext<I, B> for C
{
}

// TODO(joshlf): With all 256 protocol numbers (minus reserved ones) given their
// own associated type in both traits, running `cargo check` on a 2018 MacBook
// Pro takes over a minute. Eventually - and before we formally publish this as
// a library - we should identify the bottleneck in the compiler and optimize
// it. For the time being, however, we only support protocol numbers that we
// actually use (TCP and UDP).

/// The execution context for IPv4's transport layer.
///
/// `Ipv4TransportLayerContext` defines the [`IpTransportContext`] for each IPv4
/// protocol number. The protocol numbers 1 (ICMP) and 2 (IGMP) are used by the
/// stack itself, and cannot be overridden.
pub(crate) trait Ipv4TransportLayerContext {
    type Proto6: IpTransportContext<Ipv4, Self>;
    type Proto17: IpTransportContext<Ipv4, Self>;
}

/// The execution context for IPv6's transport layer.
///
/// `Ipv6TransportLayerContext` defines the [`IpTransportContext`] for
/// each IPv6 protocol number. The protocol numbers 0 (Hop-by-Hop Options), 58
/// (ICMPv6), 59 (No Next Header), and 60 (Destination Options) are used by the
/// stack itself, and cannot be overridden.
pub(crate) trait Ipv6TransportLayerContext {
    type Proto6: IpTransportContext<Ipv6, Self>;
    type Proto17: IpTransportContext<Ipv6, Self>;
}

impl<D: EventDispatcher> Ipv4TransportLayerContext for Ctx<D> {
    type Proto6 = ();
    type Proto17 = crate::transport::udp::UdpIpTransportContext;
}

impl<D: EventDispatcher> Ipv6TransportLayerContext for Ctx<D> {
    type Proto6 = ();
    type Proto17 = crate::transport::udp::UdpIpTransportContext;
}

impl<I: IpExt, D: EventDispatcher> TransportIpContext<I> for Ctx<D>
where
    Ctx<D>: IpSocketHandler<I>,
{
    fn is_assigned_local_addr(&self, addr: I::Addr) -> bool {
        match addr.to_ip_addr() {
            IpAddr::V4(addr) => crate::ip::device::iter_ipv4_devices(self)
                .any(|(_, state)| state.find_addr(&addr).is_some()),
            IpAddr::V6(addr) => crate::ip::device::iter_ipv6_devices(self).any(|(_, state)| {
                state
                    .find_addr(&addr)
                    .map(|entry| match entry.state {
                        AddressState::Assigned => true,
                        AddressState::Tentative { .. } | AddressState::Deprecated => false,
                    })
                    .unwrap_or(false)
            }),
        }
    }
}

/// An IP device ID.
pub trait IpDeviceId: Copy + Display + Debug + PartialEq + Send + Sync {
    /// Returns true if the device is a loopback device.
    fn is_loopback(&self) -> bool;
}

/// An execution context which provides a `DeviceId` type for various IP
/// internals to share.
///
/// This trait provides the associated `DeviceId` type, and is used by
/// [`IgmpContext`], [`MldContext`], and [`InnerIcmpContext`]. It allows them to use
/// the same `DeviceId` type rather than each providing their own, which would
/// require lots of verbose type bounds when they need to be interoperable (such
/// as when ICMP delivers an MLD packet to the `mld` module for processing).
pub trait IpDeviceIdContext<I: Ip> {
    type DeviceId: IpDeviceId + 'static;
}

/// The status of an IP address on an interface.
pub(crate) enum AddressStatus<S> {
    Present(S),
    Unassigned,
}

/// The status of an IPv6 address.
pub(crate) enum Ipv6PresentAddressStatus {
    Multicast,
    UnicastAssigned,
    UnicastTentative,
}

/// An extension trait providing IP layer properties.
pub(crate) trait IpLayerIpExt: IpExt {
    type AddressStatus;
}

impl IpLayerIpExt for Ipv4 {
    type AddressStatus = AddressStatus<()>;
}

impl IpLayerIpExt for Ipv6 {
    type AddressStatus = AddressStatus<Ipv6PresentAddressStatus>;
}

/// An extension trait providing IP layer state properties.
pub(crate) trait IpLayerStateIpExt<I: Instant, DeviceId>: IpLayerIpExt {
    type State: AsRef<IpStateInner<Self, I, DeviceId>> + AsMut<IpStateInner<Self, I, DeviceId>>;
}

impl<I: Instant, DeviceId> IpLayerStateIpExt<I, DeviceId> for Ipv4 {
    type State = Ipv4State<I, DeviceId>;
}

impl<I: Instant, DeviceId> IpLayerStateIpExt<I, DeviceId> for Ipv6 {
    type State = Ipv6State<I, DeviceId>;
}

/// The state context provided to the IP layer.
// TODO(https://fxbug.dev/48578): Do not return references to state. Instead,
// callers of methods on this trait should provide a callback that takes a state
// reference.
pub(crate) trait IpStateContext<I: IpLayerStateIpExt<Self::Instant, Self::DeviceId>>:
    IpDeviceIdContext<I> + InstantContext + CounterContext
{
    /// Gets immutable access to the IP layer state.
    fn get_ip_layer_state(&self) -> &I::State;

    /// Gets mutable access to the IP layer state.
    fn get_ip_layer_state_mut(&mut self) -> &mut I::State;
}

/// The IP device context provided to the IP layer.
pub(crate) trait IpDeviceContext<I: IpLayerIpExt>: IpDeviceIdContext<I> {
    /// Gets the status of an address on the interface.
    fn address_status(
        &self,
        device_id: Self::DeviceId,
        addr: SpecifiedAddr<I::Addr>,
    ) -> I::AddressStatus;

    /// Returns true iff the device has routing enabled.
    fn is_device_routing_enabled(&self, device_id: Self::DeviceId) -> bool;
}

/// The transport context provided to the IP layer.
pub(crate) trait TransportContext<I: Ip> {
    /// Handles an update affecting routing.
    ///
    /// This method is called after routing state has been updated.
    fn on_routing_state_updated(&mut self);
}

/// The execution context for the IP layer.
pub(crate) trait IpLayerContext<I: IpLayerStateIpExt<Self::Instant, Self::DeviceId>>:
    IpStateContext<I> + IpDeviceContext<I> + TransportContext<I>
{
}

impl<
        I: IpLayerStateIpExt<C::Instant, C::DeviceId>,
        C: IpStateContext<I> + IpDeviceContext<I> + TransportContext<I>,
    > IpLayerContext<I> for C
{
}

impl<D: EventDispatcher> IpStateContext<Ipv4> for Ctx<D> {
    fn get_ip_layer_state(&self) -> &Ipv4State<D::Instant, DeviceId> {
        &self.state.ipv4
    }

    fn get_ip_layer_state_mut(&mut self) -> &mut Ipv4State<D::Instant, DeviceId> {
        &mut self.state.ipv4
    }
}

impl<D: EventDispatcher> TransportContext<Ipv4> for Ctx<D> {
    fn on_routing_state_updated(&mut self) {
        crate::ip::socket::update_all_ipv4_sockets(self, IpSockUpdate::new());
    }
}

impl<D: EventDispatcher> IpStateContext<Ipv6> for Ctx<D> {
    fn get_ip_layer_state(&self) -> &Ipv6State<D::Instant, DeviceId> {
        &self.state.ipv6
    }

    fn get_ip_layer_state_mut(&mut self) -> &mut Ipv6State<D::Instant, DeviceId> {
        &mut self.state.ipv6
    }
}

impl<D: EventDispatcher> TransportContext<Ipv6> for Ctx<D> {
    fn on_routing_state_updated(&mut self) {
        crate::ip::socket::update_all_ipv6_sockets(self, IpSockUpdate::new());
    }
}

/// The transport context provided to the IP layer requiring a buffer type.
trait BufferTransportContext<I: IpLayerIpExt, B: BufferMut>:
    IpDeviceIdContext<I> + CounterContext
{
    /// Dispatches a received incoming IP packet to the appropriate protocol.
    fn dispatch_receive_ip_packet(
        &mut self,
        device: Option<Self::DeviceId>,
        src_ip: I::RecvSrcAddr,
        dst_ip: SpecifiedAddr<I::Addr>,
        proto: I::Proto,
        body: B,
    ) -> Result<(), (B, TransportReceiveError)>;
}

/// The IP device context provided to the IP layer requiring a buffer type.
trait BufferIpDeviceContext<I: IpLayerIpExt, B: BufferMut>: IpDeviceIdContext<I> {
    /// Sends an IP frame to the next hop.
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device_id: Self::DeviceId,
        next_hop: SpecifiedAddr<I::Addr>,
        packet: S,
    ) -> Result<(), S>;
}

/// The execution context for the IP layer requiring buffer.
trait BufferIpLayerContext<
    I: IpLayerStateIpExt<Self::Instant, Self::DeviceId> + IcmpHandlerIpExt,
    B: BufferMut,
>:
    BufferTransportContext<I, B>
    + BufferIpDeviceContext<I, B>
    + BufferIcmpHandler<I, B>
    + IpLayerContext<I>
{
}

impl<
        I: IpLayerStateIpExt<C::Instant, C::DeviceId> + IcmpHandlerIpExt,
        B: BufferMut,
        C: BufferTransportContext<I, B>
            + BufferIpDeviceContext<I, B>
            + BufferIcmpHandler<I, B>
            + IpLayerContext<I>,
    > BufferIpLayerContext<I, B> for C
{
}

impl<B: BufferMut, D: BufferDispatcher<B>> BufferTransportContext<Ipv4, B> for Ctx<D> {
    fn dispatch_receive_ip_packet(
        &mut self,
        device: Option<DeviceId>,
        src_ip: Ipv4Addr,
        dst_ip: SpecifiedAddr<Ipv4Addr>,
        proto: Ipv4Proto,
        body: B,
    ) -> Result<(), (B, TransportReceiveError)> {
        // TODO(https://fxbug.dev/93955): Deliver the packet to interested raw
        // sockets.

        match proto {
            Ipv4Proto::Icmp => {
                <IcmpIpTransportContext as BufferIpTransportContext<Ipv4, _, _>>::receive_ip_packet(
                    self, device, src_ip, dst_ip, body,
                )
            }
            Ipv4Proto::Igmp => {
                IgmpPacketHandler::<_, _>::receive_igmp_packet(
                    self,
                    device.expect("IGMP messages should come from a device"),
                    src_ip,
                    dst_ip,
                    body,
                );
                Ok(())
            }
            Ipv4Proto::Proto(IpProto::Udp) => {
                <<Ctx<D> as Ipv4TransportLayerContext>::Proto17 as BufferIpTransportContext<
                    Ipv4,
                    _,
                    _,
                >>::receive_ip_packet(self, device, src_ip, dst_ip, body)
            }
            Ipv4Proto::Proto(IpProto::Tcp) => {
                <<Ctx<D> as Ipv4TransportLayerContext>::Proto6 as BufferIpTransportContext<
                    Ipv4,
                    _,
                    _,
                >>::receive_ip_packet(self, device, src_ip, dst_ip, body)
            }
            // TODO(joshlf): Once all IP protocol numbers are covered, remove
            // this default case.
            _ => Err((
                body,
                TransportReceiveError { inner: TransportReceiveErrorInner::ProtocolUnsupported },
            )),
        }
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> BufferTransportContext<Ipv6, B> for Ctx<D> {
    fn dispatch_receive_ip_packet(
        &mut self,
        device: Option<DeviceId>,
        src_ip: Ipv6SourceAddr,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        proto: Ipv6Proto,
        body: B,
    ) -> Result<(), (B, TransportReceiveError)> {
        // TODO(https://fxbug.dev/93955): Deliver the packet to interested raw
        // sockets.

        match proto {
            Ipv6Proto::Icmpv6 => {
                <IcmpIpTransportContext as BufferIpTransportContext<Ipv6, _, _>>::receive_ip_packet(
                    self, device, src_ip, dst_ip, body,
                )
            }
            // A value of `Ipv6Proto::NoNextHeader` tells us that there is no
            // header whatsoever following the last lower-level header so we stop
            // processing here.
            Ipv6Proto::NoNextHeader => Ok(()),
            Ipv6Proto::Proto(IpProto::Tcp) => {
                <<Ctx<D> as Ipv6TransportLayerContext>::Proto6 as BufferIpTransportContext<
                    Ipv6,
                    _,
                    _,
                >>::receive_ip_packet(self, device, src_ip, dst_ip, body)
            }
            Ipv6Proto::Proto(IpProto::Udp) => {
                <<Ctx<D> as Ipv6TransportLayerContext>::Proto17 as BufferIpTransportContext<
                    Ipv6,
                    _,
                    _,
                >>::receive_ip_packet(self, device, src_ip, dst_ip, body)
            }
            // TODO(joshlf): Once all IP Next Header numbers are covered, remove
            // this default case.
            _ => Err((
                body,
                TransportReceiveError { inner: TransportReceiveErrorInner::ProtocolUnsupported },
            )),
        }
    }
}

/// A dummy device ID for use in testing.
///
/// `DummyDeviceId` is provided for use in implementing
/// `IpDeviceIdContext::DeviceId` in tests. Unlike `()`, it implements the
/// `Display` trait, which is a requirement of `IpDeviceIdContext::DeviceId`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
#[cfg(test)]
pub(crate) struct DummyDeviceId;

#[cfg(test)]
impl IpDeviceId for DummyDeviceId {
    fn is_loopback(&self) -> bool {
        false
    }
}

#[cfg(test)]
impl<I: Ip, S, Id, Meta> IpDeviceIdContext<I> for crate::context::testutil::DummyCtx<S, Id, Meta> {
    type DeviceId = DummyDeviceId;
}

#[cfg(test)]
impl Display for DummyDeviceId {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "DummyDeviceId")
    }
}

/// A builder for IPv4 state.
#[derive(Copy, Clone, Default)]
pub struct Ipv4StateBuilder {
    icmp: Icmpv4StateBuilder,
}

impl Ipv4StateBuilder {
    /// Get the builder for the ICMPv4 state.
    pub fn icmpv4_builder(&mut self) -> &mut Icmpv4StateBuilder {
        &mut self.icmp
    }

    pub(crate) fn build<Instant: crate::Instant, D>(self) -> Ipv4State<Instant, D> {
        let Ipv4StateBuilder { icmp } = self;

        Ipv4State {
            inner: IpStateInner {
                table: ForwardingTable::default(),
                fragment_cache: IpPacketFragmentCache::default(),
                pmtu_cache: PmtuCache::default(),
            },
            icmp: icmp.build(),
            next_packet_id: 0,
        }
    }
}

/// A builder for IPv6 state.
#[derive(Copy, Clone, Default)]
pub struct Ipv6StateBuilder {
    icmp: Icmpv6StateBuilder,
}

impl Ipv6StateBuilder {
    /// Get the builder for the ICMPv6 state.
    pub fn icmpv6_builder(&mut self) -> &mut Icmpv6StateBuilder {
        &mut self.icmp
    }

    pub(crate) fn build<Instant: crate::Instant, D>(self) -> Ipv6State<Instant, D> {
        let Ipv6StateBuilder { icmp } = self;

        Ipv6State {
            inner: IpStateInner {
                table: ForwardingTable::default(),
                fragment_cache: IpPacketFragmentCache::default(),
                pmtu_cache: PmtuCache::default(),
            },
            icmp: icmp.build(),
        }
    }
}

pub(crate) struct Ipv4State<Instant: crate::Instant, D> {
    inner: IpStateInner<Ipv4, Instant, D>,
    icmp: Icmpv4State<Instant, IpSock<Ipv4, D>>,
    next_packet_id: u16,
}

impl<I: Instant, DeviceId> AsRef<IpStateInner<Ipv4, I, DeviceId>> for Ipv4State<I, DeviceId> {
    fn as_ref(&self) -> &IpStateInner<Ipv4, I, DeviceId> {
        &self.inner
    }
}

impl<I: Instant, DeviceId> AsMut<IpStateInner<Ipv4, I, DeviceId>> for Ipv4State<I, DeviceId> {
    fn as_mut(&mut self) -> &mut IpStateInner<Ipv4, I, DeviceId> {
        &mut self.inner
    }
}

impl<D: EventDispatcher> crate::ip::socket::Ipv4SocketContext for Ctx<D> {
    fn gen_ipv4_packet_id(&mut self) -> u16 {
        // TODO(https://fxbug.dev/87588): Generate IPv4 IDs unpredictably
        let state = &mut self.state.ipv4;
        state.next_packet_id = state.next_packet_id.wrapping_add(1);
        state.next_packet_id
    }
}

pub(crate) struct Ipv6State<Instant: crate::Instant, D> {
    inner: IpStateInner<Ipv6, Instant, D>,
    icmp: Icmpv6State<Instant, IpSock<Ipv6, D>>,
}

impl<I: Instant, DeviceId> AsRef<IpStateInner<Ipv6, I, DeviceId>> for Ipv6State<I, DeviceId> {
    fn as_ref(&self) -> &IpStateInner<Ipv6, I, DeviceId> {
        &self.inner
    }
}

impl<I: Instant, DeviceId> AsMut<IpStateInner<Ipv6, I, DeviceId>> for Ipv6State<I, DeviceId> {
    fn as_mut(&mut self) -> &mut IpStateInner<Ipv6, I, DeviceId> {
        &mut self.inner
    }
}

pub(crate) struct IpStateInner<I: Ip, Instant: crate::Instant, DeviceId> {
    table: ForwardingTable<I, DeviceId>,
    fragment_cache: IpPacketFragmentCache<I>,
    pmtu_cache: PmtuCache<I, Instant>,
}

#[specialize_ip]
fn get_state_inner<I: Ip, D: EventDispatcher>(
    state: &StackState<D>,
) -> &IpStateInner<I, D::Instant, DeviceId> {
    #[ipv4]
    return &state.ipv4.inner;
    #[ipv6]
    return &state.ipv6.inner;
}

#[specialize_ip]
fn get_state_inner_mut<I: Ip, D: EventDispatcher>(
    state: &mut StackState<D>,
) -> &mut IpStateInner<I, D::Instant, DeviceId> {
    #[ipv4]
    return &mut state.ipv4.inner;
    #[ipv6]
    return &mut state.ipv6.inner;
}

/// The identifier for timer events in the IP layer.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub(crate) enum IpLayerTimerId {
    /// A timer event for IPv4 packet reassembly timers.
    ReassemblyTimeoutv4(FragmentCacheKey<Ipv4Addr>),
    /// A timer event for IPv6 packet reassembly timers.
    ReassemblyTimeoutv6(FragmentCacheKey<Ipv6Addr>),
    PmtuTimeout(IpVersion),
}

impl IpLayerTimerId {
    #[specialize_ip_address]
    fn new_reassembly_timer_id<A: IpAddress>(key: FragmentCacheKey<A>) -> TimerId {
        #[ipv4addr]
        let id = IpLayerTimerId::ReassemblyTimeoutv4(key);
        #[ipv6addr]
        let id = IpLayerTimerId::ReassemblyTimeoutv6(key);

        TimerId(TimerIdInner::IpLayer(id))
    }

    fn new_pmtu_timer_id<I: Ip>() -> TimerId {
        TimerId(TimerIdInner::IpLayer(IpLayerTimerId::PmtuTimeout(I::VERSION)))
    }
}

/// Handle a timer event firing in the IP layer.
pub(crate) fn handle_timer<D: EventDispatcher>(ctx: &mut Ctx<D>, id: IpLayerTimerId) {
    match id {
        IpLayerTimerId::ReassemblyTimeoutv4(key) => {
            ctx.state.ipv4.inner.fragment_cache.handle_timer(key);
        }
        IpLayerTimerId::ReassemblyTimeoutv6(key) => {
            ctx.state.ipv6.inner.fragment_cache.handle_timer(key);
        }
        IpLayerTimerId::PmtuTimeout(IpVersion::V4) => {
            ctx.state
                .ipv4
                .inner
                .pmtu_cache
                .handle_timer(&mut ctx.dispatcher, PmtuTimerId::default());
        }
        IpLayerTimerId::PmtuTimeout(IpVersion::V6) => {
            ctx.state
                .ipv6
                .inner
                .pmtu_cache
                .handle_timer(&mut ctx.dispatcher, PmtuTimerId::default());
        }
    }
}

impl<A: IpAddress, D: EventDispatcher> TimerContext<FragmentCacheKey<A>> for D {
    fn schedule_timer_instant(
        &mut self,
        time: Self::Instant,
        key: FragmentCacheKey<A>,
    ) -> Option<Self::Instant> {
        self.schedule_timer_instant(time, IpLayerTimerId::new_reassembly_timer_id(key))
    }

    fn cancel_timer(&mut self, key: FragmentCacheKey<A>) -> Option<Self::Instant> {
        self.cancel_timer(IpLayerTimerId::new_reassembly_timer_id(key))
    }

    // TODO(rheacock): the compiler thinks that `f` doesn't have to be mutable,
    // but it does. Thus we `allow(unused)` here.
    #[allow(unused)]
    fn cancel_timers_with<F: FnMut(&FragmentCacheKey<A>) -> bool>(&mut self, f: F) {
        #[specialize_ip_address]
        fn cancel_timers_with_inner<
            A: IpAddress,
            D: EventDispatcher,
            F: FnMut(&FragmentCacheKey<A>) -> bool,
        >(
            ctx: &mut D,
            mut f: F,
        ) {
            ctx.cancel_timers_with(|id| match id {
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
        self.scheduled_instant(IpLayerTimerId::new_reassembly_timer_id(key))
    }
}

impl<I: Ip, D: EventDispatcher> TimerContext<PmtuTimerId<I>> for D {
    fn schedule_timer_instant(
        &mut self,
        time: Self::Instant,
        _id: PmtuTimerId<I>,
    ) -> Option<Self::Instant> {
        self.schedule_timer_instant(time, IpLayerTimerId::new_pmtu_timer_id::<I>())
    }

    fn cancel_timer(&mut self, _id: PmtuTimerId<I>) -> Option<Self::Instant> {
        self.cancel_timer(IpLayerTimerId::new_pmtu_timer_id::<I>())
    }

    fn cancel_timers_with<F: FnMut(&PmtuTimerId<I>) -> bool>(&mut self, _f: F) {
        self.cancel_timers_with(|id| id == &IpLayerTimerId::new_pmtu_timer_id::<I>());
    }

    fn scheduled_instant(&self, _id: PmtuTimerId<I>) -> Option<Self::Instant> {
        self.scheduled_instant(IpLayerTimerId::new_pmtu_timer_id::<I>())
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
fn dispatch_receive_ipv4_packet<B: BufferMut, C: BufferIpLayerContext<Ipv4, B>>(
    ctx: &mut C,
    device: Option<C::DeviceId>,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
    proto: Ipv4Proto,
    body: B,
    parse_metadata: Option<ParseMetadata>,
) {
    ctx.increment_counter("dispatch_receive_ipv4_packet");

    let (mut body, err) = match ctx.dispatch_receive_ip_packet(device, src_ip, dst_ip, proto, body)
    {
        Ok(()) => return,
        Err(e) => e,
    };
    // All branches promise to return the buffer in the same state it was in
    // when they were executed. Thus, all we have to do is undo the parsing
    // of the IP packet header, and the buffer will be back to containing
    // the entire original IP packet.
    let meta = parse_metadata.unwrap();
    body.undo_parse(meta);

    if let Some(src_ip) = SpecifiedAddr::new(src_ip) {
        match err.inner {
            TransportReceiveErrorInner::ProtocolUnsupported => {
                ctx.send_icmp_error_message(
                    device.unwrap(),
                    frame_dst,
                    src_ip,
                    dst_ip,
                    body,
                    Icmpv4Error {
                        kind: Icmpv4ErrorKind::ProtocolUnreachable,
                        header_len: meta.header_len(),
                    },
                );
            }
            TransportReceiveErrorInner::PortUnreachable => {
                // TODO(joshlf): What if we're called from a loopback
                // handler, and device and parse_metadata are None? In other
                // words, what happens if we attempt to send to a loopback
                // port which is unreachable? We will eventually need to
                // restructure the control flow here to handle that case.
                ctx.send_icmp_error_message(
                    device.unwrap(),
                    frame_dst,
                    src_ip,
                    dst_ip,
                    body,
                    Icmpv4Error {
                        kind: Icmpv4ErrorKind::PortUnreachable,
                        header_len: meta.header_len(),
                    },
                );
            }
        }
    } else {
        trace!("dispatch_receive_ipv4_packet: Cannot send ICMP error message in response to a packet from the unspecified address");
    }
}

/// Dispatch a received IPv6 packet to the appropriate protocol.
///
/// `dispatch_receive_ipv6_packet` has the same semantics as
/// `dispatch_receive_ipv4_packet`, but for IPv6.
fn dispatch_receive_ipv6_packet<B: BufferMut, C: BufferIpLayerContext<Ipv6, B>>(
    ctx: &mut C,
    device: Option<C::DeviceId>,
    frame_dst: FrameDestination,
    src_ip: Ipv6SourceAddr,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
    proto: Ipv6Proto,
    body: B,
    parse_metadata: Option<ParseMetadata>,
) {
    // TODO(https://fxbug.dev/21227): Once we support multiple extension
    // headers in IPv6, we will need to verify that the callers of this
    // function are still sound. In particular, they may accidentally pass a
    // parse_metadata argument which corresponds to a single extension
    // header rather than all of the IPv6 headers.

    ctx.increment_counter("dispatch_receive_ipv6_packet");

    let (mut body, err) = match ctx.dispatch_receive_ip_packet(device, src_ip, dst_ip, proto, body)
    {
        Ok(()) => return,
        Err(e) => e,
    };

    // All branches promise to return the buffer in the same state it was in
    // when they were executed. Thus, all we have to do is undo the parsing
    // of the IP packet header, and the buffer will be back to containing
    // the entire original IP packet.
    let meta = parse_metadata.unwrap();
    body.undo_parse(meta);

    match err.inner {
        TransportReceiveErrorInner::ProtocolUnsupported => {
            if let Ipv6SourceAddr::Unicast(src_ip) = src_ip {
                ctx.send_icmp_error_message(
                    device.unwrap(),
                    frame_dst,
                    src_ip,
                    dst_ip,
                    body,
                    Icmpv6ErrorKind::ProtocolUnreachable { header_len: meta.header_len() },
                );
            }
        }
        TransportReceiveErrorInner::PortUnreachable => {
            // TODO(joshlf): What if we're called from a loopback handler,
            // and device and parse_metadata are None? In other words, what
            // happens if we attempt to send to a loopback port which is
            // unreachable? We will eventually need to restructure the
            // control flow here to handle that case.
            if let Ipv6SourceAddr::Unicast(src_ip) = src_ip {
                ctx.send_icmp_error_message(
                    device.unwrap(),
                    frame_dst,
                    src_ip,
                    dst_ip,
                    body,
                    Icmpv6ErrorKind::PortUnreachable,
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
/// Attempts to process a potential fragment packet and reassemble if we are
/// ready to do so. If the packet isn't fragmented, or a packet was reassembled,
/// attempt to dispatch the packet.
macro_rules! process_fragment {
    ($ctx:expr, $dispatch:ident, $device:ident, $frame_dst:expr, $buffer:expr, $packet:expr, $src_ip:expr, $dst_ip:expr, $ip:ident) => {{
        match get_state_inner_mut::<$ip, _>(&mut $ctx.state)
            .fragment_cache
            .process_fragment::<_, &mut [u8]>(&mut $ctx.dispatcher, $packet)
        {
            // Handle the packet right away since reassembly is not needed.
            FragmentProcessingState::NotNeeded(packet) => {
                trace!("receive_ip_packet: not fragmented");
                // TODO(joshlf):
                // - Check for already-expired TTL?
                let (_, _, proto, meta) = packet.into_metadata();
                $dispatch(
                    $ctx,
                    Some($device),
                    $frame_dst,
                    $src_ip,
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
                match get_state_inner_mut::<$ip, _>(&mut $ctx.state)
                    .fragment_cache
                    .reassemble_packet(&mut $ctx.dispatcher, &key, buffer.buffer_view_mut())
                {
                    // Successfully reassembled the packet, handle it.
                    Ok(packet) => {
                        trace!("receive_ip_packet: fragmented, reassembled packet: {:?}", packet);
                        // TODO(joshlf):
                        // - Check for already-expired TTL?
                        let (_, _, proto, meta) = packet.into_metadata();
                        $dispatch::<Buf<Vec<u8>>, _>(
                            $ctx,
                            Some($device),
                            $frame_dst,
                            $src_ip,
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
/// depending on the type parameter, `I`.
pub(crate) fn receive_ip_packet<B: BufferMut, D: BufferDispatcher<B>, I: Ip>(
    ctx: &mut Ctx<D>,
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
    ctx: &mut Ctx<D>,
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
        // TODO(https://fxbug.dev/77598): test this code path once
        // `Ipv4Packet::parse` can return an `IpParseError::ParameterProblem`
        // error.
        Err(IpParseError::ParameterProblem {
            src_ip,
            dst_ip,
            code,
            pointer,
            must_send_icmp,
            header_len,
            action,
        }) if must_send_icmp && action.should_send_icmp(&dst_ip) => {
            // `should_send_icmp_to_multicast` should never return `true` for IPv4.
            assert!(!action.should_send_icmp_to_multicast());
            let dst_ip = match SpecifiedAddr::new(dst_ip) {
                Some(ip) => ip,
                None => {
                    debug!("receive_ipv4_packet: Received packet with unspecified destination IP address; dropping");
                    return;
                }
            };
            let src_ip = match SpecifiedAddr::new(src_ip) {
                Some(ip) => ip,
                None => {
                    trace!("receive_ipv4_packet: Cannot send ICMP error in response to packet with unspecified source IP address");
                    return;
                }
            };
            BufferIcmpHandler::<Ipv4, _>::send_icmp_error_message(
                ctx,
                device,
                frame_dst,
                src_ip,
                dst_ip,
                buffer,
                Icmpv4Error {
                    kind: Icmpv4ErrorKind::ParameterProblem {
                        code,
                        pointer,
                        // When the call to `action.should_send_icmp` returns true, it always means that
                        // the IPv4 packet that failed parsing is an initial fragment.
                        fragment_type: Ipv4FragmentType::InitialFragment,
                    },
                    header_len,
                },
            );
            return;
        }
        _ => return, // TODO(joshlf): Do something with ICMP here?
    };

    let dst_ip = match SpecifiedAddr::new(packet.dst_ip()) {
        Some(ip) => ip,
        None => {
            debug!("receive_ipv4_packet: Received packet with unspecified destination IP address; dropping");
            return;
        }
    };

    // TODO(ghanan): Act upon options.

    match receive_ipv4_packet_action(ctx, device, dst_ip) {
        ReceivePacketAction::Deliver => {
            trace!("receive_ipv4_packet: delivering locally");
            let src_ip = packet.src_ip();

            // Process a potential IPv4 fragment if the destination is this
            // host.
            //
            // We process IPv4 packet reassembly here because, for IPv4, the
            // fragment data is in the header itself so we can handle it right
            // away.
            //
            // Note, the `process_fragment` function (which is called by the
            // `process_fragment!` macro) could panic if the packet does not
            // have fragment data. However, we are guaranteed that it will not
            // panic because the fragment data is in the fixed header so it is
            // always present (even if the fragment data has values that implies
            // that the packet is not fragmented).
            process_fragment!(
                ctx,
                dispatch_receive_ipv4_packet,
                device,
                frame_dst,
                buffer,
                packet,
                src_ip,
                dst_ip,
                Ipv4
            );
        }
        ReceivePacketAction::Forward { dst } => {
            let ttl = packet.ttl();
            if ttl > 1 {
                trace!("receive_ipv4_packet: forwarding");

                packet.set_ttl(ttl - 1);
                let _: (Ipv4Addr, Ipv4Addr, Ipv4Proto, ParseMetadata) =
                    drop_packet_and_undo_parse!(packet, buffer);
                match BufferIpDeviceContext::<Ipv4, _>::send_ip_frame(
                    ctx,
                    dst.device,
                    dst.next_hop,
                    buffer,
                ) {
                    Ok(()) => (),
                    Err(b) => {
                        let _: B = b;
                        // TODO(https://fxbug.dev/86247): Encode the MTU error
                        // more obviously in the type system.
                        debug!("failed to forward IPv4 packet: MTU exceeded");
                    }
                }
            } else {
                // TTL is 0 or would become 0 after decrement; see "TTL"
                // section, https://tools.ietf.org/html/rfc791#page-14
                use packet_formats::ipv4::Ipv4Header as _;
                debug!("received IPv4 packet dropped due to expired TTL");
                let fragment_type = packet.fragment_type();
                let (src_ip, _, proto, meta): (_, Ipv4Addr, _, _) =
                    drop_packet_and_undo_parse!(packet, buffer);
                let src_ip = match SpecifiedAddr::new(src_ip) {
                    Some(ip) => ip,
                    None => {
                        trace!("receive_ipv4_packet: Cannot send ICMP error in response to packet with unspecified source IP address");
                        return;
                    }
                };
                BufferIcmpHandler::<Ipv4, _>::send_icmp_error_message(
                    ctx,
                    device,
                    frame_dst,
                    src_ip,
                    dst_ip,
                    buffer,
                    Icmpv4Error {
                        kind: Icmpv4ErrorKind::TtlExpired { proto, fragment_type },
                        header_len: meta.header_len(),
                    },
                );
            }
        }
        ReceivePacketAction::SendNoRouteToDest => {
            use packet_formats::ipv4::Ipv4Header as _;
            debug!("received IPv4 packet with no known route to destination {}", dst_ip);
            let fragment_type = packet.fragment_type();
            let (src_ip, _, proto, meta): (_, Ipv4Addr, _, _) =
                drop_packet_and_undo_parse!(packet, buffer);
            let src_ip = match SpecifiedAddr::new(src_ip) {
                Some(ip) => ip,
                None => {
                    trace!("receive_ipv4_packet: Cannot send ICMP error in response to packet with unspecified source IP address");
                    return;
                }
            };
            BufferIcmpHandler::<Ipv4, _>::send_icmp_error_message(
                ctx,
                device,
                frame_dst,
                src_ip,
                dst_ip,
                buffer,
                Icmpv4Error {
                    kind: Icmpv4ErrorKind::NetUnreachable { proto, fragment_type },
                    header_len: meta.header_len(),
                },
            );
        }
        ReceivePacketAction::Drop { reason } => {
            debug!(
                "receive_ipv4_packet: dropping packet from {} to {} received on {}: {}",
                packet.src_ip(),
                dst_ip,
                device,
                reason
            );
        }
    }
}

/// Receive an IPv6 packet from a device.
///
/// `frame_dst` specifies whether this packet was received in a broadcast or
/// unicast link-layer frame.
pub(crate) fn receive_ipv6_packet<B: BufferMut, D: BufferDispatcher<B>>(
    ctx: &mut Ctx<D>,
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
        }) if must_send_icmp && action.should_send_icmp(&dst_ip) => {
            let dst_ip = match SpecifiedAddr::new(dst_ip) {
                Some(ip) => ip,
                None => {
                    debug!("receive_ipv6_packet: Received packet with unspecified destination IP address; dropping");
                    return;
                }
            };
            let src_ip = match UnicastAddr::new(src_ip) {
                Some(ip) => ip,
                None => {
                    trace!("receive_ipv6_packet: Cannot send ICMP error in response to packet with non unicast source IP address");
                    return;
                }
            };
            BufferIcmpHandler::<Ipv6, _>::send_icmp_error_message(
                ctx,
                device,
                frame_dst,
                src_ip,
                dst_ip,
                buffer,
                Icmpv6ErrorKind::ParameterProblem {
                    code,
                    pointer,
                    allow_dst_multicast: action.should_send_icmp_to_multicast(),
                },
            );
            return;
        }
        _ => return, // TODO(joshlf): Do something with ICMP here?
    };

    trace!("receive_ipv6_packet: parsed packet: {:?}", packet);

    // TODO(ghanan): Act upon extension headers.

    let src_ip = match packet.src_ipv6() {
        Some(ip) => ip,
        None => {
            debug!(
                "receive_ipv6_packet: received packet from non-unicast source {}; dropping",
                packet.src_ip()
            );
            increment_counter!(ctx, "receive_ipv6_packet: non-unicast source");
            return;
        }
    };
    let dst_ip = match SpecifiedAddr::new(packet.dst_ip()) {
        Some(ip) => ip,
        None => {
            debug!("receive_ipv6_packet: Received packet with unspecified destination IP address; dropping");
            return;
        }
    };

    match receive_ipv6_packet_action(ctx, device, dst_ip) {
        ReceivePacketAction::Deliver => {
            trace!("receive_ipv6_packet: delivering locally");

            // Process a potential IPv6 fragment if the destination is this
            // host.
            //
            // We need to process extension headers in the order they appear in
            // the header. With some extension headers, we do not proceed to the
            // next header, and do some action immediately. For example, say we
            // have an IPv6 packet with two extension headers (routing extension
            // header before a fragment extension header). Until we get to the
            // final destination node in the routing header, we would need to
            // reroute the packet to the next destination without reassembling.
            // Once the packet gets to the last destination in the routing
            // header, that node will process the fragment extension header and
            // handle reassembly.
            match ipv6::handle_extension_headers(ctx, device, frame_dst, &packet, true) {
                Ipv6PacketAction::_Discard => {
                    trace!(
                        "receive_ipv6_packet: handled IPv6 extension headers: discarding packet"
                    );
                }
                Ipv6PacketAction::Continue => {
                    trace!(
                        "receive_ipv6_packet: handled IPv6 extension headers: dispatching packet"
                    );

                    // TODO(joshlf):
                    // - Do something with ICMP if we don't have a handler for
                    //   that protocol?
                    // - Check for already-expired TTL?
                    let (_, _, proto, meta): (Ipv6Addr, Ipv6Addr, _, _) = packet.into_metadata();
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

                    // Note, the `IpPacketFragmentCache::process_fragment`
                    // method (which is called by the `process_fragment!` macro)
                    // could panic if the packet does not have fragment data.
                    // However, we are guaranteed that it will not panic for an
                    // IPv6 packet because the fragment data is in an (optional)
                    // fragment extension header which we attempt to handle by
                    // calling `ipv6::handle_extension_headers`. We will only
                    // end up here if its return value is
                    // `Ipv6PacketAction::ProcessFragment` which is only
                    // possible when the packet has the fragment extension
                    // header (even if the fragment data has values that implies
                    // that the packet is not fragmented).
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
                        src_ip,
                        dst_ip,
                        Ipv6
                    );
                }
            }
        }
        ReceivePacketAction::Forward { dst } => {
            increment_counter!(ctx, "receive_ipv6_packet::forward");
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
                let (_, _, proto, meta): (Ipv6Addr, Ipv6Addr, _, _) =
                    drop_packet_and_undo_parse!(packet, buffer);
                if let Err(buffer) = BufferIpDeviceContext::<Ipv6, _>::send_ip_frame(
                    ctx,
                    dst.device,
                    dst.next_hop,
                    buffer,
                ) {
                    // TODO(https://fxbug.dev/86247): Encode the MTU error more
                    // obviously in the type system.
                    debug!("failed to forward IPv6 packet: MTU exceeded");
                    if let Ipv6SourceAddr::Unicast(src_ip) = src_ip {
                        trace!("receive_ipv6_packet: Sending ICMPv6 Packet Too Big");
                        // TODO(joshlf): Increment the TTL since we just
                        // decremented it. The fact that we don't do this is
                        // technically a violation of the ICMP spec (we're not
                        // encapsulating the original packet that caused the
                        // issue, but a slightly modified version of it), but
                        // it's not that big of a deal because it won't affect
                        // the sender's ability to figure out the minimum path
                        // MTU. This may break other logic, though, so we should
                        // still fix it eventually.
                        let mtu = crate::ip::device::get_mtu::<Ipv6, _>(ctx, device);
                        BufferIcmpHandler::<Ipv6, _>::send_icmp_error_message(
                            ctx,
                            device,
                            frame_dst,
                            src_ip,
                            dst_ip,
                            buffer,
                            Icmpv6ErrorKind::PacketTooBig {
                                proto,
                                header_len: meta.header_len(),
                                mtu,
                            },
                        );
                    }
                }
            } else {
                // Hop Limit is 0 or would become 0 after decrement; see RFC
                // 2460 Section 3.
                debug!("received IPv6 packet dropped due to expired Hop Limit");

                if let Ipv6SourceAddr::Unicast(src_ip) = src_ip {
                    let (_, _, proto, meta): (Ipv6Addr, Ipv6Addr, _, _) =
                        drop_packet_and_undo_parse!(packet, buffer);
                    BufferIcmpHandler::<Ipv6, _>::send_icmp_error_message(
                        ctx,
                        device,
                        frame_dst,
                        src_ip,
                        dst_ip,
                        buffer,
                        Icmpv6ErrorKind::TtlExpired { proto, header_len: meta.header_len() },
                    );
                }
            }
        }
        ReceivePacketAction::SendNoRouteToDest => {
            let (_, _, proto, meta): (Ipv6Addr, Ipv6Addr, _, _) =
                drop_packet_and_undo_parse!(packet, buffer);
            debug!("received IPv6 packet with no known route to destination {}", dst_ip);

            if let Ipv6SourceAddr::Unicast(src_ip) = src_ip {
                BufferIcmpHandler::<Ipv6, _>::send_icmp_error_message(
                    ctx,
                    device,
                    frame_dst,
                    src_ip,
                    dst_ip,
                    buffer,
                    Icmpv6ErrorKind::NetUnreachable { proto, header_len: meta.header_len() },
                );
            }
        }
        ReceivePacketAction::Drop { reason } => {
            increment_counter!(ctx, "receive_ipv6_packet::drop");
            debug!(
                "receive_ipv6_packet: dropping packet from {} to {} received on {}: {}",
                packet.src_ip(),
                dst_ip,
                device,
                reason
            );
        }
    }
}

/// The action to take in order to process a received IP packet.
#[cfg_attr(test, derive(Debug, PartialEq))]
enum ReceivePacketAction<A: IpAddress, DeviceId> {
    /// Deliver the packet locally.
    Deliver,
    /// Forward the packet to the given destination.
    Forward { dst: Destination<A, DeviceId> },
    /// Send a Destination Unreachable ICMP error message to the packet's sender
    /// and drop the packet.
    ///
    /// For ICMPv4, use the code "net unreachable". For ICMPv6, use the code "no
    /// route to destination".
    SendNoRouteToDest,
    /// Silently drop the packet.
    ///
    /// `reason` describes why the packet was dropped. Its `Display` impl
    /// provides a human-readable description which is intended for use in
    /// logging.
    Drop { reason: DropReason },
}

#[cfg_attr(test, derive(Debug, PartialEq))]
enum DropReason {
    Tentative,
    ForwardingDisabledInboundIface,
}

impl Display for DropReason {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                DropReason::Tentative => "remote packet destined to tentative address",
                DropReason::ForwardingDisabledInboundIface => {
                    "packet should be forwarded but packet's inbound interface has forwarding disabled"
                }
            }
        )
    }
}

/// Computes the action to take in order to process a received IPv4 packet.
fn receive_ipv4_packet_action<C: IpLayerContext<Ipv4>>(
    ctx: &mut C,
    device: C::DeviceId,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
) -> ReceivePacketAction<Ipv4Addr, C::DeviceId> {
    match ctx.address_status(device, dst_ip) {
        AddressStatus::Present(()) => {
            ctx.increment_counter("receive_ipv4_packet_action::deliver");
            ReceivePacketAction::Deliver
        }
        AddressStatus::Unassigned => {
            receive_ip_packet_action_common::<Ipv4, _>(ctx, dst_ip, device)
        }
    }
}

/// Computes the action to take in order to process a received IPv6 packet.
fn receive_ipv6_packet_action<C: IpLayerContext<Ipv6>>(
    ctx: &mut C,
    device: C::DeviceId,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
) -> ReceivePacketAction<Ipv6Addr, C::DeviceId> {
    match ctx.address_status(device, dst_ip) {
        AddressStatus::Present(Ipv6PresentAddressStatus::Multicast) => {
            ctx.increment_counter("receive_ipv6_packet_action::deliver_multicast");
            ReceivePacketAction::Deliver
        }
        AddressStatus::Present(Ipv6PresentAddressStatus::UnicastAssigned) => {
            ctx.increment_counter("receive_ipv6_packet_action::deliver_unicast");
            ReceivePacketAction::Deliver
        }
        AddressStatus::Present(Ipv6PresentAddressStatus::UnicastTentative) => {
            // If the destination address is tentative (which implies that
            // we are still performing NDP's Duplicate Address Detection on
            // it), then we don't consider the address "assigned to an
            // interface", and so we drop packets instead of delivering them
            // locally.
            //
            // As per RFC 4862 section 5.4:
            //
            //   An address on which the Duplicate Address Detection
            //   procedure is applied is said to be tentative until the
            //   procedure has completed successfully. A tentative address
            //   is not considered "assigned to an interface" in the
            //   traditional sense.  That is, the interface must accept
            //   Neighbor Solicitation and Advertisement messages containing
            //   the tentative address in the Target Address field, but
            //   processes such packets differently from those whose Target
            //   Address matches an address assigned to the interface. Other
            //   packets addressed to the tentative address should be
            //   silently discarded. Note that the "other packets" include
            //   Neighbor Solicitation and Advertisement messages that have
            //   the tentative (i.e., unicast) address as the IP destination
            //   address and contain the tentative address in the Target
            //   Address field.  Such a case should not happen in normal
            //   operation, though, since these messages are multicasted in
            //   the Duplicate Address Detection procedure.
            //
            // That is, we accept no packets destined to a tentative
            // address. NS and NA packets should be addressed to a multicast
            // address that we would have joined during DAD so that we can
            // receive those packets.
            ctx.increment_counter("receive_ipv6_packet_action::drop_for_tentative");
            ReceivePacketAction::Drop { reason: DropReason::Tentative }
        }
        AddressStatus::Unassigned => {
            receive_ip_packet_action_common::<Ipv6, _>(ctx, dst_ip, device)
        }
    }
}

/// Computes the remaining protocol-agnostic actions on behalf of
/// [`receive_ipv4_packet_action`] and [`receive_ipv6_packet_action`].
fn receive_ip_packet_action_common<
    I: IpLayerStateIpExt<C::Instant, C::DeviceId>,
    C: IpLayerContext<I>,
>(
    ctx: &mut C,
    dst_ip: SpecifiedAddr<I::Addr>,
    device_id: C::DeviceId,
) -> ReceivePacketAction<I::Addr, C::DeviceId> {
    // The packet is not destined locally, so we attempt to forward it.
    if !ctx.is_device_routing_enabled(device_id) {
        // Forwarding is disabled; we are operating only as a host.
        //
        // For IPv4, per RFC 1122 Section 3.2.1.3, "A host MUST silently discard
        // an incoming datagram that is not destined for the host."
        //
        // For IPv6, per RFC 4443 Section 3.1, the only instance in which a host
        // sends an ICMPv6 Destination Unreachable message is when a packet is
        // destined to that host but on an unreachable port (Code 4 - "Port
        // unreachable"). Since the only sensible error message to send in this
        // case is a Destination Unreachable message, we interpret the RFC text
        // to mean that, consistent with IPv4's behavior, we should silently
        // discard the packet in this case.
        ctx.increment_counter("receive_ip_packet_action_common::routing_disabled_per_device");
        ReceivePacketAction::Drop { reason: DropReason::ForwardingDisabledInboundIface }
    } else {
        match lookup_route(ctx, dst_ip) {
            Some(dst) => {
                ctx.increment_counter("receive_ip_packet_action_common::forward");
                ReceivePacketAction::Forward { dst }
            }
            None => {
                ctx.increment_counter("receive_ip_packet_action_common::no_route_to_host");
                ReceivePacketAction::SendNoRouteToDest
            }
        }
    }
}

// Look up the route to a host.
fn lookup_route<I: IpLayerStateIpExt<C::Instant, C::DeviceId>, C: IpLayerContext<I>>(
    ctx: &C,
    dst_ip: SpecifiedAddr<I::Addr>,
) -> Option<Destination<I::Addr, C::DeviceId>> {
    AsRef::<IpStateInner<_, _, _>>::as_ref(ctx.get_ip_layer_state()).table.lookup(dst_ip)
}

pub(crate) fn on_routing_state_updated<
    I: IpLayerStateIpExt<C::Instant, C::DeviceId>,
    C: IpLayerContext<I>,
>(
    ctx: &mut C,
) {
    ctx.on_routing_state_updated()
}

fn get_ip_layer_state_inner_mut<
    I: IpLayerStateIpExt<C::Instant, C::DeviceId>,
    C: IpLayerContext<I>,
>(
    ctx: &mut C,
) -> &mut IpStateInner<I, C::Instant, C::DeviceId> {
    ctx.get_ip_layer_state_mut().as_mut()
}

/// Add a route to the forwarding table, returning `Err` if the subnet
/// is already in the table.
pub(crate) fn add_route<I: IpLayerStateIpExt<C::Instant, C::DeviceId>, C: IpLayerContext<I>>(
    ctx: &mut C,
    subnet: Subnet<I::Addr>,
    next_hop: SpecifiedAddr<I::Addr>,
) -> Result<(), ExistsError> {
    get_ip_layer_state_inner_mut(ctx)
        .table
        .add_route(subnet, next_hop)
        .map(|()| on_routing_state_updated(ctx))
}

/// Add a device route to the forwarding table, returning `Err` if the
/// subnet is already in the table.
pub(crate) fn add_device_route<
    I: IpLayerStateIpExt<C::Instant, C::DeviceId>,
    C: IpLayerContext<I>,
>(
    ctx: &mut C,
    subnet: Subnet<I::Addr>,
    device: C::DeviceId,
) -> Result<(), ExistsError> {
    get_ip_layer_state_inner_mut(ctx)
        .table
        .add_device_route(subnet, device)
        .map(|()| on_routing_state_updated(ctx))
}

/// Delete a route from the forwarding table, returning `Err` if no
/// route was found to be deleted.
pub(crate) fn del_route<I: IpLayerStateIpExt<C::Instant, C::DeviceId>, C: IpLayerContext<I>>(
    ctx: &mut C,
    subnet: Subnet<I::Addr>,
) -> Result<(), NotFoundError> {
    get_ip_layer_state_inner_mut(ctx)
        .table
        .del_route(subnet)
        .map(|()| on_routing_state_updated(ctx))
}

/// Returns all the routes for the provided `IpAddress` type.
pub(crate) fn iter_all_routes<D: EventDispatcher, A: IpAddress>(
    ctx: &Ctx<D>,
) -> Iter<'_, Entry<A, DeviceId>> {
    get_state_inner::<A::Version, _>(&ctx.state).table.iter_installed()
}

/// Send an IP packet to a remote host over a specific device.
///
/// `send_ip_packet_from_device` accepts a device, a source and destination IP
/// address, a next hop IP address, and a serializer. It computes
/// the routing information and serializes the request in a new IP packet and
/// sends it. `mtu` will optionally impose an MTU constraint on the whole IP
/// packet. This is useful for cases where some packets are being sent out
/// which must not exceed some size (ICMPv6 Error Responses).
///
/// # Panics
///
/// Panics if either `src_ip` or `dst_ip` is the loopback address and the device
/// is a non-loopback device.
pub(crate) fn send_ipv4_packet_from_device<
    B: BufferMut,
    D: BufferDispatcher<B>,
    S: Serializer<Buffer = B>,
>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    next_hop: SpecifiedAddr<Ipv4Addr>,
    proto: Ipv4Proto,
    body: S,
    mtu: Option<u32>,
) -> Result<(), S> {
    let builder = {
        assert!(
            (!Ipv4::LOOPBACK_SUBNET.contains(&src_ip) && !Ipv4::LOOPBACK_SUBNET.contains(&dst_ip))
                || device.is_loopback()
        );
        let mut builder =
            Ipv4PacketBuilder::new(src_ip, dst_ip, get_hop_limit::<_, Ipv4>(ctx, device), proto);
        builder.id(ctx.gen_ipv4_packet_id());
        builder
    };
    let body = body.encapsulate(builder);

    if let Some(mtu) = mtu {
        let body = body.with_mtu(mtu as usize);
        crate::ip::device::send_ip_frame::<Ipv4, _, _, _>(ctx, device, next_hop, body)
            .map_err(|ser| ser.into_inner().into_inner())
    } else {
        crate::ip::device::send_ip_frame::<Ipv4, _, _, _>(ctx, device, next_hop, body)
            .map_err(|ser| ser.into_inner())
    }
}

pub(crate) fn send_ipv6_packet_from_device<
    B: BufferMut,
    D: BufferDispatcher<B>,
    S: Serializer<Buffer = B>,
>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    next_hop: SpecifiedAddr<Ipv6Addr>,
    proto: Ipv6Proto,
    body: S,
    mtu: Option<u32>,
) -> Result<(), S> {
    let builder = {
        assert!(
            (!Ipv6::LOOPBACK_SUBNET.contains(&src_ip) && !Ipv6::LOOPBACK_SUBNET.contains(&dst_ip))
                || device.is_loopback()
        );
        Ipv6PacketBuilder::new(src_ip, dst_ip, get_hop_limit::<_, Ipv6>(ctx, device), proto)
    };

    let body = body.encapsulate(builder);

    if let Some(mtu) = mtu {
        let body = body.with_mtu(mtu as usize);
        crate::ip::device::send_ip_frame::<Ipv6, _, _, _>(ctx, device, next_hop, body)
            .map_err(|ser| ser.into_inner().into_inner())
    } else {
        crate::ip::device::send_ip_frame::<Ipv6, _, _, _>(ctx, device, next_hop, body)
            .map_err(|ser| ser.into_inner())
    }
}

impl<D: EventDispatcher> PmtuHandler<Ipv4> for Ctx<D> {
    fn update_pmtu_if_less(&mut self, src_ip: Ipv4Addr, dst_ip: Ipv4Addr, new_mtu: u32) {
        // TODO(https://fxbug.dev/92599): Do something with this `Result` or
        // change `update_pmtu_if_less` to not return one?
        let _: Result<_, _> = self.state.ipv4.inner.pmtu_cache.update_pmtu_if_less(
            &mut self.dispatcher,
            src_ip,
            dst_ip,
            new_mtu,
        );
    }
    fn update_pmtu_next_lower(&mut self, src_ip: Ipv4Addr, dst_ip: Ipv4Addr, from: u32) {
        // TODO(https://fxbug.dev/92599): Do something with this `Result` or
        // change `update_pmtu_next_lower` to not return one?
        let _: Result<_, _> = self.state.ipv4.inner.pmtu_cache.update_pmtu_next_lower(
            &mut self.dispatcher,
            src_ip,
            dst_ip,
            from,
        );
    }
}

impl<D: EventDispatcher> PmtuHandler<Ipv6> for Ctx<D> {
    fn update_pmtu_if_less(&mut self, src_ip: Ipv6Addr, dst_ip: Ipv6Addr, new_mtu: u32) {
        // TODO(https://fxbug.dev/92599): Do something with this `Result` or
        // change `update_pmtu_if_less` to not return one?
        let _: Result<_, _> = self.state.ipv6.inner.pmtu_cache.update_pmtu_if_less(
            &mut self.dispatcher,
            src_ip,
            dst_ip,
            new_mtu,
        );
    }
    fn update_pmtu_next_lower(&mut self, src_ip: Ipv6Addr, dst_ip: Ipv6Addr, from: u32) {
        // TODO(https://fxbug.dev/92599): Do something with this `Result` or
        // change `update_pmtu_next_lower` to not return one?
        let _: Result<_, _> = self.state.ipv6.inner.pmtu_cache.update_pmtu_next_lower(
            &mut self.dispatcher,
            src_ip,
            dst_ip,
            from,
        );
    }
}

impl<D: EventDispatcher> InnerIcmpContext<Ipv4> for Ctx<D> {
    fn receive_icmp_error(
        &mut self,
        original_src_ip: Option<SpecifiedAddr<Ipv4Addr>>,
        original_dst_ip: SpecifiedAddr<Ipv4Addr>,
        original_proto: Ipv4Proto,
        original_body: &[u8],
        err: Icmpv4ErrorCode,
    ) {
        self.increment_counter("InnerIcmpContext<Ipv4>::receive_icmp_error");
        trace!("InnerIcmpContext<Ipv4>::receive_icmp_error({:?})", err);

        macro_rules! mtch {
            ($($cond:pat => $ty:ident),*) => {
                match original_proto {
                    Ipv4Proto::Icmp => <IcmpIpTransportContext as IpTransportContext<Ipv4, _>>
                                ::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),
                    $($cond => <<Ctx<D> as Ipv4TransportLayerContext>::$ty as IpTransportContext<Ipv4, _>>
                                ::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),)*
                    // TODO(joshlf): Once all IP protocol numbers are covered,
                    // remove this default case.
                    _ => <() as IpTransportContext<Ipv4, _>>::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),
                }
            };
        }

        #[rustfmt::skip]
        mtch!(
            Ipv4Proto::Proto(IpProto::Tcp) => Proto6,
            Ipv4Proto::Proto(IpProto::Udp) => Proto17
        );
    }

    fn get_state_and_update_meta(
        &mut self,
    ) -> (
        &mut IcmpState<Ipv4Addr, D::Instant, IpSock<Ipv4, DeviceId>>,
        &ForwardingTable<Ipv4, DeviceId>,
    ) {
        let state = &mut self.state.ipv4;
        (state.icmp.as_mut(), &state.inner.table)
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> InnerBufferIcmpContext<Ipv4, B> for Ctx<D> {
    fn send_icmp_error_message<
        S: Serializer<Buffer = B>,
        F: FnOnce(SpecifiedAddr<Ipv4Addr>) -> S,
    >(
        &mut self,
        device: DeviceId,
        frame_dst: FrameDestination,
        original_src_ip: SpecifiedAddr<Ipv4Addr>,
        original_dst_ip: SpecifiedAddr<Ipv4Addr>,
        get_body_from_src_ip: F,
        ip_mtu: Option<u32>,
        should_send_info: ShouldSendIcmpv4ErrorInfo,
    ) -> Result<(), S> {
        trace!(
            "send_icmp_error_message({}, {}, {}, {:?})",
            device,
            original_src_ip,
            original_dst_ip,
            ip_mtu
        );
        self.increment_counter("send_icmp_error_message");

        if !crate::ip::icmp::should_send_icmpv4_error(
            frame_dst,
            original_src_ip,
            original_dst_ip,
            should_send_info,
        ) {
            return Ok(());
        }

        if let Some((device, local_ip, next_hop)) = get_icmp_error_message_destination(
            self,
            device,
            original_src_ip,
            original_dst_ip,
            |device_id| crate::ip::device::get_ipv4_addr_subnet(self, device_id),
        ) {
            send_ipv4_packet_from_device(
                self,
                device,
                local_ip.get(),
                original_src_ip.get(),
                next_hop,
                Ipv4Proto::Icmp,
                get_body_from_src_ip(local_ip),
                ip_mtu,
            )?;
        }

        Ok(())
    }
}

impl<D: EventDispatcher> InnerIcmpContext<Ipv6> for Ctx<D> {
    fn receive_icmp_error(
        &mut self,
        original_src_ip: Option<SpecifiedAddr<Ipv6Addr>>,
        original_dst_ip: SpecifiedAddr<Ipv6Addr>,
        original_next_header: Ipv6Proto,
        original_body: &[u8],
        err: Icmpv6ErrorCode,
    ) {
        self.increment_counter("InnerIcmpContext<Ipv6>::receive_icmp_error");
        trace!("InnerIcmpContext<Ipv6>::receive_icmp_error({:?})", err);

        macro_rules! mtch {
            ($($cond:pat => $ty:ident),*) => {
                match original_next_header {
                    Ipv6Proto::Icmpv6 => <IcmpIpTransportContext as IpTransportContext<Ipv6, _>>
                    ::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),
                    $($cond => <<Ctx<D> as Ipv6TransportLayerContext>::$ty as IpTransportContext<Ipv6, _>>
                                ::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),)*
                    // TODO(joshlf): Once all IP protocol numbers are covered,
                    // remove this default case.
                    _ => <() as IpTransportContext<Ipv6, _>>::receive_icmp_error(self, original_src_ip, original_dst_ip, original_body, err),
                }
            };
        }

        #[rustfmt::skip]
        mtch!(
            Ipv6Proto::Proto(IpProto::Tcp) => Proto6,
            Ipv6Proto::Proto(IpProto::Udp) => Proto17
        );
    }

    fn get_state_and_update_meta(
        &mut self,
    ) -> (
        &mut IcmpState<Ipv6Addr, D::Instant, IpSock<Ipv6, DeviceId>>,
        &ForwardingTable<Ipv6, DeviceId>,
    ) {
        let state = &mut self.state.ipv6;
        (state.icmp.as_mut(), &state.inner.table)
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> InnerBufferIcmpContext<Ipv6, B> for Ctx<D> {
    fn send_icmp_error_message<
        S: Serializer<Buffer = B>,
        F: FnOnce(SpecifiedAddr<Ipv6Addr>) -> S,
    >(
        &mut self,
        device: DeviceId,
        frame_dst: FrameDestination,
        src_ip: SpecifiedAddr<Ipv6Addr>,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        get_body: F,
        ip_mtu: Option<u32>,
        should_send_info: ShouldSendIcmpv6ErrorInfo,
    ) -> Result<(), S> {
        trace!("send_icmp_error_message({}, {}, {}, {:?})", device, src_ip, dst_ip, ip_mtu);
        self.increment_counter("send_icmp_error_message");

        if !crate::ip::icmp::should_send_icmpv6_error(frame_dst, src_ip, dst_ip, should_send_info) {
            return Ok(());
        }

        if let Some((device, local_ip, next_hop)) =
            get_icmp_error_message_destination(self, device, src_ip, dst_ip, |device_id| {
                crate::ip::device::get_ipv6_addr_subnet(self, device_id)
            })
        {
            send_ipv6_packet_from_device(
                self,
                device,
                local_ip.get(),
                src_ip.get(),
                next_hop,
                Ipv6Proto::Icmpv6,
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
fn get_icmp_error_message_destination<
    D: EventDispatcher,
    A: IpAddress,
    F: FnOnce(DeviceId) -> Option<AddrSubnet<A>>,
>(
    ctx: &Ctx<D>,
    _device: DeviceId,
    src_ip: SpecifiedAddr<A>,
    _dst_ip: SpecifiedAddr<A>,
    get_ip_addr_subnet: F,
) -> Option<(DeviceId, SpecifiedAddr<A>, SpecifiedAddr<A>)>
where
    A::Version: IpLayerStateIpExt<<Ctx<D> as InstantContext>::Instant, DeviceId>,
    Ctx<D>: IpLayerContext<A::Version> + IpDeviceIdContext<A::Version, DeviceId = DeviceId>,
{
    // TODO(joshlf): Come up with rules for when to send ICMP error messages.
    // E.g., should we send a response over a different device than the device
    // that the original packet ingressed over? We'll probably want to consult
    // BCP 38 (aka RFC 2827) and RFC 3704.

    if let Some(route) = lookup_route::<A::Version, _>(ctx, src_ip) {
        if let Some(local_ip) = get_ip_addr_subnet(route.device).as_ref().map(AddrSubnet::addr) {
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

/// Get the hop limit for new IP packets that will be sent out from `device`.
fn get_hop_limit<D: EventDispatcher, I: Ip>(ctx: &Ctx<D>, device: DeviceId) -> u8 {
    // TODO(ghanan): Should IPv4 packets use the same TTL value as IPv6 packets?
    //               Currently for the IPv6 case, we get the default hop limit
    //               from the device state which can be updated by NDP's Router
    //               Advertisement.

    match I::VERSION {
        IpVersion::V4 => DEFAULT_TTL.get(),
        // This value can be updated by NDP's Router Advertisements.
        IpVersion::V6 => crate::ip::device::get_ipv6_hop_limit(ctx, device).get(),
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

    use alloc::vec;
    use core::{convert::TryFrom, num::NonZeroU16};

    use net_types::{
        ip::{Ipv4Addr, Ipv6Addr},
        MulticastAddr, UnicastAddr,
    };
    use packet::{Buf, ParseBuffer};
    use packet_formats::{
        ethernet::{EthernetFrame, EthernetFrameBuilder, EthernetFrameLengthCheck, EthernetIpExt},
        icmp::{
            IcmpDestUnreachable, IcmpEchoRequest, IcmpPacketBuilder, IcmpParseArgs, IcmpUnusedCode,
            Icmpv4DestUnreachableCode, Icmpv6Packet, Icmpv6PacketTooBig,
            Icmpv6ParameterProblemCode, MessageBody,
        },
        ip::{IpExtByteSlice, IpPacketBuilder, Ipv6ExtHdrType},
        ipv4::Ipv4PacketBuilder,
        ipv6::{ext_hdrs::ExtensionHeaderOptionAction, Ipv6PacketBuilder},
        testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame,
    };
    use rand::Rng;
    use specialize_ip_macro::ip_test;

    use crate::{
        context::testutil::DummyInstant,
        device::{receive_frame, FrameDestination},
        ip::device::set_routing_enabled,
        testutil::*,
        {assert_empty, DeviceId, Mac, StackStateBuilder},
    };

    // Some helper functions

    /// Verify that an ICMP Parameter Problem packet was actually sent in
    /// response to a packet with an unrecognized IPv6 extension header option.
    ///
    /// `verify_icmp_for_unrecognized_ext_hdr_option` verifies that the next
    /// frame in `net` is an ICMP packet with code set to `code`, and pointer
    /// set to `pointer`.
    fn verify_icmp_for_unrecognized_ext_hdr_option(
        ctx: &mut crate::testutil::DummyCtx,
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
        let (src_ip, dst_ip, proto, _): (_, _, _, ParseMetadata) = packet.into_metadata();
        assert_eq!(dst_ip, DUMMY_CONFIG_V6.remote_ip.get());
        assert_eq!(src_ip, DUMMY_CONFIG_V6.local_ip.get());
        assert_eq!(proto, Ipv6Proto::Icmpv6);
        let icmp =
            buffer.parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, dst_ip)).unwrap();
        if let Icmpv6Packet::ParameterProblem(icmp) = icmp {
            assert_eq!(icmp.code(), code);
            assert_eq!(icmp.message().pointer(), pointer);
        } else {
            panic!("Expected ICMPv6 Parameter Problem: {:?}", icmp);
        }
    }

    /// Populate a buffer `bytes` with data required to test unrecognized
    /// options.
    ///
    /// The unrecognized option type will be located at index 48. `bytes` must
    /// be at least 64 bytes long. If `to_multicast` is `true`, the destination
    /// address of the packet will be a multicast address.
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

        let payload_len = u16::try_from(bytes.len() - 40).unwrap();
        bytes[4..6].copy_from_slice(&payload_len.to_be_bytes());

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
            IpProto::Udp.into(),
        )
    }

    /// Process an IP fragment depending on the `Ip` `process_ip_fragment` is
    /// specialized with.
    fn process_ip_fragment<I: Ip, D: EventDispatcher>(
        ctx: &mut Ctx<D>,
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
        ctx: &mut Ctx<D>,
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
        ctx: &mut Ctx<D>,
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
        bytes[44..48].copy_from_slice(&(u32::try_from(fragment_id).unwrap().to_be_bytes()));
        bytes.extend(fragment_offset * 8..fragment_offset * 8 + 8);
        let payload_len = u16::try_from(bytes.len() - 40).unwrap();
        bytes[4..6].copy_from_slice(&payload_len.to_be_bytes());
        let buffer = Buf::new(bytes, ..);
        receive_ipv6_packet(ctx, device, FrameDestination::Unicast, buffer);
    }

    #[test]
    fn test_ipv6_icmp_parameter_problem_non_must() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6).build();
        let device = DeviceId::new_ethernet(0);

        // Test parsing an IPv6 packet with invalid next header value which
        // we SHOULD send an ICMP response for (but we don't since its not a
        // MUST).

        #[rustfmt::skip]
        let bytes: &mut [u8] = &mut [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Body
            1, 2, 3, 4, 5,
        ][..];
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);
        let payload_len = u16::try_from(bytes.len() - 40).unwrap();
        bytes[4..6].copy_from_slice(&payload_len.to_be_bytes());
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
        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6).build();
        let device = DeviceId::new_ethernet(0);

        // Test parsing an IPv6 packet where we MUST send an ICMP parameter problem
        // response (invalid routing type for a routing extension header).

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
        let payload_len = u16::try_from(bytes.len() - 40).unwrap();
        bytes[4..6].copy_from_slice(&payload_len.to_be_bytes());
        bytes[6] = Ipv6ExtHdrType::Routing.into();
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(DUMMY_CONFIG_V6.remote_ip.bytes());
        bytes[24..40].copy_from_slice(DUMMY_CONFIG_V6.local_ip.bytes());
        let buf = Buf::new(bytes, ..);
        receive_ipv6_packet(&mut ctx, device, FrameDestination::Unicast, buf);
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::ErroneousHeaderField,
            42,
            0,
        );
    }

    #[test]
    fn test_ipv6_unrecognized_ext_hdr_option() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6).build();
        let device = DeviceId::new_ethernet(0);
        let mut expected_icmps = 0;
        let mut bytes = [0; 64];
        let frame_dst = FrameDestination::Unicast;

        // Test parsing an IPv6 packet where we MUST send an ICMP parameter
        // problem due to an unrecognized extension header option.

        // Test with unrecognized option type set with action = skip & continue.

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::SkipAndContinue,
            false,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 1);
        assert_eq!(ctx.dispatcher.frames_sent().len(), expected_icmps);

        // Test with unrecognized option type set with
        // action = discard.

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacket,
            false,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher.frames_sent().len(), expected_icmps);

        // Test with unrecognized option type set with
        // action = discard & send icmp
        // where dest addr is a unicast addr.

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmp,
            false,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher.frames_sent().len(), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
            expected_icmps - 1,
        );

        // Test with unrecognized option type set with
        // action = discard & send icmp
        // where dest addr is a multicast addr.

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmp,
            true,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher.frames_sent().len(), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
            expected_icmps - 1,
        );

        // Test with unrecognized option type set with
        // action = discard & send icmp if not multicast addr
        // where dest addr is a unicast addr.

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast,
            false,
        );
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher.frames_sent().len(), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
            expected_icmps - 1,
        );

        // Test with unrecognized option type set with
        // action = discard & send icmp if not multicast addr
        // but dest addr is a multicast addr.

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast,
            true,
        );
        // Do not expect an ICMP response for this packet
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(ctx.dispatcher.frames_sent().len(), expected_icmps);

        // None of our tests should have sent an icmpv4 packet, or dispatched an
        // IP packet after the first.

        assert_eq!(get_counter_val(&mut ctx, "send_icmpv4_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 1);
    }

    #[ip_test]
    fn test_ip_packet_reassembly_not_needed<I: Ip + TestIpExt>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG).build();
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        // Test that a non fragmented packet gets dispatched right away.

        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 0, 1);

        // Make sure the packet got dispatched.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);
    }

    #[ip_test]
    fn test_ip_packet_reassembly<I: Ip + TestIpExt>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG).build();
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        // Test that the received packet gets dispatched only after receiving
        // all the fragments.

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
        let mut ctx = DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG).build();
        let device = DeviceId::new_ethernet(0);
        let fragment_id_0 = 5;
        let fragment_id_1 = 10;
        let fragment_id_2 = 15;

        // Test that received packets gets dispatched only after receiving all
        // the fragments with out of order arrival of fragments.

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
    fn test_ip_packet_reassembly_timer<I: Ip + TestIpExt>() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG).build();
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        // Test to make sure that packets must arrive within the reassembly
        // timer.

        // Process fragment #0
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 0, 3);

        // Make sure a timer got added.
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);

        // Process fragment #1
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 1, 3);

        // Trigger the timer (simulate a timer for the fragmented packet)
        let key = FragmentCacheKey::<_>::new(
            I::DUMMY_CONFIG.remote_ip.get(),
            I::DUMMY_CONFIG.local_ip.get(),
            u32::from(fragment_id),
        );
        assert_eq!(
            trigger_next_timer(&mut ctx).unwrap(),
            IpLayerTimerId::new_reassembly_timer_id(key)
        );

        // Make sure no other times exist..
        assert_empty(ctx.dispatcher.timer_events());

        // Process fragment #2
        process_ip_fragment::<I, _>(&mut ctx, device, fragment_id, 2, 3);

        // Make sure no packets got dispatched yet since even though we
        // technically received all the fragments, this fragment (#2) arrived
        // too late and the reassembly timer was triggered, causing the prior
        // fragment data to be discarded.
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 0);
    }

    #[ip_test]
    fn test_ip_reassembly_only_at_destination_host<I: Ip + TestIpExt>() {
        // Create a new network with two parties (alice & bob) and enable IP
        // packet routing for alice.
        let a = "alice";
        let b = "bob";
        let dummy_config = I::DUMMY_CONFIG;
        let mut state_builder = StackStateBuilder::default();
        let mut ndp_config = crate::device::ndp::NdpConfiguration::default();
        ndp_config.set_max_router_solicitations(None);
        state_builder.device_builder().set_default_ndp_config(ndp_config);
        let mut ipv6_config = crate::ip::device::state::Ipv6DeviceConfiguration::default();
        ipv6_config.dad_transmits = None;
        state_builder.device_builder().set_default_ipv6_config(ipv6_config);
        let device = DeviceId::new_ethernet(0);
        let mut alice = DummyEventDispatcherBuilder::from_config(dummy_config.swap())
            .build_with(state_builder, DummyEventDispatcher::default());
        set_routing_enabled::<_, I>(&mut alice, device, true)
            .expect("error setting routing enabled");
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

        // Test that packets only get reassembled and dispatched at the
        // destination. In this test, Alice is receiving packets from some
        // source that is actually destined for Bob. Alice should simply forward
        // the packets without attempting to process or reassemble the
        // fragments.

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
        // Test sending an IPv6 Packet Too Big Error when receiving a packet
        // that is too big to be forwarded when it isn't destined for the node
        // it arrived at.

        let dummy_config = Ipv6::DUMMY_CONFIG;
        let mut state_builder = StackStateBuilder::default();
        let mut ndp_config = crate::device::ndp::NdpConfiguration::default();
        ndp_config.set_max_router_solicitations(None);
        state_builder.device_builder().set_default_ndp_config(ndp_config);
        let mut ipv6_config = crate::ip::device::state::Ipv6DeviceConfiguration::default();
        ipv6_config.dad_transmits = None;
        state_builder.device_builder().set_default_ipv6_config(ipv6_config);
        let mut dispatcher_builder = DummyEventDispatcherBuilder::from_config(dummy_config.clone());
        let extra_ip = UnicastAddr::new(Ipv6Addr::from_bytes([
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 100,
        ]))
        .unwrap();
        let extra_mac = UnicastAddr::new(Mac::new([12, 13, 14, 15, 16, 17])).unwrap();
        dispatcher_builder.add_ndp_table_entry(0, extra_ip, extra_mac);
        dispatcher_builder.add_ndp_table_entry(
            0,
            extra_mac.to_ipv6_link_local().addr().get(),
            extra_mac,
        );
        let mut ctx = dispatcher_builder.build_with(state_builder, DummyEventDispatcher::default());
        let device = DeviceId::new_ethernet(0);
        set_routing_enabled::<_, Ipv6>(&mut ctx, device, true)
            .expect("error setting routing enabled");
        let frame_dst = FrameDestination::Unicast;

        // Construct an IPv6 packet that is too big for our MTU (MTU = 1280;
        // body itself is 5000). Note, the final packet will be larger because
        // of IP header data.
        let mut rng = new_rng(70812476915813);
        let body: Vec<u8> = core::iter::repeat_with(|| rng.gen()).take(5000).collect();

        // Ip packet from some node destined to a remote on this network,
        // arriving locally.
        let mut ipv6_packet_buf = Buf::new(body.clone(), ..)
            .encapsulate(Ipv6PacketBuilder::new(
                extra_ip,
                dummy_config.remote_ip,
                64,
                IpProto::Udp.into(),
            ))
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
        // MTU should match the MTU for the link.
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
            .encapsulate(Ipv4PacketBuilder::new(src_ip, dst_ip, 64, Ipv4Proto::Icmp))
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
            .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, 64, Ipv6Proto::Icmpv6))
            .serialize_vec_outer()
            .unwrap();

        ret.into_inner()
    }

    #[ip_test]
    fn test_ip_update_pmtu<I: Ip + TestIpExt>() {
        // Test receiving a Packet Too Big (IPv6) or Dest Unreachable
        // Fragmentation Required (IPv4) which should update the PMTU if it is
        // less than the current value.

        let dummy_config = I::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone()).build();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        // Update PMTU from None.

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
            get_state_inner::<I, DummyEventDispatcher>(&ctx.state)
                .pmtu_cache
                .get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            new_mtu1
        );

        // Don't update PMTU when current PMTU is less than reported MTU.

        let new_mtu2 = u32::from(I::MINIMUM_LINK_MTU) + 200;

        // Create IPv6 ICMPv6 packet too big packet with MTU larger than current
        // PMTU.
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
            get_state_inner::<I, DummyEventDispatcher>(&ctx.state)
                .pmtu_cache
                .get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            new_mtu1
        );

        // Update PMTU when current PMTU is greater than the reported MTU.

        let new_mtu3 = u32::from(I::MINIMUM_LINK_MTU) + 50;

        // Create IPv6 ICMPv6 packet too big packet with MTU smaller than
        // current PMTU.
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
            get_state_inner::<I, DummyEventDispatcher>(&ctx.state)
                .pmtu_cache
                .get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            new_mtu3
        );
    }

    #[ip_test]
    fn test_ip_update_pmtu_too_low<I: Ip + TestIpExt>() {
        // Test receiving a Packet Too Big (IPv6) or Dest Unreachable
        // Fragmentation Required (IPv4) which should not update the PMTU if it
        // is less than the min MTU.

        let dummy_config = I::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone()).build();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        // Update PMTU from None but with an MTU too low.

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

        assert_eq!(
            get_state_inner::<I, DummyEventDispatcher>(&ctx.state)
                .pmtu_cache
                .get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
            None
        );
    }

    /// Create buffer to be used as the ICMPv4 message body
    /// where the original packet's body  length is `body_len`.
    fn create_orig_packet_buf(src_ip: Ipv4Addr, dst_ip: Ipv4Addr, body_len: usize) -> Buf<Vec<u8>> {
        Buf::new(vec![0; body_len], ..)
            .encapsulate(Ipv4PacketBuilder::new(src_ip, dst_ip, 64, IpProto::Udp.into()))
            .serialize_vec_outer()
            .unwrap()
            .into_inner()
    }

    #[test]
    fn test_ipv4_remote_no_rfc1191() {
        // Test receiving an IPv4 Dest Unreachable Fragmentation
        // Required from a node that does not implement RFC 1191.

        let dummy_config = Ipv4::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(dummy_config.clone()).build();
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;

        // Update from None.

        // Create ICMP IP buf w/ orig packet body len = 500; orig packet len =
        // 520
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
            get_state_inner::<Ipv4, DummyEventDispatcher>(&ctx.state)
                .pmtu_cache
                .get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            508
        );

        // Don't Update when packet size is too small.

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
            get_state_inner::<Ipv4, DummyEventDispatcher>(&ctx.state)
                .pmtu_cache
                .get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            508
        );

        // Update to lower PMTU estimate based on original packet size.

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
            get_state_inner::<Ipv4, DummyEventDispatcher>(&ctx.state)
                .pmtu_cache
                .get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            68
        );

        // Should not update PMTU because the next low PMTU from this original
        // packet size is higher than current PMTU.

        // Create ICMP IP buf w/ orig packet body len = 290; orig packet len =
        // 310
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
            get_state_inner::<Ipv4, DummyEventDispatcher>(&ctx.state)
                .pmtu_cache
                .get_pmtu(dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            68
        );
    }

    #[test]
    fn test_invalid_icmpv4_in_ipv6() {
        let ip_config = Ipv6::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(ip_config.clone()).build();
        let device = DeviceId::new_ethernet(1);
        let frame_dst = FrameDestination::Unicast;

        let ic_config = Ipv4::DUMMY_CONFIG;
        let icmp_builder = IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
            ic_config.remote_ip,
            ic_config.local_ip,
            IcmpUnusedCode,
            IcmpEchoRequest::new(0, 0),
        );

        let ip_builder = Ipv6PacketBuilder::new(
            ip_config.remote_ip,
            ip_config.local_ip,
            64,
            Ipv6Proto::Other(Ipv4Proto::Icmp.into()),
        );

        let buf = Buf::new(Vec::new(), ..)
            .encapsulate(icmp_builder)
            .encapsulate(ip_builder)
            .serialize_vec_outer()
            .unwrap();

        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);

        // Should not have dispatched the packet.
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 0);

        // In IPv6, the next header value (ICMP(v4)) would have been considered
        // unrecognized so an ICMP parameter problem response SHOULD be sent,
        // but the netstack chooses to just drop the packet since we are not
        // required to send the ICMP response.
        assert_empty(ctx.dispatcher.frames_sent().iter());
    }

    #[test]
    fn test_invalid_icmpv6_in_ipv4() {
        let ip_config = Ipv4::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(ip_config.clone()).build();
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

        let ip_builder = Ipv4PacketBuilder::new(
            ip_config.remote_ip,
            ip_config.local_ip,
            64,
            Ipv4Proto::Other(Ipv6Proto::Icmpv6.into()),
        );

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
    fn test_joining_leaving_ip_multicast_group<
        I: Ip
            + TestIpExt
            + packet_formats::ip::IpExt
            + crate::device::testutil::DeviceTestIpExt<DummyInstant>,
    >() {
        #[specialize_ip_address]
        fn get_multicast_addr<A: IpAddress>() -> A {
            #[ipv4addr]
            return Ipv4Addr::new([224, 0, 0, 1]);

            #[ipv6addr]
            return Ipv6Addr::new([0xff11, 0, 0, 0, 0, 0, 0, 1]);
        }

        // Test receiving a packet destined to a multicast IP (and corresponding
        // multicast MAC).

        let config = I::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::from_config(config.clone()).build();
        let device = DeviceId::new_ethernet(0);
        let multi_addr = get_multicast_addr::<I::Addr>();
        let dst_mac = Mac::from(&MulticastAddr::new(multi_addr).unwrap());
        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(I::PacketBuilder::new(
                config.remote_ip.get(),
                multi_addr,
                64,
                IpProto::Udp.into(),
            ))
            .encapsulate(EthernetFrameBuilder::new(config.remote_mac.get(), dst_mac, I::ETHER_TYPE))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .into_inner();

        let multi_addr = MulticastAddr::new(multi_addr).unwrap();
        // Should not have dispatched the packet since we are not in the
        // multicast group `multi_addr`.
        assert!(!I::get_ip_device_state(&ctx, device).multicast_groups.contains(&multi_addr));
        receive_frame(&mut ctx, device, buf.clone()).expect("error receiving frame");
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        // Join the multicast group and receive the packet, we should dispatch
        // it.
        crate::device::join_ip_multicast(&mut ctx, device, multi_addr);
        assert!(I::get_ip_device_state(&ctx, device).multicast_groups.contains(&multi_addr));
        receive_frame(&mut ctx, device, buf.clone()).expect("error receiving frame");
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        // Leave the multicast group and receive the packet, we should not
        // dispatch it.
        crate::device::leave_ip_multicast(&mut ctx, device, multi_addr);
        assert!(!I::get_ip_device_state(&ctx, device).multicast_groups.contains(&multi_addr));
        receive_frame(&mut ctx, device, buf.clone()).expect("error receiving frame");
        assert_eq!(get_counter_val(&mut ctx, dispatch_receive_ip_packet_name::<I>()), 1);
    }

    #[test]
    fn test_no_dispatch_non_ndp_packets_during_ndp_dad() {
        // Here we make sure we are not dispatching packets destined to a
        // tentative address (that is performing NDP's Duplicate Address
        // Detection (DAD)) -- IPv6 only.

        // We explicitly call `build_with` when building our context below
        // because `build` will set the default NDP parameter
        // DUP_ADDR_DETECT_TRANSMITS to 0 (effectively disabling DAD) so we use
        // our own custom `StackStateBuilder` to set it to the default value of
        // `1` (see `DUP_ADDR_DETECT_TRANSMITS`).
        let config = Ipv6::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::default()
            .build_with(StackStateBuilder::default(), DummyEventDispatcher::default());
        let device = ctx.state.add_ethernet_device(config.local_mac, Ipv6::MINIMUM_LINK_MTU.into());
        crate::device::initialize_device(&mut ctx, device);

        let frame_dst = FrameDestination::Unicast;

        let ip: Ipv6Addr = config.local_mac.to_ipv6_link_local().addr().get();

        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(Ipv6PacketBuilder::new(config.remote_ip, ip, 64, IpProto::Udp.into()))
            .serialize_vec_outer()
            .unwrap()
            .into_inner();

        // Received packet should not have been dispatched.
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 0);

        // Make sure all timers are done (initial DAD to complete on the
        // interface).
        trigger_timers_until(&mut ctx, |_| false);

        // Received packet should have been dispatched.
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 1);

        // Set the new IP (this should trigger DAD).
        let ip = config.local_ip.get();
        crate::device::add_ip_addr_subnet(&mut ctx, device, AddrSubnet::new(ip, 128).unwrap())
            .unwrap();

        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(Ipv6PacketBuilder::new(config.remote_ip, ip, 64, IpProto::Udp.into()))
            .serialize_vec_outer()
            .unwrap()
            .into_inner();

        // Received packet should not have been dispatched.
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 1);

        // Make sure all timers are done (DAD to complete on the interface due
        // to new IP).
        trigger_timers_until(&mut ctx, |_| false);

        // Received packet should have been dispatched.
        receive_ipv6_packet(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ipv6_packet"), 2);
    }

    #[test]
    fn test_drop_non_unicast_ipv6_source() {
        // Test that an inbound IPv6 packet with a non-unicast source address is
        // dropped.
        let cfg = DUMMY_CONFIG_V6;
        let mut ctx = DummyEventDispatcherBuilder::from_config(cfg.clone()).build();
        let device = ctx.state.add_ethernet_device(cfg.local_mac, Ipv6::MINIMUM_LINK_MTU.into());
        crate::device::initialize_device(&mut ctx, device);

        let ip: Ipv6Addr = cfg.local_mac.to_ipv6_link_local().addr().get();
        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(Ipv6PacketBuilder::new(
                Ipv6::MULTICAST_SUBNET.network(),
                ip,
                64,
                IpProto::Udp.into(),
            ))
            .serialize_vec_outer()
            .unwrap()
            .into_inner();

        receive_ipv6_packet(&mut ctx, device, FrameDestination::Unicast, buf.clone());
        assert_eq!(get_counter_val(&mut ctx, "receive_ipv6_packet: non-unicast source"), 1);
    }

    #[test]
    fn test_receive_ip_packet_action() {
        let v4_config = Ipv4::DUMMY_CONFIG;
        let v6_config = Ipv6::DUMMY_CONFIG;

        let mut builder = DummyEventDispatcherBuilder::default();
        // Both devices have the same MAC address, which is a bit weird, but not
        // a problem for this test.
        let v4_subnet = AddrSubnet::from_witness(v4_config.local_ip, 16).unwrap().subnet();
        builder.add_device_with_ip(v4_config.local_mac, v4_config.local_ip.get(), v4_subnet);
        builder.add_device_with_ip(
            v6_config.local_mac,
            v6_config.local_ip.get(),
            AddrSubnet::from_witness(v6_config.local_ip, 64).unwrap().subnet(),
        );
        let v4_dev = DeviceId::new_ethernet(0);
        let v6_dev = DeviceId::new_ethernet(1);

        let mut ctx = builder.clone().build();

        // Receive packet addressed to us.
        assert_eq!(
            receive_ipv4_packet_action(&mut ctx, v4_dev, v4_config.local_ip),
            ReceivePacketAction::Deliver
        );
        assert_eq!(
            receive_ipv6_packet_action(&mut ctx, v6_dev, v6_config.local_ip),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to the IPv4 subnet broadcast address.
        assert_eq!(
            receive_ipv4_packet_action(
                &mut ctx,
                v4_dev,
                SpecifiedAddr::new(v4_subnet.broadcast()).unwrap()
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to the IPv4 limited broadcast address.
        assert_eq!(
            receive_ipv4_packet_action(&mut ctx, v4_dev, Ipv4::LIMITED_BROADCAST_ADDRESS),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to a multicast address we're subscribed to.
        crate::device::join_ip_multicast(&mut ctx, v4_dev, Ipv4::ALL_ROUTERS_MULTICAST_ADDRESS);
        assert_eq!(
            receive_ipv4_packet_action(
                &mut ctx,
                v4_dev,
                Ipv4::ALL_ROUTERS_MULTICAST_ADDRESS.into_specified()
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to the all-nodes multicast address.
        assert_eq!(
            receive_ipv6_packet_action(
                &mut ctx,
                v6_dev,
                Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.into_specified()
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to a multicast address we're subscribed to.
        assert_eq!(
            receive_ipv6_packet_action(
                &mut ctx,
                v6_dev,
                v6_config.local_ip.to_solicited_node_address().into_specified(),
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to a tentative address.
        {
            // Construct a one-off context that has DAD enabled. The context
            // built above with `builder.clone().build()` has DAD disabled, and
            // so addresses start off in the assigned state rather than the
            // tentative state.
            let mut ctx =
                builder.build_with(StackStateBuilder::default(), DummyEventDispatcher::default());
            let tentative: UnicastAddr<Ipv6Addr> =
                v6_config.local_mac.to_ipv6_link_local().addr().get();
            assert_eq!(
                receive_ipv6_packet_action(&mut ctx, v6_dev, tentative.into_specified()),
                ReceivePacketAction::Drop { reason: DropReason::Tentative }
            );
        }

        // Receive packet destined to a remote address when forwarding is
        // disabled on the inbound interface.
        assert_eq!(
            receive_ipv4_packet_action(&mut ctx, v4_dev, v4_config.remote_ip),
            ReceivePacketAction::Drop { reason: DropReason::ForwardingDisabledInboundIface }
        );
        assert_eq!(
            receive_ipv6_packet_action(&mut ctx, v6_dev, v6_config.remote_ip),
            ReceivePacketAction::Drop { reason: DropReason::ForwardingDisabledInboundIface }
        );

        // Receive packet destined to a remote address when forwarding is
        // enabled both globally and on the inbound device.
        set_routing_enabled::<_, Ipv4>(&mut ctx, v4_dev, true)
            .expect("error setting routing enabled");
        set_routing_enabled::<_, Ipv6>(&mut ctx, v6_dev, true)
            .expect("error setting routing enabled");
        assert_eq!(
            receive_ipv4_packet_action(&mut ctx, v4_dev, v4_config.remote_ip),
            ReceivePacketAction::Forward {
                dst: Destination { next_hop: v4_config.remote_ip, device: v4_dev }
            }
        );
        assert_eq!(
            receive_ipv6_packet_action(&mut ctx, v6_dev, v6_config.remote_ip),
            ReceivePacketAction::Forward {
                dst: Destination { next_hop: v6_config.remote_ip, device: v6_dev }
            }
        );

        // Receive packet destined to a host with no route when forwarding is
        // enabled both globally and on the inbound device.
        ctx.state.ipv4.inner.table = Default::default();
        ctx.state.ipv6.inner.table = Default::default();
        assert_eq!(
            receive_ipv4_packet_action(&mut ctx, v4_dev, v4_config.remote_ip),
            ReceivePacketAction::SendNoRouteToDest
        );
        assert_eq!(
            receive_ipv6_packet_action(&mut ctx, v6_dev, v6_config.remote_ip),
            ReceivePacketAction::SendNoRouteToDest
        );
    }
}
