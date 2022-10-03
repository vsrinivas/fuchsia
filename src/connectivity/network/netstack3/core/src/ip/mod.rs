// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Protocol, versions 4 and 6.

#[macro_use]
pub(crate) mod path_mtu;

pub mod device;
pub mod forwarding;
pub(crate) mod gmp;
pub mod icmp;
mod integration;
mod ipv6;
pub(crate) mod reassembly;
pub mod socket;
pub mod types;

use alloc::vec::Vec;
use core::{
    fmt::{self, Debug, Display, Formatter},
    hash::Hash,
    num::NonZeroU8,
    sync::atomic::{AtomicU16, Ordering},
};

use derivative::Derivative;
use log::{debug, trace};
use net_types::{
    ip::{Ip, IpAddress, IpVersion, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, Ipv6SourceAddr, Subnet},
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

use crate::{
    context::{
        CounterContext, EventContext, InstantContext, NonTestCtxMarker, RngContext, TimerHandler,
    },
    data_structures::token_bucket::TokenBucket,
    device::{DeviceId, FrameDestination},
    error::{ExistsError, NotFoundError},
    ip::{
        device::IpDeviceNonSyncContext,
        forwarding::{AddRouteError, Destination, ForwardingTable},
        gmp::igmp::IgmpPacketHandler,
        icmp::{
            BufferIcmpHandler, IcmpHandlerIpExt, IcmpIpExt, IcmpIpTransportContext, IcmpSockets,
            Icmpv4Error, Icmpv4ErrorCode, Icmpv4ErrorKind, Icmpv4State, Icmpv4StateBuilder,
            Icmpv6ErrorCode, Icmpv6ErrorKind, Icmpv6State, Icmpv6StateBuilder, InnerIcmpContext,
        },
        ipv6::Ipv6PacketAction,
        path_mtu::{PmtuCache, PmtuTimerId},
        reassembly::{
            FragmentCacheKey, FragmentHandler, FragmentProcessingState, IpPacketFragmentCache,
        },
        socket::{
            BufferIpSocketHandler, DefaultSendOptions, IpSock, IpSockRoute, IpSockRouteError,
            IpSockUnroutableError, IpSocketContext, IpSocketHandler,
        },
    },
    sync::{Mutex, RwLock},
    BufferNonSyncContext, Instant, NonSyncContext, SyncCtx,
};

/// Default IPv4 TTL.
const DEFAULT_TTL: NonZeroU8 = nonzero!(64u8);

/// Hop limits for packets sent to multicast and unicast destinations.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct HopLimits {
    pub(crate) unicast: NonZeroU8,
    pub(crate) multicast: NonZeroU8,
}

/// Default hop limits for sockets.
pub(crate) const DEFAULT_HOP_LIMITS: HopLimits =
    HopLimits { unicast: DEFAULT_TTL, multicast: nonzero!(1u8) };

/// The IPv6 subnet that contains all addresses; `::/0`.
// Safe because 0 is less than the number of IPv6 address bits.
const IPV6_DEFAULT_SUBNET: Subnet<Ipv6Addr> =
    unsafe { Subnet::new_unchecked(Ipv6::UNSPECIFIED_ADDRESS, 0) };

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
pub(crate) trait IpTransportContext<I: IcmpIpExt, C, SC: IpDeviceIdContext<I> + ?Sized> {
    /// Receive an ICMP error message.
    ///
    /// All arguments beginning with `original_` are fields from the IP packet
    /// that triggered the error. The `original_body` is provided here so that
    /// the error can be associated with a transport-layer socket. `device`
    /// identifies the device that received the ICMP error message packet.
    ///
    /// While ICMPv4 error messages are supposed to contain the first 8 bytes of
    /// the body of the offending packet, and ICMPv6 error messages are supposed
    /// to contain as much of the offending packet as possible without violating
    /// the IPv6 minimum MTU, the caller does NOT guarantee that either of these
    /// hold. It is `receive_icmp_error`'s responsibility to handle any length
    /// of `original_body`, and to perform any necessary validation.
    fn receive_icmp_error(
        sync_ctx: &mut SC,
        ctx: &mut C,
        device: SC::DeviceId,
        original_src_ip: Option<SpecifiedAddr<I::Addr>>,
        original_dst_ip: SpecifiedAddr<I::Addr>,
        original_body: &[u8],
        err: I::ErrorCode,
    );
}

/// The execution context provided by a transport layer protocol to the IP layer
/// when a buffer is required.
pub(crate) trait BufferIpTransportContext<
    I: IpExt,
    C,
    SC: IpDeviceIdContext<I> + ?Sized,
    B: BufferMut,
>: IpTransportContext<I, C, SC>
{
    /// Receive a transport layer packet in an IP packet.
    ///
    /// In the event of an unreachable port, `receive_ip_packet` returns the
    /// buffer in its original state (with the transport packet un-parsed) in
    /// the `Err` variant.
    fn receive_ip_packet(
        sync_ctx: &mut SC,
        ctx: &mut C,
        device: SC::DeviceId,
        src_ip: I::RecvSrcAddr,
        dst_ip: SpecifiedAddr<I::Addr>,
        buffer: B,
    ) -> Result<(), (B, TransportReceiveError)>;
}

impl<I: IcmpIpExt, C, SC: IpDeviceIdContext<I> + ?Sized> IpTransportContext<I, C, SC> for () {
    fn receive_icmp_error(
        _sync_ctx: &mut SC,
        _ctx: &mut C,
        _device: SC::DeviceId,
        _original_src_ip: Option<SpecifiedAddr<I::Addr>>,
        _original_dst_ip: SpecifiedAddr<I::Addr>,
        _original_body: &[u8],
        err: I::ErrorCode,
    ) {
        trace!("IpTransportContext::receive_icmp_error: Received ICMP error message ({:?}) for unsupported IP protocol", err);
    }
}

impl<I: IpExt, C, SC: IpDeviceIdContext<I> + ?Sized, B: BufferMut>
    BufferIpTransportContext<I, C, SC, B> for ()
{
    fn receive_ip_packet(
        _sync_ctx: &mut SC,
        _ctx: &mut C,
        _device: SC::DeviceId,
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
pub(crate) trait TransportIpContext<I: IpExt, C>:
    IpDeviceIdContext<I> + IpSocketHandler<I, C>
{
    /// Is this one of our local addresses, and is it in the assigned state?
    ///
    /// If `addr` is the address associated with a local interface and, for
    /// IPv6, if it is in the "assigned" state, this method returns the
    /// identifier for the device with the address assigned. Otherwise returns
    /// `None`.
    fn get_device_with_assigned_addr(&self, addr: SpecifiedAddr<I::Addr>)
        -> Option<Self::DeviceId>;

    /// Get default hop limits.
    ///
    /// If `device` is not `None` and exists, its hop limits will be returned.
    /// Otherwise the system defaults are returned.
    fn get_default_hop_limits(&self, device: Option<Self::DeviceId>) -> HopLimits;
}

/// The execution context provided by the IP layer to transport layer protocols
/// when a buffer is provided.
///
/// `BufferTransportIpContext` is like [`TransportIpContext`], except that it
/// also requires that the context be capable of receiving frames in buffers of
/// type `B`. This is used when a buffer of type `B` is provided to IP, and
/// allows any generated link-layer frames to reuse that buffer rather than
/// needing to always allocate a new one.
pub(crate) trait BufferTransportIpContext<I: IpExt, C, B: BufferMut>:
    TransportIpContext<I, C> + BufferIpSocketHandler<I, C, B>
{
}

impl<I: IpExt, B: BufferMut, C, SC: TransportIpContext<I, C> + BufferIpSocketHandler<I, C, B>>
    BufferTransportIpContext<I, C, B> for SC
{
}

// TODO(joshlf): With all 256 protocol numbers (minus reserved ones) given their
// own associated type in both traits, running `cargo check` on a 2018 MacBook
// Pro takes over a minute. Eventually - and before we formally publish this as
// a library - we should identify the bottleneck in the compiler and optimize
// it. For the time being, however, we only support protocol numbers that we
// actually use (TCP and UDP).

/// The execution context for IP's transport layer.
///
/// `IpTransportLayerContext` defines the [`IpTransportContext`] for each IP
/// protocol number that is common to all IP protocols.
trait IpTransportLayerContext<I: IpExt, C>: IpDeviceIdContext<I> {
    type Tcp: IpTransportContext<I, C, Self>;
    type Udp: IpTransportContext<I, C, Self>;
}

impl<
        I: IpExt,
        C: crate::transport::udp::UdpStateNonSyncContext<I>
            + crate::transport::tcp::socket::TcpNonSyncContext,
        SC: crate::transport::udp::UdpStateContext<I, C>
            + crate::transport::tcp::socket::TcpSyncContext<I, C>,
    > IpTransportLayerContext<I, C> for SC
{
    type Tcp = crate::transport::tcp::socket::TcpIpTransportContext;
    type Udp = crate::transport::udp::UdpIpTransportContext;
}

/// Execution context for IP's transport layer with a buffer.
trait BufferIpTransportLayerContext<I: IpExt, C, B: BufferMut>: IpTransportLayerContext<I, C> {
    type Tcp: BufferIpTransportContext<I, C, Self, B>;
    type Udp: BufferIpTransportContext<I, C, Self, B>;
}

impl<I: IpExt, B: BufferMut, C: RngContext, SC: IpTransportLayerContext<I, C>>
    BufferIpTransportLayerContext<I, C, B> for SC
where
    SC::Tcp: BufferIpTransportContext<I, C, SC, B>,
    SC::Udp: BufferIpTransportContext<I, C, SC, B>,
{
    type Tcp = SC::Tcp;
    type Udp = SC::Udp;
}

impl<C, SC: IpDeviceContext<Ipv4, C> + IpSocketHandler<Ipv4, C>> TransportIpContext<Ipv4, C>
    for SC
{
    fn get_device_with_assigned_addr(&self, addr: SpecifiedAddr<Ipv4Addr>) -> Option<SC::DeviceId> {
        match self.address_status(addr) {
            AddressStatus::Present((device, state)) => match state {
                Ipv4PresentAddressStatus::Unicast => Some(device),
                Ipv4PresentAddressStatus::LimitedBroadcast
                | Ipv4PresentAddressStatus::SubnetBroadcast
                | Ipv4PresentAddressStatus::Multicast => None,
            },
            AddressStatus::Unassigned => None,
        }
    }

    fn get_default_hop_limits(&self, device: Option<Self::DeviceId>) -> HopLimits {
        match device {
            Some(device) => HopLimits { unicast: self.get_hop_limit(device), ..DEFAULT_HOP_LIMITS },
            None => DEFAULT_HOP_LIMITS,
        }
    }
}

impl<C, SC: IpDeviceContext<Ipv6, C> + IpSocketHandler<Ipv6, C>> TransportIpContext<Ipv6, C>
    for SC
{
    fn get_device_with_assigned_addr(&self, addr: SpecifiedAddr<Ipv6Addr>) -> Option<SC::DeviceId> {
        match self.address_status(addr) {
            AddressStatus::Present((device, status)) => match status {
                Ipv6PresentAddressStatus::UnicastAssigned => Some(device),
                Ipv6PresentAddressStatus::Multicast
                | Ipv6PresentAddressStatus::UnicastTentative => None,
            },
            AddressStatus::Unassigned => None,
        }
    }

    fn get_default_hop_limits(&self, device: Option<Self::DeviceId>) -> HopLimits {
        match device {
            Some(device) => HopLimits { unicast: self.get_hop_limit(device), ..DEFAULT_HOP_LIMITS },
            None => DEFAULT_HOP_LIMITS,
        }
    }
}

/// An IP device ID.
pub trait IpDeviceId: Copy + Display + Debug + Eq + Hash + PartialEq + Send + Sync {
    /// Returns true if the device is a loopback device.
    fn is_loopback(&self) -> bool;
}

pub(crate) trait DualStackDeviceIdContext {
    type DualStackDeviceId: IpDeviceId;
}

/// An execution context which provides a `DeviceId` type for various IP
/// internals to share.
///
/// This trait provides the associated `DeviceId` type, and is used by
/// [`IgmpContext`], [`MldContext`], and [`InnerIcmpContext`]. It allows them to use
/// the same `DeviceId` type rather than each providing their own, which would
/// require lots of verbose type bounds when they need to be interoperable (such
/// as when ICMP delivers an MLD packet to the `mld` module for processing).
pub(crate) trait IpDeviceIdContext<I: Ip> {
    /// The type of device IDs.
    type DeviceId: IpDeviceId + 'static;

    /// Returns the ID of the loopback interface, if one exists on the system
    /// and is initialized.
    fn loopback_id(&self) -> Option<Self::DeviceId>;
}

/// The status of an IP address on an interface.
pub(crate) enum AddressStatus<S> {
    Present(S),
    Unassigned,
}

impl<D: IpDeviceId, S> AddressStatus<(D, S)> {
    fn drop_device(self) -> AddressStatus<S> {
        match self {
            Self::Present((_d, s)) => AddressStatus::Present(s),
            Self::Unassigned => AddressStatus::Unassigned,
        }
    }
}

/// The status of an IPv4 address.
pub(crate) enum Ipv4PresentAddressStatus {
    LimitedBroadcast,
    SubnetBroadcast,
    Multicast,
    Unicast,
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
    type AddressStatus = Ipv4PresentAddressStatus;
}

impl IpLayerIpExt for Ipv6 {
    type AddressStatus = Ipv6PresentAddressStatus;
}

/// An extension trait providing IP layer state properties.
pub(crate) trait IpLayerStateIpExt<I: Instant, DeviceId>: IpLayerIpExt {
    type State: AsRef<IpStateInner<Self, I, DeviceId>>;
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
pub(crate) trait IpStateContext<
    I: IpLayerStateIpExt<Instant, Self::DeviceId>,
    Instant: crate::Instant,
>: IpDeviceIdContext<I>
{
    /// Calls the function with an immutable reference to IP layer state.
    fn with_ip_layer_state<O, F: FnOnce(&I::State) -> O>(&self, cb: F) -> O;
}

/// The IP device context provided to the IP layer.
pub(crate) trait IpDeviceContext<I: IpLayerIpExt, C>: IpDeviceIdContext<I> {
    /// Is the device enabled?
    fn is_ip_device_enabled(&self, device_id: Self::DeviceId) -> bool;

    /// Returns the best local address with communicating with the remote.
    fn get_local_addr_for_remote(
        &self,
        device_id: Self::DeviceId,
        remote: SpecifiedAddr<I::Addr>,
    ) -> Option<SpecifiedAddr<I::Addr>>;

    /// Gets the status of an address.
    ///
    /// Returns the status of the address if it is assigned, and the device it
    /// is assigned to.
    fn address_status(
        &self,
        addr: SpecifiedAddr<I::Addr>,
    ) -> AddressStatus<(Self::DeviceId, I::AddressStatus)>;

    /// Gets the status of an address.
    ///
    /// Only the specified device will be checked for the address. Returns
    /// [`AddressStatus::Unassigned`] if the address is not assigned to the
    /// device.
    fn address_status_for_device(
        &self,
        addr: SpecifiedAddr<I::Addr>,
        device_id: Self::DeviceId,
    ) -> AddressStatus<I::AddressStatus>;

    /// Returns true iff the device has routing enabled.
    fn is_device_routing_enabled(&self, device_id: Self::DeviceId) -> bool;

    /// Returns the hop limit.
    fn get_hop_limit(&self, device_id: Self::DeviceId) -> NonZeroU8;

    /// Returns the MTU of the device.
    fn get_mtu(&self, device_id: Self::DeviceId) -> u32;
}

/// Events observed at the IP layer.
#[derive(Debug, Eq, Hash, PartialEq)]
pub enum IpLayerEvent<DeviceId, I: Ip> {
    /// A device route was added.
    DeviceRouteAdded {
        /// The resolved device.
        device: DeviceId,
        /// The destination subnet.
        subnet: Subnet<I::Addr>,
    },
    /// A device route was removed.
    DeviceRouteRemoved {
        /// The resolved device.
        device: DeviceId,
        /// The destination subnet.
        subnet: Subnet<I::Addr>,
    },
}

/// The non-synchronized execution context for the IP layer.
pub(crate) trait IpLayerNonSyncContext<I: Ip, DeviceId>:
    InstantContext + EventContext<IpLayerEvent<DeviceId, I>> + CounterContext
{
}
impl<
        I: Ip,
        DeviceId,
        C: InstantContext + EventContext<IpLayerEvent<DeviceId, I>> + CounterContext,
    > IpLayerNonSyncContext<I, DeviceId> for C
{
}

/// The execution context for the IP layer.
pub(crate) trait IpLayerContext<
    I: IpLayerStateIpExt<C::Instant, Self::DeviceId>,
    C: IpLayerNonSyncContext<I, <Self as IpDeviceIdContext<I>>::DeviceId>,
>: IpStateContext<I, C::Instant> + IpDeviceContext<I, C>
{
}

impl<
        I: IpLayerStateIpExt<C::Instant, SC::DeviceId>,
        C: IpLayerNonSyncContext<I, <SC as IpDeviceIdContext<I>>::DeviceId>,
        SC: IpStateContext<I, C::Instant> + IpDeviceContext<I, C>,
    > IpLayerContext<I, C> for SC
{
}

impl<
        C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId> + IpLayerNonSyncContext<Ipv4, SC::DeviceId>,
        SC: IpLayerContext<Ipv4, C> + device::IpDeviceContext<Ipv4, C>,
    > IpSocketContext<Ipv4, C> for SC
{
    fn lookup_route(
        &self,
        ctx: &mut C,
        device: Option<SC::DeviceId>,
        local_ip: Option<SpecifiedAddr<Ipv4Addr>>,
        addr: SpecifiedAddr<Ipv4Addr>,
    ) -> Result<IpSockRoute<Ipv4, SC::DeviceId>, IpSockRouteError> {
        let get_local_addr = |device, local_ip| {
            if let Some(local_ip) = local_ip {
                match self.address_status_for_device(local_ip, device) {
                    AddressStatus::Present(Ipv4PresentAddressStatus::Unicast) => Ok(local_ip),
                    AddressStatus::Present(
                        Ipv4PresentAddressStatus::LimitedBroadcast
                        | Ipv4PresentAddressStatus::SubnetBroadcast
                        | Ipv4PresentAddressStatus::Multicast,
                    )
                    | AddressStatus::Unassigned => {
                        Err(IpSockUnroutableError::LocalAddrNotAssigned.into())
                    }
                }
            } else {
                self.get_local_addr_for_remote(device, addr)
                    .ok_or(IpSockRouteError::NoLocalAddrAvailable)
            }
        };

        // Check if locally destined.
        match self.address_status(addr) {
            AddressStatus::Present((device_id, Ipv4PresentAddressStatus::Unicast)) => {
                if let Some(loopback) = self.loopback_id() {
                    Ok(IpSockRoute {
                        // TODO(https://fxbug.dev/94965): Allow local IPs from any
                        // interface for locally-destined packets.
                        local_ip: get_local_addr(device_id, local_ip)?,
                        destination: Destination { device: loopback, next_hop: addr },
                    })
                } else {
                    Err(IpSockUnroutableError::NoRouteToRemoteAddr.into())
                }
            }
            AddressStatus::Present((
                _,
                Ipv4PresentAddressStatus::LimitedBroadcast
                | Ipv4PresentAddressStatus::SubnetBroadcast
                | Ipv4PresentAddressStatus::Multicast,
            ))
            | AddressStatus::Unassigned => lookup_route(self, ctx, device, addr)
                .map(|destination| {
                    let Destination { device, next_hop: _ } = &destination;
                    Ok(IpSockRoute { local_ip: get_local_addr(*device, local_ip)?, destination })
                })
                .unwrap_or(Err(IpSockUnroutableError::NoRouteToRemoteAddr.into())),
        }
    }
}

impl<
        C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId> + IpLayerNonSyncContext<Ipv6, SC::DeviceId>,
        SC: IpLayerContext<Ipv6, C> + device::IpDeviceContext<Ipv6, C>,
    > IpSocketContext<Ipv6, C> for SC
{
    fn lookup_route(
        &self,
        ctx: &mut C,
        device: Option<SC::DeviceId>,
        local_ip: Option<SpecifiedAddr<Ipv6Addr>>,
        addr: SpecifiedAddr<Ipv6Addr>,
    ) -> Result<IpSockRoute<Ipv6, SC::DeviceId>, IpSockRouteError> {
        let get_local_addr = |device, local_ip| {
            if let Some(local_ip) = local_ip {
                // TODO(joshlf):
                // - Allow the specified local IP to be the local IP of a
                //   different device so long as we're operating in the weak
                //   host model.
                // - What about when the socket is bound to a device? How does
                //   that affect things?
                match self.address_status_for_device(local_ip, device) {
                    AddressStatus::Present(Ipv6PresentAddressStatus::UnicastAssigned) => {
                        Ok(local_ip)
                    }
                    AddressStatus::Present(
                        Ipv6PresentAddressStatus::Multicast
                        | Ipv6PresentAddressStatus::UnicastTentative,
                    )
                    | AddressStatus::Unassigned => {
                        Err(IpSockUnroutableError::LocalAddrNotAssigned.into())
                    }
                }
            } else {
                self.get_local_addr_for_remote(device, addr)
                    .ok_or(IpSockRouteError::NoLocalAddrAvailable)
            }
        };

        // Check if locally destined.
        //
        // TODO(https://fxbug.dev/93870): Encode the delivery of locally
        // destined packets to loopback in the route table.
        match self.address_status(addr) {
            AddressStatus::Present((device_id, Ipv6PresentAddressStatus::UnicastAssigned)) => {
                if let Some(loopback) = self.loopback_id() {
                    Ok(IpSockRoute {
                        // TODO(https://fxbug.dev/94965): Allow local IPs from any
                        // interface for locally-destined packets.
                        local_ip: get_local_addr(device_id, local_ip)?,
                        destination: Destination { device: loopback, next_hop: addr },
                    })
                } else {
                    return Err(IpSockUnroutableError::NoRouteToRemoteAddr.into());
                }
            }
            AddressStatus::Present((
                _,
                Ipv6PresentAddressStatus::UnicastTentative | Ipv6PresentAddressStatus::Multicast,
            ))
            | AddressStatus::Unassigned => lookup_route(self, ctx, device, addr)
                .map(|destination| {
                    let Destination { device, next_hop: _ } = &destination;
                    Ok(IpSockRoute { local_ip: get_local_addr(*device, local_ip)?, destination })
                })
                .unwrap_or(Err(IpSockUnroutableError::NoRouteToRemoteAddr.into())),
        }
    }
}

impl<NonSyncCtx: NonSyncContext> IpStateContext<Ipv4, NonSyncCtx::Instant>
    for &'_ SyncCtx<NonSyncCtx>
{
    fn with_ip_layer_state<O, F: FnOnce(&Ipv4State<NonSyncCtx::Instant, DeviceId>) -> O>(
        &self,
        cb: F,
    ) -> O {
        cb(&self.state.ipv4)
    }
}

impl<NonSyncCtx: NonSyncContext> IpStateContext<Ipv6, NonSyncCtx::Instant>
    for &'_ SyncCtx<NonSyncCtx>
{
    fn with_ip_layer_state<O, F: FnOnce(&Ipv6State<NonSyncCtx::Instant, DeviceId>) -> O>(
        &self,
        cb: F,
    ) -> O {
        cb(&self.state.ipv6)
    }
}

/// The transport context provided to the IP layer requiring a buffer type.
pub(crate) trait BufferTransportContext<I: IpLayerIpExt, C, B: BufferMut>:
    IpDeviceIdContext<I>
{
    /// Dispatches a received incoming IP packet to the appropriate protocol.
    fn dispatch_receive_ip_packet(
        &mut self,
        ctx: &mut C,
        device: Self::DeviceId,
        src_ip: I::RecvSrcAddr,
        dst_ip: SpecifiedAddr<I::Addr>,
        proto: I::Proto,
        body: B,
    ) -> Result<(), (B, TransportReceiveError)>;
}

/// The IP device context provided to the IP layer requiring a buffer type.
pub(crate) trait BufferIpDeviceContext<I: IpLayerIpExt, C, B: BufferMut>:
    IpDeviceContext<I, C>
{
    /// Sends an IP frame to the next hop.
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        next_hop: SpecifiedAddr<I::Addr>,
        packet: S,
    ) -> Result<(), S>;
}

/// The execution context for the IP layer requiring buffer.
pub(crate) trait BufferIpLayerContext<
    I: IpLayerStateIpExt<C::Instant, Self::DeviceId> + IcmpHandlerIpExt,
    C: IpLayerNonSyncContext<I, Self::DeviceId>,
    B: BufferMut,
>:
    BufferTransportContext<I, C, B>
    + BufferIpDeviceContext<I, C, B>
    + BufferIcmpHandler<I, C, B>
    + IpLayerContext<I, C>
    + FragmentHandler<I, C>
{
}

impl<
        I: IpLayerStateIpExt<C::Instant, Self::DeviceId> + IcmpHandlerIpExt,
        C: IpLayerNonSyncContext<I, SC::DeviceId>,
        B: BufferMut,
        SC: BufferTransportContext<I, C, B>
            + BufferIpDeviceContext<I, C, B>
            + BufferIcmpHandler<I, C, B>
            + IpLayerContext<I, C>
            + FragmentHandler<I, C>,
    > BufferIpLayerContext<I, C, B> for SC
{
}

impl<
        C: IpLayerNonSyncContext<Ipv4, SC::DeviceId>,
        SC: BufferIpTransportLayerContext<Ipv4, C, B> + IgmpPacketHandler<C, SC::DeviceId, B>,
        B: BufferMut,
    > BufferTransportContext<Ipv4, C, B> for SC
where
    IcmpIpTransportContext: BufferIpTransportContext<Ipv4, C, SC, B>,
{
    fn dispatch_receive_ip_packet(
        &mut self,
        ctx: &mut C,
        device: SC::DeviceId,
        src_ip: Ipv4Addr,
        dst_ip: SpecifiedAddr<Ipv4Addr>,
        proto: Ipv4Proto,
        body: B,
    ) -> Result<(), (B, TransportReceiveError)> {
        // TODO(https://fxbug.dev/93955): Deliver the packet to interested raw
        // sockets.

        match proto {
            Ipv4Proto::Icmp => <IcmpIpTransportContext as BufferIpTransportContext<
                Ipv4,
                _,
                _,
                _,
            >>::receive_ip_packet(
                self, ctx, device, src_ip, dst_ip, body
            ),
            Ipv4Proto::Igmp => {
                IgmpPacketHandler::receive_igmp_packet(self, ctx, device, src_ip, dst_ip, body);
                Ok(())
            }
            Ipv4Proto::Proto(IpProto::Udp) => {
                <<SC as BufferIpTransportLayerContext<_, _, _>>::Udp as BufferIpTransportContext<
                    Ipv4,
                    _,
                    _,
                    _,
                >>::receive_ip_packet(self, ctx, device, src_ip, dst_ip, body)
            }
            Ipv4Proto::Proto(IpProto::Tcp) => {
                <<SC as BufferIpTransportLayerContext<_, _, _>>::Tcp as BufferIpTransportContext<
                    Ipv4,
                    _,
                    _,
                    _,
                >>::receive_ip_packet(self, ctx, device, src_ip, dst_ip, body)
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

impl<
        C: IpLayerNonSyncContext<Ipv6, SC::DeviceId>,
        SC: BufferIpTransportLayerContext<Ipv6, C, B>,
        B: BufferMut,
    > BufferTransportContext<Ipv6, C, B> for SC
where
    IcmpIpTransportContext: BufferIpTransportContext<Ipv6, C, SC, B>,
{
    fn dispatch_receive_ip_packet(
        &mut self,
        ctx: &mut C,
        device: SC::DeviceId,
        src_ip: Ipv6SourceAddr,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        proto: Ipv6Proto,
        body: B,
    ) -> Result<(), (B, TransportReceiveError)> {
        // TODO(https://fxbug.dev/93955): Deliver the packet to interested raw
        // sockets.

        match proto {
            Ipv6Proto::Icmpv6 => <IcmpIpTransportContext as BufferIpTransportContext<
                Ipv6,
                _,
                _,
                _,
            >>::receive_ip_packet(
                self, ctx, device, src_ip, dst_ip, body
            ),
            // A value of `Ipv6Proto::NoNextHeader` tells us that there is no
            // header whatsoever following the last lower-level header so we stop
            // processing here.
            Ipv6Proto::NoNextHeader => Ok(()),
            Ipv6Proto::Proto(IpProto::Tcp) => {
                <<SC as BufferIpTransportLayerContext<_, _, _>>::Tcp as BufferIpTransportContext<
                    Ipv6,
                    _,
                    _,
                    _,
                >>::receive_ip_packet(self, ctx, device, src_ip, dst_ip, body)
            }
            Ipv6Proto::Proto(IpProto::Udp) => {
                <<SC as BufferIpTransportLayerContext<_, _, _>>::Udp as BufferIpTransportContext<
                    Ipv6,
                    _,
                    _,
                    _,
                >>::receive_ip_packet(self, ctx, device, src_ip, dst_ip, body)
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
            inner: Default::default(),
            icmp: icmp.build(),
            next_packet_id: Default::default(),
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

        Ipv6State { inner: Default::default(), icmp: icmp.build() }
    }
}

pub(crate) struct Ipv4State<Instant: crate::Instant, D> {
    inner: IpStateInner<Ipv4, Instant, D>,
    icmp: Icmpv4State<Instant, IpSock<Ipv4, D, DefaultSendOptions>>,
    next_packet_id: AtomicU16,
}

impl<I: Instant, DeviceId> AsRef<IpStateInner<Ipv4, I, DeviceId>> for Ipv4State<I, DeviceId> {
    fn as_ref(&self) -> &IpStateInner<Ipv4, I, DeviceId> {
        &self.inner
    }
}

fn gen_ipv4_packet_id<I: Instant, C: IpStateContext<Ipv4, I>>(sync_ctx: &mut C) -> u16 {
    // Relaxed ordering as we only need atomicity without synchronization. See
    // https://en.cppreference.com/w/cpp/atomic/memory_order#Relaxed_ordering
    // for more details.
    //
    // TODO(https://fxbug.dev/87588): Generate IPv4 IDs unpredictably
    sync_ctx.with_ip_layer_state(|state| state.next_packet_id.fetch_add(1, Ordering::Relaxed))
}

pub(crate) struct Ipv6State<Instant: crate::Instant, D> {
    inner: IpStateInner<Ipv6, Instant, D>,
    icmp: Icmpv6State<Instant, IpSock<Ipv6, D, DefaultSendOptions>>,
}

impl<I: Instant, DeviceId> AsRef<IpStateInner<Ipv6, I, DeviceId>> for Ipv6State<I, DeviceId> {
    fn as_ref(&self) -> &IpStateInner<Ipv6, I, DeviceId> {
        &self.inner
    }
}

#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct IpStateInner<I: Ip, Instant: crate::Instant, DeviceId> {
    table: RwLock<ForwardingTable<I, DeviceId>>,
    fragment_cache: Mutex<IpPacketFragmentCache<I, Instant>>,
    pmtu_cache: Mutex<PmtuCache<I, Instant>>,
}

/// The identifier for timer events in the IP layer.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub(crate) enum IpLayerTimerId {
    /// A timer event for IPv4 packet reassembly timers.
    ReassemblyTimeoutv4(FragmentCacheKey<Ipv4Addr>),
    /// A timer event for IPv6 packet reassembly timers.
    ReassemblyTimeoutv6(FragmentCacheKey<Ipv6Addr>),
    /// A timer event for IPv4 path MTU discovery.
    PmtuTimeoutv4(PmtuTimerId<Ipv4>),
    /// A timer event for IPv6 path MTU discovery.
    PmtuTimeoutv6(PmtuTimerId<Ipv6>),
}

impl From<FragmentCacheKey<Ipv4Addr>> for IpLayerTimerId {
    fn from(timer: FragmentCacheKey<Ipv4Addr>) -> IpLayerTimerId {
        IpLayerTimerId::ReassemblyTimeoutv4(timer)
    }
}

impl From<FragmentCacheKey<Ipv6Addr>> for IpLayerTimerId {
    fn from(timer: FragmentCacheKey<Ipv6Addr>) -> IpLayerTimerId {
        IpLayerTimerId::ReassemblyTimeoutv6(timer)
    }
}

impl From<PmtuTimerId<Ipv4>> for IpLayerTimerId {
    fn from(timer: PmtuTimerId<Ipv4>) -> IpLayerTimerId {
        IpLayerTimerId::PmtuTimeoutv4(timer)
    }
}

impl From<PmtuTimerId<Ipv6>> for IpLayerTimerId {
    fn from(timer: PmtuTimerId<Ipv6>) -> IpLayerTimerId {
        IpLayerTimerId::PmtuTimeoutv6(timer)
    }
}

impl_timer_context!(
    IpLayerTimerId,
    FragmentCacheKey<Ipv4Addr>,
    IpLayerTimerId::ReassemblyTimeoutv4(id),
    id
);
impl_timer_context!(
    IpLayerTimerId,
    FragmentCacheKey<Ipv6Addr>,
    IpLayerTimerId::ReassemblyTimeoutv6(id),
    id
);
impl_timer_context!(IpLayerTimerId, PmtuTimerId<Ipv4>, IpLayerTimerId::PmtuTimeoutv4(id), id);
impl_timer_context!(IpLayerTimerId, PmtuTimerId<Ipv6>, IpLayerTimerId::PmtuTimeoutv6(id), id);

/// Handle a timer event firing in the IP layer.
pub(crate) fn handle_timer<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    id: IpLayerTimerId,
) {
    match id {
        IpLayerTimerId::ReassemblyTimeoutv4(key) => {
            TimerHandler::handle_timer(&mut sync_ctx, ctx, key)
        }
        IpLayerTimerId::ReassemblyTimeoutv6(key) => {
            TimerHandler::handle_timer(&mut sync_ctx, ctx, key)
        }
        IpLayerTimerId::PmtuTimeoutv4(id) => TimerHandler::handle_timer(&mut sync_ctx, ctx, id),
        IpLayerTimerId::PmtuTimeoutv6(id) => TimerHandler::handle_timer(&mut sync_ctx, ctx, id),
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
fn dispatch_receive_ipv4_packet<
    C: IpLayerNonSyncContext<Ipv4, SC::DeviceId>,
    B: BufferMut,
    SC: BufferIpLayerContext<Ipv4, C, B>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device: <SC as IpDeviceIdContext<Ipv4>>::DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
    proto: Ipv4Proto,
    body: B,
    parse_metadata: Option<ParseMetadata>,
) {
    ctx.increment_counter("dispatch_receive_ipv4_packet");

    let (mut body, err) =
        match sync_ctx.dispatch_receive_ip_packet(ctx, device, src_ip, dst_ip, proto, body) {
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
                sync_ctx.send_icmp_error_message(
                    ctx,
                    device,
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
                sync_ctx.send_icmp_error_message(
                    ctx,
                    device,
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
fn dispatch_receive_ipv6_packet<
    C: IpLayerNonSyncContext<Ipv6, SC::DeviceId>,
    B: BufferMut,
    SC: BufferIpLayerContext<Ipv6, C, B>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device: <SC as IpDeviceIdContext<Ipv6>>::DeviceId,
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

    let (mut body, err) =
        match sync_ctx.dispatch_receive_ip_packet(ctx, device, src_ip, dst_ip, proto, body) {
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
                sync_ctx.send_icmp_error_message(
                    ctx,
                    device,
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
                sync_ctx.send_icmp_error_message(
                    ctx,
                    device,
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
    ($sync_ctx:expr, $ctx:expr, $dispatch:ident, $device:ident, $frame_dst:expr, $buffer:expr, $packet:expr, $src_ip:expr, $dst_ip:expr, $ip:ident) => {{
        match FragmentHandler::<$ip, _>::process_fragment::<&mut [u8]>(
            $sync_ctx,
            $ctx,
            $packet,
        ) {
            // Handle the packet right away since reassembly is not needed.
            FragmentProcessingState::NotNeeded(packet) => {
                trace!("receive_ip_packet: not fragmented");
                // TODO(joshlf):
                // - Check for already-expired TTL?
                let (_, _, proto, meta) = packet.into_metadata();
                $dispatch(
                    $sync_ctx,
                    $ctx,
                    $device,
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
                match FragmentHandler::<$ip, _>::reassemble_packet(
                    $sync_ctx,
                    $ctx,
                    &key,
                    buffer.buffer_view_mut(),
                ) {
                    // Successfully reassembled the packet, handle it.
                    Ok(packet) => {
                        trace!("receive_ip_packet: fragmented, reassembled packet: {:?}", packet);
                        // TODO(joshlf):
                        // - Check for already-expired TTL?
                        let (_, _, proto, meta) = packet.into_metadata();
                        $dispatch::<_, Buf<Vec<u8>>, _>(
                            $sync_ctx,
                            $ctx,
                            $device,
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
pub(crate) fn receive_ip_packet<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>, I: Ip>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    frame_dst: FrameDestination,
    buffer: B,
) {
    match I::VERSION {
        IpVersion::V4 => receive_ipv4_packet(&mut sync_ctx, ctx, device, frame_dst, buffer),
        IpVersion::V6 => receive_ipv6_packet(&mut sync_ctx, ctx, device, frame_dst, buffer),
    }
}

/// Receive an IPv4 packet from a device.
///
/// `frame_dst` specifies whether this packet was received in a broadcast or
/// unicast link-layer frame.
pub(crate) fn receive_ipv4_packet<
    C: IpLayerNonSyncContext<Ipv4, SC::DeviceId>,
    B: BufferMut,
    SC: BufferIpLayerContext<Ipv4, C, B> + BufferIpLayerContext<Ipv4, C, Buf<Vec<u8>>>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device: SC::DeviceId,
    frame_dst: FrameDestination,
    mut buffer: B,
) {
    if !sync_ctx.is_ip_device_enabled(device) {
        return;
    }

    ctx.increment_counter("receive_ipv4_packet");
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
            BufferIcmpHandler::<Ipv4, _, _>::send_icmp_error_message(
                sync_ctx,
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

    match receive_ipv4_packet_action(sync_ctx, ctx, device, dst_ip) {
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
                sync_ctx,
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
                match BufferIpDeviceContext::<Ipv4, _, _>::send_ip_frame(
                    sync_ctx,
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
                BufferIcmpHandler::<Ipv4, _, _>::send_icmp_error_message(
                    sync_ctx,
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
            BufferIcmpHandler::<Ipv4, _, _>::send_icmp_error_message(
                sync_ctx,
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
pub(crate) fn receive_ipv6_packet<
    C: IpLayerNonSyncContext<Ipv6, SC::DeviceId>,
    B: BufferMut,
    SC: BufferIpLayerContext<Ipv6, C, B> + BufferIpLayerContext<Ipv6, C, Buf<Vec<u8>>>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device: SC::DeviceId,
    frame_dst: FrameDestination,
    mut buffer: B,
) {
    if !sync_ctx.is_ip_device_enabled(device) {
        return;
    }

    ctx.increment_counter("receive_ipv6_packet");
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
            BufferIcmpHandler::<Ipv6, _, _>::send_icmp_error_message(
                sync_ctx,
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
            ctx.increment_counter("receive_ipv6_packet: non-unicast source");
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

    match receive_ipv6_packet_action(sync_ctx, ctx, device, dst_ip) {
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
            match ipv6::handle_extension_headers(sync_ctx, device, frame_dst, &packet, true) {
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
                        sync_ctx,
                        ctx,
                        device,
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
                        sync_ctx,
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
            ctx.increment_counter("receive_ipv6_packet::forward");
            let ttl = packet.ttl();
            if ttl > 1 {
                trace!("receive_ipv6_packet: forwarding");

                // Handle extension headers first.
                match ipv6::handle_extension_headers(sync_ctx, device, frame_dst, &packet, false) {
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
                if let Err(buffer) = BufferIpDeviceContext::<Ipv6, _, _>::send_ip_frame(
                    sync_ctx,
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
                        let mtu = sync_ctx.get_mtu(device);
                        BufferIcmpHandler::<Ipv6, _, _>::send_icmp_error_message(
                            sync_ctx,
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
                    BufferIcmpHandler::<Ipv6, _, _>::send_icmp_error_message(
                        sync_ctx,
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
                BufferIcmpHandler::<Ipv6, _, _>::send_icmp_error_message(
                    sync_ctx,
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
            ctx.increment_counter("receive_ipv6_packet::drop");
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
fn receive_ipv4_packet_action<
    C: IpLayerNonSyncContext<Ipv4, SC::DeviceId>,
    SC: IpLayerContext<Ipv4, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device: SC::DeviceId,
    dst_ip: SpecifiedAddr<Ipv4Addr>,
) -> ReceivePacketAction<Ipv4Addr, SC::DeviceId> {
    // If the packet arrived at the loopback interface, check if any local
    // interface has the destination address assigned. This effectively lets
    // the loopback interface operate as a weak host for incoming packets.
    //
    // Note that (as of writing) the stack sends all locally destined traffic to
    // the loopback interface so we need this hack to allow the stack to accept
    // packets that arrive at the loopback interface (after being looped back)
    // but destined to an address that is assigned to another local interface.
    //
    // TODO(https://fxbug.dev/93870): This should instead be controlled by the
    // routing table.
    let address_status = if device.is_loopback() {
        sync_ctx.address_status(dst_ip).drop_device()
    } else {
        sync_ctx.address_status_for_device(dst_ip, device)
    };
    match address_status {
        AddressStatus::Present(state) => match state {
            Ipv4PresentAddressStatus::LimitedBroadcast
            | Ipv4PresentAddressStatus::SubnetBroadcast
            | Ipv4PresentAddressStatus::Multicast
            | Ipv4PresentAddressStatus::Unicast => {
                ctx.increment_counter("receive_ipv4_packet_action::deliver");
                ReceivePacketAction::Deliver
            }
        },
        AddressStatus::Unassigned => {
            receive_ip_packet_action_common::<Ipv4, _, _>(sync_ctx, ctx, dst_ip, device)
        }
    }
}

/// Computes the action to take in order to process a received IPv6 packet.
fn receive_ipv6_packet_action<
    C: IpLayerNonSyncContext<Ipv6, SC::DeviceId>,
    SC: IpLayerContext<Ipv6, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device: SC::DeviceId,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
) -> ReceivePacketAction<Ipv6Addr, SC::DeviceId> {
    // If the packet arrived at the loopback interface, check if any local
    // interface has the destination address assigned. This effectively lets
    // the loopback interface operate as a weak host for incoming packets.
    //
    // Note that (as of writing) the stack sends all locally destined traffic to
    // the loopback interface so we need this hack to allow the stack to accept
    // packets that arrive at the loopback interface (after being looped back)
    // but destined to an address that is assigned to another local interface.
    //
    // TODO(https://fxbug.dev/93870): This should instead be controlled by the
    // routing table.
    let address_status = if device.is_loopback() {
        sync_ctx.address_status(dst_ip).drop_device()
    } else {
        sync_ctx.address_status_for_device(dst_ip, device)
    };
    match address_status {
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
            receive_ip_packet_action_common::<Ipv6, _, _>(sync_ctx, ctx, dst_ip, device)
        }
    }
}

/// Computes the remaining protocol-agnostic actions on behalf of
/// [`receive_ipv4_packet_action`] and [`receive_ipv6_packet_action`].
fn receive_ip_packet_action_common<
    I: IpLayerStateIpExt<C::Instant, SC::DeviceId>,
    C: IpLayerNonSyncContext<I, SC::DeviceId>,
    SC: IpLayerContext<I, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    dst_ip: SpecifiedAddr<I::Addr>,
    device_id: SC::DeviceId,
) -> ReceivePacketAction<I::Addr, SC::DeviceId> {
    // The packet is not destined locally, so we attempt to forward it.
    if !sync_ctx.is_device_routing_enabled(device_id) {
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
        match lookup_route(sync_ctx, ctx, None, dst_ip) {
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
fn lookup_route<
    I: IpLayerStateIpExt<C::Instant, SC::DeviceId>,
    C: IpLayerNonSyncContext<I, SC::DeviceId>,
    SC: IpLayerContext<I, C>,
>(
    sync_ctx: &SC,
    _ctx: &mut C,
    device: Option<SC::DeviceId>,
    dst_ip: SpecifiedAddr<I::Addr>,
) -> Option<Destination<I::Addr, SC::DeviceId>> {
    sync_ctx.with_ip_layer_state(|state| {
        AsRef::<IpStateInner<_, _, _>>::as_ref(state).table.read().lookup(device, dst_ip)
    })
}

fn with_ip_layer_state_inner<
    'a,
    I: IpLayerStateIpExt<C::Instant, SC::DeviceId>,
    C: IpLayerNonSyncContext<I, SC::DeviceId>,
    SC: IpLayerContext<I, C>,
    O,
    F: FnOnce(&IpStateInner<I, C::Instant, SC::DeviceId>) -> O,
>(
    sync_ctx: &mut SC,
    cb: F,
) -> O {
    sync_ctx.with_ip_layer_state(|state| cb(state.as_ref()))
}

/// Add a route to the forwarding table, returning `Err` if the subnet
/// is already in the table.
pub(crate) fn add_route<
    I: IpLayerStateIpExt<C::Instant, SC::DeviceId>,
    C: IpLayerNonSyncContext<I, SC::DeviceId>,
    SC: IpLayerContext<I, C>,
>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    subnet: Subnet<I::Addr>,
    next_hop: SpecifiedAddr<I::Addr>,
) -> Result<(), AddRouteError> {
    with_ip_layer_state_inner(sync_ctx, |state| state.table.write().add_route(subnet, next_hop))
}

/// Add a device route to the forwarding table, returning `Err` if the
/// subnet is already in the table.
pub(crate) fn add_device_route<
    I: IpLayerStateIpExt<C::Instant, SC::DeviceId>,
    C: IpLayerNonSyncContext<I, SC::DeviceId>,
    SC: IpLayerContext<I, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    subnet: Subnet<I::Addr>,
    device: SC::DeviceId,
) -> Result<(), ExistsError> {
    with_ip_layer_state_inner(sync_ctx, |state| {
        state.table.write().add_device_route(subnet, device).map(|()| {
            ctx.on_event(IpLayerEvent::DeviceRouteAdded { device, subnet });
        })
    })
}

/// Delete a route from the forwarding table, returning `Err` if no
/// route was found to be deleted.
pub(crate) fn del_route<
    I: IpLayerStateIpExt<C::Instant, SC::DeviceId>,
    C: IpLayerNonSyncContext<I, SC::DeviceId>,
    SC: IpLayerContext<I, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    subnet: Subnet<I::Addr>,
) -> Result<(), NotFoundError> {
    with_ip_layer_state_inner(sync_ctx, |state| {
        state.table.write().del_route(subnet).map(|removed| {
            removed.into_iter().for_each(|types::Entry { subnet, device, gateway }| match gateway {
                None => ctx.on_event(IpLayerEvent::DeviceRouteRemoved { device, subnet }),
                Some(SpecifiedAddr { .. }) => (),
            })
        })
    })
}

pub(crate) fn del_device_routes<
    I: IpLayerStateIpExt<C::Instant, SC::DeviceId>,
    SC: IpLayerContext<I, C>,
    C: IpLayerNonSyncContext<I, SC::DeviceId>,
>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    to_delete: &SC::DeviceId,
) {
    with_ip_layer_state_inner(sync_ctx, |state| {
        state
            .table
            .write()
            .retain(|types::Entry { subnet: _, device, gateway: _ }| device != to_delete)
    })
}

/// Calls the function with an immutable reference to the IPv4 & IPv6 routing
/// table.
///
/// Helper function to enforce lock ordering in a single place.
fn with_ipv4_and_ipv6_routing_tables<
    C: NonSyncContext,
    O,
    F: FnOnce(&ForwardingTable<Ipv4, DeviceId>, &ForwardingTable<Ipv6, DeviceId>) -> O,
>(
    sync_ctx: &SyncCtx<C>,
    cb: F,
) -> O {
    cb(&sync_ctx.state.ipv4.inner.table.read(), &sync_ctx.state.ipv6.inner.table.read())
}

/// Get all the routes.
pub fn get_all_routes<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
) -> Vec<types::EntryEither<DeviceId>> {
    with_ipv4_and_ipv6_routing_tables(&mut sync_ctx, |ipv4, ipv6| {
        ipv4.iter_table()
            .cloned()
            .map(From::from)
            .chain(ipv6.iter_table().cloned().map(From::from))
            .collect()
    })
}

/// The metadata associated with an outgoing IP packet.
#[cfg_attr(test, derive(Debug))]
pub(crate) struct SendIpPacketMeta<I: packet_formats::ip::IpExt, D, Src> {
    /// The outgoing device.
    pub(crate) device: D,

    /// The source address of the packet.
    pub(crate) src_ip: Src,

    /// The destination address of the packet.
    pub(crate) dst_ip: SpecifiedAddr<I::Addr>,

    /// The next-hop node that the packet should be sent to.
    pub(crate) next_hop: SpecifiedAddr<I::Addr>,

    /// The upper-layer protocol held in the packet's payload.
    pub(crate) proto: I::Proto,

    /// The time-to-live (IPv4) or hop limit (IPv6) for the packet.
    ///
    /// If not set, a default TTL may be used.
    pub(crate) ttl: Option<NonZeroU8>,

    /// An MTU to artificially impose on the whole IP packet.
    ///
    /// Note that the device's MTU will still be imposed on the packet.
    pub(crate) mtu: Option<u32>,
}

impl<I: packet_formats::ip::IpExt, D> From<SendIpPacketMeta<I, D, SpecifiedAddr<I::Addr>>>
    for SendIpPacketMeta<I, D, Option<SpecifiedAddr<I::Addr>>>
{
    fn from(
        SendIpPacketMeta { device, src_ip, dst_ip, next_hop, proto, ttl, mtu }: SendIpPacketMeta<
            I,
            D,
            SpecifiedAddr<I::Addr>,
        >,
    ) -> SendIpPacketMeta<I, D, Option<SpecifiedAddr<I::Addr>>> {
        SendIpPacketMeta { device, src_ip: Some(src_ip), dst_ip, next_hop, proto, ttl, mtu }
    }
}

pub(crate) trait BufferIpLayerHandler<I: IpExt, C, B: BufferMut>:
    IpDeviceIdContext<I>
{
    fn send_ip_packet_from_device<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        meta: SendIpPacketMeta<I, Self::DeviceId, Option<SpecifiedAddr<I::Addr>>>,
        body: S,
    ) -> Result<(), S>;
}

impl<
        B: BufferMut,
        C: IpLayerNonSyncContext<Ipv4, <SC as IpDeviceIdContext<Ipv4>>::DeviceId>,
        SC: BufferIpDeviceContext<Ipv4, C, B> + IpStateContext<Ipv4, C::Instant> + NonTestCtxMarker,
    > BufferIpLayerHandler<Ipv4, C, B> for SC
{
    fn send_ip_packet_from_device<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        meta: SendIpPacketMeta<Ipv4, SC::DeviceId, Option<SpecifiedAddr<Ipv4Addr>>>,
        body: S,
    ) -> Result<(), S> {
        send_ipv4_packet_from_device(self, ctx, meta, body)
    }
}

impl<
        B: BufferMut,
        C: IpLayerNonSyncContext<Ipv6, <SC as IpDeviceIdContext<Ipv6>>::DeviceId>,
        SC: BufferIpDeviceContext<Ipv6, C, B> + IpStateContext<Ipv6, C::Instant> + NonTestCtxMarker,
    > BufferIpLayerHandler<Ipv6, C, B> for SC
{
    fn send_ip_packet_from_device<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        meta: SendIpPacketMeta<Ipv6, SC::DeviceId, Option<SpecifiedAddr<Ipv6Addr>>>,
        body: S,
    ) -> Result<(), S> {
        send_ipv6_packet_from_device(self, ctx, meta, body)
    }
}

/// Sends an IPv4 packet with the specified metadata.
///
/// # Panics
///
/// Panics if either the source or destination address is the loopback address
/// and the device is a non-loopback device.
pub(crate) fn send_ipv4_packet_from_device<
    B: BufferMut,
    C: IpLayerNonSyncContext<Ipv4, <SC as IpDeviceIdContext<Ipv4>>::DeviceId>,
    SC: BufferIpDeviceContext<Ipv4, C, B> + IpStateContext<Ipv4, C::Instant>,
    S: Serializer<Buffer = B>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    SendIpPacketMeta { device, src_ip, dst_ip, next_hop, proto, ttl, mtu }: SendIpPacketMeta<
        Ipv4,
        <SC as IpDeviceIdContext<Ipv4>>::DeviceId,
        Option<SpecifiedAddr<Ipv4Addr>>,
    >,
    body: S,
) -> Result<(), S> {
    let src_ip = src_ip.map_or(Ipv4::UNSPECIFIED_ADDRESS, |a| a.get());
    let builder = {
        assert!(
            (!Ipv4::LOOPBACK_SUBNET.contains(&src_ip) && !Ipv4::LOOPBACK_SUBNET.contains(&dst_ip))
                || device.is_loopback()
        );
        let mut builder = Ipv4PacketBuilder::new(
            src_ip,
            dst_ip,
            ttl.unwrap_or_else(|| sync_ctx.get_hop_limit(device)).get(),
            proto,
        );
        builder.id(gen_ipv4_packet_id(sync_ctx));
        builder
    };
    let body = body.encapsulate(builder);

    if let Some(mtu) = mtu {
        let body = body.with_mtu(mtu as usize);
        sync_ctx
            .send_ip_frame(ctx, device, next_hop, body)
            .map_err(|ser| ser.into_inner().into_inner())
    } else {
        sync_ctx.send_ip_frame(ctx, device, next_hop, body).map_err(|ser| ser.into_inner())
    }
}

/// Sends an IPv6 packet with the specified metadata.
///
/// # Panics
///
/// Panics if either the source or destination address is the loopback address
/// and the device is a non-loopback device.
pub(crate) fn send_ipv6_packet_from_device<
    B: BufferMut,
    C: IpLayerNonSyncContext<Ipv6, SC::DeviceId>,
    SC: BufferIpDeviceContext<Ipv6, C, B>,
    S: Serializer<Buffer = B>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    SendIpPacketMeta { device, src_ip, dst_ip, next_hop, proto, ttl, mtu }: SendIpPacketMeta<
        Ipv6,
        SC::DeviceId,
        Option<SpecifiedAddr<Ipv6Addr>>,
    >,
    body: S,
) -> Result<(), S> {
    let src_ip = src_ip.map_or(Ipv6::UNSPECIFIED_ADDRESS, |a| a.get());
    let builder = {
        assert!(
            (!Ipv6::LOOPBACK_SUBNET.contains(&src_ip) && !Ipv6::LOOPBACK_SUBNET.contains(&dst_ip))
                || device.is_loopback()
        );
        Ipv6PacketBuilder::new(
            src_ip,
            dst_ip,
            ttl.unwrap_or_else(|| sync_ctx.get_hop_limit(device)).get(),
            proto,
        )
    };

    let body = body.encapsulate(builder);

    if let Some(mtu) = mtu {
        let body = body.with_mtu(mtu as usize);
        sync_ctx
            .send_ip_frame(ctx, device, next_hop, body)
            .map_err(|ser| ser.into_inner().into_inner())
    } else {
        sync_ctx.send_ip_frame(ctx, device, next_hop, body).map_err(|ser| ser.into_inner())
    }
}

impl<C: NonSyncContext> InnerIcmpContext<Ipv4, C> for &'_ SyncCtx<C> {
    fn receive_icmp_error(
        &mut self,
        ctx: &mut C,
        device: DeviceId,
        original_src_ip: Option<SpecifiedAddr<Ipv4Addr>>,
        original_dst_ip: SpecifiedAddr<Ipv4Addr>,
        original_proto: Ipv4Proto,
        original_body: &[u8],
        err: Icmpv4ErrorCode,
    ) {
        ctx.increment_counter("InnerIcmpContext<Ipv4>::receive_icmp_error");
        trace!("InnerIcmpContext<Ipv4>::receive_icmp_error({:?})", err);

        macro_rules! mtch {
            ($($cond:pat => $ty:ident),*) => {
                match original_proto {
                    Ipv4Proto::Icmp => <IcmpIpTransportContext as IpTransportContext<Ipv4, _, _>>
                                ::receive_icmp_error(self,ctx, device, original_src_ip, original_dst_ip, original_body, err),
                    $($cond => <<Self as IpTransportLayerContext<Ipv4, _>>::$ty as IpTransportContext<Ipv4, _, _>>
                                ::receive_icmp_error(self,ctx, device, original_src_ip, original_dst_ip, original_body, err),)*
                    // TODO(joshlf): Once all IP protocol numbers are covered,
                    // remove this default case.
                    _ => <() as IpTransportContext<Ipv4, _, _>>::receive_icmp_error(self,ctx, device, original_src_ip, original_dst_ip, original_body, err),
                }
            };
        }

        #[rustfmt::skip]
        mtch!(
            Ipv4Proto::Proto(IpProto::Tcp) => Tcp,
            Ipv4Proto::Proto(IpProto::Udp) => Udp
        );
    }

    fn with_icmp_sockets<
        O,
        F: FnOnce(&IcmpSockets<Ipv4Addr, IpSock<Ipv4, DeviceId, DefaultSendOptions>>) -> O,
    >(
        &self,
        cb: F,
    ) -> O {
        cb(&self.state.ipv4.icmp.as_ref().sockets.read())
    }

    fn with_icmp_sockets_mut<
        O,
        F: FnOnce(&mut IcmpSockets<Ipv4Addr, IpSock<Ipv4, DeviceId, DefaultSendOptions>>) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O {
        cb(&mut self.state.ipv4.icmp.as_ref().sockets.write())
    }

    fn with_error_send_bucket_mut<O, F: FnOnce(&mut TokenBucket<C::Instant>) -> O>(
        &mut self,
        cb: F,
    ) -> O {
        cb(&mut self.state.ipv4.icmp.as_ref().error_send_bucket.lock())
    }
}

impl<C: NonSyncContext> InnerIcmpContext<Ipv6, C> for &'_ SyncCtx<C> {
    fn receive_icmp_error(
        &mut self,
        ctx: &mut C,
        device: DeviceId,
        original_src_ip: Option<SpecifiedAddr<Ipv6Addr>>,
        original_dst_ip: SpecifiedAddr<Ipv6Addr>,
        original_next_header: Ipv6Proto,
        original_body: &[u8],
        err: Icmpv6ErrorCode,
    ) {
        ctx.increment_counter("InnerIcmpContext<Ipv6>::receive_icmp_error");
        trace!("InnerIcmpContext<Ipv6>::receive_icmp_error({:?})", err);

        macro_rules! mtch {
            ($($cond:pat => $ty:ident),*) => {
                match original_next_header {
                    Ipv6Proto::Icmpv6 => <IcmpIpTransportContext as IpTransportContext<Ipv6, _, _>>
                    ::receive_icmp_error(self, ctx, device, original_src_ip, original_dst_ip, original_body, err),
                    $($cond => <<Self as IpTransportLayerContext<Ipv6, _>>::$ty as IpTransportContext<Ipv6, _, _>>
                                ::receive_icmp_error(self, ctx, device, original_src_ip, original_dst_ip, original_body, err),)*
                    // TODO(joshlf): Once all IP protocol numbers are covered,
                    // remove this default case.
                    _ => <() as IpTransportContext<Ipv6, _, _>>::receive_icmp_error(self, ctx, device, original_src_ip, original_dst_ip, original_body, err),
                }
            };
        }

        #[rustfmt::skip]
        mtch!(
            Ipv6Proto::Proto(IpProto::Tcp) => Tcp,
            Ipv6Proto::Proto(IpProto::Udp) => Udp
        );
    }

    fn with_icmp_sockets<
        O,
        F: FnOnce(&IcmpSockets<Ipv6Addr, IpSock<Ipv6, DeviceId, DefaultSendOptions>>) -> O,
    >(
        &self,
        cb: F,
    ) -> O {
        cb(&self.state.ipv6.icmp.as_ref().sockets.read())
    }

    fn with_icmp_sockets_mut<
        O,
        F: FnOnce(&mut IcmpSockets<Ipv6Addr, IpSock<Ipv6, DeviceId, DefaultSendOptions>>) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O {
        cb(&mut self.state.ipv6.icmp.as_ref().sockets.write())
    }

    fn with_error_send_bucket_mut<O, F: FnOnce(&mut TokenBucket<C::Instant>) -> O>(
        &mut self,
        cb: F,
    ) -> O {
        cb(&mut self.state.ipv6.icmp.as_ref().error_send_bucket.lock())
    }
}

// Used in testing in other modules.
#[cfg(test)]
pub(crate) fn dispatch_receive_ip_packet_name<I: Ip>() -> &'static str {
    match I::VERSION {
        IpVersion::V4 => "dispatch_receive_ipv4_packet",
        IpVersion::V6 => "dispatch_receive_ipv6_packet",
    }
}

#[cfg(test)]
pub(crate) mod testutil {
    use super::*;

    use net_types::{ip::IpAddr, MulticastAddr};

    use crate::testutil::DummySyncCtx;

    impl<I: Ip, S, Meta, D: IpDeviceId + 'static> IpDeviceIdContext<I>
        for crate::context::testutil::DummySyncCtx<S, Meta, D>
    {
        type DeviceId = D;

        fn loopback_id(&self) -> Option<Self::DeviceId> {
            None
        }
    }

    impl<I: Ip, Outer, S, Meta, D: IpDeviceId + 'static> IpDeviceIdContext<I>
        for crate::context::testutil::WrappedDummySyncCtx<Outer, S, Meta, D>
    {
        type DeviceId = D;

        fn loopback_id(&self) -> Option<Self::DeviceId> {
            None
        }
    }

    /// A dummy device ID for use in testing.
    ///
    /// `DummyDeviceId` is provided for use in implementing
    /// `IpDeviceIdContext::DeviceId` in tests. Unlike `()`, it implements the
    /// `Display` trait, which is a requirement of `IpDeviceIdContext::DeviceId`.
    #[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
    pub(crate) struct DummyDeviceId;

    impl IpDeviceId for DummyDeviceId {
        fn is_loopback(&self) -> bool {
            false
        }
    }

    impl Display for DummyDeviceId {
        fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
            write!(f, "DummyDeviceId")
        }
    }

    /// A device ID type that supports identifying more than one distinct
    /// device.
    #[derive(Copy, Clone, Eq, PartialEq, Hash, Debug, Ord, PartialOrd)]
    pub(crate) enum MultipleDevicesId {
        A,
        B,
    }
    impl MultipleDevicesId {
        pub(crate) fn all() -> [Self; 2] {
            [Self::A, Self::B]
        }
    }

    impl core::fmt::Display for MultipleDevicesId {
        fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
            core::fmt::Debug::fmt(self, f)
        }
    }

    impl IpDeviceId for MultipleDevicesId {
        fn is_loopback(&self) -> bool {
            false
        }
    }

    pub(crate) fn is_in_ip_multicast<A: IpAddress>(
        sync_ctx: &DummySyncCtx,
        device: DeviceId,
        addr: MulticastAddr<A>,
    ) -> bool {
        match addr.into() {
            IpAddr::V4(addr) => {
                match IpDeviceContext::<Ipv4, _>::address_status_for_device(
                    &sync_ctx,
                    addr.into_specified(),
                    device,
                ) {
                    AddressStatus::Present(Ipv4PresentAddressStatus::Multicast) => true,
                    AddressStatus::Unassigned => false,
                    AddressStatus::Present(
                        Ipv4PresentAddressStatus::Unicast
                        | Ipv4PresentAddressStatus::LimitedBroadcast
                        | Ipv4PresentAddressStatus::SubnetBroadcast,
                    ) => unreachable!(),
                }
            }
            IpAddr::V6(addr) => {
                match IpDeviceContext::<Ipv6, _>::address_status_for_device(
                    &sync_ctx,
                    addr.into_specified(),
                    device,
                ) {
                    AddressStatus::Present(Ipv6PresentAddressStatus::Multicast) => true,
                    AddressStatus::Unassigned => false,
                    AddressStatus::Present(
                        Ipv6PresentAddressStatus::UnicastAssigned
                        | Ipv6PresentAddressStatus::UnicastTentative,
                    ) => unreachable!(),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use alloc::vec;
    use core::{convert::TryFrom, num::NonZeroU16, time::Duration};

    use ip_test_macro::ip_test;
    use net_types::{
        ethernet::Mac,
        ip::{AddrSubnet, IpAddr, Ipv4Addr, Ipv6Addr},
        MulticastAddr, UnicastAddr,
    };
    use packet::{Buf, ParseBuffer};
    use packet_formats::{
        ethernet::{EthernetFrame, EthernetFrameBuilder, EthernetFrameLengthCheck},
        icmp::{
            IcmpDestUnreachable, IcmpEchoRequest, IcmpPacketBuilder, IcmpParseArgs, IcmpUnusedCode,
            Icmpv4DestUnreachableCode, Icmpv6Packet, Icmpv6PacketTooBig,
            Icmpv6ParameterProblemCode, MessageBody,
        },
        ip::{IpPacketBuilder, Ipv6ExtHdrType},
        ipv4::Ipv4PacketBuilder,
        ipv6::{ext_hdrs::ExtensionHeaderOptionAction, Ipv6PacketBuilder},
        testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame,
    };
    use rand::Rng;

    use super::*;
    use crate::{
        context::testutil::{handle_timer_helper_with_sc_ref, DummyInstant, DummyTimerCtxExt as _},
        device::{receive_frame, testutil::receive_frame_or_panic, FrameDestination},
        ip::{device::set_routing_enabled, testutil::is_in_ip_multicast},
        testutil::{
            assert_empty, get_counter_val, handle_timer, new_rng, DummyCtx,
            DummyEventDispatcherBuilder, DummyNonSyncCtx, TestIpExt, DUMMY_CONFIG_V4,
            DUMMY_CONFIG_V6,
        },
        Ctx, DeviceId, StackState,
    };

    // Some helper functions

    /// Verify that an ICMP Parameter Problem packet was actually sent in
    /// response to a packet with an unrecognized IPv6 extension header option.
    ///
    /// `verify_icmp_for_unrecognized_ext_hdr_option` verifies that the next
    /// frame in `net` is an ICMP packet with code set to `code`, and pointer
    /// set to `pointer`.
    fn verify_icmp_for_unrecognized_ext_hdr_option(
        ctx: &mut DummyNonSyncCtx,
        code: Icmpv6ParameterProblemCode,
        pointer: u32,
        offset: usize,
    ) {
        // Check the ICMP that bob attempted to send to alice
        let device_frames = ctx.frames_sent();
        assert!(!device_frames.is_empty());
        let mut buffer = Buf::new(device_frames[offset].1.as_slice(), ..);
        let _frame =
            buffer.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
        let packet = buffer.parse::<<Ipv6 as packet_formats::ip::IpExt>::Packet<_>>().unwrap();
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
    fn process_ip_fragment<I: Ip, NonSyncCtx: NonSyncContext>(
        sync_ctx: &mut &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        device: DeviceId,
        fragment_id: u16,
        fragment_offset: u8,
        fragment_count: u8,
    ) {
        match I::VERSION {
            IpVersion::V4 => process_ipv4_fragment(
                sync_ctx,
                ctx,
                device,
                fragment_id,
                fragment_offset,
                fragment_count,
            ),
            IpVersion::V6 => process_ipv6_fragment(
                sync_ctx,
                ctx,
                device,
                fragment_id,
                fragment_offset,
                fragment_count,
            ),
        }
    }

    /// Generate and 'receive' an IPv4 fragment packet.
    ///
    /// `fragment_offset` is the fragment offset. `fragment_count` is the number
    /// of fragments for a packet. The generated packet will have a body of size
    /// 8 bytes.
    fn process_ipv4_fragment<NonSyncCtx: NonSyncContext>(
        sync_ctx: &mut &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
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
        receive_ipv4_packet(sync_ctx, ctx, device, FrameDestination::Unicast, buffer);
    }

    /// Generate and 'receive' an IPv6 fragment packet.
    ///
    /// `fragment_offset` is the fragment offset. `fragment_count` is the number
    /// of fragments for a packet. The generated packet will have a body of size
    /// 8 bytes.
    fn process_ipv6_fragment<NonSyncCtx: NonSyncContext>(
        sync_ctx: &mut &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
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
        receive_ipv6_packet(sync_ctx, ctx, device, FrameDestination::Unicast, buffer);
    }

    #[test]
    fn test_ipv6_icmp_parameter_problem_non_must() {
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6).build();
        let mut sync_ctx = &sync_ctx;
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

        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            FrameDestination::Unicast,
            buf,
        );

        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv4_parameter_problem"), 0);
        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv6_parameter_problem"), 0);
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv4_packet"), 0);
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv6_packet"), 0);
    }

    #[test]
    fn test_ipv6_icmp_parameter_problem_must() {
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6).build();
        let mut sync_ctx = &sync_ctx;
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
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            FrameDestination::Unicast,
            buf,
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut non_sync_ctx,
            Icmpv6ParameterProblemCode::ErroneousHeaderField,
            42,
            0,
        );
    }

    #[test]
    fn test_ipv6_unrecognized_ext_hdr_option() {
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6).build();
        let mut sync_ctx = &sync_ctx;
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
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv6_packet"), 1);
        assert_eq!(non_sync_ctx.frames_sent().len(), expected_icmps);

        // Test with unrecognized option type set with
        // action = discard.

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacket,
            false,
        );
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(non_sync_ctx.frames_sent().len(), expected_icmps);

        // Test with unrecognized option type set with
        // action = discard & send icmp
        // where dest addr is a unicast addr.

        let buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmp,
            false,
        );
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(non_sync_ctx.frames_sent().len(), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut non_sync_ctx,
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
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(non_sync_ctx.frames_sent().len(), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut non_sync_ctx,
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
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(non_sync_ctx.frames_sent().len(), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut non_sync_ctx,
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
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        assert_eq!(non_sync_ctx.frames_sent().len(), expected_icmps);

        // None of our tests should have sent an icmpv4 packet, or dispatched an
        // IP packet after the first.

        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv4_parameter_problem"), 0);
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv6_packet"), 1);
    }

    #[ip_test]
    fn test_ip_packet_reassembly_not_needed<I: Ip + TestIpExt>() {
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG).build();
        let mut sync_ctx = &sync_ctx;
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        // Test that a non fragmented packet gets dispatched right away.

        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id, 0, 1);

        // Make sure the packet got dispatched.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 1);
    }

    #[ip_test]
    fn test_ip_packet_reassembly<I: Ip + TestIpExt>() {
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG).build();
        let mut sync_ctx = &sync_ctx;
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        // Test that the received packet gets dispatched only after receiving
        // all the fragments.

        // Process fragment #0
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id, 0, 3);

        // Process fragment #1
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id, 1, 3);

        // Make sure no packets got dispatched yet.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        // Process fragment #2
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id, 2, 3);

        // Make sure the packet finally got dispatched now that the final
        // fragment has been 'received'.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 1);
    }

    #[ip_test]
    fn test_ip_packet_reassembly_with_packets_arriving_out_of_order<I: Ip + TestIpExt>() {
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG).build();
        let mut sync_ctx = &sync_ctx;
        let device = DeviceId::new_ethernet(0);
        let fragment_id_0 = 5;
        let fragment_id_1 = 10;
        let fragment_id_2 = 15;

        // Test that received packets gets dispatched only after receiving all
        // the fragments with out of order arrival of fragments.

        // Process packet #0, fragment #1
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id_0, 1, 3);

        // Process packet #1, fragment #2
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id_1, 2, 3);

        // Process packet #1, fragment #0
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id_1, 0, 3);

        // Make sure no packets got dispatched yet.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        // Process a packet that does not require reassembly (packet #2, fragment #0).
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id_2, 0, 1);

        // Make packet #1 got dispatched since it didn't need reassembly.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        // Process packet #0, fragment #2
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id_0, 2, 3);

        // Make sure no other packets got dispatched yet.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        // Process packet #0, fragment #0
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id_0, 0, 3);

        // Make sure that packet #0 finally got dispatched now that the final
        // fragment has been 'received'.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 2);

        // Process packet #1, fragment #1
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id_1, 1, 3);

        // Make sure the packet finally got dispatched now that the final
        // fragment has been 'received'.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 3);
    }

    #[ip_test]
    fn test_ip_packet_reassembly_timer<I: Ip + TestIpExt>()
    where
        IpLayerTimerId: From<FragmentCacheKey<I::Addr>>,
    {
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(I::DUMMY_CONFIG).build();
        let mut sync_ctx = &sync_ctx;
        let device = DeviceId::new_ethernet(0);
        let fragment_id = 5;

        // Test to make sure that packets must arrive within the reassembly
        // timer.

        // Process fragment #0
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id, 0, 3);

        // Make sure a timer got added.
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            IpLayerTimerId::from(FragmentCacheKey::new(
                I::DUMMY_CONFIG.remote_ip.get(),
                I::DUMMY_CONFIG.local_ip.get(),
                fragment_id.into(),
            ))
            .into(),
            ..,
        )]);

        // Process fragment #1
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id, 1, 3);

        // Trigger the timer (simulate a timer for the fragmented packet)
        let key = FragmentCacheKey::new(
            I::DUMMY_CONFIG.remote_ip.get(),
            I::DUMMY_CONFIG.local_ip.get(),
            u32::from(fragment_id),
        );
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            IpLayerTimerId::from(key.into()).into(),
        );

        // Make sure no other timers exist.
        non_sync_ctx.timer_ctx().assert_no_timers_installed();

        // Process fragment #2
        process_ip_fragment::<I, _>(&mut sync_ctx, &mut non_sync_ctx, device, fragment_id, 2, 3);

        // Make sure no packets got dispatched yet since even though we
        // technically received all the fragments, this fragment (#2) arrived
        // too late and the reassembly timer was triggered, causing the prior
        // fragment data to be discarded.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 0);
    }

    #[ip_test]
    fn test_ip_reassembly_only_at_destination_host<I: Ip + TestIpExt>() {
        // Create a new network with two parties (alice & bob) and enable IP
        // packet routing for alice.
        let a = "alice";
        let b = "bob";
        let dummy_config = I::DUMMY_CONFIG;
        let device = DeviceId::new_ethernet(0);
        let mut alice = DummyEventDispatcherBuilder::from_config(dummy_config.swap()).build();
        {
            let Ctx { sync_ctx, non_sync_ctx } = &mut alice;
            set_routing_enabled::<_, _, I>(&mut &*sync_ctx, non_sync_ctx, device, true)
                .expect("qerror setting routing enabled");
        }
        let bob = DummyEventDispatcherBuilder::from_config(dummy_config).build();
        let mut net = crate::context::testutil::new_legacy_simple_dummy_network(a, alice, b, bob);
        let fragment_id = 5;

        // Test that packets only get reassembled and dispatched at the
        // destination. In this test, Alice is receiving packets from some
        // source that is actually destined for Bob. Alice should simply forward
        // the packets without attempting to process or reassemble the
        // fragments.

        // Process fragment #0
        net.with_context("alice", |Ctx { sync_ctx, non_sync_ctx }| {
            process_ip_fragment::<I, _>(&mut &*sync_ctx, non_sync_ctx, device, fragment_id, 0, 3);
        });
        // Make sure the packet got sent from alice to bob
        assert!(!net.step(receive_frame_or_panic, handle_timer).is_idle());

        // Process fragment #1
        net.with_context("alice", |Ctx { sync_ctx, non_sync_ctx }| {
            process_ip_fragment::<I, _>(&mut &*sync_ctx, non_sync_ctx, device, fragment_id, 1, 3);
        });
        assert!(!net.step(receive_frame_or_panic, handle_timer).is_idle());

        // Make sure no packets got dispatched yet.
        assert_eq!(
            get_counter_val(net.non_sync_ctx("alice"), dispatch_receive_ip_packet_name::<I>()),
            0
        );
        assert_eq!(
            get_counter_val(net.non_sync_ctx("bob"), dispatch_receive_ip_packet_name::<I>()),
            0
        );

        // Process fragment #2
        net.with_context("alice", |Ctx { sync_ctx, non_sync_ctx }| {
            process_ip_fragment::<I, _>(&mut &*sync_ctx, non_sync_ctx, device, fragment_id, 2, 3);
        });
        assert!(!net.step(receive_frame_or_panic, handle_timer).is_idle());

        // Make sure the packet finally got dispatched now that the final
        // fragment has been received by bob.
        assert_eq!(
            get_counter_val(net.non_sync_ctx("alice"), dispatch_receive_ip_packet_name::<I>()),
            0
        );
        assert_eq!(
            get_counter_val(net.non_sync_ctx("bob"), dispatch_receive_ip_packet_name::<I>()),
            1
        );

        // Make sure there are no more events.
        assert!(net.step(receive_frame_or_panic, handle_timer).is_idle());
    }

    #[test]
    fn test_ipv6_packet_too_big() {
        // Test sending an IPv6 Packet Too Big Error when receiving a packet
        // that is too big to be forwarded when it isn't destined for the node
        // it arrived at.

        let dummy_config = Ipv6::DUMMY_CONFIG;
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
        let Ctx { sync_ctx, mut non_sync_ctx } = dispatcher_builder.build();
        let mut sync_ctx = &sync_ctx;
        let device = DeviceId::new_ethernet(0);
        set_routing_enabled::<_, _, Ipv6>(&mut sync_ctx, &mut non_sync_ctx, device, true)
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
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            frame_dst,
            ipv6_packet_buf.clone(),
        );

        // Should not have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv6_packet"), 0);
        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv6_packet_too_big"), 1);

        // Should have sent out one frame though.
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);

        // Received packet should be a Packet Too Big ICMP error message.
        let buf = &non_sync_ctx.frames_sent()[0].1[..];
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

    fn create_packet_too_big_buf<A: IpAddress>(
        src_ip: A,
        dst_ip: A,
        mtu: u16,
        body: Option<Buf<Vec<u8>>>,
    ) -> Buf<Vec<u8>> {
        let body = body.unwrap_or_else(|| Buf::new(Vec::new(), ..));

        match [src_ip, dst_ip].into() {
            IpAddr::V4([src_ip, dst_ip]) => body
                .encapsulate(IcmpPacketBuilder::<Ipv4, &mut [u8], IcmpDestUnreachable>::new(
                    dst_ip,
                    src_ip,
                    Icmpv4DestUnreachableCode::FragmentationRequired,
                    NonZeroU16::new(mtu)
                        .map(IcmpDestUnreachable::new_for_frag_req)
                        .unwrap_or_else(Default::default),
                ))
                .encapsulate(Ipv4PacketBuilder::new(src_ip, dst_ip, 64, Ipv4Proto::Icmp))
                .serialize_vec_outer()
                .unwrap(),
            IpAddr::V6([src_ip, dst_ip]) => body
                .encapsulate(IcmpPacketBuilder::<Ipv6, &mut [u8], Icmpv6PacketTooBig>::new(
                    dst_ip,
                    src_ip,
                    IcmpUnusedCode,
                    Icmpv6PacketTooBig::new(u32::from(mtu)),
                ))
                .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, 64, Ipv6Proto::Icmpv6))
                .serialize_vec_outer()
                .unwrap(),
        }
        .into_inner()
    }

    trait GetPmtuIpExt: Ip {
        fn get_pmtu<C: NonSyncContext>(
            state: &StackState<C>,
            local_ip: Self::Addr,
            remote_ip: Self::Addr,
        ) -> Option<u32>;
    }

    impl GetPmtuIpExt for Ipv4 {
        fn get_pmtu<C: NonSyncContext>(
            state: &StackState<C>,
            local_ip: Ipv4Addr,
            remote_ip: Ipv4Addr,
        ) -> Option<u32> {
            state.ipv4.inner.pmtu_cache.lock().get_pmtu(local_ip, remote_ip)
        }
    }

    impl GetPmtuIpExt for Ipv6 {
        fn get_pmtu<C: NonSyncContext>(
            state: &StackState<C>,
            local_ip: Ipv6Addr,
            remote_ip: Ipv6Addr,
        ) -> Option<u32> {
            state.ipv6.inner.pmtu_cache.lock().get_pmtu(local_ip, remote_ip)
        }
    }

    #[ip_test]
    fn test_ip_update_pmtu<I: Ip + TestIpExt + GetPmtuIpExt>() {
        // Test receiving a Packet Too Big (IPv6) or Dest Unreachable
        // Fragmentation Required (IPv4) which should update the PMTU if it is
        // less than the current value.

        let dummy_config = I::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(dummy_config.clone()).build();
        let mut sync_ctx = &sync_ctx;
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
        receive_ip_packet::<_, _, I>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            frame_dst,
            packet_buf,
        );

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        assert_eq!(
            I::get_pmtu(&sync_ctx.state, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
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
        receive_ip_packet::<_, _, I>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            frame_dst,
            packet_buf,
        );

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 2);

        // The PMTU should not have updated to `new_mtu2`
        assert_eq!(
            I::get_pmtu(&sync_ctx.state, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
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
        receive_ip_packet::<_, _, I>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            frame_dst,
            packet_buf,
        );

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 3);

        // The PMTU should have updated to 1900.
        assert_eq!(
            I::get_pmtu(&sync_ctx.state, dummy_config.local_ip.get(), dummy_config.remote_ip.get())
                .unwrap(),
            new_mtu3
        );
    }

    #[ip_test]
    fn test_ip_update_pmtu_too_low<I: Ip + TestIpExt + GetPmtuIpExt>() {
        // Test receiving a Packet Too Big (IPv6) or Dest Unreachable
        // Fragmentation Required (IPv4) which should not update the PMTU if it
        // is less than the min MTU.

        let dummy_config = I::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(dummy_config.clone()).build();
        let mut sync_ctx = &sync_ctx;
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
        receive_ip_packet::<_, _, I>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            frame_dst,
            packet_buf,
        );

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        assert_eq!(
            I::get_pmtu(&sync_ctx.state, dummy_config.local_ip.get(), dummy_config.remote_ip.get()),
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
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(dummy_config.clone()).build();
        let mut sync_ctx = &sync_ctx;
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
        receive_ipv4_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv4_packet"), 1);

        // Should have decreased PMTU value to the next lower PMTU
        // plateau from `crate::ip::path_mtu::PMTU_PLATEAUS`.
        assert_eq!(
            Ipv4::get_pmtu(
                &sync_ctx.state,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get()
            )
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
        receive_ipv4_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv4_packet"), 2);

        // Should not have updated PMTU as there is no other valid
        // lower PMTU value.
        assert_eq!(
            Ipv4::get_pmtu(
                &sync_ctx.state,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get()
            )
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
        receive_ipv4_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv4_packet"), 3);

        // Should have decreased PMTU value to the next lower PMTU
        // plateau from `crate::ip::path_mtu::PMTU_PLATEAUS`.
        assert_eq!(
            Ipv4::get_pmtu(
                &sync_ctx.state,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get()
            )
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
        receive_ipv4_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, packet_buf);

        // Should have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv4_packet"), 4);

        // Should not have updated the PMTU as the current PMTU is lower.
        assert_eq!(
            Ipv4::get_pmtu(
                &sync_ctx.state,
                dummy_config.local_ip.get(),
                dummy_config.remote_ip.get()
            )
            .unwrap(),
            68
        );
    }

    #[test]
    fn test_invalid_icmpv4_in_ipv6() {
        let ip_config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(ip_config.clone()).build();
        let mut sync_ctx = &sync_ctx;
        let device = DeviceId::new_ethernet(0);
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

        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);

        // Should not have dispatched the packet.
        assert_eq!(get_counter_val(&non_sync_ctx, "receive_ipv6_packet"), 1);
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv6_packet"), 0);

        // In IPv6, the next header value (ICMP(v4)) would have been considered
        // unrecognized so an ICMP parameter problem response SHOULD be sent,
        // but the netstack chooses to just drop the packet since we are not
        // required to send the ICMP response.
        assert_empty(non_sync_ctx.frames_sent().iter());
    }

    #[test]
    fn test_invalid_icmpv6_in_ipv4() {
        let ip_config = Ipv4::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(ip_config.clone()).build();
        let mut sync_ctx = &sync_ctx;
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

        receive_ipv4_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);

        // Should have dispatched the packet but resulted in an ICMP error.
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv4_packet"), 1);
        assert_eq!(get_counter_val(&non_sync_ctx, "send_icmpv4_dest_unreachable"), 1);
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        let buf = &non_sync_ctx.frames_sent()[0].1[..];
        let (_, _, _, _, _, _, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, IcmpDestUnreachable, _>(
                buf,
                |_| {},
            )
            .unwrap();
        assert_eq!(code, Icmpv4DestUnreachableCode::DestProtocolUnreachable);
    }

    #[ip_test]
    fn test_joining_leaving_ip_multicast_group<I: Ip + TestIpExt + packet_formats::ip::IpExt>() {
        // Test receiving a packet destined to a multicast IP (and corresponding
        // multicast MAC).

        let config = I::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(config.clone()).build();
        let mut sync_ctx = &sync_ctx;
        let device = DeviceId::new_ethernet(0);
        let multi_addr = I::get_multicast_addr(3).get();
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
        assert!(!is_in_ip_multicast(&sync_ctx, device, multi_addr));
        receive_frame(&mut sync_ctx, &mut non_sync_ctx, device, buf.clone())
            .expect("error receiving frame");
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 0);

        // Join the multicast group and receive the packet, we should dispatch
        // it.
        match multi_addr.into() {
            IpAddr::V4(multicast_addr) => crate::ip::device::join_ip_multicast::<Ipv4, _, _>(
                &mut sync_ctx,
                &mut non_sync_ctx,
                device,
                multicast_addr,
            ),
            IpAddr::V6(multicast_addr) => crate::ip::device::join_ip_multicast::<Ipv6, _, _>(
                &mut sync_ctx,
                &mut non_sync_ctx,
                device,
                multicast_addr,
            ),
        }
        assert!(is_in_ip_multicast(&sync_ctx, device, multi_addr));
        receive_frame(&mut sync_ctx, &mut non_sync_ctx, device, buf.clone())
            .expect("error receiving frame");
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        // Leave the multicast group and receive the packet, we should not
        // dispatch it.
        match multi_addr.into() {
            IpAddr::V4(multicast_addr) => crate::ip::device::leave_ip_multicast::<Ipv4, _, _>(
                &mut sync_ctx,
                &mut non_sync_ctx,
                device,
                multicast_addr,
            ),
            IpAddr::V6(multicast_addr) => crate::ip::device::leave_ip_multicast::<Ipv6, _, _>(
                &mut sync_ctx,
                &mut non_sync_ctx,
                device,
                multicast_addr,
            ),
        }
        assert!(!is_in_ip_multicast(&sync_ctx, device, multi_addr));
        receive_frame(&mut sync_ctx, &mut non_sync_ctx, device, buf.clone())
            .expect("error receiving frame");
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 1);
    }

    #[test]
    fn test_no_dispatch_non_ndp_packets_during_ndp_dad() {
        // Here we make sure we are not dispatching packets destined to a
        // tentative address (that is performing NDP's Duplicate Address
        // Detection (DAD)) -- IPv6 only.

        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::ip::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |config| {
                config.ip_config.ip_enabled = true;

                // Doesn't matter as long as DAD is enabled.
                config.dad_transmits = NonZeroU8::new(1);
            },
        );

        let frame_dst = FrameDestination::Unicast;

        let ip: Ipv6Addr = config.local_mac.to_ipv6_link_local().addr().get();

        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(Ipv6PacketBuilder::new(config.remote_ip, ip, 64, IpProto::Udp.into()))
            .serialize_vec_outer()
            .unwrap()
            .into_inner();

        // Received packet should not have been dispatched.
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv6_packet"), 0);

        // Wait until DAD is complete. Arbitrarily choose a year in the future
        // as a time after which we're confident DAD will be complete. We can't
        // run until there are no timers because some timers will always exist
        // for background tasks.
        //
        // TODO(https://fxbug.dev/48578): Once this test is contextified, use a
        // more precise condition to ensure that DAD is complete.
        let now = non_sync_ctx.now();
        let _: Vec<_> = non_sync_ctx.trigger_timers_until_instant(
            now + Duration::from_secs(60 * 60 * 24 * 365),
            handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer),
        );

        // Received packet should have been dispatched.
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv6_packet"), 1);

        // Set the new IP (this should trigger DAD).
        let ip = config.local_ip.get();
        crate::device::add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            AddrSubnet::new(ip, 128).unwrap(),
        )
        .unwrap();

        let buf = Buf::new(vec![0; 10], ..)
            .encapsulate(Ipv6PacketBuilder::new(config.remote_ip, ip, 64, IpProto::Udp.into()))
            .serialize_vec_outer()
            .unwrap()
            .into_inner();

        // Received packet should not have been dispatched.
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf.clone());
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv6_packet"), 1);

        // Make sure all timers are done (DAD to complete on the interface due
        // to new IP).
        //
        // TODO(https://fxbug.dev/48578): Once this test is contextified, use a
        // more precise condition to ensure that DAD is complete.
        let _: Vec<_> = non_sync_ctx.trigger_timers_until_instant(
            DummyInstant::LATEST,
            handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer),
        );

        // Received packet should have been dispatched.
        receive_ipv6_packet(&mut sync_ctx, &mut non_sync_ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ipv6_packet"), 2);
    }

    #[test]
    fn test_drop_non_unicast_ipv6_source() {
        // Test that an inbound IPv6 packet with a non-unicast source address is
        // dropped.
        let cfg = DUMMY_CONFIG_V6;
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(cfg.clone()).build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            cfg.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

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

        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            FrameDestination::Unicast,
            buf.clone(),
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "receive_ipv6_packet: non-unicast source"), 1);
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

        let Ctx { sync_ctx, mut non_sync_ctx } = builder.clone().build();
        let mut sync_ctx = &sync_ctx;

        // Receive packet addressed to us.
        assert_eq!(
            receive_ipv4_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v4_dev,
                v4_config.local_ip
            ),
            ReceivePacketAction::Deliver
        );
        assert_eq!(
            receive_ipv6_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v6_dev,
                v6_config.local_ip
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to the IPv4 subnet broadcast address.
        assert_eq!(
            receive_ipv4_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v4_dev,
                SpecifiedAddr::new(v4_subnet.broadcast()).unwrap()
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to the IPv4 limited broadcast address.
        assert_eq!(
            receive_ipv4_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v4_dev,
                Ipv4::LIMITED_BROADCAST_ADDRESS
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to a multicast address we're subscribed to.
        crate::ip::device::join_ip_multicast::<Ipv4, _, _>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            v4_dev,
            Ipv4::ALL_ROUTERS_MULTICAST_ADDRESS,
        );
        assert_eq!(
            receive_ipv4_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v4_dev,
                Ipv4::ALL_ROUTERS_MULTICAST_ADDRESS.into_specified()
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to the all-nodes multicast address.
        assert_eq!(
            receive_ipv6_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v6_dev,
                Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.into_specified()
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to a multicast address we're subscribed to.
        assert_eq!(
            receive_ipv6_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v6_dev,
                v6_config.local_ip.to_solicited_node_address().into_specified(),
            ),
            ReceivePacketAction::Deliver
        );

        // Receive packet addressed to a tentative address.
        {
            // Construct a one-off context that has DAD enabled. The context
            // built above has DAD disabled, and so addresses start off in the
            // assigned state rather than the tentative state.
            let Ctx { sync_ctx, mut non_sync_ctx } = DummyCtx::default();
            let mut sync_ctx = &sync_ctx;
            let local_mac = v6_config.local_mac;
            let device = crate::device::add_ethernet_device(
                &mut sync_ctx,
                &mut non_sync_ctx,
                local_mac,
                Ipv6::MINIMUM_LINK_MTU.into(),
            );
            crate::ip::device::update_ipv6_configuration(
                &mut sync_ctx,
                &mut non_sync_ctx,
                device,
                |config| {
                    config.ip_config.ip_enabled = true;

                    // Doesn't matter as long as DAD is enabled.
                    config.dad_transmits = NonZeroU8::new(1);
                },
            );
            let tentative: UnicastAddr<Ipv6Addr> = local_mac.to_ipv6_link_local().addr().get();
            assert_eq!(
                receive_ipv6_packet_action(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    device,
                    tentative.into_specified()
                ),
                ReceivePacketAction::Drop { reason: DropReason::Tentative }
            );
        }

        // Receive packet destined to a remote address when forwarding is
        // disabled on the inbound interface.
        assert_eq!(
            receive_ipv4_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v4_dev,
                v4_config.remote_ip
            ),
            ReceivePacketAction::Drop { reason: DropReason::ForwardingDisabledInboundIface }
        );
        assert_eq!(
            receive_ipv6_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v6_dev,
                v6_config.remote_ip
            ),
            ReceivePacketAction::Drop { reason: DropReason::ForwardingDisabledInboundIface }
        );

        // Receive packet destined to a remote address when forwarding is
        // enabled both globally and on the inbound device.
        set_routing_enabled::<_, _, Ipv4>(&mut sync_ctx, &mut non_sync_ctx, v4_dev, true)
            .expect("error setting routing enabled");
        set_routing_enabled::<_, _, Ipv6>(&mut sync_ctx, &mut non_sync_ctx, v6_dev, true)
            .expect("error setting routing enabled");
        assert_eq!(
            receive_ipv4_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v4_dev,
                v4_config.remote_ip
            ),
            ReceivePacketAction::Forward {
                dst: Destination { next_hop: v4_config.remote_ip, device: v4_dev }
            }
        );
        assert_eq!(
            receive_ipv6_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v6_dev,
                v6_config.remote_ip
            ),
            ReceivePacketAction::Forward {
                dst: Destination { next_hop: v6_config.remote_ip, device: v6_dev }
            }
        );

        // Receive packet destined to a host with no route when forwarding is
        // enabled both globally and on the inbound device.
        *sync_ctx.state.ipv4.inner.table.write() = Default::default();
        *sync_ctx.state.ipv6.inner.table.write() = Default::default();
        assert_eq!(
            receive_ipv4_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v4_dev,
                v4_config.remote_ip
            ),
            ReceivePacketAction::SendNoRouteToDest
        );
        assert_eq!(
            receive_ipv6_packet_action(
                &mut sync_ctx,
                &mut non_sync_ctx,
                v6_dev,
                v6_config.remote_ip
            ),
            ReceivePacketAction::SendNoRouteToDest
        );
    }
}
