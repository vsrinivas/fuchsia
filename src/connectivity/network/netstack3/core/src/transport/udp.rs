// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The User Datagram Protocol (UDP).

use alloc::{collections::hash_map::DefaultHasher, vec::Vec};
use core::{
    convert::Infallible as Never,
    fmt::Debug,
    hash::{Hash, Hasher},
    marker::PhantomData,
    mem,
    num::{NonZeroU16, NonZeroU8, NonZeroUsize},
    ops::RangeInclusive,
};

use derivative::Derivative;
use either::Either;
use log::trace;
use net_types::{
    ip::{GenericOverIp, Ip, IpAddress, IpInvariant as IpInv, IpVersionMarker, Ipv4, Ipv6},
    MulticastAddr, MulticastAddress as _, SpecifiedAddr, Witness, ZonedAddr,
};
use nonzero_ext::nonzero;
use packet::{BufferMut, ParsablePacket, ParseBuffer, Serializer};
use packet_formats::{
    error::ParseError,
    ip::IpProto,
    udp::{UdpPacket, UdpPacketBuilder, UdpPacketRaw, UdpParseArgs},
};
use rand::RngCore;
use thiserror::Error;

use crate::{
    algorithm::{PortAlloc, PortAllocImpl, ProtocolFlowId},
    context::{CounterContext, InstantContext, RngContext},
    convert::OwnedOrCloned,
    data_structures::{
        id_map::Entry as IdMapEntry,
        id_map_collection::IdMapCollectionKey,
        socketmap::{IterShadows as _, SocketMap, Tagged as _},
    },
    error::{LocalAddressError, SocketError, ZonedAddressError},
    ip::{
        icmp::IcmpIpExt,
        socket::{IpSockCreationError, IpSockSendError},
        BufferIpTransportContext, BufferTransportIpContext, IpDeviceId, IpDeviceIdContext, IpExt,
        IpTransportContext, TransportIpContext, TransportReceiveError,
    },
    socket::{
        self,
        address::{ConnAddr, ConnIpAddr, IpPortSpec, ListenerAddr, ListenerIpAddr},
        datagram::{
            self, ConnState, DatagramBoundId, DatagramSocketId, DatagramSocketStateSpec,
            DatagramSockets, DatagramStateContext, DatagramStateNonSyncContext, InUseError,
            IpOptions, ListenerState, MulticastMembershipInterfaceSelector,
            SetMulticastMembershipError, SocketHopLimits, UnboundSocketState,
        },
        posix::{
            PosixAddrState, PosixAddrVecIter, PosixAddrVecTag, PosixConflictPolicy,
            PosixSharingOptions, PosixSocketStateSpec, ToPosixSharingOptions,
        },
        AddrVec, Bound, BoundSocketMap, InsertError, SocketAddrType, SocketMapAddrSpec,
        SocketTypeState as _, SocketTypeStateEntry as _, SocketTypeStateMut as _,
    },
    sync::RwLock,
    BufferNonSyncContext, DeviceId, NonSyncContext, SyncCtx,
};

/// A builder for UDP layer state.
#[derive(Clone)]
pub struct UdpStateBuilder {
    send_port_unreachable: bool,
}

impl Default for UdpStateBuilder {
    fn default() -> UdpStateBuilder {
        UdpStateBuilder { send_port_unreachable: false }
    }
}

impl UdpStateBuilder {
    /// Enable or disable sending ICMP Port Unreachable messages in response to
    /// inbound UDP packets for which a corresponding local socket does not
    /// exist (default: disabled).
    ///
    /// Responding with an ICMP Port Unreachable error is a vector for reflected
    /// Denial-of-Service (DoS) attacks. The attacker can send a UDP packet to a
    /// closed port with the source address set to the address of the victim,
    /// and ICMP response will be sent there.
    ///
    /// According to [RFC 1122 Section 4.1.3.1], "\[i\]f a datagram arrives
    /// addressed to a UDP port for which there is no pending LISTEN call, UDP
    /// SHOULD send an ICMP Port Unreachable message." Since an ICMP response is
    /// not mandatory, and due to the security risks described, responses are
    /// disabled by default.
    ///
    /// [RFC 1122 Section 4.1.3.1]: https://tools.ietf.org/html/rfc1122#section-4.1.3.1
    pub fn send_port_unreachable(&mut self, send_port_unreachable: bool) -> &mut Self {
        self.send_port_unreachable = send_port_unreachable;
        self
    }

    pub(crate) fn build<I: IpExt, D: IpDeviceId>(self) -> UdpState<I, D> {
        UdpState { sockets: Default::default(), send_port_unreachable: self.send_port_unreachable }
    }
}

/// Convenience alias to make names shorter.
type UdpBoundSocketMap<I, D> = BoundSocketMap<IpPortSpec<I, D>, Udp<I, D>>;

/// A collection of UDP sockets.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub struct UdpSockets<I: Ip + IpExt, D: IpDeviceId> {
    sockets: DatagramSockets<IpPortSpec<I, D>, Udp<I, D>>,
    /// lazy_port_alloc is lazy-initialized when it's used.
    lazy_port_alloc: Option<PortAlloc<UdpBoundSocketMap<I, D>>>,
}

impl<I: IpExt, D: IpDeviceId> UdpSockets<I, D> {
    /// Helper function to allocate a local port.
    ///
    /// Attempts to allocate a new unused local port with the given flow identifier
    /// `id`.
    pub(crate) fn try_alloc_local_port<R: RngCore>(
        &mut self,
        rng: &mut R,
        id: ProtocolFlowId<I::Addr>,
    ) -> Option<NonZeroU16> {
        let Self { lazy_port_alloc, sockets: DatagramSockets { bound, unbound: _ } } = self;
        // Lazily init port_alloc if it hasn't been inited yet.
        let port_alloc = lazy_port_alloc.get_or_insert_with(|| PortAlloc::new(rng));
        port_alloc.try_alloc(&id, bound).and_then(NonZeroU16::new)
    }
}

/// The state associated with the UDP protocol.
///
/// `D` is the device ID type.
pub(crate) struct UdpState<I: IpExt, D: IpDeviceId> {
    pub(crate) sockets: RwLock<UdpSockets<I, D>>,
    pub(crate) send_port_unreachable: bool,
}

impl<I: IpExt, D: IpDeviceId> Default for UdpState<I, D> {
    fn default() -> UdpState<I, D> {
        UdpStateBuilder::default().build()
    }
}

/// Uninstantiatable type for implementing PosixSocketMapSpec.
struct Udp<I, D>(PhantomData<(I, D)>, Never);

/// Produces an iterator over eligible receiving socket addresses.
fn iter_receiving_addrs<I: Ip + IpExt, D: IpDeviceId>(
    addr: ConnIpAddr<I::Addr, NonZeroU16, NonZeroU16>,
    device: D,
) -> impl Iterator<Item = AddrVec<IpPortSpec<I, D>>> {
    PosixAddrVecIter::with_device(addr, device)
}

pub(crate) fn check_posix_sharing<A: SocketMapAddrSpec, P: PosixSocketStateSpec>(
    new_sharing: PosixSharingOptions,
    dest: AddrVec<A>,
    socketmap: &SocketMap<AddrVec<A>, Bound<P>>,
) -> Result<(), InsertError> {
    // Having a value present at a shadowed address is disqualifying, unless
    // both the new and existing sockets allow port sharing.
    if dest.iter_shadows().any(|a| {
        socketmap.get(&a).map_or(false, |bound| {
            !bound.tag(&a).to_sharing_options().is_shareable_with_new_state(new_sharing)
        })
    }) {
        return Err(InsertError::ShadowAddrExists);
    }

    // Likewise, the presence of a value that shadows the target address is
    // disqualifying unless both allow port sharing.
    match &dest {
        AddrVec::Conn(ConnAddr { ip: _, device: None }) | AddrVec::Listen(_) => {
            if socketmap.descendant_counts(&dest).any(|(tag, _): &(_, NonZeroUsize)| {
                !tag.to_sharing_options().is_shareable_with_new_state(new_sharing)
            }) {
                return Err(InsertError::ShadowerExists);
            }
        }
        AddrVec::Conn(ConnAddr { ip: _, device: Some(_) }) => {
            // No need to check shadows here because there are no addresses
            // that shadow a ConnAddr with a device.
            debug_assert_eq!(socketmap.descendant_counts(&dest).len(), 0)
        }
    }

    // There are a few combinations of addresses that can conflict with
    // each other even though there is not a direct shadowing relationship:
    // - listener address with device and connected address without.
    // - "any IP" listener with device and specific IP listener without.
    // - "any IP" listener with device and connected address without.
    //
    // The complication is that since these pairs of addresses don't have a
    // direct shadowing relationship, it's not possible to query for one
    // from the other in the socketmap without a linear scan. Instead. we
    // rely on the fact that the tag values in the socket map have different
    // values for entries with and without device IDs specified.
    fn conflict_exists<A: SocketMapAddrSpec, P: PosixSocketStateSpec>(
        new_sharing: PosixSharingOptions,
        socketmap: &SocketMap<AddrVec<A>, Bound<P>>,
        addr: impl Into<AddrVec<A>>,
        mut is_conflicting: impl FnMut(&PosixAddrVecTag) -> bool,
    ) -> bool {
        socketmap.descendant_counts(&addr.into()).any(|(tag, _): &(_, NonZeroUsize)| {
            is_conflicting(tag)
                && !tag.to_sharing_options().is_shareable_with_new_state(new_sharing)
        })
    }

    let found_indirect_conflict = match dest {
        AddrVec::Listen(ListenerAddr {
            ip: ListenerIpAddr { addr: None, identifier },
            device: Some(_device),
        }) => {
            // An address with a device will shadow an any-IP listener
            // `dest` with a device so we only need to check for addresses
            // without a device. Likewise, an any-IP listener will directly
            // shadow `dest`, so an indirect conflict can only come from a
            // specific listener or connected socket (without a device).
            conflict_exists(
                new_sharing,
                socketmap,
                ListenerAddr { ip: ListenerIpAddr { addr: None, identifier }, device: None },
                |PosixAddrVecTag { has_device, addr_type, sharing: _ }| {
                    !*has_device
                        && match addr_type {
                            SocketAddrType::SpecificListener | SocketAddrType::Connected => true,
                            SocketAddrType::AnyListener => false,
                        }
                },
            )
        }
        AddrVec::Listen(ListenerAddr {
            ip: ListenerIpAddr { addr: Some(ip), identifier },
            device: Some(_device),
        }) => {
            // A specific-IP listener `dest` with a device will be shadowed
            // by a connected socket with a device and will shadow
            // specific-IP addresses without a device and any-IP listeners
            // with and without devices. That means an indirect conflict can
            // only come from a connected socket without a device.
            conflict_exists(
                new_sharing,
                socketmap,
                ListenerAddr { ip: ListenerIpAddr { addr: Some(ip), identifier }, device: None },
                |PosixAddrVecTag { has_device, addr_type, sharing: _ }| {
                    !*has_device
                        && match addr_type {
                            SocketAddrType::Connected => true,
                            SocketAddrType::AnyListener | SocketAddrType::SpecificListener => false,
                        }
                },
            )
        }
        AddrVec::Listen(ListenerAddr {
            ip: ListenerIpAddr { addr: Some(_), identifier },
            device: None,
        }) => {
            // A specific-IP listener `dest` without a device will be
            // shadowed by a specific-IP listener with a device and by any
            // connected socket (with or without a device).  It will also
            // shadow an any-IP listener without a device, which means an
            // indirect conflict can only come from an any-IP listener with
            // a device.
            conflict_exists(
                new_sharing,
                socketmap,
                ListenerAddr { ip: ListenerIpAddr { addr: None, identifier }, device: None },
                |PosixAddrVecTag { has_device, addr_type, sharing: _ }| {
                    *has_device
                        && match addr_type {
                            SocketAddrType::AnyListener => true,
                            SocketAddrType::SpecificListener | SocketAddrType::Connected => false,
                        }
                },
            )
        }
        AddrVec::Conn(ConnAddr {
            ip: ConnIpAddr { local: (local_ip, local_identifier), remote: _ },
            device: None,
        }) => {
            // A connected socket `dest` without a device shadows listeners
            // without devices, and is shadowed by a connected socket with
            // a device. It can indirectly conflict with listening sockets
            // with devices.

            // Check for specific-IP listeners with devices, which would
            // indirectly conflict.
            conflict_exists(
                new_sharing,
                socketmap,
                ListenerAddr {
                    ip: ListenerIpAddr {
                        addr: Some(local_ip),
                        identifier: local_identifier.clone(),
                    },
                    device: None,
                },
                |PosixAddrVecTag { has_device, addr_type, sharing: _ }| {
                    *has_device
                        && match addr_type {
                            SocketAddrType::SpecificListener => true,
                            SocketAddrType::AnyListener | SocketAddrType::Connected => false,
                        }
                },
            ) ||
            // Check for any-IP listeners with devices since they conflict.
            // Note that this check cannot be combined with the one above
            // since they examine tag counts for different addresses. While
            // the counts of tags matched above *will* also be propagated to
            // the any-IP listener entry, they would be indistinguishable
            // from non-conflicting counts. For a connected address with
            // `Some(local_ip)`, the descendant counts at the listener
            // address with `addr = None` would include any
            // `SpecificListener` tags for both addresses with
            // `Some(local_ip)` and `Some(other_local_ip)`. The former
            // indirectly conflicts with `dest` but the latter does not,
            // hence this second distinct check.
            conflict_exists(
                new_sharing,
                socketmap,
                ListenerAddr {
                    ip: ListenerIpAddr { addr: None, identifier: local_identifier },
                    device: None,
                },
                |PosixAddrVecTag { has_device, addr_type, sharing: _ }| {
                    *has_device
                        && match addr_type {
                            SocketAddrType::AnyListener => true,
                            SocketAddrType::SpecificListener | SocketAddrType::Connected => false,
                        }
                },
            )
        }
        AddrVec::Listen(ListenerAddr {
            ip: ListenerIpAddr { addr: None, identifier: _ },
            device: _,
        }) => false,
        AddrVec::Conn(ConnAddr { ip: _, device: Some(_device) }) => false,
    };
    if found_indirect_conflict {
        Err(InsertError::IndirectConflict)
    } else {
        Ok(())
    }
}

impl<I: IpExt, D: IpDeviceId> PosixSocketStateSpec for Udp<I, D> {
    type ListenerId = UdpListenerId<I>;
    type ConnId = UdpConnId<I>;

    type ListenerState = ListenerState<I::Addr, D>;
    type ConnState = ConnState<I, D>;
}

impl<I: IpExt, D: IpDeviceId> PosixConflictPolicy<IpPortSpec<I, D>> for Udp<I, D> {
    fn check_posix_sharing(
        new_sharing: PosixSharingOptions,
        addr: AddrVec<IpPortSpec<I, D>>,
        socketmap: &SocketMap<AddrVec<IpPortSpec<I, D>>, Bound<Self>>,
    ) -> Result<(), InsertError> {
        check_posix_sharing(new_sharing, addr, socketmap)
    }
}

impl<I: IpExt, D: IpDeviceId> DatagramSocketStateSpec for Udp<I, D> {
    type UnboundId = UdpUnboundId<I>;
    type UnboundSharingState = PosixSharingOptions;
}

enum LookupResult<I: Ip, D: IpDeviceId> {
    Conn(UdpConnId<I>, ConnAddr<I::Addr, D, NonZeroU16, NonZeroU16>),
    Listener(UdpListenerId<I>, ListenerAddr<I::Addr, D, NonZeroU16>),
}

#[derive(Hash, Copy, Clone)]
struct SocketSelectorParams<I: Ip, A: AsRef<I::Addr>> {
    src_ip: A,
    dst_ip: A,
    src_port: u16,
    dst_port: u16,
    _ip: IpVersionMarker<I>,
}

impl<T> PosixAddrState<T> {
    fn select_receiver<I: Ip, A: AsRef<I::Addr> + Hash>(
        &self,
        selector: SocketSelectorParams<I, A>,
    ) -> &T {
        match self {
            PosixAddrState::Exclusive(id) => id,
            PosixAddrState::ReusePort(ids) => {
                let mut hasher = DefaultHasher::new();
                selector.hash(&mut hasher);
                let index: usize = hasher.finish() as usize % ids.len();
                &ids[index]
            }
        }
    }

    fn collect_all_ids(&self) -> impl Iterator<Item = &'_ T> {
        match self {
            PosixAddrState::Exclusive(id) => Either::Left(core::iter::once(id)),
            PosixAddrState::ReusePort(ids) => Either::Right(ids.iter()),
        }
    }
}

enum AddrEntry<'a, A: SocketMapAddrSpec> {
    Listen(
        &'a PosixAddrState<UdpListenerId<<A::IpAddr as IpAddress>::Version>>,
        ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
    ),
    Conn(
        &'a PosixAddrState<UdpConnId<<A::IpAddr as IpAddress>::Version>>,
        ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    ),
}

impl<'a, I: Ip + IpExt, D: IpDeviceId + 'a> AddrEntry<'a, IpPortSpec<I, D>> {
    /// Returns an iterator that yields a `LookupResult` for each contained ID.
    fn collect_all_ids(self) -> impl Iterator<Item = LookupResult<I, D>> + 'a {
        match self {
            Self::Listen(state, l) => Either::Left(
                state.collect_all_ids().map(move |id| LookupResult::Listener(*id, l.clone())),
            ),
            Self::Conn(state, c) => Either::Right(
                state.collect_all_ids().map(move |id| LookupResult::Conn(*id, c.clone())),
            ),
        }
    }

    /// Returns a `LookupResult` for the contained ID that matches the selector.
    fn select_receiver<A: AsRef<I::Addr> + Hash>(
        self,
        selector: SocketSelectorParams<I, A>,
    ) -> LookupResult<I, D> {
        match self {
            Self::Listen(state, l) => LookupResult::Listener(*state.select_receiver(selector), l),
            Self::Conn(state, c) => LookupResult::Conn(*state.select_receiver(selector), c),
        }
    }
}

impl<I: Ip + IpExt, D: IpDeviceId> UdpBoundSocketMap<I, D> {
    /// Finds the socket(s) that should receive an incoming packet.
    ///
    /// Uses the provided addresses and receiving device to look up sockets that
    /// should receive a matching incoming packet. The returned iterator may
    /// yield 0, 1, or multiple sockets.
    fn lookup(
        &self,
        dst_ip: SpecifiedAddr<I::Addr>,
        src_ip: SpecifiedAddr<I::Addr>,
        dst_port: NonZeroU16,
        src_port: NonZeroU16,
        device: D,
    ) -> impl Iterator<Item = LookupResult<I, D>> + '_ {
        let mut matching_entries = iter_receiving_addrs(
            ConnIpAddr { local: (dst_ip, dst_port), remote: (src_ip, src_port) },
            device,
        )
        .filter_map(move |addr: AddrVec<IpPortSpec<I, D>>| match addr {
            AddrVec::Listen(l) => {
                self.listeners().get_by_addr(&l).map(|state| AddrEntry::Listen(state, l))
            }
            AddrVec::Conn(c) => self.conns().get_by_addr(&c).map(|state| AddrEntry::Conn(state, c)),
        });

        if dst_ip.is_multicast() {
            let all_ids = matching_entries.flat_map(AddrEntry::collect_all_ids);
            Either::Left(all_ids)
        } else {
            let selector = SocketSelectorParams::<I, _> {
                src_ip,
                dst_ip,
                src_port: src_port.get(),
                dst_port: dst_port.get(),
                _ip: IpVersionMarker::default(),
            };

            let single_id: Option<_> =
                matching_entries.next().map(move |entry| entry.select_receiver(selector));
            Either::Right(single_id.into_iter())
        }
    }
}

/// Helper function to allocate a listen port.
///
/// Finds a random ephemeral port that is not in the provided `used_ports` set.
fn try_alloc_listen_port<I: IpExt, C: UdpStateNonSyncContext<I>, D: IpDeviceId>(
    ctx: &mut C,
    is_available: impl Fn(NonZeroU16) -> Result<(), InUseError>,
) -> Option<NonZeroU16> {
    let mut port = UdpBoundSocketMap::<I, D>::rand_ephemeral(ctx.rng_mut());
    for _ in UdpBoundSocketMap::<I, D>::EPHEMERAL_RANGE {
        // We can unwrap here because we know that the EPHEMERAL_RANGE doesn't
        // include 0.
        let tryport = NonZeroU16::new(port.get()).unwrap();
        match is_available(tryport) {
            Ok(()) => return Some(tryport),
            Err(InUseError {}) => port.next(),
        }
    }
    None
}

impl<I: IpExt, D: IpDeviceId> PortAllocImpl for UdpBoundSocketMap<I, D> {
    const EPHEMERAL_RANGE: RangeInclusive<u16> = 49152..=65535;
    const TABLE_SIZE: NonZeroUsize = nonzero!(20usize);
    type Id = ProtocolFlowId<I::Addr>;

    fn is_port_available(&self, id: &Self::Id, port: u16) -> bool {
        // We can safely unwrap here, because the ports received in
        // `is_port_available` are guaranteed to be in `EPHEMERAL_RANGE`.
        let port = NonZeroU16::new(port).unwrap();
        let conn = ConnAddr::from_protocol_flow_and_local_port(id, port);

        // A port is free if there are no sockets currently using it, and if
        // there are no sockets that are shadowing it.
        AddrVec::from(conn).iter_shadows().all(|a| match &a {
            AddrVec::Listen(l) => self.listeners().get_by_addr(&l).is_none(),
            AddrVec::Conn(c) => self.conns().get_by_addr(&c).is_none(),
        } && self.get_shadower_counts(&a) == 0)
    }
}

/// Information associated with a UDP connection.
#[derive(Debug, GenericOverIp)]
#[cfg_attr(test, derive(PartialEq))]
pub struct UdpConnInfo<A: IpAddress, D> {
    /// The local address associated with a UDP connection.
    pub local_ip: ZonedAddr<A, D>,
    /// The local port associated with a UDP connection.
    pub local_port: NonZeroU16,
    /// The remote address associated with a UDP connection.
    pub remote_ip: ZonedAddr<A, D>,
    /// The remote port associated with a UDP connection.
    pub remote_port: NonZeroU16,
}

fn maybe_with_zone<A: IpAddress, D>(
    addr: SpecifiedAddr<A>,
    device: impl OwnedOrCloned<Option<D>>,
) -> ZonedAddr<A, D> {
    // Invariant guaranteed by bind/connect/reconnect: if a socket has an
    // address that must have a zone, it has a bound device.
    if let Some(addr_and_zone) = socket::try_into_null_zoned(&addr) {
        let device = device.into_owned().unwrap_or_else(|| {
            unreachable!("connected address has zoned address {:?} but no device", addr)
        });
        ZonedAddr::Zoned(addr_and_zone.map_zone(|()| device))
    } else {
        ZonedAddr::Unzoned(addr)
    }
}

impl<A: IpAddress, D: Clone + Debug> From<ConnAddr<A, D, NonZeroU16, NonZeroU16>>
    for UdpConnInfo<A, D>
{
    fn from(c: ConnAddr<A, D, NonZeroU16, NonZeroU16>) -> Self {
        let ConnAddr {
            device,
            ip: ConnIpAddr { local: (local_ip, local_port), remote: (remote_ip, remote_port) },
        } = c;
        Self {
            local_ip: maybe_with_zone(local_ip, &device),
            local_port,
            remote_ip: maybe_with_zone(remote_ip, device),
            remote_port,
        }
    }
}

/// Information associated with a UDP listener
#[derive(GenericOverIp)]
pub struct UdpListenerInfo<A: IpAddress, D> {
    /// The local address associated with a UDP listener, or `None` for any
    /// address.
    pub local_ip: Option<ZonedAddr<A, D>>,
    /// The local port associated with a UDP listener.
    pub local_port: NonZeroU16,
}

impl<A: IpAddress, D> From<ListenerAddr<A, D, NonZeroU16>> for UdpListenerInfo<A, D> {
    fn from(
        ListenerAddr { ip: ListenerIpAddr { addr, identifier }, device }: ListenerAddr<
            A,
            D,
            NonZeroU16,
        >,
    ) -> Self {
        let local_ip = addr.map(|addr| maybe_with_zone(addr, device));
        Self { local_ip, local_port: identifier }
    }
}

impl<A: IpAddress, D> From<NonZeroU16> for UdpListenerInfo<A, D> {
    fn from(local_port: NonZeroU16) -> Self {
        Self { local_ip: None, local_port }
    }
}

/// The identifier for an unbound UDP socket.
///
/// New UDP sockets are created in an unbound state, and are assigned opaque
/// identifiers. These identifiers can then be used to connect the socket or
/// make it a listener.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash, GenericOverIp)]
pub struct UdpUnboundId<I: Ip>(usize, IpVersionMarker<I>);

impl<I: Ip> UdpUnboundId<I> {
    fn new(id: usize) -> UdpUnboundId<I> {
        UdpUnboundId(id, IpVersionMarker::default())
    }
}

impl<I: Ip> From<usize> for UdpUnboundId<I> {
    fn from(index: usize) -> Self {
        Self::new(index)
    }
}

impl<I: Ip> From<UdpUnboundId<I>> for usize {
    fn from(UdpUnboundId(index, _): UdpUnboundId<I>) -> usize {
        index
    }
}

impl<I: Ip> IdMapCollectionKey for UdpUnboundId<I> {
    const VARIANT_COUNT: usize = 1;

    fn get_variant(&self) -> usize {
        0
    }

    fn get_id(&self) -> usize {
        (*self).into()
    }
}

/// The ID identifying a UDP connection.
///
/// When a new UDP connection is added, it is given a unique `UdpConnId`. These
/// are opaque `usize`s which are intentionally allocated as densely as possible
/// around 0, making it possible to store any associated data in a `Vec` indexed
/// by the ID. `UdpConnId` implements `Into<usize>`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash, GenericOverIp)]
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

impl<I: Ip> From<usize> for UdpConnId<I> {
    fn from(index: usize) -> Self {
        UdpConnId::new(index)
    }
}

/// The ID identifying a UDP listener.
///
/// When a new UDP listener is added, it is given a unique `UdpListenerId`.
/// These are opaque `usize`s which are intentionally allocated as densely as
/// possible around 0, making it possible to store any associated data in a
/// `Vec` indexed by the ID. The `listener_type` field is used to look at the
/// correct backing `Vec`: `listeners` or `wildcard_listeners`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash, GenericOverIp)]
pub struct UdpListenerId<I: Ip> {
    id: usize,
    _marker: IpVersionMarker<I>,
}

impl<I: Ip> UdpListenerId<I> {
    fn new(id: usize) -> Self {
        UdpListenerId { id, _marker: IpVersionMarker::default() }
    }
}

impl<I: Ip> IdMapCollectionKey for UdpListenerId<I> {
    const VARIANT_COUNT: usize = 1;
    fn get_variant(&self) -> usize {
        0
    }
    fn get_id(&self) -> usize {
        self.id
    }
}

impl<I: Ip> From<UdpListenerId<I>> for usize {
    fn from(UdpListenerId { id, _marker }: UdpListenerId<I>) -> Self {
        id
    }
}

impl<I: Ip> From<usize> for UdpListenerId<I> {
    fn from(index: usize) -> Self {
        UdpListenerId::new(index)
    }
}

/// A unique identifier for a bound UDP connection or listener.
///
/// Contains either a [`UdpConnId`] or [`UdpListenerId`] in contexts where either
/// can be present.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash, GenericOverIp)]
pub enum UdpBoundId<I: Ip> {
    /// A UDP connection.
    Connected(UdpConnId<I>),
    /// A UDP listener.
    Listening(UdpListenerId<I>),
}

impl<I: Ip> From<UdpConnId<I>> for UdpBoundId<I> {
    fn from(id: UdpConnId<I>) -> Self {
        Self::Connected(id)
    }
}

impl<I: Ip> From<UdpListenerId<I>> for UdpBoundId<I> {
    fn from(id: UdpListenerId<I>) -> Self {
        Self::Listening(id)
    }
}

impl<I: Ip + IpExt, D: IpDeviceId> From<UdpBoundId<I>> for DatagramBoundId<Udp<I, D>> {
    fn from(id: UdpBoundId<I>) -> Self {
        match id {
            UdpBoundId::Connected(id) => DatagramBoundId::Connected(id),
            UdpBoundId::Listening(id) => DatagramBoundId::Listener(id),
        }
    }
}

/// A unique identifier for a bound or unbound UDP socket.
///
/// Contains either a [`UdpBoundId`] or [`UdpUnboundId`] in contexts where
/// either can be present.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash, GenericOverIp)]
pub enum UdpSocketId<I: Ip> {
    /// A bound UDP socket ID.
    Bound(UdpBoundId<I>),
    /// An unbound UDP socket ID.
    Unbound(UdpUnboundId<I>),
}

impl<I: Ip> From<UdpBoundId<I>> for UdpSocketId<I> {
    fn from(id: UdpBoundId<I>) -> Self {
        Self::Bound(id)
    }
}

impl<I: Ip> From<UdpUnboundId<I>> for UdpSocketId<I> {
    fn from(id: UdpUnboundId<I>) -> Self {
        Self::Unbound(id)
    }
}

impl<I: Ip> From<UdpListenerId<I>> for UdpSocketId<I> {
    fn from(id: UdpListenerId<I>) -> Self {
        Self::Bound(id.into())
    }
}

impl<I: Ip> From<UdpConnId<I>> for UdpSocketId<I> {
    fn from(id: UdpConnId<I>) -> Self {
        Self::Bound(id.into())
    }
}

impl<I: IpExt, D: IpDeviceId> From<UdpSocketId<I>> for DatagramSocketId<Udp<I, D>> {
    fn from(id: UdpSocketId<I>) -> Self {
        match id {
            UdpSocketId::Unbound(id) => Self::Unbound(id),
            UdpSocketId::Bound(id) => Self::Bound(id.into()),
        }
    }
}

/// An execution context for the UDP protocol.
pub trait UdpContext<I: IcmpIpExt> {
    /// Receives an ICMP error message related to a previously-sent UDP packet.
    ///
    /// `err` is the specific error identified by the incoming ICMP error
    /// message.
    ///
    /// Concretely, this method is called when an ICMP error message is received
    /// which contains an original packet which - based on its source and
    /// destination IPs and ports - most likely originated from the given
    /// socket. Note that the offending packet is not guaranteed to have
    /// originated from the given socket. For example, it may have originated
    /// from a previous socket with the same addresses, it may be the result of
    /// packet corruption on the network, it may have been injected by a
    /// malicious party, etc.
    fn receive_icmp_error(&mut self, _id: UdpBoundId<I>, _err: I::ErrorCode) {
        log_unimplemented!((), "UdpContext::receive_icmp_error: not implemented");
    }
}

/// The non-synchronized context for UDP.
pub trait UdpStateNonSyncContext<I: IpExt>:
    InstantContext + RngContext + UdpContext<I> + CounterContext
{
}
impl<I: IpExt, C: InstantContext + RngContext + UdpContext<I> + CounterContext>
    UdpStateNonSyncContext<I> for C
{
}

/// An execution context for the UDP protocol which also provides access to state.
pub(crate) trait UdpStateContext<I: IpExt, C: UdpStateNonSyncContext<I>>:
    TransportIpContext<I, C>
{
    /// The synchronized context passed to the callback provided to
    /// `with_sockets_mut`.
    type IpSocketsCtx: TransportIpContext<I, C, DeviceId = Self::DeviceId>;

    /// Requests that the specified device join the given multicast group.
    ///
    /// If this method is called multiple times with the same device and
    /// address, the device will remain joined to the multicast group until
    /// [`UdpContext::leave_multicast_group`] has been called the same number of times.
    fn join_multicast_group(
        &mut self,
        ctx: &mut C,
        device: &Self::DeviceId,
        addr: MulticastAddr<I::Addr>,
    );

    /// Requests that the specified device leave the given multicast group.
    ///
    /// Each call to this method must correspond to an earlier call to
    /// [`UdpContext::join_multicast_group`]. The device remains a member of the
    /// multicast group so long as some call to `join_multicast_group` has been
    /// made without a corresponding call to `leave_multicast_group`.
    fn leave_multicast_group(
        &mut self,
        ctx: &mut C,
        device: &Self::DeviceId,
        addr: MulticastAddr<I::Addr>,
    );

    /// Calls the function with an immutable reference to UDP sockets.
    fn with_sockets<O, F: FnOnce(&Self::IpSocketsCtx, &UdpSockets<I, Self::DeviceId>) -> O>(
        &self,
        cb: F,
    ) -> O;

    /// Calls the function with a mutable reference to UDP sockets.
    fn with_sockets_mut<
        O,
        F: FnOnce(&mut Self::IpSocketsCtx, &mut UdpSockets<I, Self::DeviceId>) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O;

    /// Returns true if UDP may send a port unreachable ICMP error message.
    fn should_send_port_unreachable(&self) -> bool;
}

/// An execution context for the UDP protocol when a buffer is provided.
///
/// `BufferUdpContext` is like [`UdpContext`], except that it also requires that
/// the context be capable of receiving frames in buffers of type `B`. This is
/// used when a buffer of type `B` is provided to UDP (in particular, in
/// [`send_udp_conn`] and [`send_udp_listener`]), and allows any generated
/// link-layer frames to reuse that buffer rather than needing to always
/// allocate a new one.
pub trait BufferUdpContext<I: IpExt, B: BufferMut>: UdpContext<I> {
    /// Receives a UDP packet from a connection socket.
    fn receive_udp_from_conn(
        &mut self,
        _conn: UdpConnId<I>,
        _src_ip: I::Addr,
        _src_port: NonZeroU16,
        _body: &B,
    ) {
        log_unimplemented!((), "BufferUdpContext::receive_udp_from_conn: not implemented");
    }

    /// Receives a UDP packet from a listener socket.
    fn receive_udp_from_listen(
        &mut self,
        _listener: UdpListenerId<I>,
        _src_ip: I::Addr,
        _dst_ip: I::Addr,
        _src_port: Option<NonZeroU16>,
        _body: &B,
    ) {
        log_unimplemented!((), "BufferUdpContext::receive_udp_from_listen: not implemented");
    }
}

/// The non-synchronized context for UDP with a buffer.
pub trait BufferUdpStateNonSyncContext<I: IpExt, B: BufferMut>:
    UdpStateNonSyncContext<I> + BufferUdpContext<I, B>
{
}
impl<I: IpExt, B: BufferMut, C: UdpStateNonSyncContext<I> + BufferUdpContext<I, B>>
    BufferUdpStateNonSyncContext<I, B> for C
{
}

/// An execution context for the UDP protocol when a buffer is provided which
/// also provides access to state.
pub(crate) trait BufferUdpStateContext<
    I: IpExt,
    C: BufferUdpStateNonSyncContext<I, B>,
    B: BufferMut,
>: BufferTransportIpContext<I, C, B> + UdpStateContext<I, C>
{
}

impl<
        I: IpExt,
        B: BufferMut,
        C: BufferUdpStateNonSyncContext<I, B>,
        SC: BufferTransportIpContext<I, C, B> + UdpStateContext<I, C>,
    > BufferUdpStateContext<I, C, B> for SC
{
}

/// An implementation of [`IpTransportContext`] for UDP.
pub(crate) enum UdpIpTransportContext {}

impl<I: IpExt, C: UdpStateNonSyncContext<I>, SC: UdpStateContext<I, C>> IpTransportContext<I, C, SC>
    for UdpIpTransportContext
{
    fn receive_icmp_error(
        sync_ctx: &mut SC,
        ctx: &mut C,
        device: &SC::DeviceId,
        src_ip: Option<SpecifiedAddr<I::Addr>>,
        dst_ip: SpecifiedAddr<I::Addr>,
        mut udp_packet: &[u8],
        err: I::ErrorCode,
    ) {
        ctx.increment_counter("UdpIpTransportContext::receive_icmp_error");
        trace!("UdpIpTransportContext::receive_icmp_error({:?})", err);

        let udp_packet =
            match udp_packet.parse_with::<_, UdpPacketRaw<_>>(IpVersionMarker::<I>::default()) {
                Ok(packet) => packet,
                Err(e) => {
                    let _: ParseError = e;
                    // TODO(joshlf): Do something with this error.
                    return;
                }
            };
        if let (Some(src_ip), Some(src_port), Some(dst_port)) =
            (src_ip, udp_packet.src_port(), udp_packet.dst_port())
        {
            sync_ctx.with_sockets(|_sync_ctx, state| {
        let UdpSockets {sockets: DatagramSockets{bound, unbound: _, }, lazy_port_alloc: _} = state;
                let receiver = bound
                    .lookup(src_ip, dst_ip, src_port, dst_port, device.clone())
                    .next();

                if let Some(id) = receiver {
                    let id = match id {
                        LookupResult::Listener(id, _) => id.into(),
                        LookupResult::Conn(id, _) => id.into(),
                    };
                    ctx.receive_icmp_error(id, err);
                } else {
                    trace!("UdpIpTransportContext::receive_icmp_error: Got ICMP error message for nonexistent UDP socket; either the socket responsible has since been removed, or the error message was sent in error or corrupted");
                }
            });
        } else {
            trace!("UdpIpTransportContext::receive_icmp_error: Got ICMP error message for IP packet with an invalid source or destination IP or port");
        }
    }
}

impl<
        I: IpExt,
        B: BufferMut,
        C: BufferUdpStateNonSyncContext<I, B>,
        SC: BufferUdpStateContext<I, C, B>,
    > BufferIpTransportContext<I, C, SC, B> for UdpIpTransportContext
{
    fn receive_ip_packet(
        sync_ctx: &mut SC,
        ctx: &mut C,
        device: &SC::DeviceId,
        src_ip: I::RecvSrcAddr,
        dst_ip: SpecifiedAddr<I::Addr>,
        mut buffer: B,
    ) -> Result<(), (B, TransportReceiveError)> {
        trace!("received UDP packet: {:x?}", buffer.as_mut());
        let src_ip = src_ip.into();

        let send_port_unreachable = sync_ctx.should_send_port_unreachable();

        sync_ctx.with_sockets(|_sync_ctx, state| {
            let packet = if let Ok(packet) =
                buffer.parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(src_ip, dst_ip.get()))
            {
                packet
            } else {
                // TODO(joshlf): Do something with ICMP here?
                return Ok(());
            };
            let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
                state;

            let recipients: Vec<LookupResult<_, _>> = SpecifiedAddr::new(src_ip)
                .and_then(|src_ip| {
                    packet.src_port().map(|src_port| {
                        bound.lookup(dst_ip, src_ip, packet.dst_port(), src_port, device.clone())
                    })
                })
                .into_iter()
                .flatten()
                .collect();

            if !recipients.is_empty() {
                let src_port = packet.src_port();
                mem::drop(packet);
                for lookup_result in recipients {
                    match lookup_result {
                        LookupResult::Conn(
                            id,
                            ConnAddr {
                                ip: ConnIpAddr { local: _, remote: (remote_ip, remote_port) },
                                device: _,
                            },
                        ) => ctx.receive_udp_from_conn(id, remote_ip.get(), remote_port, &buffer),
                        LookupResult::Listener(id, _) => {
                            ctx.receive_udp_from_listen(id, src_ip, dst_ip.get(), src_port, &buffer)
                        }
                    }
                }
                Ok(())
            } else if send_port_unreachable {
                // Unfortunately, type inference isn't smart enough for us to just
                // do packet.parse_metadata().
                let meta =
                    ParsablePacket::<_, packet_formats::udp::UdpParseArgs<I::Addr>>::parse_metadata(
                        &packet,
                    );
                core::mem::drop(packet);
                buffer.undo_parse(meta);
                Err((buffer, TransportReceiveError::new_port_unreachable()))
            } else {
                Ok(())
            }
        })
    }
}

/// An error when sending using [`send_udp_conn_to`].
#[derive(Error, Copy, Clone, Debug, Eq, PartialEq)]
pub enum UdpSendError {
    /// An error was encountered while trying to create a temporary UDP
    /// connection socket.
    #[error("could not create a temporary connection socket: {}", _0)]
    CreateSock(IpSockCreationError),
    /// An error was encountered while sending.
    #[error("ip socket error: {}", _0)]
    Send(IpSockSendError),
    /// There was a problem with the remote address relating to its zone.
    #[error("zone error: {}", _0)]
    Zone(ZonedAddressError),
}

pub(crate) trait UdpSocketHandler<I: IpExt, C>: IpDeviceIdContext<I> {
    fn create_udp_unbound(&mut self) -> UdpUnboundId<I>;

    fn remove_udp_unbound(&mut self, ctx: &mut C, id: UdpUnboundId<I>);

    fn connect_udp(
        &mut self,
        ctx: &mut C,
        id: UdpUnboundId<I>,
        remote_ip: ZonedAddr<I::Addr, Self::DeviceId>,
        remote_port: NonZeroU16,
    ) -> Result<UdpConnId<I>, UdpSockCreationError>;

    fn set_unbound_udp_device(
        &mut self,
        ctx: &mut C,
        id: UdpUnboundId<I>,
        device_id: Option<&Self::DeviceId>,
    );

    fn set_listener_udp_device(
        &mut self,
        ctx: &mut C,
        id: UdpListenerId<I>,
        device_id: Option<&Self::DeviceId>,
    ) -> Result<(), LocalAddressError>;

    fn set_conn_udp_device(
        &mut self,
        ctx: &mut C,
        id: UdpConnId<I>,
        device_id: Option<&Self::DeviceId>,
    ) -> Result<(), SocketError>;

    fn get_udp_bound_device(&self, ctx: &C, id: UdpSocketId<I>) -> Option<Self::DeviceId>;

    fn set_udp_posix_reuse_port(&mut self, ctx: &mut C, id: UdpUnboundId<I>, reuse_port: bool);

    fn get_udp_posix_reuse_port(&self, ctx: &C, id: UdpSocketId<I>) -> bool;

    fn set_udp_multicast_membership(
        &mut self,
        ctx: &mut C,
        id: UdpSocketId<I>,
        multicast_group: MulticastAddr<I::Addr>,
        interface: MulticastMembershipInterfaceSelector<I::Addr, Self::DeviceId>,
        want_membership: bool,
    ) -> Result<(), SetMulticastMembershipError>;

    fn set_udp_unicast_hop_limit(
        &mut self,
        ctx: &mut C,
        id: UdpSocketId<I>,
        unicast_hop_limit: Option<NonZeroU8>,
    );

    fn set_udp_multicast_hop_limit(
        &mut self,
        ctx: &mut C,
        id: UdpSocketId<I>,
        multicast_hop_limit: Option<NonZeroU8>,
    );

    fn get_udp_unicast_hop_limit(&self, ctx: &C, id: UdpSocketId<I>) -> NonZeroU8;

    fn get_udp_multicast_hop_limit(&self, ctx: &C, id: UdpSocketId<I>) -> NonZeroU8;

    fn connect_udp_listener(
        &mut self,
        ctx: &mut C,
        id: UdpListenerId<I>,
        remote_ip: ZonedAddr<I::Addr, Self::DeviceId>,
        remote_port: NonZeroU16,
    ) -> Result<UdpConnId<I>, (UdpConnectListenerError, UdpListenerId<I>)>;

    fn disconnect_udp_connected(&mut self, ctx: &mut C, id: UdpConnId<I>) -> UdpListenerId<I>;

    fn reconnect_udp(
        &mut self,
        ctx: &mut C,
        id: UdpConnId<I>,
        remote_ip: ZonedAddr<I::Addr, Self::DeviceId>,
        remote_port: NonZeroU16,
    ) -> Result<UdpConnId<I>, (UdpConnectListenerError, UdpConnId<I>)>;

    fn remove_udp_conn(
        &mut self,
        ctx: &mut C,
        id: UdpConnId<I>,
    ) -> UdpConnInfo<I::Addr, Self::DeviceId>;

    fn get_udp_conn_info(
        &self,
        ctx: &mut C,
        id: UdpConnId<I>,
    ) -> UdpConnInfo<I::Addr, Self::DeviceId>;

    fn listen_udp(
        &mut self,
        ctx: &mut C,
        id: UdpUnboundId<I>,
        addr: Option<ZonedAddr<I::Addr, Self::DeviceId>>,
        port: Option<NonZeroU16>,
    ) -> Result<UdpListenerId<I>, LocalAddressError>;

    fn remove_udp_listener(
        &mut self,
        ctx: &mut C,
        id: UdpListenerId<I>,
    ) -> UdpListenerInfo<I::Addr, Self::DeviceId>;

    fn get_udp_listener_info(
        &self,
        ctx: &mut C,
        id: UdpListenerId<I>,
    ) -> UdpListenerInfo<I::Addr, Self::DeviceId>;
}

impl<I: IpExt, C: UdpStateNonSyncContext<I>, SC: UdpStateContext<I, C>> UdpSocketHandler<I, C>
    for SC
{
    fn create_udp_unbound(&mut self) -> UdpUnboundId<I> {
        datagram::create_unbound(self)
    }

    fn remove_udp_unbound(&mut self, ctx: &mut C, id: UdpUnboundId<I>) {
        datagram::remove_unbound(self, ctx, id)
    }

    fn connect_udp(
        &mut self,
        ctx: &mut C,
        id: UdpUnboundId<I>,
        remote_ip: ZonedAddr<I::Addr, Self::DeviceId>,
        remote_port: NonZeroU16,
    ) -> Result<UdpConnId<I>, UdpSockCreationError> {
        // First remove the unbound socket being promoted.
        let (remote_ip, device, sharing, ip_options) = self
            .with_sockets_mut(
                |_sync_ctx,
                 UdpSockets {
                     sockets: DatagramSockets { bound: _, unbound },
                     lazy_port_alloc: _,
                 }| {
                    let occupied = match unbound.entry(id.into()) {
                        IdMapEntry::Vacant(_) => panic!("unbound socket {:?} not found", id),
                        IdMapEntry::Occupied(o) => o,
                    };
                    let UnboundSocketState { device, sharing: _, ip_options: _ } = occupied.get();

                    let (remote_ip, socket_device) =
                        datagram::resolve_addr_with_device(remote_ip, device.as_ref())?;

                    let UnboundSocketState { device: _, sharing, ip_options } = occupied.remove();
                    Ok((remote_ip, socket_device, sharing, ip_options))
                },
            )
            .map_err(UdpSockCreationError::Zone)?;

        create_udp_conn(
            self,
            ctx,
            None,
            None,
            device.as_ref(),
            remote_ip,
            remote_port,
            sharing,
            ip_options,
            false,
        )
        .map_err(|(e, ip_options)| {
            assert_matches::assert_matches!(
                self.with_sockets_mut(|_sync_ctx, state| {
                    let UdpSockets {
                        sockets: DatagramSockets { unbound, bound: _ },
                        lazy_port_alloc: _,
                    } = state;
                    unbound.insert(id.into(), UnboundSocketState { device, sharing, ip_options })
                }),
                None,
                "just-cleared-entry for {:?} is occupied",
                id
            );
            e
        })
    }

    fn set_unbound_udp_device(
        &mut self,
        ctx: &mut C,
        id: UdpUnboundId<I>,
        device_id: Option<&Self::DeviceId>,
    ) {
        datagram::set_unbound_device(self, ctx, id, device_id)
    }

    fn set_listener_udp_device(
        &mut self,
        ctx: &mut C,
        id: UdpListenerId<I>,
        device_id: Option<&Self::DeviceId>,
    ) -> Result<(), LocalAddressError> {
        datagram::set_listener_device(self, ctx, id, device_id)
    }

    fn set_conn_udp_device(
        &mut self,
        ctx: &mut C,
        id: UdpConnId<I>,
        device_id: Option<&Self::DeviceId>,
    ) -> Result<(), SocketError> {
        datagram::set_connected_device(self, ctx, id, device_id)
    }

    fn get_udp_bound_device(&self, ctx: &C, id: UdpSocketId<I>) -> Option<Self::DeviceId> {
        datagram::get_bound_device(self, ctx, id)
    }

    fn set_udp_posix_reuse_port(&mut self, _ctx: &mut C, id: UdpUnboundId<I>, reuse_port: bool) {
        self.with_sockets_mut(|_sync_ctx, state| {
            let UdpSockets { sockets: DatagramSockets { unbound, bound: _ }, lazy_port_alloc: _ } =
                state;

            let UnboundSocketState { device: _, sharing, ip_options: _ } =
                unbound.get_mut(id.into()).expect("unbound UDP socket not found");
            *sharing = if reuse_port {
                PosixSharingOptions::ReusePort
            } else {
                PosixSharingOptions::Exclusive
            };
        })
    }

    fn get_udp_posix_reuse_port(&self, _ctx: &C, id: UdpSocketId<I>) -> bool {
        self.with_sockets(|_sync_ctx, state| {
            let UdpSockets { sockets: DatagramSockets { bound, unbound }, lazy_port_alloc: _ } =
                state;
            match id {
                UdpSocketId::Unbound(id) => {
                    let UnboundSocketState { device: _, sharing, ip_options: _ } =
                        unbound.get(id.into()).expect("unbound UDP socket not found");
                    sharing
                }
                UdpSocketId::Bound(id) => match id {
                    UdpBoundId::Listening(id) => {
                        let (_, sharing, _): &(ListenerState<_, _>, _, ListenerAddr<_, _, _>) =
                            bound
                                .listeners()
                                .get_by_id(&id)
                                .expect("listener UDP socket not found");
                        sharing
                    }
                    UdpBoundId::Connected(id) => {
                        let (_, sharing, _): &(ConnState<_, _>, _, ConnAddr<_, _, _, _>) =
                            bound.conns().get_by_id(&id).expect("conneted UDP socket not found");
                        sharing
                    }
                },
            }
            .is_reuse_port()
        })
    }

    fn set_udp_multicast_membership(
        &mut self,
        ctx: &mut C,
        id: UdpSocketId<I>,
        multicast_group: MulticastAddr<I::Addr>,
        interface: MulticastMembershipInterfaceSelector<I::Addr, Self::DeviceId>,
        want_membership: bool,
    ) -> Result<(), SetMulticastMembershipError> {
        datagram::set_multicast_membership(
            self,
            ctx,
            id,
            multicast_group,
            interface,
            want_membership,
        )
        .map_err(Into::into)
    }

    fn set_udp_unicast_hop_limit(
        &mut self,
        ctx: &mut C,
        id: UdpSocketId<I>,
        unicast_hop_limit: Option<NonZeroU8>,
    ) {
        crate::socket::datagram::update_ip_hop_limit(
            self,
            ctx,
            id,
            SocketHopLimits::set_unicast(unicast_hop_limit),
        )
    }

    fn set_udp_multicast_hop_limit(
        &mut self,
        ctx: &mut C,
        id: UdpSocketId<I>,
        multicast_hop_limit: Option<NonZeroU8>,
    ) {
        crate::socket::datagram::update_ip_hop_limit(
            self,
            ctx,
            id,
            SocketHopLimits::set_multicast(multicast_hop_limit),
        )
    }

    fn get_udp_unicast_hop_limit(&self, ctx: &C, id: UdpSocketId<I>) -> NonZeroU8 {
        crate::socket::datagram::get_ip_hop_limits(self, ctx, id).unicast
    }

    fn get_udp_multicast_hop_limit(&self, ctx: &C, id: UdpSocketId<I>) -> NonZeroU8 {
        crate::socket::datagram::get_ip_hop_limits(self, ctx, id).multicast
    }

    fn connect_udp_listener(
        &mut self,
        ctx: &mut C,
        id: UdpListenerId<I>,
        remote_ip: ZonedAddr<I::Addr, Self::DeviceId>,
        remote_port: NonZeroU16,
    ) -> Result<UdpConnId<I>, (UdpConnectListenerError, UdpListenerId<I>)> {
        let (ip, remote_ip, device, original_addr, ip_options, sharing, clear_device_on_disconnect) =
            self.with_sockets_mut(|_sync_ctx, state| {
                let UdpSockets {
                    sockets: DatagramSockets { bound, unbound: _ },
                    lazy_port_alloc: _,
                } = state;
                let entry = bound.listeners_mut().entry(&id).expect("Invalid UDP listener ID");
                let (_, _, ListenerAddr { ip, device }): &(
                    ListenerState<_, _>,
                    PosixSharingOptions,
                    _,
                ) = entry.get();

                let (remote_ip, socket_device) =
                    datagram::resolve_addr_with_device(remote_ip, device.as_ref())?;
                let clear_device_on_disconnect = device.is_none() && socket_device.is_some();
                let ip = *ip;
                let (ListenerState { ip_options }, sharing, original_addr) = entry.remove();
                Ok((
                    ip,
                    remote_ip,
                    socket_device,
                    original_addr,
                    ip_options,
                    sharing,
                    clear_device_on_disconnect,
                ))
            })
            .map_err(|e| (UdpConnectListenerError::Zone(e), id))?;

        let ListenerIpAddr { addr: local_ip, identifier: local_port } = ip;

        create_udp_conn(
            self,
            ctx,
            local_ip,
            Some(local_port),
            device.as_ref(),
            remote_ip,
            remote_port,
            sharing,
            ip_options,
            clear_device_on_disconnect,
        )
        .map_err(|(e, ip_options)| {
            let e = match e {
                UdpSockCreationError::CouldNotAllocateLocalPort => {
                    unreachable!("local port is already provided")
                }
                UdpSockCreationError::SockAddrConflict => {
                    unreachable!("the socket was just vacated")
                }
                UdpSockCreationError::Ip(ip) => ip.into(),
                UdpSockCreationError::Zone(e) => UdpConnectListenerError::Zone(e),
            };
            self.with_sockets_mut(|_sync_ctx, state| {
                let UdpSockets {
                    sockets: DatagramSockets { bound, unbound: _ },
                    lazy_port_alloc: _,
                } = state;
                let listener = bound
                    .listeners_mut()
                    .try_insert(original_addr, ListenerState { ip_options }, sharing)
                    .expect("reinserting just-removed listener failed");
                (e, listener)
            })
        })
    }

    fn disconnect_udp_connected(&mut self, ctx: &mut C, id: UdpConnId<I>) -> UdpListenerId<I> {
        datagram::disconnect_connected(self, ctx, id)
    }

    fn reconnect_udp(
        &mut self,
        ctx: &mut C,
        id: UdpConnId<I>,
        remote_ip: ZonedAddr<I::Addr, Self::DeviceId>,
        remote_port: NonZeroU16,
    ) -> Result<UdpConnId<I>, (UdpConnectListenerError, UdpConnId<I>)> {
        let ((local_ip, local_port), remote_ip, device, original_addr, conn_state, sharing) = self
            .with_sockets_mut(|_sync_ctx, state| {
                let UdpSockets {
                    sockets: DatagramSockets { bound, unbound: _ },
                    lazy_port_alloc: _,
                } = state;
                let entry = bound.conns_mut().entry(&id).expect("Invalid UDP conn ID");
                let (_, _, ConnAddr { ip: ConnIpAddr { local, remote: _ }, device }): &(
                    ConnState<_, _>,
                    PosixSharingOptions,
                    _,
                ) = entry.get();

                let (remote_ip, socket_device) =
                    datagram::resolve_addr_with_device(remote_ip, device.as_ref())?;
                let local = *local;
                let (state, sharing, original_addr) = entry.remove();
                Ok((local, remote_ip, socket_device, original_addr, state, sharing))
            })
            .map_err(|e| (UdpConnectListenerError::Zone(e), id))?;
        let ConnState { mut socket, clear_device_on_disconnect } = conn_state;
        let ip_options = socket.take_options();

        create_udp_conn(
            self,
            ctx,
            Some(local_ip),
            Some(local_port),
            device.as_ref(),
            remote_ip,
            remote_port,
            sharing,
            ip_options,
            clear_device_on_disconnect,
        )
        .map_err(|(e, ip_options)| {
            let e = match e {
                UdpSockCreationError::CouldNotAllocateLocalPort => {
                    unreachable!("local port is already provided")
                }
                UdpSockCreationError::SockAddrConflict => {
                    unreachable!("the socket was just vacated")
                }
                UdpSockCreationError::Ip(ip) => ip.into(),
                UdpSockCreationError::Zone(e) => UdpConnectListenerError::Zone(e),
            };
            let _: IpOptions<_, _> = socket.replace_options(ip_options);
            // Restore the original socket if creation of the new socket fails.
            self.with_sockets_mut(|_sync_ctx, state| {
                let UdpSockets {
                    sockets: DatagramSockets { bound, unbound: _ },
                    lazy_port_alloc: _,
                } = state;
                let conn = bound
                    .conns_mut()
                    .try_insert(
                        original_addr,
                        ConnState { socket, clear_device_on_disconnect },
                        sharing,
                    )
                    .unwrap_or_else(|(e, _, _): (_, ConnState<_, _>, PosixSharingOptions)| {
                        unreachable!("reinserting just-removed connected socket failed: {:?}", e)
                    });
                (e, conn)
            })
        })
    }

    fn remove_udp_conn(
        &mut self,
        ctx: &mut C,
        id: UdpConnId<I>,
    ) -> UdpConnInfo<I::Addr, Self::DeviceId> {
        let addr = datagram::remove_conn(self, ctx, id);
        addr.into()
    }

    fn get_udp_conn_info(
        &self,
        _ctx: &mut C,
        id: UdpConnId<I>,
    ) -> UdpConnInfo<I::Addr, Self::DeviceId> {
        self.with_sockets(|_sync_ctx, state| {
            let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
                state;
            let (_state, _sharing, addr) =
                bound.conns().get_by_id(&id).expect("UDP connection not found");
            addr.clone().into()
        })
    }

    fn listen_udp(
        &mut self,
        ctx: &mut C,
        id: UdpUnboundId<I>,
        addr: Option<ZonedAddr<I::Addr, Self::DeviceId>>,
        port: Option<NonZeroU16>,
    ) -> Result<UdpListenerId<I>, LocalAddressError> {
        datagram::listen(self, ctx, id, addr, port)
    }

    fn remove_udp_listener(
        &mut self,
        ctx: &mut C,
        id: UdpListenerId<I>,
    ) -> UdpListenerInfo<I::Addr, Self::DeviceId> {
        datagram::remove_listener(self, ctx, id).into()
    }

    fn get_udp_listener_info(
        &self,
        _ctx: &mut C,
        id: UdpListenerId<I>,
    ) -> UdpListenerInfo<I::Addr, Self::DeviceId> {
        self.with_sockets(|_sync_ctx, state| {
            let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
                state;
            let (_, _sharing, addr): &(ListenerState<_, _>, PosixSharingOptions, _) =
                bound.listeners().get_by_id(&id).expect("UDP listener not found");
            addr.clone().into()
        })
    }
}

pub(crate) trait BufferUdpSocketHandler<I: IpExt, C, B: BufferMut>:
    UdpSocketHandler<I, C>
{
    fn send_udp_conn(
        &mut self,
        ctx: &mut C,
        conn: UdpConnId<I>,
        body: B,
    ) -> Result<(), (B, IpSockSendError)>;

    fn send_udp_conn_to(
        &mut self,
        ctx: &mut C,
        conn: UdpConnId<I>,
        remote_ip: ZonedAddr<I::Addr, Self::DeviceId>,
        remote_port: NonZeroU16,
        body: B,
    ) -> Result<(), (B, UdpSendError)>;

    fn send_udp_listener(
        &mut self,
        ctx: &mut C,
        listener: UdpListenerId<I>,
        remote_ip: SpecifiedAddr<I::Addr>,
        remote_port: NonZeroU16,
        body: B,
    ) -> Result<(), (B, UdpSendListenerError)>;
}

impl<
        I: IpExt,
        B: BufferMut,
        C: BufferUdpStateNonSyncContext<I, B>,
        SC: BufferUdpStateContext<I, C, B>,
    > BufferUdpSocketHandler<I, C, B> for SC
{
    fn send_udp_conn(
        &mut self,
        ctx: &mut C,
        conn: UdpConnId<I>,
        body: B,
    ) -> Result<(), (B, IpSockSendError)> {
        let (sock, local_ip, local_port, remote_ip, remote_port) = self.with_sockets(|_sync_ctx, state| {
            let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
                state;
            let (ConnState { socket, clear_device_on_disconnect: _ }, _sharing, addr) =
                bound.conns().get_by_id(&conn).expect("no such connection");
            let sock = socket.clone();
            let ConnAddr {
                ip: ConnIpAddr { local: (local_ip, local_port), remote: (remote_ip, remote_port) },
                device: _,
            } = *addr;

            (sock, local_ip, local_port, remote_ip, remote_port)
        });

        self.send_ip_packet(
            ctx,
            &sock,
            body.encapsulate(UdpPacketBuilder::new(
                local_ip.get(),
                remote_ip.get(),
                Some(local_port),
                remote_port,
            )),
            None,
        )
        .map_err(|(body, err)| (body.into_inner(), err))
    }

    fn send_udp_conn_to(
        &mut self,
        ctx: &mut C,
        conn: UdpConnId<I>,
        remote_ip: ZonedAddr<I::Addr, Self::DeviceId>,
        remote_port: NonZeroU16,
        body: B,
    ) -> Result<(), (B, UdpSendError)> {
        let ((local_ip, local_port), device, ip_options) = self.with_sockets(|_sync_ctx, state| {
            let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
                state;
            let (
                ConnState { socket, clear_device_on_disconnect: _ },
                _,
                ConnAddr { ip: ConnIpAddr { local, remote: _ }, device },
            ): &(_, PosixSharingOptions, _) =
                bound.conns().get_by_id(&conn).expect("no such connection");

            // This must clone the socket options because we don't yet have access
            // to a context here that will allow creating IP sockets and sending
            // packets.
            (*local, device.clone(), socket.options().clone())
        });

        let (remote_ip, device) =
            match datagram::resolve_addr_with_device(remote_ip, device.as_ref()) {
                Ok(addr) => addr,
                Err(e) => return Err((body, UdpSendError::Zone(e))),
            };

        let sock = match self.new_ip_socket(
            ctx,
            device.as_ref(),
            Some(local_ip),
            remote_ip,
            IpProto::Udp.into(),
            ip_options,
        ) {
            Ok(sock) => sock,
            Err((e, _ip_options)) => return Err((body, UdpSendError::CreateSock(e))),
        };

        self.send_ip_packet(
            ctx,
            &sock,
            body.encapsulate(UdpPacketBuilder::new(
                local_ip.get(),
                remote_ip.get(),
                Some(local_port),
                remote_port,
            )),
            None,
        )
        .map_err(|(body, err)| (body.into_inner(), UdpSendError::Send(err)))
    }

    fn send_udp_listener(
        &mut self,
        ctx: &mut C,
        listener: UdpListenerId<I>,
        remote_ip: SpecifiedAddr<I::Addr>,
        remote_port: NonZeroU16,
        body: B,
    ) -> Result<(), (B, UdpSendListenerError)> {
        // TODO(https://fxbug.dev/92447) If `local_ip` is `None`, and so
        // `new_ip_socket` picks a local IP address for us, it may cause problems
        // when we don't match the bound listener addresses. We should revisit
        // whether that check is actually necessary.
        //
        // Also, if the local IP address is a multicast address this function should
        // probably fail and `send_udp_conn_to` must be used instead.
        let (device, local_ip, local_port, ip_options) = self.with_sockets(|_sync_ctx, state| {
            let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
                state;
            let (
                ListenerState { ip_options },
                _,
                ListenerAddr {
                    ip: ListenerIpAddr { addr: local_ip, identifier: local_port },
                    device,
                },
            ): &(_, PosixSharingOptions, _) =
                bound.listeners().get_by_id(&listener).expect("specified listener not found");

            // This must clone the socket options because we don't yet have access
            // to a context here that will allow creating IP sockets and sending
            // packets.
            (device.clone(), *local_ip, *local_port, ip_options.clone())
        });

        let sock = match self.new_ip_socket(
            ctx,
            device.as_ref(),
            local_ip,
            remote_ip,
            IpProto::Udp.into(),
            ip_options,
        ) {
            Ok(sock) => sock,
            Err((err, _ip_options)) => return Err((body, UdpSendListenerError::CreateSock(err))),
        };

        self.send_ip_packet(
        ctx,
        &sock,
        body.encapsulate(UdpPacketBuilder::new(
            sock.local_ip().get(),
            sock.remote_ip().get(),
            Some(local_port),
            remote_port,
        )),
        None
    )
    .map_err(|(body, err)| (body.into_inner(), match err {
        IpSockSendError::Mtu => UdpSendListenerError::Mtu,
        IpSockSendError::Unroutable(err) => unreachable!("temporary IP socket which was created with `UnroutableBehavior::Close` should still be routable, but got error {:?}", err),
    }))
    }
}

/// Sends a UDP packet on an existing connected socket.
///
/// # Errors
///
/// On error, the original `body` is returned unmodified so that it can be
/// reused by the caller.
///
/// # Panics
///
/// Panics if `conn` is not a valid UDP connection identifier.
pub fn send_udp_conn<I: IpExt, B: BufferMut, C: BufferNonSyncContext<B>>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    conn: UdpConnId<I>,
    body: B,
) -> Result<(), (B, IpSockSendError)> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, body)), conn),
        |(IpInv((sync_ctx, ctx, body)), conn)| {
            BufferUdpSocketHandler::<Ipv4, _, _>::send_udp_conn(sync_ctx, ctx, conn, body)
                .map_err(IpInv)
        },
        |(IpInv((sync_ctx, ctx, body)), conn)| {
            BufferUdpSocketHandler::<Ipv6, _, _>::send_udp_conn(sync_ctx, ctx, conn, body)
                .map_err(IpInv)
        },
    )
    .map_err(|IpInv(a)| a)
}

/// Sends a UDP packet using an existing connected socket but overriding the
/// destination address.
pub fn send_udp_conn_to<I: IpExt, B: BufferMut, C: BufferNonSyncContext<B>>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    conn: UdpConnId<I>,
    remote_ip: ZonedAddr<I::Addr, DeviceId>,
    remote_port: NonZeroU16,
    body: B,
) -> Result<(), (B, UdpSendError)> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, remote_port, body)), conn, remote_ip),
        |(IpInv((sync_ctx, ctx, remote_port, body)), conn, remote_ip)| {
            BufferUdpSocketHandler::<Ipv4, _, _>::send_udp_conn_to(
                sync_ctx,
                ctx,
                conn,
                remote_ip,
                remote_port,
                body,
            )
            .map_err(IpInv)
        },
        |(IpInv((sync_ctx, ctx, remote_port, body)), conn, remote_ip)| {
            BufferUdpSocketHandler::<Ipv6, _, _>::send_udp_conn_to(
                sync_ctx,
                ctx,
                conn,
                remote_ip,
                remote_port,
                body,
            )
            .map_err(IpInv)
        },
    )
    .map_err(|IpInv(e)| e)
}

/// An error encountered while sending a UDP packet on a listener socket.
#[derive(Error, Copy, Clone, Debug, Eq, PartialEq)]
pub enum UdpSendListenerError {
    /// An error was encountered while trying to create a temporary IP socket
    /// to use for the send operation.
    #[error("could not create a temporary connection socket: {}", _0)]
    CreateSock(IpSockCreationError),
    /// A local IP address was specified, but it did not match any of the IP
    /// addresses associated with the listener socket.
    #[error("the provided local IP address is not associated with the socket")]
    LocalIpAddrMismatch,
    /// An MTU was exceeded.
    #[error("the maximum transmission unit (MTU) was exceeded")]
    Mtu,
}

/// Send a UDP packet on an existing listener.
///
/// `send_udp_listener` sends a UDP packet on an existing listener.
///
/// # Panics
///
/// `send_udp_listener` panics if `listener` is not associated with a listener
/// for this IP version.
pub fn send_udp_listener<I: IpExt, B: BufferMut, C: BufferNonSyncContext<B>>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    listener: UdpListenerId<I>,
    remote_ip: SpecifiedAddr<I::Addr>,
    remote_port: NonZeroU16,
    body: B,
) -> Result<(), (B, UdpSendListenerError)> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, remote_port, body)), listener, remote_ip),
        |(IpInv((sync_ctx, ctx, remote_port, body)), listener, remote_ip)| {
            BufferUdpSocketHandler::<Ipv4, _, _>::send_udp_listener(
                sync_ctx,
                ctx,
                listener,
                remote_ip,
                remote_port,
                body,
            )
            .map_err(IpInv)
        },
        |(IpInv((sync_ctx, ctx, remote_port, body)), listener, remote_ip)| {
            BufferUdpSocketHandler::<Ipv6, _, _>::send_udp_listener(
                sync_ctx,
                ctx,
                listener,
                remote_ip,
                remote_port,
                body,
            )
            .map_err(IpInv)
        },
    )
    .map_err(|IpInv(e)| e)
}

impl<I: IpExt, C: UdpStateNonSyncContext<I>, SC: UdpStateContext<I, C>>
    DatagramStateContext<IpPortSpec<I, SC::DeviceId>, C, Udp<I, SC::DeviceId>> for SC
{
    type IpSocketsCtx = SC::IpSocketsCtx;

    fn join_multicast_group(
        &mut self,
        ctx: &mut C,
        device: &SC::DeviceId,
        addr: MulticastAddr<I::Addr>,
    ) {
        UdpStateContext::join_multicast_group(self, ctx, device, addr)
    }

    fn leave_multicast_group(
        &mut self,
        ctx: &mut C,
        device: &SC::DeviceId,
        addr: MulticastAddr<I::Addr>,
    ) {
        UdpStateContext::leave_multicast_group(self, ctx, device, addr)
    }

    fn with_sockets<
        O,
        F: FnOnce(
            &Self::IpSocketsCtx,
            &DatagramSockets<IpPortSpec<I, SC::DeviceId>, Udp<I, SC::DeviceId>>,
        ) -> O,
    >(
        &self,
        cb: F,
    ) -> O {
        self.with_sockets(|sync_ctx, UdpSockets { sockets: state, lazy_port_alloc: _ }| {
            cb(sync_ctx, state)
        })
    }

    fn with_sockets_mut<
        O,
        F: FnOnce(
            &mut Self::IpSocketsCtx,
            &mut DatagramSockets<IpPortSpec<I, SC::DeviceId>, Udp<I, SC::DeviceId>>,
        ) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O {
        self.with_sockets_mut(|sync_ctx, UdpSockets { sockets: state, lazy_port_alloc: _ }| {
            cb(sync_ctx, state)
        })
    }
}

impl<I: IpExt, D: IpDeviceId, C: UdpStateNonSyncContext<I>>
    DatagramStateNonSyncContext<IpPortSpec<I, D>> for C
{
    fn try_alloc_listen_identifier(
        &mut self,
        is_available: impl Fn(NonZeroU16) -> Result<(), InUseError>,
    ) -> Option<NonZeroU16> {
        try_alloc_listen_port::<_, _, D>(self, is_available)
    }
}

/// Creates an unbound UDP socket.
///
/// `create_udp_unbound` creates a new unbound UDP socket and returns an
/// identifier for it. The ID can be used to connect the socket to a remote
/// address or to listen for incoming packets.
pub fn create_udp_unbound<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
) -> UdpUnboundId<I> {
    I::map_ip(
        IpInv(&mut sync_ctx),
        |IpInv(sync_ctx)| UdpSocketHandler::<Ipv4, _>::create_udp_unbound(sync_ctx),
        |IpInv(sync_ctx)| UdpSocketHandler::<Ipv6, _>::create_udp_unbound(sync_ctx),
    )
}

/// Removes a socket that has been created but not bound.
///
/// `remove_udp_unbound` removes state for a socket that has been created
/// but not bound.
///
/// # Panics if `id` is not a valid [`UdpUnboundId`].
pub fn remove_udp_unbound<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpUnboundId<I>,
) {
    I::map_ip(
        (IpInv((&mut sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv4, _>::remove_udp_unbound(sync_ctx, ctx, id)
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv6, _>::remove_udp_unbound(sync_ctx, ctx, id)
        },
    )
}

/// Create a UDP connection.
///
/// `connect_udp` binds `conn` as a connection to the remote address and port.
/// It is also bound to a local address and port, meaning that packets sent on
/// this connection will always come from that address and port. The local
/// address will be chosen based on the route to the remote address, and the
/// local port will be chosen from the available ones.
///
/// # Errors
///
/// `connect_udp` will fail in the following cases:
/// - If both `local_ip` and `local_port` are specified but conflict with an
///   existing connection or listener
/// - If one or both are left unspecified but there is still no way to satisfy
///   the request (e.g., `local_ip` is specified but there are no available
///   local ports for that address)
/// - If there is no route to `remote_ip`
///
/// # Panics
///
/// `connect_udp` panics if `id` is not a valid [`UdpUnboundId`].
pub fn connect_udp<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpUnboundId<I>,
    remote_ip: ZonedAddr<I::Addr, DeviceId>,
    remote_port: NonZeroU16,
) -> Result<UdpConnId<I>, UdpSockCreationError> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, remote_port)), id, remote_ip),
        |(IpInv((sync_ctx, ctx, remote_port)), id, remote_ip)| {
            UdpSocketHandler::<Ipv4, _>::connect_udp(sync_ctx, ctx, id, remote_ip, remote_port)
                .map_err(IpInv)
        },
        |(IpInv((sync_ctx, ctx, remote_port)), id, remote_ip)| {
            UdpSocketHandler::<Ipv6, _>::connect_udp(sync_ctx, ctx, id, remote_ip, remote_port)
                .map_err(IpInv)
        },
    )
    .map_err(|IpInv(a)| a)
}

fn create_udp_conn<I: IpExt, C: UdpStateNonSyncContext<I>, SC: UdpStateContext<I, C>>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    local_ip: Option<SpecifiedAddr<I::Addr>>,
    local_port: Option<NonZeroU16>,
    device: Option<&SC::DeviceId>,
    remote_ip: SpecifiedAddr<I::Addr>,
    remote_port: NonZeroU16,
    sharing: PosixSharingOptions,
    ip_options: IpOptions<I::Addr, SC::DeviceId>,
    clear_device_on_disconnect: bool,
) -> Result<UdpConnId<I>, (UdpSockCreationError, IpOptions<I::Addr, SC::DeviceId>)> {
    let ip_sock = match sync_ctx.new_ip_socket(
        ctx,
        None,
        local_ip,
        remote_ip,
        IpProto::Udp.into(),
        ip_options,
    ) {
        Ok(ip_sock) => ip_sock,
        Err((e, ip_options)) => return Err((e.into(), ip_options)),
    };

    let local_ip = *ip_sock.local_ip();
    let remote_ip = *ip_sock.remote_ip();

    sync_ctx.with_sockets_mut(|_sync_ctx, state| {
        let local_port = if let Some(local_port) = local_port {
            local_port
        } else {
            match state.try_alloc_local_port(
                ctx.rng_mut(),
                ProtocolFlowId::new(local_ip, remote_ip, remote_port),
            ) {
                Some(port) => port,
                None => {
                    return Err((
                        UdpSockCreationError::CouldNotAllocateLocalPort,
                        ip_sock.into_options(),
                    ));
                }
            }
        };

        let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
            state;
        let c = ConnAddr {
            ip: ConnIpAddr { local: (local_ip, local_port), remote: (remote_ip, remote_port) },
            device: device.cloned(),
        };
        bound
            .conns_mut()
            .try_insert(c, ConnState { socket: ip_sock, clear_device_on_disconnect }, sharing)
            .map_err(
                |(_, ConnState { socket, clear_device_on_disconnect: _ }, _): (
                    InsertError,
                    _,
                    PosixSharingOptions,
                )| {
                    (UdpSockCreationError::SockAddrConflict, socket.into_options())
                },
            )
    })
}

/// Sets the device to be bound to for an unbound socket.
///
/// # Panics
///
/// `set_unbound_udp_device` panics if `id` is not a valid [`UdpUnboundId`].
pub fn set_unbound_udp_device<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpUnboundId<I>,
    device_id: Option<&DeviceId>,
) {
    I::map_ip(
        (IpInv((&mut sync_ctx, ctx, device_id)), id),
        |(IpInv((sync_ctx, ctx, device_id)), id)| {
            UdpSocketHandler::<Ipv4, _>::set_unbound_udp_device(sync_ctx, ctx, id, device_id)
        },
        |(IpInv((sync_ctx, ctx, device_id)), id)| {
            UdpSocketHandler::<Ipv6, _>::set_unbound_udp_device(sync_ctx, ctx, id, device_id)
        },
    )
}

/// Sets the device the specified listening socket is bound to.
///
/// Updates the socket state to set the bound-to device if one is provided, or
/// to remove any device binding if not.
///
/// # Panics
///
/// `set_listener_udp_device` panics if `id` is not a valid [`UdpListenerId`].
pub fn set_listener_udp_device<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpListenerId<I>,
    device_id: Option<&DeviceId>,
) -> Result<(), LocalAddressError> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, device_id)), id),
        |(IpInv((sync_ctx, ctx, device_id)), id)| {
            UdpSocketHandler::<Ipv4, _>::set_listener_udp_device(sync_ctx, ctx, id, device_id)
                .map_err(IpInv)
        },
        |(IpInv((sync_ctx, ctx, device_id)), id)| {
            UdpSocketHandler::<Ipv6, _>::set_listener_udp_device(sync_ctx, ctx, id, device_id)
                .map_err(IpInv)
        },
    )
    .map_err(|IpInv(a)| a)
}

/// Sets the device the specified connected socket is bound to.
///
/// Updates the socket state to set the bound-to device if one is provided, or
/// to remove any device binding if not.
///
/// # Panics
///
/// `set_conn_udp_device` panics if `id` is not a valid [`UdpConnId`].
pub fn set_conn_udp_device<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpConnId<I>,
    device_id: Option<&DeviceId>,
) -> Result<(), SocketError> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, device_id)), id),
        |(IpInv((sync_ctx, ctx, device_id)), id)| {
            UdpSocketHandler::<Ipv4, _>::set_conn_udp_device(sync_ctx, ctx, id, device_id)
                .map_err(IpInv)
        },
        |(IpInv((sync_ctx, ctx, device_id)), id)| {
            UdpSocketHandler::<Ipv6, _>::set_conn_udp_device(sync_ctx, ctx, id, device_id)
                .map_err(IpInv)
        },
    )
    .map_err(|IpInv(a)| a)
}

/// Gets the device the specified socket is bound to.
///
/// # Panics
///
/// Panics if `id` is not a valid socket ID.
pub fn get_udp_bound_device<I: IpExt, C: NonSyncContext>(
    sync_ctx: &SyncCtx<C>,
    ctx: &C,
    id: UdpSocketId<I>,
) -> Option<DeviceId> {
    let IpInv(device) = I::map_ip::<_, IpInv<Option<DeviceId>>>(
        (IpInv((&sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            IpInv(UdpSocketHandler::<Ipv4, _>::get_udp_bound_device(sync_ctx, ctx, id))
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            IpInv(UdpSocketHandler::<Ipv6, _>::get_udp_bound_device(sync_ctx, ctx, id))
        },
    );
    device
}

/// Sets the POSIX `SO_REUSEPORT` option for the specified socket.
///
/// # Panics
///
/// `set_udp_posix_reuse_port` panics if `id` is not a valid `UdpUnboundId`.
pub fn set_udp_posix_reuse_port<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpUnboundId<I>,
    reuse_port: bool,
) {
    I::map_ip(
        (IpInv((&mut sync_ctx, ctx, reuse_port)), id),
        |(IpInv((sync_ctx, ctx, reuse_port)), id)| {
            UdpSocketHandler::<Ipv4, _>::set_udp_posix_reuse_port(sync_ctx, ctx, id, reuse_port)
        },
        |(IpInv((sync_ctx, ctx, reuse_port)), id)| {
            UdpSocketHandler::<Ipv6, _>::set_udp_posix_reuse_port(sync_ctx, ctx, id, reuse_port)
        },
    )
}

/// Gets the POSIX `SO_REUSEPORT` option for the specified socket.
///
/// # Panics
///
/// Panics if `id` is not a valid `UdpSocketId`.
pub fn get_udp_posix_reuse_port<I: IpExt, C: NonSyncContext>(
    sync_ctx: &SyncCtx<C>,
    ctx: &C,
    id: UdpSocketId<I>,
) -> bool {
    let IpInv(reuse_port) = I::map_ip::<_, IpInv<bool>>(
        (IpInv((&sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            IpInv(UdpSocketHandler::<Ipv4, _>::get_udp_posix_reuse_port(sync_ctx, ctx, id))
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            IpInv(UdpSocketHandler::<Ipv6, _>::get_udp_posix_reuse_port(sync_ctx, ctx, id))
        },
    );
    reuse_port
}

/// Sets the specified socket's membership status for the given group.
///
/// If `id` is unbound, the membership state will take effect when it is bound.
/// An error is returned if the membership change request is invalid (e.g.
/// leaving a group that was not joined, or joining a group multiple times) or
/// if the device to use to join is unspecified or conflicts with the existing
/// socket state.
pub fn set_udp_multicast_membership<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpSocketId<I>,
    multicast_group: MulticastAddr<I::Addr>,
    interface: MulticastMembershipInterfaceSelector<I::Addr, DeviceId>,
    want_membership: bool,
) -> Result<(), SetMulticastMembershipError> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, want_membership)), id, multicast_group, interface),
        |(IpInv((sync_ctx, ctx, want_membership)), id, multicast_group, interface)| {
            UdpSocketHandler::<Ipv4, _>::set_udp_multicast_membership(
                sync_ctx,
                ctx,
                id,
                multicast_group,
                interface,
                want_membership,
            )
            .map_err(IpInv)
        },
        |(IpInv((sync_ctx, ctx, want_membership)), id, multicast_group, interface)| {
            UdpSocketHandler::<Ipv6, _>::set_udp_multicast_membership(
                sync_ctx,
                ctx,
                id,
                multicast_group,
                interface,
                want_membership,
            )
            .map_err(IpInv)
        },
    )
    .map_err(|IpInv(e)| e)
}

/// Sets the hop limit for packets sent by the socket to a unicast destination.
///
/// Sets the hop limit (IPv6) or TTL (IPv4) for outbound packets going to a
/// unicast address.
pub fn set_udp_unicast_hop_limit<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpSocketId<I>,
    unicast_hop_limit: Option<NonZeroU8>,
) {
    I::map_ip(
        (IpInv((&mut sync_ctx, ctx, unicast_hop_limit)), id),
        |(IpInv((sync_ctx, ctx, unicast_hop_limit)), id)| {
            UdpSocketHandler::<Ipv4, _>::set_udp_unicast_hop_limit(
                sync_ctx,
                ctx,
                id,
                unicast_hop_limit,
            )
        },
        |(IpInv((sync_ctx, ctx, unicast_hop_limit)), id)| {
            UdpSocketHandler::<Ipv6, _>::set_udp_unicast_hop_limit(
                sync_ctx,
                ctx,
                id,
                unicast_hop_limit,
            )
        },
    )
}

/// Sets the hop limit for packets sent by the socket to a multicast destination.
///
/// Sets the hop limit (IPv6) or TTL (IPv4) for outbound packets going to a
/// unicast address.
pub fn set_udp_multicast_hop_limit<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpSocketId<I>,
    multicast_hop_limit: Option<NonZeroU8>,
) {
    I::map_ip(
        (IpInv((&mut sync_ctx, ctx, multicast_hop_limit)), id),
        |(IpInv((sync_ctx, ctx, multicast_hop_limit)), id)| {
            UdpSocketHandler::<Ipv4, _>::set_udp_multicast_hop_limit(
                sync_ctx,
                ctx,
                id,
                multicast_hop_limit,
            )
        },
        |(IpInv((sync_ctx, ctx, multicast_hop_limit)), id)| {
            UdpSocketHandler::<Ipv6, _>::set_udp_multicast_hop_limit(
                sync_ctx,
                ctx,
                id,
                multicast_hop_limit,
            )
        },
    )
}

/// Gets the hop limit for packets sent by the socket to a unicast destination.
pub fn get_udp_unicast_hop_limit<I: IpExt, C: NonSyncContext>(
    sync_ctx: &SyncCtx<C>,
    ctx: &C,
    id: UdpSocketId<I>,
) -> NonZeroU8 {
    let IpInv(hop_limit) = I::map_ip::<_, IpInv<NonZeroU8>>(
        (IpInv((&sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            IpInv(UdpSocketHandler::<Ipv4, _>::get_udp_unicast_hop_limit(sync_ctx, ctx, id))
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            IpInv(UdpSocketHandler::<Ipv6, _>::get_udp_unicast_hop_limit(sync_ctx, ctx, id))
        },
    );
    hop_limit
}

/// Sets the hop limit for packets sent by the socket to a multicast destination.
///
/// Sets the hop limit (IPv6) or TTL (IPv4) for outbound packets going to a
/// unicast address.
pub fn get_udp_multicast_hop_limit<I: IpExt, C: NonSyncContext>(
    sync_ctx: &SyncCtx<C>,
    ctx: &C,
    id: UdpSocketId<I>,
) -> NonZeroU8 {
    let IpInv(hop_limit) = I::map_ip::<_, IpInv<NonZeroU8>>(
        (IpInv((&sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            IpInv(UdpSocketHandler::<Ipv4, _>::get_udp_multicast_hop_limit(sync_ctx, ctx, id))
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            IpInv(UdpSocketHandler::<Ipv6, _>::get_udp_multicast_hop_limit(sync_ctx, ctx, id))
        },
    );
    hop_limit
}

/// Error returned when [`connect_udp_listener`] fails.
#[derive(Debug, Error, Eq, PartialEq)]
pub enum UdpConnectListenerError {
    /// An error was encountered creating an IP socket.
    #[error("{}", _0)]
    Ip(#[from] IpSockCreationError),
    /// There was a problem with the provided address relating to its zone.
    #[error("{}", _0)]
    Zone(#[from] ZonedAddressError),
}

/// Connects an already existing UDP socket to a remote destination.
///
/// Replaces a previously created UDP socket indexed by the [`UdpListenerId`]
/// `id`.  It returns the new connected socket ID on success. On failure, an
/// error status is returned, along with a replacement `UdpListenerId` that
/// should be used in place of `id` for future operations.
///
/// # Panics
///
/// `connect_udp_listener` panics if `id` is not a valid `UdpListenerId`.
pub fn connect_udp_listener<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpListenerId<I>,
    remote_ip: ZonedAddr<I::Addr, DeviceId>,
    remote_port: NonZeroU16,
) -> Result<UdpConnId<I>, (UdpConnectListenerError, UdpListenerId<I>)> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, remote_port)), id, remote_ip),
        |(IpInv((sync_ctx, ctx, remote_port)), id, remote_ip)| {
            UdpSocketHandler::<Ipv4, _>::connect_udp_listener(
                sync_ctx,
                ctx,
                id,
                remote_ip,
                remote_port,
            )
            .map_err(|(a, b)| (IpInv(a), b))
        },
        |(IpInv((sync_ctx, ctx, remote_port)), id, remote_ip)| {
            UdpSocketHandler::<Ipv6, _>::connect_udp_listener(
                sync_ctx,
                ctx,
                id,
                remote_ip,
                remote_port,
            )
            .map_err(|(a, b)| (IpInv(a), b))
        },
    )
    .map_err(|(IpInv(a), b)| (a, b))
}

/// Disconnects a connected UDP socket.
///
/// `disconnect_udp_connected` removes an existing connected socket and replaces
/// it with a listening socket bound to the same local address and port.
///
/// # Panics
///
/// Panics if `id` is not a valid `UdpConnId`.
pub fn disconnect_udp_connected<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpConnId<I>,
) -> UdpListenerId<I> {
    I::map_ip(
        (IpInv((&mut sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv4, _>::disconnect_udp_connected(sync_ctx, ctx, id)
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv6, _>::disconnect_udp_connected(sync_ctx, ctx, id)
        },
    )
}

/// Disconnects an already existing UDP socket and connects it to a new remote
/// destination.
///
/// # Panics
///
/// `reconnect_udp` panics if `id` is not a valid `UdpConnId`.
pub fn reconnect_udp<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpConnId<I>,
    remote_ip: ZonedAddr<I::Addr, DeviceId>,
    remote_port: NonZeroU16,
) -> Result<UdpConnId<I>, (UdpConnectListenerError, UdpConnId<I>)> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, remote_port)), id, remote_ip),
        |(IpInv((sync_ctx, ctx, remote_port)), id, remote_ip)| {
            UdpSocketHandler::<Ipv4, _>::reconnect_udp(sync_ctx, ctx, id, remote_ip, remote_port)
                .map_err(|(a, b)| (IpInv(a), b))
        },
        |(IpInv((sync_ctx, ctx, remote_port)), id, remote_ip)| {
            UdpSocketHandler::<Ipv6, _>::reconnect_udp(sync_ctx, ctx, id, remote_ip, remote_port)
                .map_err(|(a, b)| (IpInv(a), b))
        },
    )
    .map_err(|(IpInv(a), b)| (a, b))
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
pub fn remove_udp_conn<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpConnId<I>,
) -> UdpConnInfo<I::Addr, DeviceId> {
    I::map_ip(
        (IpInv((&mut sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv4, _>::remove_udp_conn(sync_ctx, ctx, id)
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv6, _>::remove_udp_conn(sync_ctx, ctx, id)
        },
    )
}

/// Gets the [`UdpConnInfo`] associated with the UDP connection referenced by [`id`].
///
/// # Panics
///
/// `get_udp_conn_info` panics if `id` is not a valid `UdpConnId`.
pub fn get_udp_conn_info<I: IpExt, C: NonSyncContext>(
    sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpConnId<I>,
) -> UdpConnInfo<I::Addr, DeviceId> {
    I::map_ip(
        (IpInv((&sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv4, _>::get_udp_conn_info(sync_ctx, ctx, id)
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv6, _>::get_udp_conn_info(sync_ctx, ctx, id)
        },
    )
}

/// Use an existing socket to listen for incoming UDP packets.
///
/// `listen_udp` converts `id` into a listening socket and registers `listener`
/// as a listener for incoming UDP packets on the given `port`. If `addr` is
/// `None`, the listener is a "wildcard listener", and is bound to all local
/// addresses. See the `transport` module documentation for more details.
///
/// If `addr` is `Some``, and `addr` is already bound on the given port (either
/// by a listener or a connection), `listen_udp` will fail. If `addr` is `None`,
/// and a wildcard listener is already bound to the given port, `listen_udp`
/// will fail.
///
/// # Panics
///
/// `listen_udp` panics if `listener` is already in use, or if `id` is not a
/// valid [`UdpUnboundId`].
pub fn listen_udp<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpUnboundId<I>,
    addr: Option<ZonedAddr<I::Addr, DeviceId>>,
    port: Option<NonZeroU16>,
) -> Result<UdpListenerId<I>, LocalAddressError> {
    I::map_ip::<_, Result<_, _>>(
        (IpInv((&mut sync_ctx, ctx, port)), id, addr),
        |(IpInv((sync_ctx, ctx, port)), id, addr)| {
            UdpSocketHandler::<Ipv4, _>::listen_udp(sync_ctx, ctx, id, addr, port).map_err(IpInv)
        },
        |(IpInv((sync_ctx, ctx, port)), id, addr)| {
            UdpSocketHandler::<Ipv6, _>::listen_udp(sync_ctx, ctx, id, addr, port).map_err(IpInv)
        },
    )
    .map_err(|IpInv(a)| a)
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
pub fn remove_udp_listener<I: IpExt, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpListenerId<I>,
) -> UdpListenerInfo<I::Addr, DeviceId> {
    I::map_ip(
        (IpInv((&mut sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv4, _>::remove_udp_listener(sync_ctx, ctx, id)
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv6, _>::remove_udp_listener(sync_ctx, ctx, id)
        },
    )
}

/// Gets the [`UdpListenerInfo`] associated with the UDP listener referenced by
/// [`id`].
///
/// # Panics
///
/// `get_udp_conn_info` panics if `id` is not a valid `UdpListenerId`.
pub fn get_udp_listener_info<I: IpExt, C: NonSyncContext>(
    sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UdpListenerId<I>,
) -> UdpListenerInfo<I::Addr, DeviceId> {
    I::map_ip(
        (IpInv((&sync_ctx, ctx)), id),
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv4, _>::get_udp_listener_info(sync_ctx, ctx, id)
        },
        |(IpInv((sync_ctx, ctx)), id)| {
            UdpSocketHandler::<Ipv6, _>::get_udp_listener_info(sync_ctx, ctx, id)
        },
    )
}

/// An error when attempting to create a UDP socket.
#[derive(Error, Copy, Clone, Debug, Eq, PartialEq)]
pub enum UdpSockCreationError {
    /// An error was encountered creating an IP socket.
    #[error("{}", _0)]
    Ip(#[from] IpSockCreationError),
    /// No local port was specified, and none could be automatically allocated.
    #[error("a local port could not be allocated")]
    CouldNotAllocateLocalPort,
    /// The specified socket addresses (IP addresses and ports) conflict with an
    /// existing UDP socket.
    #[error("the socket's IP address and port conflict with an existing socket")]
    SockAddrConflict,
    /// There was a problem with the provided address relating to its zone.
    #[error("{}", _0)]
    Zone(#[from] ZonedAddressError),
}

#[cfg(test)]
mod tests {
    use alloc::{
        borrow::ToOwned,
        collections::{hash_map::Entry as HashMapEntry, HashMap, HashSet},
        vec,
        vec::Vec,
    };
    use core::convert::TryInto as _;

    use assert_matches::assert_matches;
    use ip_test_macro::ip_test;
    use net_declare::net_ip_v6;
    use net_types::{
        ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, Ipv6SourceAddr},
        AddrAndZone, LinkLocalAddr, MulticastAddr, Scope as _, ScopeableAddress as _,
    };
    use nonzero_ext::nonzero;
    use packet::{Buf, InnerPacketBuilder, ParsablePacket, Serializer};
    use packet_formats::{
        icmp::{Icmpv4DestUnreachableCode, Icmpv6DestUnreachableCode},
        ip::IpPacketBuilder,
        ipv4::{Ipv4Header, Ipv4PacketRaw},
        ipv6::{Ipv6Header, Ipv6PacketRaw},
    };
    use test_case::test_case;

    use super::*;
    use crate::{
        context::testutil::DummyFrameCtx,
        error::RemoteAddressError,
        ip::{
            device::state::IpDeviceStateIpExt,
            icmp::{Icmpv4ErrorCode, Icmpv6ErrorCode},
            socket::{
                testutil::{DummyDeviceConfig, DummyIpSocketCtx},
                BufferIpSocketHandler, IpSockRouteError, IpSockUnroutableError, IpSocketHandler,
            },
            testutil::{DummyDeviceId, MultipleDevicesId},
            HopLimits, IpDeviceIdContext, SendIpPacketMeta, TransportIpContext, DEFAULT_HOP_LIMITS,
        },
        socket::datagram::MulticastInterfaceSelector,
        testutil::{assert_empty, set_logger_for_test},
    };

    /// The listener data sent through a [`DummyUdpCtx`].
    #[derive(Debug, PartialEq)]
    struct ListenData<I: Ip> {
        listener: UdpListenerId<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: Vec<u8>,
    }

    /// The UDP connection data sent through a [`DummyUdpCtx`].
    #[derive(Debug, PartialEq)]
    struct ConnData<I: Ip> {
        conn: UdpConnId<I>,
        body: Vec<u8>,
    }

    /// An ICMP error delivered to a [`DummyUdpCtx`].
    #[derive(Debug, Eq, PartialEq)]
    struct IcmpError<I: TestIpExt> {
        id: UdpBoundId<I>,
        err: I::ErrorCode,
    }

    struct DummyUdpCtx<I: TestIpExt, D: IpDeviceId> {
        sockets: UdpSockets<I, D>,
        ip_socket_ctx: DummyIpSocketCtx<I, D>,
        ip_options: HashMap<(D, MulticastAddr<I::Addr>), NonZeroUsize>,
    }

    impl<I: TestIpExt> Default for DummyUdpCtx<I, DummyDeviceId>
    where
        DummyUdpCtx<I, DummyDeviceId>: DummyUdpCtxExt<I>,
    {
        fn default() -> Self {
            DummyUdpCtx::with_local_remote_ip_addrs(vec![local_ip::<I>()], vec![remote_ip::<I>()])
        }
    }

    impl<I: TestIpExt, D: IpDeviceId> DummyUdpCtx<I, D> {
        fn with_ip_socket_ctx(ip_socket_ctx: DummyIpSocketCtx<I, D>) -> Self {
            DummyUdpCtx {
                sockets: Default::default(),
                ip_socket_ctx,
                ip_options: Default::default(),
            }
        }
    }

    trait DummyUdpCtxExt<I: Ip> {
        fn with_local_remote_ip_addrs(
            local_ips: Vec<SpecifiedAddr<I::Addr>>,
            remote_ips: Vec<SpecifiedAddr<I::Addr>>,
        ) -> Self;
    }

    trait DummyIpSocketCtxExt<I: Ip + TestIpExt, D> {
        fn new(devices: impl IntoIterator<Item = DummyDeviceConfig<D, I::Addr>>) -> Self;
    }

    trait DummyIpSocketCtxDummyExt<I: Ip + TestIpExt>: DummyIpSocketCtxExt<I, DummyDeviceId> {
        fn new_dummy(
            local_ips: Vec<SpecifiedAddr<I::Addr>>,
            remote_ips: Vec<SpecifiedAddr<I::Addr>>,
        ) -> Self;
    }

    impl<I: Ip + TestIpExt, C: DummyIpSocketCtxExt<I, DummyDeviceId>> DummyIpSocketCtxDummyExt<I>
        for C
    {
        fn new_dummy(
            local_ips: Vec<SpecifiedAddr<<I as Ip>::Addr>>,
            remote_ips: Vec<SpecifiedAddr<<I as Ip>::Addr>>,
        ) -> Self {
            Self::new([DummyDeviceConfig { device: DummyDeviceId, local_ips, remote_ips }])
        }
    }

    impl<D: IpDeviceId> DummyIpSocketCtxExt<Ipv4, D> for DummyIpSocketCtx<Ipv4, D> {
        fn new(
            devices: impl IntoIterator<Item = DummyDeviceConfig<D, Ipv4Addr>>,
        ) -> DummyIpSocketCtx<Ipv4, D> {
            Self::new_ipv4(devices)
        }
    }

    impl<D: IpDeviceId> DummyIpSocketCtxExt<Ipv6, D> for DummyIpSocketCtx<Ipv6, D> {
        fn new(
            devices: impl IntoIterator<Item = DummyDeviceConfig<D, Ipv6Addr>>,
        ) -> DummyIpSocketCtx<Ipv6, D> {
            Self::new_ipv6(devices)
        }
    }

    impl<I: Ip + TestIpExt> DummyUdpCtxExt<I> for DummyUdpCtx<I, DummyDeviceId>
    where
        DummyIpSocketCtx<I, DummyDeviceId>: DummyIpSocketCtxExt<I, DummyDeviceId>,
    {
        fn with_local_remote_ip_addrs(
            local_ips: Vec<SpecifiedAddr<I::Addr>>,
            remote_ips: Vec<SpecifiedAddr<I::Addr>>,
        ) -> Self {
            DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new_dummy(local_ips, remote_ips))
        }
    }

    impl<I: TestIpExt, D: IpDeviceId> AsRef<DummyIpSocketCtx<I, D>> for DummyUdpCtx<I, D> {
        fn as_ref(&self) -> &DummyIpSocketCtx<I, D> {
            &self.ip_socket_ctx
        }
    }

    impl<I: TestIpExt, D: IpDeviceId> AsMut<DummyIpSocketCtx<I, D>> for DummyUdpCtx<I, D> {
        fn as_mut(&mut self) -> &mut DummyIpSocketCtx<I, D> {
            &mut self.ip_socket_ctx
        }
    }

    type DummyDeviceCtx<I, D> = crate::context::testutil::DummyCtx<
        DummyUdpCtx<I, D>,
        (),
        SendIpPacketMeta<I, D, SpecifiedAddr<<I as Ip>::Addr>>,
        (),
        D,
        DummyDeviceNonSyncCtxState<I>,
    >;

    type DummyDeviceSyncCtx<I, D> = crate::context::testutil::DummySyncCtx<
        DummyUdpCtx<I, D>,
        SendIpPacketMeta<I, D, SpecifiedAddr<<I as Ip>::Addr>>,
        D,
    >;

    type DummyDeviceNonSyncCtx<I> =
        crate::context::testutil::DummyNonSyncCtx<(), (), DummyDeviceNonSyncCtxState<I>>;

    #[derive(Default)]
    struct DummyDeviceNonSyncCtxState<I: TestIpExt> {
        listen_data: Vec<ListenData<I>>,
        conn_data: Vec<ConnData<I>>,
        icmp_errors: Vec<IcmpError<I>>,
    }

    impl<I: TestIpExt> DummyDeviceNonSyncCtxState<I> {
        fn listen_data(&self) -> HashMap<UdpListenerId<I>, Vec<&'_ [u8]>> {
            self.listen_data.iter().fold(
                HashMap::new(),
                |mut map, ListenData { listener, body, src_ip: _, dst_ip: _, src_port: _ }| {
                    map.entry(*listener).or_default().push(&body);
                    map
                },
            )
        }
    }

    trait DummySyncCtxBound<I: TestIpExt>: DummyDeviceSyncCtxBound<I, DummyDeviceId> {}
    impl<I: TestIpExt, C: DummyDeviceSyncCtxBound<I, DummyDeviceId>> DummySyncCtxBound<I> for C {}

    impl<I: TestIpExt, D: IpDeviceId> TransportIpContext<I, DummyDeviceNonSyncCtx<I>>
        for DummyDeviceSyncCtx<I, D>
    where
        Self: IpDeviceIdContext<I, DeviceId = D> + IpSocketHandler<I, DummyDeviceNonSyncCtx<I>>,
    {
        fn get_device_with_assigned_addr(&self, addr: SpecifiedAddr<<I as Ip>::Addr>) -> Option<D> {
            let ip = &self.get_ref();
            ip.ip_socket_ctx.find_device_with_addr(addr)
        }

        fn get_default_hop_limits(&self, device: Option<&Self::DeviceId>) -> HopLimits {
            let ip = &self.get_ref();
            let mut limits = DEFAULT_HOP_LIMITS;
            if let Some(device) = device {
                limits.unicast = ip.ip_socket_ctx.get_device_state(device).default_hop_limit;
            }
            limits
        }
    }

    impl<I: TestIpExt> UdpContext<I> for DummyDeviceNonSyncCtx<I> {
        fn receive_icmp_error(&mut self, id: UdpBoundId<I>, err: I::ErrorCode) {
            self.state_mut().icmp_errors.push(IcmpError { id, err })
        }
    }

    impl<I: TestIpExt, D: IpDeviceId + 'static> UdpStateContext<I, DummyDeviceNonSyncCtx<I>>
        for DummyDeviceSyncCtx<I, D>
    {
        type IpSocketsCtx = DummyIpSocketCtx<I, D>;

        fn join_multicast_group(
            &mut self,
            _ctx: &mut DummyDeviceNonSyncCtx<I>,
            device: &Self::DeviceId,
            addr: MulticastAddr<<I>::Addr>,
        ) {
            match self.get_mut().ip_options.entry((device.clone(), addr)) {
                HashMapEntry::Vacant(v) => {
                    let _: &mut NonZeroUsize = v.insert(nonzero!(1usize));
                }
                HashMapEntry::Occupied(mut o) => {
                    let count = o.get_mut();
                    *count = NonZeroUsize::new(count.get() + 1).unwrap();
                }
            }
        }

        fn leave_multicast_group(
            &mut self,
            _ctx: &mut DummyDeviceNonSyncCtx<I>,
            device: &Self::DeviceId,
            addr: MulticastAddr<<I>::Addr>,
        ) {
            match self.get_mut().ip_options.entry((device.clone(), addr)) {
                HashMapEntry::Vacant(_) => {
                    panic!("not a member of group {:?} on {:?}", addr, device)
                }
                HashMapEntry::Occupied(mut o) => {
                    let count = o.get_mut();
                    match NonZeroUsize::new(count.get() - 1) {
                        Some(c) => *count = c,
                        None => {
                            let _: NonZeroUsize = o.remove();
                        }
                    }
                }
            }
        }

        fn with_sockets<O, F: FnOnce(&Self::IpSocketsCtx, &UdpSockets<I, Self::DeviceId>) -> O>(
            &self,
            cb: F,
        ) -> O {
            let DummyUdpCtx { sockets, ip_socket_ctx, ip_options: _ } = self.get_ref();
            cb(ip_socket_ctx, sockets)
        }

        fn with_sockets_mut<
            O,
            F: FnOnce(&mut Self::IpSocketsCtx, &mut UdpSockets<I, Self::DeviceId>) -> O,
        >(
            &mut self,
            cb: F,
        ) -> O {
            let DummyUdpCtx { sockets, ip_socket_ctx, ip_options: _ } = self.get_mut();
            cb(ip_socket_ctx, sockets)
        }

        fn should_send_port_unreachable(&self) -> bool {
            false
        }
    }

    impl<I: TestIpExt, B: BufferMut> BufferUdpContext<I, B> for DummyDeviceNonSyncCtx<I> {
        fn receive_udp_from_conn(
            &mut self,
            conn: UdpConnId<I>,
            _src_ip: <I as Ip>::Addr,
            _src_port: NonZeroU16,
            body: &B,
        ) {
            self.state_mut().conn_data.push(ConnData { conn, body: body.as_ref().to_owned() })
        }

        fn receive_udp_from_listen(
            &mut self,
            listener: UdpListenerId<I>,
            src_ip: <I as Ip>::Addr,
            dst_ip: <I as Ip>::Addr,
            src_port: Option<NonZeroU16>,
            body: &B,
        ) {
            self.state_mut().listen_data.push(ListenData {
                listener,
                src_ip,
                dst_ip,
                src_port,
                body: body.as_ref().to_owned(),
            })
        }
    }

    type DummyCtx<I> = DummyDeviceCtx<I, DummyDeviceId>;
    type DummySyncCtx<I> = DummyDeviceSyncCtx<I, DummyDeviceId>;
    type DummyNonSyncCtx<I> = DummyDeviceNonSyncCtx<I>;

    /// The trait bounds required of `DummySyncCtx<I>` in tests.
    trait DummyDeviceSyncCtxBound<I: TestIpExt, D: IpDeviceId>:
        Default + BufferIpSocketHandler<I, DummyDeviceNonSyncCtx<I>, Buf<Vec<u8>>, DeviceId = D>
    {
    }
    impl<I: TestIpExt, D: IpDeviceId> DummyDeviceSyncCtxBound<I, D> for DummyDeviceSyncCtx<I, D> where
        DummyDeviceSyncCtx<I, D>: Default
            + BufferIpSocketHandler<I, DummyDeviceNonSyncCtx<I>, Buf<Vec<u8>>, DeviceId = D>
    {
    }

    fn local_ip<I: TestIpExt>() -> SpecifiedAddr<I::Addr> {
        I::get_other_ip_address(1)
    }

    fn remote_ip<I: TestIpExt>() -> SpecifiedAddr<I::Addr> {
        I::get_other_ip_address(2)
    }

    trait TestIpExt: crate::testutil::TestIpExt + IpExt + IpDeviceStateIpExt {
        const SOME_MULTICAST_ADDR: MulticastAddr<Self::Addr>;
        fn try_into_recv_src_addr(addr: Self::Addr) -> Option<Self::RecvSrcAddr>;
    }

    impl TestIpExt for Ipv4 {
        const SOME_MULTICAST_ADDR: MulticastAddr<Ipv4Addr> = Ipv4::ALL_SYSTEMS_MULTICAST_ADDRESS;
        fn try_into_recv_src_addr(addr: Ipv4Addr) -> Option<Ipv4Addr> {
            Some(addr)
        }
    }

    impl TestIpExt for Ipv6 {
        const SOME_MULTICAST_ADDR: MulticastAddr<Ipv6Addr> =
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS;
        fn try_into_recv_src_addr(addr: Ipv6Addr) -> Option<Ipv6SourceAddr> {
            Ipv6SourceAddr::new(addr)
        }
    }

    /// Helper function to inject an UDP packet with the provided parameters.
    fn receive_udp_packet<I: TestIpExt, D: IpDeviceId + 'static>(
        sync_ctx: &mut DummyDeviceSyncCtx<I, D>,
        ctx: &mut DummyDeviceNonSyncCtx<I>,
        device: D,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        src_port: NonZeroU16,
        dst_port: NonZeroU16,
        body: &[u8],
    ) where
        DummyDeviceSyncCtx<I, D>: DummyDeviceSyncCtxBound<I, D>,
    {
        let builder = UdpPacketBuilder::new(src_ip, dst_ip, Some(src_port), dst_port);
        let buffer = Buf::new(body.to_owned(), ..)
            .encapsulate(builder)
            .serialize_vec_outer()
            .unwrap()
            .into_inner();
        UdpIpTransportContext::receive_ip_packet(
            sync_ctx,
            ctx,
            &device,
            I::try_into_recv_src_addr(src_ip).unwrap(),
            SpecifiedAddr::new(dst_ip).unwrap(),
            buffer,
        )
        .expect("Receive IP packet succeeds");
    }

    const LOCAL_PORT: NonZeroU16 = nonzero!(100u16);
    const REMOTE_PORT: NonZeroU16 = nonzero!(200u16);

    fn conn_addr<I>(device: Option<DummyDeviceId>) -> AddrVec<IpPortSpec<I, DummyDeviceId>>
    where
        I: Ip + TestIpExt,
    {
        let local_ip = local_ip::<I>();
        let remote_ip = remote_ip::<I>();
        ConnAddr {
            ip: ConnIpAddr { local: (local_ip, LOCAL_PORT), remote: (remote_ip, REMOTE_PORT) },
            device,
        }
        .into()
    }

    fn local_listener<I>(device: Option<DummyDeviceId>) -> AddrVec<IpPortSpec<I, DummyDeviceId>>
    where
        I: Ip + TestIpExt,
    {
        let local_ip = local_ip::<I>();
        ListenerAddr { ip: ListenerIpAddr { identifier: LOCAL_PORT, addr: Some(local_ip) }, device }
            .into()
    }

    fn wildcard_listener<I>(device: Option<DummyDeviceId>) -> AddrVec<IpPortSpec<I, DummyDeviceId>>
    where
        I: Ip + TestIpExt,
    {
        ListenerAddr { ip: ListenerIpAddr { identifier: LOCAL_PORT, addr: None }, device }.into()
    }

    #[ip_test]
    #[test_case(conn_addr(Some(DummyDeviceId)), [
            conn_addr(None), local_listener(Some(DummyDeviceId)), local_listener(None),
            wildcard_listener(Some(DummyDeviceId)), wildcard_listener(None)
        ]; "conn with device")]
    #[test_case(local_listener(Some(DummyDeviceId)),
        [local_listener(None), wildcard_listener(Some(DummyDeviceId)), wildcard_listener(None)];
        "local listener with device")]
    #[test_case(wildcard_listener(Some(DummyDeviceId)), [wildcard_listener(None)];
        "wildcard listener with device")]
    #[test_case(conn_addr(None), [local_listener(None), wildcard_listener(None)]; "conn no device")]
    #[test_case(local_listener(None), [wildcard_listener(None)]; "local listener no device")]
    #[test_case(wildcard_listener(None), []; "wildcard listener no device")]
    fn test_udp_addr_vec_iter_shadows_conn<I: Ip + IpExt, D: IpDeviceId, const N: usize>(
        addr: AddrVec<IpPortSpec<I, D>>,
        expected_shadows: [AddrVec<IpPortSpec<I, D>>; N],
    ) {
        assert_eq!(addr.iter_shadows().collect::<HashSet<_>>(), HashSet::from(expected_shadows));
    }

    #[ip_test]
    fn test_iter_receiving_addrs<I: Ip + TestIpExt>() {
        let addr = ConnIpAddr {
            local: (local_ip::<I>(), LOCAL_PORT),
            remote: (remote_ip::<I>(), REMOTE_PORT),
        };
        assert_eq!(
            iter_receiving_addrs::<I, _>(addr, DummyDeviceId).collect::<Vec<_>>(),
            vec![
                // A socket connected on exactly the receiving vector has precedence.
                conn_addr(Some(DummyDeviceId)),
                // Connected takes precedence over listening with device match.
                conn_addr(None),
                local_listener(Some(DummyDeviceId)),
                // Specific IP takes precedence over device match.
                local_listener(None),
                wildcard_listener(Some(DummyDeviceId)),
                // Fallback to least specific
                wildcard_listener(None)
            ]
        );
    }

    /// Tests UDP listeners over different IP versions.
    ///
    /// Tests that a listener can be created, that the context receives packet
    /// notifications for that listener, and that we can send data using that
    /// listener.
    #[ip_test]
    fn test_listen_udp<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = local_ip::<I>();
        let remote_ip = remote_ip::<I>();
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        // Create a listener on local port 100, bound to the local IP:
        let listener = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            NonZeroU16::new(100),
        )
        .expect("listen_udp failed");

        // Inject a packet and check that the context receives it:
        let body = [1, 2, 3, 4, 5];
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip.get(),
            local_ip.get(),
            NonZeroU16::new(200).unwrap(),
            NonZeroU16::new(100).unwrap(),
            &body[..],
        );

        let listen_data = &non_sync_ctx.state().listen_data;
        assert_eq!(listen_data.len(), 1);
        let pkt = &listen_data[0];
        assert_eq!(pkt.listener, listener);
        assert_eq!(pkt.src_ip, remote_ip.get());
        assert_eq!(pkt.dst_ip, local_ip.get());
        assert_eq!(pkt.src_port.unwrap().get(), 200);
        assert_eq!(pkt.body, &body[..]);

        // Send a packet providing a local ip:
        BufferUdpSocketHandler::send_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            remote_ip,
            NonZeroU16::new(200).unwrap(),
            Buf::new(body.to_vec(), ..),
        )
        .expect("send_udp_listener failed");
        // And send a packet that doesn't:
        BufferUdpSocketHandler::send_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            remote_ip,
            NonZeroU16::new(200).unwrap(),
            Buf::new(body.to_vec(), ..),
        )
        .expect("send_udp_listener failed");
        let frames = sync_ctx.frames();
        assert_eq!(frames.len(), 2);
        let check_frame = |(meta, frame_body): &(
            SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<I::Addr>>,
            Vec<u8>,
        )| {
            let SendIpPacketMeta { device: _, src_ip, dst_ip, next_hop, proto, ttl: _, mtu: _ } =
                meta;
            assert_eq!(next_hop, &remote_ip);
            assert_eq!(src_ip, &local_ip);
            assert_eq!(dst_ip, &remote_ip);
            assert_eq!(proto, &IpProto::Udp.into());
            let mut buf = &frame_body[..];
            let udp_packet =
                UdpPacket::parse(&mut buf, UdpParseArgs::new(src_ip.get(), dst_ip.get()))
                    .expect("Parsed sent UDP packet");
            assert_eq!(udp_packet.src_port().unwrap().get(), 100);
            assert_eq!(udp_packet.dst_port().get(), 200);
            assert_eq!(udp_packet.body(), &body[..]);
        };
        check_frame(&frames[0]);
        check_frame(&frames[1]);
    }

    /// Tests that UDP packets without a connection are dropped.
    ///
    /// Tests that receiving a UDP packet on a port over which there isn't a
    /// listener causes the packet to be dropped correctly.
    #[ip_test]
    fn test_udp_drop<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = local_ip::<I>();
        let remote_ip = remote_ip::<I>();

        let body = [1, 2, 3, 4, 5];
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip.get(),
            local_ip.get(),
            NonZeroU16::new(200).unwrap(),
            NonZeroU16::new(100).unwrap(),
            &body[..],
        );
        assert_empty(non_sync_ctx.state().listen_data.iter());
        assert_empty(non_sync_ctx.state().conn_data.iter());
    }

    /// Tests that UDP connections can be created and data can be transmitted
    /// over it.
    ///
    /// Only tests with specified local port and address bounds.
    #[ip_test]
    fn test_udp_conn_basic<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = local_ip::<I>();
        let remote_ip = remote_ip::<I>();
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        // Create a UDP connection with a specified local port and local IP.
        let listener = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            Some(NonZeroU16::new(100).unwrap()),
        )
        .expect("listen_udp failed");
        let conn = UdpSocketHandler::<I, _>::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            ZonedAddr::Unzoned(remote_ip),
            NonZeroU16::new(200).unwrap(),
        )
        .expect("connect_udp_listener failed");

        // Inject a UDP packet and see if we receive it on the context.
        let body = [1, 2, 3, 4, 5];
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip.get(),
            local_ip.get(),
            NonZeroU16::new(200).unwrap(),
            NonZeroU16::new(100).unwrap(),
            &body[..],
        );

        let conn_data = &non_sync_ctx.state().conn_data;
        assert_eq!(conn_data.len(), 1);
        let pkt = &conn_data[0];
        assert_eq!(pkt.conn, conn);
        assert_eq!(pkt.body, &body[..]);

        // Now try to send something over this new connection.
        BufferUdpSocketHandler::send_udp_conn(
            &mut sync_ctx,
            &mut non_sync_ctx,
            conn,
            Buf::new(body.to_vec(), ..),
        )
        .expect("send_udp_conn returned an error");

        let frames = sync_ctx.frames();
        assert_eq!(frames.len(), 1);

        // Check first frame.
        let (
            SendIpPacketMeta { device: _, src_ip, dst_ip, next_hop, proto, ttl: _, mtu: _ },
            frame_body,
        ) = &frames[0];
        assert_eq!(next_hop, &remote_ip);
        assert_eq!(src_ip, &local_ip);
        assert_eq!(dst_ip, &remote_ip);
        assert_eq!(proto, &IpProto::Udp.into());
        let mut buf = &frame_body[..];
        let udp_packet = UdpPacket::parse(&mut buf, UdpParseArgs::new(src_ip.get(), dst_ip.get()))
            .expect("Parsed sent UDP packet");
        assert_eq!(udp_packet.src_port().unwrap().get(), 100);
        assert_eq!(udp_packet.dst_port().get(), 200);
        assert_eq!(udp_packet.body(), &body[..]);
    }

    /// Tests that UDP connections fail with an appropriate error for
    /// non-routable remote addresses.
    #[ip_test]
    fn test_udp_conn_unroutable<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        // Set dummy context callback to treat all addresses as unroutable.
        let _local_ip = local_ip::<I>();
        let remote_ip = I::get_other_ip_address(127);
        // Create a UDP connection with a specified local port and local IP.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let conn_err = UdpSocketHandler::<I, _>::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(remote_ip),
            NonZeroU16::new(200).unwrap(),
        )
        .unwrap_err();

        assert_eq!(
            conn_err,
            UdpSockCreationError::Ip(
                IpSockRouteError::Unroutable(IpSockUnroutableError::NoRouteToRemoteAddr).into()
            )
        );
    }

    /// Tests that UDP listener creation fails with an appropriate error when
    /// local address is non-local.
    #[ip_test]
    fn test_udp_conn_cannot_bind<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());

        // Use remote address to trigger IpSockCreationError::LocalAddrNotAssigned.
        let remote_ip = remote_ip::<I>();
        // Create a UDP listener with a specified local port and local ip:
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let result = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(remote_ip)),
            NonZeroU16::new(200),
        );

        assert_eq!(result, Err(LocalAddressError::CannotBindToAddress));
    }

    /// Tests that UDP connections fail with an appropriate error when local
    /// ports are exhausted.
    #[ip_test]
    fn test_udp_conn_exhausted<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());

        let local_ip = local_ip::<I>();
        // Exhaust local ports to trigger FailedToAllocateLocalPort error.
        for port_num in UdpBoundSocketMap::<I, DummyDeviceId>::EPHEMERAL_RANGE {
            let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
            let _: UdpListenerId<_> = UdpSocketHandler::listen_udp(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                Some(ZonedAddr::Unzoned(local_ip)),
                NonZeroU16::new(port_num),
            )
            .unwrap();
        }

        let remote_ip = remote_ip::<I>();
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let conn_err = UdpSocketHandler::<I, _>::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(remote_ip),
            NonZeroU16::new(100).unwrap(),
        )
        .unwrap_err();

        assert_eq!(conn_err, UdpSockCreationError::CouldNotAllocateLocalPort);
    }

    #[ip_test]
    fn test_connect_udp_listener_success<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());

        let local_ip = local_ip::<I>();
        let remote_ip = remote_ip::<I>();
        let local_port = nonzero!(100u16);
        let multicast_addr = I::get_multicast_addr(3);
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);

        // Set some properties on the socket that should be preserved.
        UdpSocketHandler::set_udp_posix_reuse_port(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            true,
        );
        UdpSocketHandler::set_udp_multicast_membership(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            multicast_addr,
            MulticastInterfaceSelector::LocalAddress(local_ip).into(),
            true,
        )
        .expect("join multicast group should succeed");

        let socket = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            Some(local_port),
        )
        .expect("Initial call to listen_udp was expected to succeed");

        let conn = UdpSocketHandler::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            socket,
            ZonedAddr::Unzoned(remote_ip),
            nonzero!(200u16),
        )
        .expect("connect should succeed");

        // Check that socket options set on the listener are propagated to the
        // connected socket.
        assert!(UdpSocketHandler::get_udp_posix_reuse_port(
            &sync_ctx,
            &mut non_sync_ctx,
            conn.into()
        ));
        assert_eq!(
            sync_ctx.get_ref().ip_options,
            HashMap::from([((DummyDeviceId, multicast_addr), nonzero!(1usize))])
        );
        assert_eq!(
            UdpSocketHandler::set_udp_multicast_membership(
                &mut sync_ctx,
                &mut non_sync_ctx,
                conn.into(),
                multicast_addr,
                MulticastInterfaceSelector::LocalAddress(local_ip).into(),
                true
            ),
            Err(SetMulticastMembershipError::NoMembershipChange)
        );
    }

    #[ip_test]
    fn test_connect_udp_listener_fails<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = local_ip::<I>();
        let remote_ip = I::get_other_ip_address(127);
        let multicast_addr = I::get_multicast_addr(3);
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);

        // Set some properties on the socket that should be preserved.
        UdpSocketHandler::set_udp_posix_reuse_port(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            true,
        );
        UdpSocketHandler::set_udp_multicast_membership(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            multicast_addr,
            MulticastInterfaceSelector::LocalAddress(local_ip).into(),
            true,
        )
        .expect("join multicast group should succeed");

        // Create a UDP connection with a specified local port and local IP.
        let listener = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            Some(nonzero!(100u16)),
        )
        .expect("Initial call to listen_udp was expected to succeed");

        let listener = assert_matches!(
            UdpSocketHandler::connect_udp_listener(
                &mut sync_ctx,
                &mut non_sync_ctx,
                listener,
                ZonedAddr::Unzoned(remote_ip),
                nonzero!(1234u16)
            ),
            Err((
                UdpConnectListenerError::Ip(
                IpSockCreationError::Route(IpSockRouteError::Unroutable(
                    IpSockUnroutableError::NoRouteToRemoteAddr
                ))),
                listener
            )) => listener
        );

        // The listener that was returned is not necessarily the same one that
        // was passed in to `connect_udp_listener`. Check that socket options
        // that were set on it before attempting to connect were preserved.
        assert!(UdpSocketHandler::get_udp_posix_reuse_port(
            &sync_ctx,
            &mut non_sync_ctx,
            listener.into()
        ));
        assert_eq!(
            sync_ctx.get_ref().ip_options,
            HashMap::from([((DummyDeviceId, multicast_addr), nonzero!(1usize))])
        );
        assert_eq!(
            UdpSocketHandler::set_udp_multicast_membership(
                &mut sync_ctx,
                &mut non_sync_ctx,
                listener.into(),
                multicast_addr,
                MulticastInterfaceSelector::LocalAddress(local_ip).into(),
                true
            ),
            Err(SetMulticastMembershipError::NoMembershipChange)
        );
    }

    #[ip_test]
    fn test_reconnect_udp_conn_success<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
        DummyUdpCtx<I, DummyDeviceId>: DummyUdpCtxExt<I>,
    {
        set_logger_for_test();

        let local_ip = local_ip::<I>();
        let remote_ip = remote_ip::<I>();
        let other_remote_ip = I::get_other_ip_address(3);

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } = DummyCtx::with_sync_ctx(
            DummySyncCtx::<I>::with_state(DummyUdpCtx::with_local_remote_ip_addrs(
                vec![local_ip],
                vec![remote_ip, other_remote_ip],
            )),
        );

        let local_port = NonZeroU16::new(100).unwrap();
        let local_ip = ZonedAddr::Unzoned(local_ip);
        let remote_ip = ZonedAddr::Unzoned(remote_ip);
        let other_remote_ip = ZonedAddr::Unzoned(other_remote_ip);

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let bound = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(local_ip),
            Some(local_port),
        )
        .expect("listen should succeed");

        let socket = UdpSocketHandler::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            bound,
            remote_ip,
            NonZeroU16::new(200).unwrap(),
        )
        .expect("connect was expected to succeed");
        let other_remote_port = NonZeroU16::new(300).unwrap();
        let socket = UdpSocketHandler::reconnect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            socket,
            other_remote_ip,
            other_remote_port,
        )
        .expect("reconnect_udp should succeed");
        assert_eq!(
            UdpSocketHandler::get_udp_conn_info(&sync_ctx, &mut non_sync_ctx, socket),
            UdpConnInfo {
                local_ip,
                local_port,
                remote_ip: other_remote_ip,
                remote_port: other_remote_port
            }
        );
    }

    #[ip_test]
    fn test_reconnect_udp_conn_fails<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = ZonedAddr::Unzoned(local_ip::<I>());
        let remote_ip = ZonedAddr::Unzoned(remote_ip::<I>());
        let other_remote_ip = ZonedAddr::Unzoned(I::get_other_ip_address(3));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let bound = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(local_ip),
            Some(LOCAL_PORT),
        )
        .expect("listen should succeed");

        let socket = UdpSocketHandler::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            bound,
            remote_ip,
            REMOTE_PORT,
        )
        .expect("connect was expected to succeed");
        let other_remote_port = NonZeroU16::new(300).unwrap();
        let (error, socket) = UdpSocketHandler::reconnect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            socket,
            other_remote_ip,
            other_remote_port,
        )
        .expect_err("reconnect_udp should fail");
        assert_matches!(
            error,
            UdpConnectListenerError::Ip(IpSockCreationError::Route(IpSockRouteError::Unroutable(
                _
            )))
        );

        assert_eq!(
            UdpSocketHandler::get_udp_conn_info(&sync_ctx, &mut non_sync_ctx, socket),
            UdpConnInfo { local_ip, local_port: LOCAL_PORT, remote_ip, remote_port: REMOTE_PORT }
        );
    }

    #[ip_test]
    fn test_send_udp_conn_to<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
        DummyUdpCtx<I, DummyDeviceId>: DummyUdpCtxExt<I>,
    {
        set_logger_for_test();

        let local_ip = local_ip::<I>();
        let remote_ip = remote_ip::<I>();
        let other_remote_ip = I::get_other_ip_address(3);

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } = DummyCtx::with_sync_ctx(
            DummySyncCtx::<I>::with_state(DummyUdpCtx::with_local_remote_ip_addrs(
                vec![local_ip],
                vec![remote_ip, other_remote_ip],
            )),
        );

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listener = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            Some(LOCAL_PORT),
        )
        .expect("listen should succeed");
        let conn = UdpSocketHandler::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            ZonedAddr::Unzoned(remote_ip),
            REMOTE_PORT,
        )
        .expect("connect should succeed");

        let body = [1, 2, 3, 4, 5];
        // Try to send something with send_udp_conn_to
        BufferUdpSocketHandler::send_udp_conn_to(
            &mut sync_ctx,
            &mut non_sync_ctx,
            conn,
            ZonedAddr::Unzoned(other_remote_ip),
            NonZeroU16::new(200).unwrap(),
            Buf::new(body.to_vec(), ..),
        )
        .expect("send_udp_conn_to failed");

        // The socket should not have been affected.
        let info = UdpSocketHandler::get_udp_conn_info(&sync_ctx, &mut non_sync_ctx, conn);
        assert_eq!(info.local_ip, ZonedAddr::Unzoned(local_ip));
        assert_eq!(info.remote_ip, ZonedAddr::Unzoned(remote_ip));
        assert_eq!(info.remote_port, REMOTE_PORT);

        // Check first frame.
        let frames = sync_ctx.frames();
        let (
            SendIpPacketMeta { device: _, src_ip, dst_ip, next_hop, proto, ttl: _, mtu: _ },
            frame_body,
        ) = &frames[0];

        assert_eq!(next_hop, &other_remote_ip);
        assert_eq!(src_ip, &local_ip);
        assert_eq!(dst_ip, &other_remote_ip);
        assert_eq!(proto, &I::Proto::from(IpProto::Udp));
        let mut buf = &frame_body[..];
        let udp_packet = UdpPacket::parse(&mut buf, UdpParseArgs::new(src_ip.get(), dst_ip.get()))
            .expect("Parsed sent UDP packet");
        assert_eq!(udp_packet.src_port().unwrap(), LOCAL_PORT);
        assert_eq!(udp_packet.dst_port(), REMOTE_PORT);
        assert_eq!(udp_packet.body(), &body[..]);
    }

    /// Tests that UDP send failures are propagated as errors.
    ///
    /// Only tests with specified local port and address bounds.
    #[ip_test]
    fn test_send_udp_conn_failure<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let _local_ip = local_ip::<I>();
        let remote_ip = remote_ip::<I>();
        // Create a UDP connection with a specified local port and local IP.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let conn = UdpSocketHandler::<I, _>::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(remote_ip),
            NonZeroU16::new(200).unwrap(),
        )
        .expect("connect_udp failed");

        // Instruct the dummy frame context to throw errors.
        let frames: &mut DummyFrameCtx<SendIpPacketMeta<I, _, _>> = sync_ctx.as_mut();
        frames.set_should_error_for_frame(|_frame_meta| true);

        // Now try to send something over this new connection:
        let (_, send_err) = BufferUdpSocketHandler::send_udp_conn(
            &mut sync_ctx,
            &mut non_sync_ctx,
            conn,
            Buf::new(Vec::new(), ..),
        )
        .unwrap_err();
        assert_eq!(send_err, IpSockSendError::Mtu);
    }

    /// Tests that if we have multiple listeners and connections, demuxing the
    /// flows is performed correctly.
    #[ip_test]
    fn test_udp_demux<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
        DummyUdpCtx<I, DummyDeviceId>: DummyUdpCtxExt<I>,
    {
        set_logger_for_test();
        let local_ip = local_ip::<I>();
        let remote_ip_a = I::get_other_ip_address(70);
        let remote_ip_b = I::get_other_ip_address(72);
        let local_port_a = NonZeroU16::new(100).unwrap();
        let local_port_b = NonZeroU16::new(101).unwrap();
        let local_port_c = NonZeroU16::new(102).unwrap();
        let local_port_d = NonZeroU16::new(103).unwrap();
        let remote_port_a = NonZeroU16::new(200).unwrap();

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } = DummyCtx::with_sync_ctx(
            DummySyncCtx::<I>::with_state(DummyUdpCtx::with_local_remote_ip_addrs(
                vec![local_ip],
                vec![remote_ip_a, remote_ip_b],
            )),
        );

        // Create some UDP connections and listeners:
        // conn2 has just a remote addr different than conn1, which requires
        // allowing them to share the local port.
        let [conn1, conn2] = [remote_ip_a, remote_ip_b].map(|remote_ip| {
            let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
            UdpSocketHandler::set_udp_posix_reuse_port(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound.into(),
                true,
            );
            let listen = UdpSocketHandler::listen_udp(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                Some(ZonedAddr::Unzoned(local_ip)),
                Some(local_port_d),
            )
            .expect("connect_udp_listener failed");
            UdpSocketHandler::<I, _>::connect_udp_listener(
                &mut sync_ctx,
                &mut non_sync_ctx,
                listen,
                ZonedAddr::Unzoned(remote_ip),
                remote_port_a,
            )
            .expect("connect_udp_listener failed")
        });
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let list1 = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            Some(local_port_a),
        )
        .expect("listen_udp failed");
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let list2 = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            Some(local_port_b),
        )
        .expect("listen_udp failed");
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let wildcard_list = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            None,
            Some(local_port_c),
        )
        .expect("listen_udp failed");

        // Now inject UDP packets that each of the created connections should
        // receive.
        let body_conn1 = [1, 1, 1, 1];
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip_a.get(),
            local_ip.get(),
            remote_port_a,
            local_port_d,
            &body_conn1[..],
        );
        let body_conn2 = [2, 2, 2, 2];
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip_b.get(),
            local_ip.get(),
            remote_port_a,
            local_port_d,
            &body_conn2[..],
        );
        let body_list1 = [3, 3, 3, 3];
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip_a.get(),
            local_ip.get(),
            remote_port_a,
            local_port_a,
            &body_list1[..],
        );
        let body_list2 = [4, 4, 4, 4];
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip_a.get(),
            local_ip.get(),
            remote_port_a,
            local_port_b,
            &body_list2[..],
        );
        let body_wildcard_list = [5, 5, 5, 5];
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip_a.get(),
            local_ip.get(),
            remote_port_a,
            local_port_c,
            &body_wildcard_list[..],
        );
        // Check that we got everything in order.
        let conn_packets = &non_sync_ctx.state().conn_data;
        assert_eq!(conn_packets.len(), 2);
        let pkt = &conn_packets[0];
        assert_eq!(pkt.conn, conn1);
        assert_eq!(pkt.body, &body_conn1[..]);
        let pkt = &conn_packets[1];
        assert_eq!(pkt.conn, conn2);
        assert_eq!(pkt.body, &body_conn2[..]);

        let list_packets = &non_sync_ctx.state().listen_data;
        assert_eq!(list_packets.len(), 3);
        let pkt = &list_packets[0];
        assert_eq!(pkt.listener, list1);
        assert_eq!(pkt.src_ip, remote_ip_a.get());
        assert_eq!(pkt.dst_ip, local_ip.get());
        assert_eq!(pkt.src_port.unwrap(), remote_port_a);
        assert_eq!(pkt.body, &body_list1[..]);

        let pkt = &list_packets[1];
        assert_eq!(pkt.listener, list2);
        assert_eq!(pkt.src_ip, remote_ip_a.get());
        assert_eq!(pkt.dst_ip, local_ip.get());
        assert_eq!(pkt.src_port.unwrap(), remote_port_a);
        assert_eq!(pkt.body, &body_list2[..]);

        let pkt = &list_packets[2];
        assert_eq!(pkt.listener, wildcard_list);
        assert_eq!(pkt.src_ip, remote_ip_a.get());
        assert_eq!(pkt.dst_ip, local_ip.get());
        assert_eq!(pkt.src_port.unwrap(), remote_port_a);
        assert_eq!(pkt.body, &body_wildcard_list[..]);
    }

    /// Tests UDP wildcard listeners for different IP versions.
    #[ip_test]
    fn test_wildcard_listeners<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip_a = I::get_other_ip_address(1);
        let local_ip_b = I::get_other_ip_address(2);
        let remote_ip_a = I::get_other_ip_address(70);
        let remote_ip_b = I::get_other_ip_address(72);
        let listener_port = NonZeroU16::new(100).unwrap();
        let remote_port = NonZeroU16::new(200).unwrap();
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listener = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            None,
            Some(listener_port),
        )
        .expect("listen_udp failed");

        let body = [1, 2, 3, 4, 5];
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip_a.get(),
            local_ip_a.get(),
            remote_port,
            listener_port,
            &body[..],
        );
        // Receive into a different local IP.
        receive_udp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            remote_ip_b.get(),
            local_ip_b.get(),
            remote_port,
            listener_port,
            &body[..],
        );

        // Check that we received both packets for the listener.
        let listen_packets = &non_sync_ctx.state().listen_data;
        assert_eq!(listen_packets.len(), 2);
        let pkt = &listen_packets[0];
        assert_eq!(pkt.listener, listener);
        assert_eq!(pkt.src_ip, remote_ip_a.get());
        assert_eq!(pkt.dst_ip, local_ip_a.get());
        assert_eq!(pkt.src_port.unwrap(), remote_port);
        assert_eq!(pkt.body, &body[..]);
        let pkt = &listen_packets[1];
        assert_eq!(pkt.listener, listener);
        assert_eq!(pkt.src_ip, remote_ip_b.get());
        assert_eq!(pkt.dst_ip, local_ip_b.get());
        assert_eq!(pkt.src_port.unwrap(), remote_port);
        assert_eq!(pkt.body, &body[..]);
    }

    #[ip_test]
    fn test_receive_multicast_packet<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
        DummyUdpCtx<I, DummyDeviceId>: DummyUdpCtxExt<I>,
    {
        set_logger_for_test();
        let local_ip = local_ip::<I>();
        let remote_ip = I::get_other_ip_address(70);
        let local_port = NonZeroU16::new(100).unwrap();
        let remote_port = NonZeroU16::new(200).unwrap();
        let multicast_addr = I::get_multicast_addr(0);
        let multicast_addr_other = I::get_multicast_addr(1);

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::with_state(
                DummyUdpCtx::with_local_remote_ip_addrs(vec![local_ip], vec![remote_ip]),
            ));

        // Create 3 sockets: one listener for all IPs, two listeners on the same
        // local address.
        let any_listener = {
            let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
            UdpSocketHandler::set_udp_posix_reuse_port(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                true,
            );
            UdpSocketHandler::<I, _>::listen_udp(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                None,
                Some(local_port),
            )
            .expect("listen_udp failed")
        };

        let specific_listeners = [(); 2].map(|()| {
            let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
            UdpSocketHandler::set_udp_posix_reuse_port(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                true,
            );
            UdpSocketHandler::<I, _>::listen_udp(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                Some(ZonedAddr::Unzoned(multicast_addr.into_specified())),
                Some(local_port),
            )
            .expect("listen_udp failed")
        });

        let mut receive_packet = |body, local_ip: MulticastAddr<I::Addr>| {
            let body = [body];
            receive_udp_packet(
                &mut sync_ctx,
                &mut non_sync_ctx,
                DummyDeviceId,
                remote_ip.get(),
                local_ip.get(),
                remote_port,
                local_port,
                &body,
            )
        };

        // These packets should be received by all listeners.
        receive_packet(1, multicast_addr);
        receive_packet(2, multicast_addr);

        // This packet should be received only by the all-IPs listener.
        receive_packet(3, multicast_addr_other);

        assert_eq!(
            non_sync_ctx.state().listen_data(),
            HashMap::from([
                (specific_listeners[0], vec![[1].as_slice(), &[2]]),
                (specific_listeners[1], vec![&[1], &[2]]),
                (any_listener, vec![&[1], &[2], &[3]]),
            ]),
        );
    }

    type MultiDeviceDummyCtx<I> = DummyDeviceCtx<I, MultipleDevicesId>;
    type MultiDeviceDummySyncCtx<I> = DummyDeviceSyncCtx<I, MultipleDevicesId>;
    type MultiDeviceDummyNonSyncCtx<I> = DummyDeviceNonSyncCtx<I>;

    impl<I: Ip + TestIpExt> Default for DummyUdpCtx<I, MultipleDevicesId>
    where
        DummyIpSocketCtx<I, MultipleDevicesId>: DummyIpSocketCtxExt<I, MultipleDevicesId>,
    {
        fn default() -> Self {
            let remote_ips = vec![I::get_other_remote_ip_address(1)];
            DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new(
                MultipleDevicesId::all().into_iter().enumerate().map(|(i, device)| {
                    DummyDeviceConfig {
                        device,
                        local_ips: vec![I::get_other_ip_address((i + 1).try_into().unwrap())],
                        remote_ips: remote_ips.clone(),
                    }
                }),
            ))
        }
    }

    /// Tests that if sockets are bound to devices, they will only receive
    /// packets that are received on those devices.
    #[ip_test]
    fn test_bound_to_device_receive<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        set_logger_for_test();
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::default());
        let sync_ctx = &mut sync_ctx;
        let bound_first_device = {
            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            let listen = UdpSocketHandler::listen_udp(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                Some(ZonedAddr::Unzoned(local_ip::<I>())),
                Some(LOCAL_PORT),
            )
            .expect("listen should succeed");
            let conn = UdpSocketHandler::connect_udp_listener(
                sync_ctx,
                &mut non_sync_ctx,
                listen,
                ZonedAddr::Unzoned(I::get_other_remote_ip_address(1)),
                REMOTE_PORT,
            )
            .expect("connect should succeed");
            UdpSocketHandler::set_conn_udp_device(
                sync_ctx,
                &mut non_sync_ctx,
                conn.into(),
                Some(&MultipleDevicesId::A),
            )
            .expect("bind should succeed");
            conn
        };

        let bound_second_device = {
            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            UdpSocketHandler::set_unbound_udp_device(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                Some(&MultipleDevicesId::B),
            );
            UdpSocketHandler::listen_udp(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                None,
                Some(LOCAL_PORT),
            )
            .expect("listen should succeed")
        };

        // Inject a packet received on `MultipleDevicesId::A` from the specified
        // remote; this should go to the first socket.
        let body = [1, 2, 3, 4, 5];
        receive_udp_packet(
            sync_ctx,
            &mut non_sync_ctx,
            MultipleDevicesId::A,
            I::get_other_remote_ip_address(1).get(),
            local_ip::<I>().get(),
            REMOTE_PORT,
            LOCAL_PORT,
            &body[..],
        );

        let conn_data = &non_sync_ctx.state().conn_data;
        assert_matches!(&conn_data[..], &[ConnData {conn, body: _ }] if conn == bound_first_device);

        // A second packet received on `MultipleDevicesId::B` will go to the
        // second socket.
        receive_udp_packet(
            sync_ctx,
            &mut non_sync_ctx,
            MultipleDevicesId::B,
            I::get_other_remote_ip_address(1).get(),
            local_ip::<I>().get(),
            REMOTE_PORT,
            LOCAL_PORT,
            &body[..],
        );

        let listen_data = &non_sync_ctx.state().listen_data;
        assert_matches!(&listen_data[..], &[ListenData {
            listener, src_ip: _, dst_ip: _, src_port: _, body: _
        }] if listener== bound_second_device);
    }

    /// Tests that if sockets are bound to devices, they will send packets out
    /// of those devices.
    #[ip_test]
    fn test_bound_to_device_send<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        set_logger_for_test();
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::default());
        let sync_ctx = &mut sync_ctx;
        let bound_on_devices = MultipleDevicesId::all().map(|device| {
            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            UdpSocketHandler::set_unbound_udp_device(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                Some(&device),
            );
            UdpSocketHandler::listen_udp(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                None,
                Some(LOCAL_PORT),
            )
            .expect("listen should succeed")
        });

        // Send a packet from each socket.
        let body = [1, 2, 3, 4, 5];
        for socket in bound_on_devices {
            BufferUdpSocketHandler::send_udp_listener(
                sync_ctx,
                &mut non_sync_ctx,
                socket,
                I::get_other_remote_ip_address(1),
                REMOTE_PORT,
                Buf::new(body.to_vec(), ..),
            )
            .expect("send should succeed");
        }

        let mut received_devices = sync_ctx
            .frames()
            .iter()
            .map(
                |(
                    SendIpPacketMeta {
                        device,
                        src_ip: _,
                        dst_ip,
                        next_hop: _,
                        proto,
                        ttl: _,
                        mtu: _,
                    },
                    _body,
                )| {
                    assert_eq!(proto, &IpProto::Udp.into());
                    assert_eq!(dst_ip, &I::get_other_remote_ip_address(1),);
                    device
                },
            )
            .collect::<Vec<_>>();
        received_devices.sort();
        assert_eq!(received_devices, [&MultipleDevicesId::A, &MultipleDevicesId::B]);
    }

    fn receive_packet_on<I: Ip + TestIpExt>(
        sync_ctx: &mut MultiDeviceDummySyncCtx<I>,
        ctx: &mut MultiDeviceDummyNonSyncCtx<I>,
        device: MultipleDevicesId,
    ) where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        const BODY: [u8; 5] = [1, 2, 3, 4, 5];
        receive_udp_packet(
            sync_ctx,
            ctx,
            device,
            I::get_other_remote_ip_address(1).get(),
            local_ip::<I>().get(),
            REMOTE_PORT,
            LOCAL_PORT,
            &BODY[..],
        )
    }

    /// Check that sockets can be bound to and unbound from devices.
    #[ip_test]
    fn test_bind_unbind_device<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        set_logger_for_test();
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::default());
        let sync_ctx = &mut sync_ctx;

        // Start with `socket` bound to a device on all IPs.
        let socket = {
            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            UdpSocketHandler::set_unbound_udp_device(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                Some(&MultipleDevicesId::A),
            );
            UdpSocketHandler::listen_udp(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                None,
                Some(LOCAL_PORT),
            )
            .expect("listen failed")
        };

        // Since it is bound, it does not receive a packet from another device.
        receive_packet_on(sync_ctx, &mut non_sync_ctx, MultipleDevicesId::B);
        let listen_data = &non_sync_ctx.state().listen_data;
        assert_matches!(&listen_data[..], &[]);

        // When unbound, the socket can receive packets on the other device.
        UdpSocketHandler::set_listener_udp_device(sync_ctx, &mut non_sync_ctx, socket.into(), None)
            .expect("clearing bound device failed");
        receive_packet_on(sync_ctx, &mut non_sync_ctx, MultipleDevicesId::B);
        let listen_data = &non_sync_ctx.state().listen_data;
        assert_matches!(&listen_data[..],
            &[ListenData {listener, body:_, src_ip: _, dst_ip: _, src_port: _ }] =>
            assert_eq!(listener, socket));
    }

    /// Check that bind fails as expected when it would cause illegal shadowing.
    #[ip_test]
    fn test_unbind_device_fails<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        set_logger_for_test();
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::default());
        let sync_ctx = &mut sync_ctx;

        let bound_on_devices = MultipleDevicesId::all().map(|device| {
            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            UdpSocketHandler::set_unbound_udp_device(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                Some(&device),
            );
            UdpSocketHandler::listen_udp(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                None,
                Some(LOCAL_PORT),
            )
            .expect("listen should succeed")
        });

        // Clearing the bound device is not allowed for either socket since it
        // would then be shadowed by the other socket.
        for socket in bound_on_devices {
            assert_matches!(
                UdpSocketHandler::set_listener_udp_device(
                    sync_ctx,
                    &mut non_sync_ctx,
                    socket.into(),
                    None
                ),
                Err(LocalAddressError::AddressInUse)
            );
        }
    }

    /// Check that binding a device fails if it would make a connected socket
    /// unroutable.
    #[ip_test]
    fn test_bind_conn_socket_device_fails<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
        DummyIpSocketCtx<I, MultipleDevicesId>: DummyIpSocketCtxExt<I, MultipleDevicesId>,
    {
        set_logger_for_test();
        let device_configs = HashMap::from(
            [(MultipleDevicesId::A, 1), (MultipleDevicesId::B, 2)].map(|(device, i)| {
                (
                    device,
                    DummyDeviceConfig {
                        device,
                        local_ips: vec![I::get_other_ip_address(i)],
                        remote_ips: vec![I::get_other_remote_ip_address(i)],
                    },
                )
            }),
        );
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::with_state(
                DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new(
                    device_configs.iter().map(|(_, v)| v).cloned(),
                )),
            ));

        let socket = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let socket = UdpSocketHandler::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            socket,
            ZonedAddr::Unzoned(device_configs[&MultipleDevicesId::A].remote_ips[0]),
            LOCAL_PORT,
        )
        .expect("connect should succeed");

        // `socket` is not explicitly bound to device `A` but its route must
        // go through it because of the destination address. Therefore binding
        // to device `B` wil not work.
        assert_matches!(
            UdpSocketHandler::set_conn_udp_device(
                &mut sync_ctx,
                &mut non_sync_ctx,
                socket,
                Some(&MultipleDevicesId::B)
            ),
            Err(SocketError::Remote(RemoteAddressError::NoRoute))
        );

        // Binding to device `A` should be fine.
        UdpSocketHandler::set_conn_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            socket,
            Some(&MultipleDevicesId::A),
        )
        .expect("routing picked A already");
    }

    #[ip_test]
    fn test_bound_device_receive_multicast_packet<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        set_logger_for_test();
        let remote_ip = I::get_other_ip_address(1);
        let local_port = NonZeroU16::new(100).unwrap();
        let remote_port = NonZeroU16::new(200).unwrap();
        let multicast_addr = I::get_multicast_addr(0);

        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::default());

        // Create 3 sockets: one listener bound on each device and one not bound
        // to a device.

        let sync_ctx = &mut sync_ctx;
        let bound_on_devices = MultipleDevicesId::all().map(|device| {
            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            UdpSocketHandler::set_unbound_udp_device(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                Some(&device),
            );
            UdpSocketHandler::set_udp_posix_reuse_port(sync_ctx, &mut non_sync_ctx, unbound, true);
            let listener = UdpSocketHandler::listen_udp(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                None,
                Some(LOCAL_PORT),
            )
            .expect("listen should succeed");

            (device, listener)
        });

        let listener = {
            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            UdpSocketHandler::set_udp_posix_reuse_port(sync_ctx, &mut non_sync_ctx, unbound, true);
            UdpSocketHandler::listen_udp(
                sync_ctx,
                &mut non_sync_ctx,
                unbound,
                None,
                Some(LOCAL_PORT),
            )
            .expect("listen should succeed")
        };

        fn index_for_device(id: MultipleDevicesId) -> u8 {
            match id {
                MultipleDevicesId::A => 0,
                MultipleDevicesId::B => 1,
            }
        }

        let mut receive_packet = |remote_ip: SpecifiedAddr<I::Addr>, device: MultipleDevicesId| {
            let body = vec![index_for_device(device)];
            receive_udp_packet(
                sync_ctx,
                &mut non_sync_ctx,
                device,
                remote_ip.get(),
                multicast_addr.get(),
                remote_port,
                local_port,
                &body,
            )
        };

        // Receive packets from the remote IP on each device (2 packets total).
        // Listeners bound on devices should receive one, and the other listener
        // should receive both.
        for device in MultipleDevicesId::all() {
            receive_packet(remote_ip, device);
        }

        let listen_data = non_sync_ctx.state().listen_data();

        for (device, listener) in bound_on_devices {
            assert_eq!(listen_data[&listener], vec![&[index_for_device(device)]]);
        }
        let expected_listener_data: &[&[u8]] =
            &[&[index_for_device(MultipleDevicesId::A)], &[index_for_device(MultipleDevicesId::B)]];
        assert_eq!(&listen_data[&listener], expected_listener_data);
    }

    /// Tests establishing a UDP connection without providing a local IP
    #[ip_test]
    fn test_conn_unspecified_local_ip<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_port = NonZeroU16::new(100).unwrap();
        let remote_port = NonZeroU16::new(200).unwrap();
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listener = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            None,
            Some(local_port),
        )
        .expect("listen_udp failed");
        let conn = UdpSocketHandler::<I, _>::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            ZonedAddr::Unzoned(remote_ip::<I>()),
            remote_port,
        )
        .expect("connect_udp_listener failed");
        let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
            &sync_ctx.get_ref().sockets;
        let (_state, _tag_state, addr) = bound.conns().get_by_id(&conn).unwrap();

        assert_eq!(
            addr,
            &ConnAddr {
                ip: ConnIpAddr {
                    local: (local_ip::<I>(), local_port),
                    remote: (remote_ip::<I>(), remote_port),
                },
                device: None
            }
        );
    }

    /// Tests local port allocation for [`connect_udp`].
    ///
    /// Tests that calling [`connect_udp`] causes a valid local port to be
    /// allocated.
    #[ip_test]
    fn test_udp_local_port_alloc<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
        DummyUdpCtx<I, DummyDeviceId>: DummyUdpCtxExt<I>,
    {
        let local_ip = local_ip::<I>();
        let ip_a = I::get_other_ip_address(100);
        let ip_b = I::get_other_ip_address(200);

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::with_state(
                DummyUdpCtx::with_local_remote_ip_addrs(vec![local_ip], vec![ip_a, ip_b]),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let conn_a = UdpSocketHandler::<I, _>::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(ip_a),
            NonZeroU16::new(1010).unwrap(),
        )
        .expect("connect_udp failed");
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let conn_b = UdpSocketHandler::<I, _>::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(ip_b),
            NonZeroU16::new(1010).unwrap(),
        )
        .expect("connect_udp failed");
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let conn_c = UdpSocketHandler::<I, _>::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(ip_a),
            NonZeroU16::new(2020).unwrap(),
        )
        .expect("connect_udp failed");
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let conn_d = UdpSocketHandler::<I, _>::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(ip_a),
            NonZeroU16::new(1010).unwrap(),
        )
        .expect("connect_udp failed");
        let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
            &sync_ctx.get_ref().sockets;
        let valid_range = &UdpBoundSocketMap::<I, DummyDeviceId>::EPHEMERAL_RANGE;
        let port_a = assert_matches!(bound.conns().get_by_id(&conn_a),
            Some((_state, _tag_state, ConnAddr{ip: ConnIpAddr{local: (_, local_identifier), ..}, device: _})) => local_identifier)
        .get();
        assert!(valid_range.contains(&port_a));
        let port_b = assert_matches!(bound.conns().get_by_id(&conn_b),
            Some((_state, _tag_state, ConnAddr{ip: ConnIpAddr{local: (_, local_identifier), ..}, device: _})) => local_identifier)
        .get();
        assert_ne!(port_a, port_b);
        let port_c = assert_matches!(bound.conns().get_by_id(&conn_c),
            Some((_state, _tag_state, ConnAddr{ip: ConnIpAddr{local: (_, local_identifier), ..}, device: _})) => local_identifier)
        .get();
        assert_ne!(port_a, port_c);
        let port_d = assert_matches!(bound.conns().get_by_id(&conn_d),
            Some((_state, _tag_state, ConnAddr{ip: ConnIpAddr{local: (_, local_identifier), ..}, device: _})) => local_identifier)
        .get();
        assert_ne!(port_a, port_d);
    }

    /// Tests that if `listen_udp` fails, it can be retried later.
    #[ip_test]
    fn test_udp_retry_listen_after_removing_conflict<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        set_logger_for_test();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());

        fn listen_unbound<I: Ip + TestIpExt, C: UdpStateNonSyncContext<I>>(
            sync_ctx: &mut impl UdpStateContext<I, C>,
            non_sync_ctx: &mut C,
            unbound: UdpUnboundId<I>,
        ) -> Result<UdpListenerId<I>, LocalAddressError> {
            UdpSocketHandler::<I, _>::listen_udp(
                sync_ctx,
                non_sync_ctx,
                unbound,
                Some(ZonedAddr::Unzoned(local_ip::<I>())),
                Some(NonZeroU16::new(100).unwrap()),
            )
        }

        // Tie up the address so the second call to `connect_udp` fails.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listener = listen_unbound(&mut sync_ctx, &mut non_sync_ctx, unbound)
            .expect("Initial call to listen_udp was expected to succeed");

        // Trying to connect on the same address should fail.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        assert_eq!(
            listen_unbound(&mut sync_ctx, &mut non_sync_ctx, unbound),
            Err(LocalAddressError::AddressInUse)
        );

        // Once the first listener is removed, the second socket can be
        // connected.
        let _: UdpListenerInfo<_, _> =
            UdpSocketHandler::remove_udp_listener(&mut sync_ctx, &mut non_sync_ctx, listener);

        let _: UdpListenerId<_> = listen_unbound(&mut sync_ctx, &mut non_sync_ctx, unbound)
            .expect("listen should succeed");
    }

    /// Tests local port allocation for [`listen_udp`].
    ///
    /// Tests that calling [`listen_udp`] causes a valid local port to be
    /// allocated when no local port is passed.
    #[ip_test]
    fn test_udp_listen_port_alloc<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = local_ip::<I>();

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let wildcard_list = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            None,
            None,
        )
        .expect("listen_udp failed");
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let specified_list = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            None,
        )
        .expect("listen_udp failed");

        let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
            &sync_ctx.get_ref().sockets;
        let wildcard_port = assert_matches!(bound.listeners().get_by_id(&wildcard_list),
            Some((
                _,
                _,
                ListenerAddr{ ip: ListenerIpAddr {identifier, addr: None}, device: None}
            )) => identifier);
        let specified_port = assert_matches!(bound.listeners().get_by_id(&specified_list),
            Some((
                _,
                _,
                ListenerAddr{ ip: ListenerIpAddr {identifier, addr: _}, device: None}
            )) => identifier);
        assert!(
            UdpBoundSocketMap::<I, DummyDeviceId>::EPHEMERAL_RANGE.contains(&wildcard_port.get())
        );
        assert!(
            UdpBoundSocketMap::<I, DummyDeviceId>::EPHEMERAL_RANGE.contains(&specified_port.get())
        );
        assert_ne!(wildcard_port, specified_port);
    }

    #[ip_test]
    fn test_bind_multiple_reuse_port<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let local_port = NonZeroU16::new(100).unwrap();

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let listeners = [(), ()].map(|()| {
            let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
            UdpSocketHandler::set_udp_posix_reuse_port(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                true,
            );
            UdpSocketHandler::listen_udp(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                None,
                Some(local_port),
            )
            .expect("listen_udp failed")
        });

        let expected_addr = ListenerAddr {
            ip: ListenerIpAddr { addr: None, identifier: local_port },
            device: None,
        };
        let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
            &sync_ctx.get_ref().sockets;
        for listener in listeners {
            assert_matches!(bound.listeners().get_by_id(&listener),
                Some((_, _, addr)) => assert_eq!(addr, &expected_addr));
        }
    }

    #[ip_test]
    fn test_set_unset_reuse_port<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let local_port = NonZeroU16::new(100).unwrap();

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let _listener = {
            let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
            UdpSocketHandler::set_udp_posix_reuse_port(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                true,
            );
            UdpSocketHandler::set_udp_posix_reuse_port(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                false,
            );
            UdpSocketHandler::listen_udp(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                None,
                Some(local_port),
            )
            .expect("listen_udp failed")
        };

        // Because there is already a listener bound without `SO_REUSEPORT` set,
        // the next bind to the same address should fail.
        assert_eq!(
            {
                let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
                UdpSocketHandler::listen_udp(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    unbound,
                    None,
                    Some(local_port),
                )
            },
            Err(LocalAddressError::AddressInUse)
        );
    }

    /// Tests [`remove_udp_conn`]
    #[ip_test]
    fn test_remove_udp_conn<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = ZonedAddr::Unzoned(local_ip::<I>());
        let remote_ip = ZonedAddr::Unzoned(remote_ip::<I>());
        let local_port = NonZeroU16::new(100).unwrap();
        let remote_port = NonZeroU16::new(200).unwrap();
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listener = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(local_ip),
            Some(local_port),
        )
        .unwrap();
        let conn = UdpSocketHandler::<I, _>::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            remote_ip,
            remote_port,
        )
        .expect("connect_udp failed");
        let info = UdpSocketHandler::remove_udp_conn(&mut sync_ctx, &mut non_sync_ctx, conn);
        // Assert that the info gotten back matches what was expected.
        assert_eq!(info.local_ip, local_ip);
        assert_eq!(info.local_port, local_port);
        assert_eq!(info.remote_ip, remote_ip);
        assert_eq!(info.remote_port, remote_port);

        // Assert that that connection id was removed from the connections
        // state.
        let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
            &sync_ctx.get_ref().sockets;
        assert_matches!(bound.conns().get_by_id(&conn), None);
    }

    /// Tests [`remove_udp_listener`]
    #[ip_test]
    fn test_remove_udp_listener<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = ZonedAddr::Unzoned(local_ip::<I>());
        let local_port = NonZeroU16::new(100).unwrap();

        // Test removing a specified listener.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let list = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(local_ip),
            Some(local_port),
        )
        .expect("listen_udp failed");
        let info = UdpSocketHandler::remove_udp_listener(&mut sync_ctx, &mut non_sync_ctx, list);
        assert_eq!(info.local_ip.unwrap(), local_ip);
        assert_eq!(info.local_port, local_port);
        let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
            &sync_ctx.get_ref().sockets;
        assert_matches!(bound.listeners().get_by_id(&list), None);

        // Test removing a wildcard listener.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let list = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            None,
            Some(local_port),
        )
        .expect("listen_udp failed");
        let info = UdpSocketHandler::remove_udp_listener(&mut sync_ctx, &mut non_sync_ctx, list);
        assert_eq!(info.local_ip, None);
        assert_eq!(info.local_port, local_port);
        let UdpSockets { sockets: DatagramSockets { bound, unbound: _ }, lazy_port_alloc: _ } =
            &sync_ctx.get_ref().sockets;
        assert_matches!(bound.listeners().get_by_id(&list), None);
    }

    fn try_join_leave_multicast<I: Ip + TestIpExt>(
        mcast_addr: MulticastAddr<I::Addr>,
        interface: MulticastMembershipInterfaceSelector<I::Addr, MultipleDevicesId>,
        make_socket: impl FnOnce(
            &mut MultiDeviceDummySyncCtx<I>,
            &mut DummyNonSyncCtx<I>,
            UdpUnboundId<I>,
        ) -> UdpSocketId<I>,
    ) -> (
        Result<(), SetMulticastMembershipError>,
        HashMap<(MultipleDevicesId, MulticastAddr<I::Addr>), NonZeroUsize>,
    )
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::default());

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let socket = make_socket(&mut sync_ctx, &mut non_sync_ctx, unbound);
        let result = UdpSocketHandler::set_udp_multicast_membership(
            &mut sync_ctx,
            &mut non_sync_ctx,
            socket,
            mcast_addr,
            interface,
            true,
        );

        let memberships_snapshot = sync_ctx.get_ref().ip_options.clone();
        if let Ok(()) = result {
            UdpSocketHandler::set_udp_multicast_membership(
                &mut sync_ctx,
                &mut non_sync_ctx,
                socket,
                mcast_addr,
                interface,
                false,
            )
            .expect("leaving group failed");
        }
        assert_eq!(sync_ctx.get_ref().ip_options, HashMap::default());

        (result, memberships_snapshot)
    }

    fn leave_unbound<I: TestIpExt>(
        _sync_ctx: &mut MultiDeviceDummySyncCtx<I>,
        _non_sync_ctx: &mut DummyNonSyncCtx<I>,
        unbound: UdpUnboundId<I>,
    ) -> UdpSocketId<I> {
        unbound.into()
    }

    fn bind_as_listener<I: TestIpExt>(
        sync_ctx: &mut MultiDeviceDummySyncCtx<I>,
        non_sync_ctx: &mut DummyNonSyncCtx<I>,
        unbound: UdpUnboundId<I>,
    ) -> UdpSocketId<I>
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        UdpSocketHandler::<I, _>::listen_udp(
            sync_ctx,
            non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip::<I>())),
            NonZeroU16::new(100),
        )
        .expect("listen should succeed")
        .into()
    }

    fn bind_as_connected<I: TestIpExt>(
        sync_ctx: &mut MultiDeviceDummySyncCtx<I>,
        non_sync_ctx: &mut DummyNonSyncCtx<I>,
        unbound: UdpUnboundId<I>,
    ) -> UdpSocketId<I>
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        UdpSocketHandler::<I, _>::connect_udp(
            sync_ctx,
            non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(I::get_other_remote_ip_address(1)),
            NonZeroU16::new(200).unwrap(),
        )
        .expect("connect should succeed")
        .into()
    }

    fn iface_id<A>(id: MultipleDevicesId) -> MulticastInterfaceSelector<A, MultipleDevicesId> {
        MulticastInterfaceSelector::Interface(id)
    }
    fn iface_addr<A: IpAddress>(
        addr: SpecifiedAddr<A>,
    ) -> MulticastInterfaceSelector<A, MultipleDevicesId> {
        MulticastInterfaceSelector::LocalAddress(addr)
    }

    #[ip_test]
    #[test_case(iface_id(MultipleDevicesId::A), leave_unbound::<I>; "device_no_addr_unbound")]
    #[test_case(iface_addr(local_ip::<I>()), leave_unbound::<I>; "addr_no_device_unbound")]
    #[test_case(iface_id(MultipleDevicesId::A), bind_as_listener::<I>; "device_no_addr_listener")]
    #[test_case(iface_addr(local_ip::<I>()), bind_as_listener::<I>; "addr_no_device_listener")]
    #[test_case(iface_id(MultipleDevicesId::A), bind_as_connected::<I>; "device_no_addr_connected")]
    #[test_case(iface_addr(local_ip::<I>()), bind_as_connected::<I>; "addr_no_device_connected")]
    fn test_join_leave_multicast_succeeds<I: Ip + TestIpExt>(
        interface: MulticastInterfaceSelector<I::Addr, MultipleDevicesId>,
        make_socket: impl FnOnce(
            &mut MultiDeviceDummySyncCtx<I>,
            &mut DummyNonSyncCtx<I>,
            UdpUnboundId<I>,
        ) -> UdpSocketId<I>,
    ) where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        let mcast_addr = I::get_multicast_addr(3);

        let (result, ip_options) =
            try_join_leave_multicast(mcast_addr, interface.into(), make_socket);
        assert_eq!(result, Ok(()));
        assert_eq!(
            ip_options,
            HashMap::from([((MultipleDevicesId::A, mcast_addr), nonzero!(1usize))])
        );
    }

    #[ip_test]
    #[test_case(MultipleDevicesId::A, Some(local_ip::<I>()), leave_unbound, Ok(());
        "with_ip_unbound")]
    #[test_case(MultipleDevicesId::A, None, leave_unbound, Ok(());
        "without_ip_unbound")]
    #[test_case(MultipleDevicesId::A, Some(local_ip::<I>()), bind_as_listener, Ok(());
        "with_ip_listener")]
    #[test_case(MultipleDevicesId::A, Some(local_ip::<I>()), bind_as_connected, Ok(());
        "with_ip_connected")]
    fn test_join_leave_multicast_interface_inferred_from_bound_device<I: Ip + TestIpExt>(
        bound_device: MultipleDevicesId,
        interface_addr: Option<SpecifiedAddr<I::Addr>>,
        make_socket: impl FnOnce(
            &mut MultiDeviceDummySyncCtx<I>,
            &mut DummyNonSyncCtx<I>,
            UdpUnboundId<I>,
        ) -> UdpSocketId<I>,
        expected_result: Result<(), SetMulticastMembershipError>,
    ) where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        let mcast_addr = I::get_multicast_addr(3);
        let (result, ip_options) = try_join_leave_multicast(
            mcast_addr,
            interface_addr
                .map(MulticastInterfaceSelector::LocalAddress)
                .map(Into::into)
                .unwrap_or(MulticastMembershipInterfaceSelector::AnyInterfaceWithRoute),
            |sync_ctx, non_sync_ctx, unbound| {
                UdpSocketHandler::set_unbound_udp_device(
                    sync_ctx,
                    non_sync_ctx,
                    unbound,
                    Some(&bound_device),
                );
                make_socket(sync_ctx, non_sync_ctx, unbound)
            },
        );
        assert_eq!(result, expected_result);
        assert_eq!(
            ip_options,
            expected_result.map_or(HashMap::default(), |()| HashMap::from([(
                (bound_device, mcast_addr),
                nonzero!(1usize)
            )]))
        );
    }

    #[ip_test]
    fn test_remove_udp_unbound_leaves_multicast_groups<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::default());

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let group = I::get_multicast_addr(4);
        UdpSocketHandler::set_udp_multicast_membership(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            group,
            MulticastInterfaceSelector::LocalAddress(local_ip::<I>()).into(),
            true,
        )
        .expect("join group failed");

        assert_eq!(
            sync_ctx.get_ref().ip_options,
            HashMap::from([((MultipleDevicesId::A, group), nonzero!(1usize))])
        );

        UdpSocketHandler::remove_udp_unbound(&mut sync_ctx, &mut non_sync_ctx, unbound);
        assert_eq!(sync_ctx.get_ref().ip_options, HashMap::default());
    }

    #[ip_test]
    fn test_remove_udp_listener_leaves_multicast_groups<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::default());
        let local_ip = local_ip::<I>();
        let local_port = NonZeroU16::new(100).unwrap();

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let first_group = I::get_multicast_addr(4);
        UdpSocketHandler::set_udp_multicast_membership(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            first_group,
            MulticastInterfaceSelector::LocalAddress(local_ip).into(),
            true,
        )
        .expect("join group failed");

        let list = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            Some(local_port),
        )
        .expect("listen_udp failed");
        let second_group = I::get_multicast_addr(5);
        UdpSocketHandler::set_udp_multicast_membership(
            &mut sync_ctx,
            &mut non_sync_ctx,
            list.into(),
            second_group,
            MulticastInterfaceSelector::LocalAddress(local_ip).into(),
            true,
        )
        .expect("join group failed");

        assert_eq!(
            sync_ctx.get_ref().ip_options,
            HashMap::from([
                ((MultipleDevicesId::A, first_group), nonzero!(1usize)),
                ((MultipleDevicesId::A, second_group), nonzero!(1usize))
            ])
        );

        let _: UdpListenerInfo<_, _> =
            UdpSocketHandler::remove_udp_listener(&mut sync_ctx, &mut non_sync_ctx, list);
        assert_eq!(sync_ctx.get_ref().ip_options, HashMap::default());
    }

    #[ip_test]
    fn test_remove_udp_connected_leaves_multicast_groups<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
    {
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::default());
        let local_ip = local_ip::<I>();

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let first_group = I::get_multicast_addr(4);
        UdpSocketHandler::set_udp_multicast_membership(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            first_group,
            MulticastInterfaceSelector::LocalAddress(local_ip).into(),
            true,
        )
        .expect("join group failed");

        let conn = UdpSocketHandler::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(I::get_other_remote_ip_address(1)),
            NonZeroU16::new(200).unwrap(),
        )
        .expect("connect_udp failed");

        let second_group = I::get_multicast_addr(5);
        UdpSocketHandler::set_udp_multicast_membership(
            &mut sync_ctx,
            &mut non_sync_ctx,
            conn.into(),
            second_group,
            MulticastInterfaceSelector::LocalAddress(local_ip).into(),
            true,
        )
        .expect("join group failed");

        assert_eq!(
            sync_ctx.get_ref().ip_options,
            HashMap::from([
                ((MultipleDevicesId::A, first_group), nonzero!(1usize)),
                ((MultipleDevicesId::A, second_group), nonzero!(1usize))
            ])
        );

        let _: UdpConnInfo<_, _> =
            UdpSocketHandler::remove_udp_conn(&mut sync_ctx, &mut non_sync_ctx, conn);
        assert_eq!(sync_ctx.get_ref().ip_options, HashMap::default());
    }

    #[ip_test]
    #[should_panic(expected = "unbound")]
    fn test_listen_udp_removes_unbound<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = local_ip::<I>();
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);

        let _: UdpListenerId<_> = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            NonZeroU16::new(100),
        )
        .expect("listen_udp failed");

        // Attempting to create a new listener from the same unbound ID should
        // panic since the unbound socket ID is now invalid.
        let _: UdpListenerId<_> = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            NonZeroU16::new(200),
        )
        .expect("listen_udp failed");
    }

    #[ip_test]
    fn test_get_conn_info<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = ZonedAddr::Unzoned(local_ip::<I>());
        let remote_ip = ZonedAddr::Unzoned(remote_ip::<I>());
        // Create a UDP connection with a specified local port and local IP.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listener = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(local_ip),
            NonZeroU16::new(100),
        )
        .expect("listen_udp failed");
        let conn = UdpSocketHandler::<I, _>::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            remote_ip,
            NonZeroU16::new(200).unwrap(),
        )
        .expect("connect_udp_listener failed");
        let info = UdpSocketHandler::get_udp_conn_info(&sync_ctx, &mut non_sync_ctx, conn);
        assert_eq!(info.local_ip, local_ip);
        assert_eq!(info.local_port.get(), 100);
        assert_eq!(info.remote_ip, remote_ip);
        assert_eq!(info.remote_port.get(), 200);
    }

    #[ip_test]
    fn test_get_listener_info<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let local_ip = ZonedAddr::Unzoned(local_ip::<I>());

        // Check getting info on specified listener.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let list = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(local_ip),
            NonZeroU16::new(100),
        )
        .expect("listen_udp failed");
        let info = UdpSocketHandler::get_udp_listener_info(&sync_ctx, &mut non_sync_ctx, list);
        assert_eq!(info.local_ip.unwrap(), local_ip);
        assert_eq!(info.local_port.get(), 100);

        // Check getting info on wildcard listener.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let list = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            None,
            NonZeroU16::new(200),
        )
        .expect("listen_udp failed");
        let info = UdpSocketHandler::get_udp_listener_info(&sync_ctx, &mut non_sync_ctx, list);
        assert_eq!(info.local_ip, None);
        assert_eq!(info.local_port.get(), 200);
    }

    #[ip_test]
    fn test_get_reuse_port<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        assert_eq!(
            UdpSocketHandler::get_udp_posix_reuse_port(
                &sync_ctx,
                &mut non_sync_ctx,
                unbound.into()
            ),
            false,
        );

        UdpSocketHandler::set_udp_posix_reuse_port(&mut sync_ctx, &mut non_sync_ctx, unbound, true);

        assert_eq!(
            UdpSocketHandler::get_udp_posix_reuse_port(
                &sync_ctx,
                &mut non_sync_ctx,
                unbound.into()
            ),
            true
        );

        let listen = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip::<I>())),
            None,
        )
        .expect("listen failed");
        assert_eq!(
            UdpSocketHandler::get_udp_posix_reuse_port(&sync_ctx, &mut non_sync_ctx, listen.into()),
            true
        );
        let _: UdpListenerInfo<_, _> =
            UdpSocketHandler::remove_udp_listener(&mut sync_ctx, &mut non_sync_ctx, listen);

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        UdpSocketHandler::set_udp_posix_reuse_port(&mut sync_ctx, &mut non_sync_ctx, unbound, true);
        let conn = UdpSocketHandler::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(remote_ip::<I>()),
            nonzero!(569u16),
        )
        .expect("connect failed");

        assert_eq!(
            UdpSocketHandler::get_udp_posix_reuse_port(&sync_ctx, &mut non_sync_ctx, conn.into()),
            true
        );
    }

    #[ip_test]
    fn test_get_bound_device_unbound<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);

        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &mut non_sync_ctx, unbound.into()),
            None
        );

        UdpSocketHandler::set_unbound_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            Some(&DummyDeviceId),
        );
        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &mut non_sync_ctx, unbound.into()),
            Some(DummyDeviceId)
        );
    }

    #[ip_test]
    fn test_get_bound_device_listener<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);

        UdpSocketHandler::set_unbound_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(&DummyDeviceId),
        );
        let listen = UdpSocketHandler::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip::<I>())),
            Some(NonZeroU16::new(100).unwrap()),
        )
        .expect("failed to listen");
        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &mut non_sync_ctx, listen.into()),
            Some(DummyDeviceId)
        );

        UdpSocketHandler::set_listener_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listen.into(),
            None,
        )
        .expect("failed to set device");
        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &mut non_sync_ctx, listen.into()),
            None
        );
    }

    #[ip_test]
    fn test_get_bound_device_connected<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);

        UdpSocketHandler::set_unbound_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(&DummyDeviceId),
        );
        let conn = UdpSocketHandler::connect_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            ZonedAddr::Unzoned(remote_ip::<I>()),
            NonZeroU16::new(200).unwrap(),
        )
        .expect("failed to connect");
        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &mut non_sync_ctx, conn.into()),
            Some(DummyDeviceId)
        );
        UdpSocketHandler::set_conn_udp_device(&mut sync_ctx, &mut non_sync_ctx, conn.into(), None)
            .expect("failed to set device");
        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &mut non_sync_ctx, conn.into()),
            None
        );
    }

    #[ip_test]
    fn test_listen_udp_forwards_errors<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let remote_ip = remote_ip::<I>();

        // Check listening to a non-local IP fails.
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listen_err = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(remote_ip)),
            NonZeroU16::new(100),
        )
        .expect_err("listen_udp unexpectedly succeeded");
        assert_eq!(listen_err, LocalAddressError::CannotBindToAddress);

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let _ = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            None,
            NonZeroU16::new(200),
        )
        .expect("listen_udp failed");
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listen_err = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            None,
            NonZeroU16::new(200),
        )
        .expect_err("listen_udp unexpectedly succeeded");
        assert_eq!(listen_err, LocalAddressError::AddressInUse);
    }

    const IPV6_LINK_LOCAL_ADDR: Ipv6Addr = net_ip_v6!("fe80::1234");
    #[test_case(IPV6_LINK_LOCAL_ADDR, IPV6_LINK_LOCAL_ADDR; "unicast")]
    #[test_case(IPV6_LINK_LOCAL_ADDR, MulticastAddr::new(net_ip_v6!("ff02::1234")).unwrap().get(); "multicast")]
    fn test_listen_udp_ipv6_link_local_requires_zone(
        interface_addr: Ipv6Addr,
        bind_addr: Ipv6Addr,
    ) {
        type I = Ipv6;
        let interface_addr = LinkLocalAddr::new(interface_addr).unwrap().into_specified();

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } = DummyCtx::with_sync_ctx(
            DummySyncCtx::<I>::with_state(DummyUdpCtx::with_local_remote_ip_addrs(
                vec![interface_addr],
                vec![remote_ip::<I>()],
            )),
        );

        let bind_addr = LinkLocalAddr::new(bind_addr).unwrap().into_specified();
        assert!(bind_addr.scope().can_have_zone());

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let result = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(bind_addr)),
            NonZeroU16::new(200),
        );
        assert_eq!(
            result,
            Err(LocalAddressError::Zone(ZonedAddressError::RequiredZoneNotProvided))
        );
    }

    #[test_case(MultipleDevicesId::A, Ok(()); "matching")]
    #[test_case(MultipleDevicesId::B, Err(LocalAddressError::Zone(ZonedAddressError::DeviceZoneMismatch)); "not matching")]
    fn test_listen_udp_ipv6_link_local_with_bound_device_set(
        zone_id: MultipleDevicesId,
        expected_result: Result<(), LocalAddressError>,
    ) {
        type I = Ipv6;
        let ll_addr = LinkLocalAddr::new(net_ip_v6!("fe80::1234")).unwrap().into_specified();
        assert!(ll_addr.scope().can_have_zone());

        let remote_ips = vec![remote_ip::<I>()];
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::with_state(
                DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new_ipv6(
                    [(MultipleDevicesId::A, ll_addr), (MultipleDevicesId::B, local_ip::<I>())].map(
                        |(device, local_ip)| DummyDeviceConfig {
                            device,
                            local_ips: vec![local_ip],
                            remote_ips: remote_ips.clone(),
                        },
                    ),
                )),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        UdpSocketHandler::set_unbound_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(&MultipleDevicesId::A),
        );

        let result = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Zoned(AddrAndZone::new(ll_addr.get(), zone_id).unwrap())),
            NonZeroU16::new(200),
        )
        .map(|_: UdpListenerId<I>| ());
        assert_eq!(result, expected_result);
    }

    #[test_case(MultipleDevicesId::A, Ok(()); "matching")]
    #[test_case(MultipleDevicesId::B, Err(LocalAddressError::AddressMismatch); "not matching")]
    fn test_listen_udp_ipv6_link_local_with_zone_requires_addr_assigned_to_device(
        zone_id: MultipleDevicesId,
        expected_result: Result<(), LocalAddressError>,
    ) {
        type I = Ipv6;
        let ll_addr = LinkLocalAddr::new(net_ip_v6!("fe80::1234")).unwrap().into_specified();
        assert!(ll_addr.scope().can_have_zone());

        let remote_ips = vec![remote_ip::<I>()];
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::with_state(
                DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new_ipv6(
                    [(MultipleDevicesId::A, ll_addr), (MultipleDevicesId::B, local_ip::<I>())].map(
                        |(device, local_ip)| DummyDeviceConfig {
                            device,
                            local_ips: vec![local_ip],
                            remote_ips: remote_ips.clone(),
                        },
                    ),
                )),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let result = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Zoned(AddrAndZone::new(ll_addr.get(), zone_id).unwrap())),
            NonZeroU16::new(200),
        )
        .map(|_: UdpListenerId<I>| ());
        assert_eq!(result, expected_result);
    }

    #[test_case(None, Err(LocalAddressError::Zone(ZonedAddressError::DeviceZoneMismatch)); "clear device")]
    #[test_case(Some(MultipleDevicesId::A), Ok(()); "set same device")]
    #[test_case(Some(MultipleDevicesId::B),
                Err(LocalAddressError::Zone(ZonedAddressError::DeviceZoneMismatch)); "change device")]
    fn test_listen_udp_ipv6_listen_link_local_update_bound_device(
        new_device: Option<MultipleDevicesId>,
        expected_result: Result<(), LocalAddressError>,
    ) {
        type I = Ipv6;
        let ll_addr = LinkLocalAddr::new(net_ip_v6!("fe80::1234")).unwrap().into_specified();
        assert!(ll_addr.scope().can_have_zone());

        let remote_ips = vec![remote_ip::<I>()];
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::with_state(
                DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new_ipv6(
                    [(MultipleDevicesId::A, ll_addr), (MultipleDevicesId::B, local_ip::<I>())].map(
                        |(device, local_ip)| DummyDeviceConfig {
                            device,
                            local_ips: vec![local_ip],
                            remote_ips: remote_ips.clone(),
                        },
                    ),
                )),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listener = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Zoned(AddrAndZone::new(ll_addr.get(), MultipleDevicesId::A).unwrap())),
            NonZeroU16::new(200),
        )
        .expect("listen failed");

        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(
                &mut sync_ctx,
                &mut non_sync_ctx,
                listener.into()
            ),
            Some(MultipleDevicesId::A)
        );

        assert_eq!(
            UdpSocketHandler::set_listener_udp_device(
                &mut sync_ctx,
                &mut non_sync_ctx,
                listener.into(),
                new_device.as_ref()
            ),
            expected_result,
        );
    }

    #[test_case(None; "bind all IPs")]
    #[test_case(Some(ZonedAddr::Unzoned(local_ip::<Ipv6>())); "bind unzoned")]
    #[test_case(Some(ZonedAddr::Zoned(AddrAndZone::new(net_ip_v6!("fe80::1"),
        MultipleDevicesId::A).unwrap())); "bind with same zone")]
    fn test_udp_ipv6_connect_with_unzoned(
        bound_addr: Option<ZonedAddr<Ipv6Addr, MultipleDevicesId>>,
    ) where
        MultiDeviceDummySyncCtx<Ipv6>: DummyDeviceSyncCtxBound<Ipv6, MultipleDevicesId>,
        DummyIpSocketCtx<Ipv6, MultipleDevicesId>: DummyIpSocketCtxExt<Ipv6, MultipleDevicesId>,
    {
        let remote_ips = vec![remote_ip::<Ipv6>()];

        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::with_state(
                DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new([
                    DummyDeviceConfig {
                        device: MultipleDevicesId::A,
                        local_ips: vec![
                            local_ip::<Ipv6>(),
                            SpecifiedAddr::new(net_ip_v6!("fe80::1")).unwrap(),
                        ],
                        remote_ips: remote_ips.clone(),
                    },
                    DummyDeviceConfig {
                        device: MultipleDevicesId::B,
                        local_ips: vec![SpecifiedAddr::new(net_ip_v6!("fe80::2")).unwrap()],
                        remote_ips: remote_ips.clone(),
                    },
                ])),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);

        let listener = UdpSocketHandler::<Ipv6, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            bound_addr,
            Some(LOCAL_PORT),
        )
        .unwrap();

        assert_matches!(
            UdpSocketHandler::connect_udp_listener(
                &mut sync_ctx,
                &mut non_sync_ctx,
                listener,
                ZonedAddr::Unzoned(remote_ip::<Ipv6>()),
                REMOTE_PORT,
            ),
            Ok(_)
        );
    }

    #[test]
    fn test_udp_ipv6_connect_zoned_get_info() {
        let ll_addr = LinkLocalAddr::new(net_ip_v6!("fe80::1234")).unwrap().into_specified();
        assert!(socket::must_have_zone(&ll_addr));

        let remote_ips = vec![remote_ip::<Ipv6>()];
        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<Ipv6>::with_state(
                DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new_ipv6(
                    [(MultipleDevicesId::A, ll_addr), (MultipleDevicesId::B, local_ip::<Ipv6>())]
                        .map(|(device, local_ip)| DummyDeviceConfig {
                            device,
                            local_ips: vec![local_ip],
                            remote_ips: remote_ips.clone(),
                        }),
                )),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        UdpSocketHandler::set_unbound_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(&MultipleDevicesId::A),
        );

        let zoned_local_addr =
            ZonedAddr::Zoned(AddrAndZone::new(ll_addr.get(), MultipleDevicesId::A).unwrap());
        let listener = UdpSocketHandler::<Ipv6, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(zoned_local_addr),
            Some(LOCAL_PORT),
        )
        .unwrap();

        let conn = UdpSocketHandler::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            ZonedAddr::Unzoned(remote_ip::<Ipv6>()),
            REMOTE_PORT,
        )
        .expect("connect should succeed");

        assert_eq!(
            UdpSocketHandler::get_udp_conn_info(&sync_ctx, &mut non_sync_ctx, conn),
            UdpConnInfo {
                local_ip: zoned_local_addr,
                local_port: LOCAL_PORT,
                remote_ip: ZonedAddr::Unzoned(remote_ip::<Ipv6>()),
                remote_port: REMOTE_PORT,
            }
        );
    }

    #[test_case(ZonedAddr::Zoned(AddrAndZone::new(net_ip_v6!("fe80::2"),
        MultipleDevicesId::B).unwrap()),
        Err(UdpConnectListenerError::Zone(ZonedAddressError::DeviceZoneMismatch));
        "connect to different zone")]
    #[test_case(ZonedAddr::Unzoned(SpecifiedAddr::new(net_ip_v6!("fe80::3")).unwrap()),
        Ok(MultipleDevicesId::A); "connect implicit zone")]
    fn test_udp_ipv6_bind_zoned(
        remote_addr: ZonedAddr<Ipv6Addr, MultipleDevicesId>,
        expected: Result<MultipleDevicesId, UdpConnectListenerError>,
    ) {
        let remote_ips = vec![SpecifiedAddr::new(net_ip_v6!("fe80::3")).unwrap()];

        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::with_state(
                DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new([
                    DummyDeviceConfig {
                        device: MultipleDevicesId::A,
                        local_ips: vec![SpecifiedAddr::new(net_ip_v6!("fe80::1")).unwrap()],
                        remote_ips: remote_ips.clone(),
                    },
                    DummyDeviceConfig {
                        device: MultipleDevicesId::B,
                        local_ips: vec![SpecifiedAddr::new(net_ip_v6!("fe80::2")).unwrap()],
                        remote_ips: remote_ips.clone(),
                    },
                ])),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);

        let listener = UdpSocketHandler::<Ipv6, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Zoned(
                AddrAndZone::new(net_ip_v6!("fe80::1"), MultipleDevicesId::A).unwrap(),
            )),
            Some(LOCAL_PORT),
        )
        .unwrap();

        let result = UdpSocketHandler::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            remote_addr,
            REMOTE_PORT,
        )
        .map(|id: UdpConnId<_>| {
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &mut non_sync_ctx, id.into()).unwrap()
        })
        .map_err(|(e, _id): (_, UdpListenerId<_>)| e);
        assert_eq!(result, expected);
    }

    #[ip_test]
    fn test_listen_udp_loopback_no_zone_is_required<I: Ip + TestIpExt>()
    where
        MultiDeviceDummySyncCtx<I>: DummyDeviceSyncCtxBound<I, MultipleDevicesId>,
        DummyIpSocketCtx<I, MultipleDevicesId>: DummyIpSocketCtxExt<I, MultipleDevicesId>,
    {
        let loopback_addr = I::LOOPBACK_ADDRESS;
        let remote_ips = vec![remote_ip::<I>()];

        let MultiDeviceDummyCtx { mut sync_ctx, mut non_sync_ctx } =
            MultiDeviceDummyCtx::with_sync_ctx(MultiDeviceDummySyncCtx::<I>::with_state(
                DummyUdpCtx::with_ip_socket_ctx(DummyIpSocketCtx::new(
                    [
                        (MultipleDevicesId::A, loopback_addr),
                        (MultipleDevicesId::B, local_ip::<I>()),
                    ]
                    .map(|(device, local_ip)| DummyDeviceConfig {
                        device,
                        local_ips: vec![local_ip],
                        remote_ips: remote_ips.clone(),
                    }),
                )),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        UdpSocketHandler::set_unbound_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(&MultipleDevicesId::A),
        );

        let result = UdpSocketHandler::<I, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(loopback_addr)),
            NonZeroU16::new(200),
        );
        assert_matches!(result, Ok(_));
    }

    #[test_case(None; "removes implicit")]
    #[test_case(Some(DummyDeviceId); "preserves implicit")]
    fn test_connect_disconnect_affects_bound_device(bind_device: Option<DummyDeviceId>) {
        // If a socket is bound to an unzoned address, whether or not it has a
        // bound device should be restored after `connect` then `disconnect`.
        let ll_addr = LinkLocalAddr::new(net_ip_v6!("fe80::1234")).unwrap().into_specified();
        assert!(socket::must_have_zone(&ll_addr));

        let local_ip = local_ip::<Ipv6>();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<Ipv6>::with_state(
                DummyUdpCtx::with_local_remote_ip_addrs(vec![local_ip], vec![ll_addr]),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        UdpSocketHandler::set_unbound_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            bind_device.as_ref(),
        );

        let listener = UdpSocketHandler::<Ipv6, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            Some(LOCAL_PORT),
        )
        .unwrap();
        let conn = UdpSocketHandler::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            ZonedAddr::Zoned(AddrAndZone::new(ll_addr.get(), DummyDeviceId).unwrap()),
            REMOTE_PORT,
        )
        .expect("connect should succeed");

        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &non_sync_ctx, conn.into()),
            Some(DummyDeviceId)
        );

        let listener =
            UdpSocketHandler::disconnect_udp_connected(&mut sync_ctx, &mut non_sync_ctx, conn);

        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &non_sync_ctx, listener.into()),
            bind_device
        );
    }

    #[test]
    fn test_bind_zoned_addr_connect_disconnect() {
        // If a socket is bound to a zoned address, the address's device should
        // be retained after `connect` then `disconnect`.
        let ll_addr = LinkLocalAddr::new(net_ip_v6!("fe80::1234")).unwrap().into_specified();
        assert!(socket::must_have_zone(&ll_addr));

        let remote_ip = remote_ip::<Ipv6>();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<Ipv6>::with_state(
                DummyUdpCtx::with_local_remote_ip_addrs(vec![ll_addr], vec![remote_ip]),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listener = UdpSocketHandler::<Ipv6, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Zoned(AddrAndZone::new(ll_addr.get(), DummyDeviceId).unwrap())),
            Some(LOCAL_PORT),
        )
        .unwrap();
        let conn = UdpSocketHandler::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            ZonedAddr::Unzoned(remote_ip),
            REMOTE_PORT,
        )
        .expect("connect should succeed");

        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &non_sync_ctx, conn.into()),
            Some(DummyDeviceId)
        );

        let listener =
            UdpSocketHandler::disconnect_udp_connected(&mut sync_ctx, &mut non_sync_ctx, conn);
        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &non_sync_ctx, listener.into()),
            Some(DummyDeviceId)
        );
    }

    #[test]
    fn test_bind_device_after_connect_persists_after_disconnect() {
        // If a socket is bound to an unzoned address, connected to a zoned address, and then has
        // its device set, the device should be *retained* after `disconnect`.
        let ll_addr = LinkLocalAddr::new(net_ip_v6!("fe80::1234")).unwrap().into_specified();
        assert!(socket::must_have_zone(&ll_addr));

        let local_ip = local_ip::<Ipv6>();
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<Ipv6>::with_state(
                DummyUdpCtx::with_local_remote_ip_addrs(vec![local_ip], vec![ll_addr]),
            ));

        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        let listener = UdpSocketHandler::<Ipv6, _>::listen_udp(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            Some(ZonedAddr::Unzoned(local_ip)),
            Some(LOCAL_PORT),
        )
        .unwrap();
        let conn = UdpSocketHandler::connect_udp_listener(
            &mut sync_ctx,
            &mut non_sync_ctx,
            listener,
            ZonedAddr::Zoned(AddrAndZone::new(ll_addr.get(), DummyDeviceId).unwrap()),
            REMOTE_PORT,
        )
        .expect("connect should succeed");

        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &non_sync_ctx, conn.into()),
            Some(DummyDeviceId)
        );

        // This is a no-op functionally since the socket is already bound to the
        // device but it implies that we shouldn't unbind the device on
        // disconnect.
        UdpSocketHandler::set_conn_udp_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            conn,
            Some(&DummyDeviceId),
        )
        .expect("binding same device should succeed");

        let listener =
            UdpSocketHandler::disconnect_udp_connected(&mut sync_ctx, &mut non_sync_ctx, conn);
        assert_eq!(
            UdpSocketHandler::get_udp_bound_device(&sync_ctx, &non_sync_ctx, listener.into()),
            Some(DummyDeviceId)
        );
    }

    #[ip_test]
    fn test_remove_udp_unbound<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(DummySyncCtx::<I>::default());
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);
        UdpSocketHandler::remove_udp_unbound(&mut sync_ctx, &mut non_sync_ctx, unbound);

        let UdpSockets {
            sockets: DatagramSockets { bound: _, unbound: unbound_sockets },
            lazy_port_alloc: _,
        } = &sync_ctx.get_ref().sockets;
        assert_matches!(unbound_sockets.get(unbound.into()), None)
    }

    #[ip_test]
    fn test_hop_limits_used_for_sending_packets<I: Ip + TestIpExt>()
    where
        DummySyncCtx<I>: DummySyncCtxBound<I>,
        DummyUdpCtx<I, DummyDeviceId>: DummyUdpCtxExt<I>,
    {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } = DummyCtx::with_sync_ctx(
            DummySyncCtx::<I>::with_state(DummyUdpCtx::with_local_remote_ip_addrs(
                vec![local_ip::<I>()],
                vec![remote_ip::<I>(), I::SOME_MULTICAST_ADDR.into_specified()],
            )),
        );
        let unbound = UdpSocketHandler::create_udp_unbound(&mut sync_ctx);

        const UNICAST_HOPS: NonZeroU8 = nonzero!(23u8);
        const MULTICAST_HOPS: NonZeroU8 = nonzero!(98u8);
        UdpSocketHandler::set_udp_unicast_hop_limit(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            Some(UNICAST_HOPS),
        );
        UdpSocketHandler::set_udp_multicast_hop_limit(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound.into(),
            Some(MULTICAST_HOPS),
        );

        let listener =
            UdpSocketHandler::listen_udp(&mut sync_ctx, &mut non_sync_ctx, unbound, None, None)
                .expect("listen failed");

        let mut send_and_get_ttl = |remote_ip| {
            BufferUdpSocketHandler::send_udp_listener(
                &mut sync_ctx,
                &mut non_sync_ctx,
                listener,
                remote_ip,
                nonzero!(9090u16),
                Buf::new(vec![], ..),
            )
            .expect("send failed");

            let (
                SendIpPacketMeta {
                    device: _,
                    src_ip: _,
                    dst_ip,
                    next_hop: _,
                    proto: _,
                    ttl,
                    mtu: _,
                },
                _body,
            ) = sync_ctx.frames().last().unwrap();
            assert_eq!(*dst_ip, remote_ip);
            *ttl
        };

        assert_eq!(send_and_get_ttl(I::SOME_MULTICAST_ADDR.into_specified()), Some(MULTICAST_HOPS));
        assert_eq!(send_and_get_ttl(remote_ip::<I>()), Some(UNICAST_HOPS));
    }

    /// Tests that incoming ICMP errors are properly delivered to a connection,
    /// a listener, and a wildcard listener.
    #[test]
    fn test_icmp_error() {
        // Create a context with:
        // - A wildcard listener on port 1
        // - A listener on the local IP and port 2
        // - A connection from the local IP to the remote IP on local port 2 and
        //   remote port 3
        fn initialize_context<I: TestIpExt>() -> DummyCtx<I>
        where
            DummySyncCtx<I>: DummySyncCtxBound<I>,
        {
            let mut ctx = DummyCtx::with_sync_ctx(DummySyncCtx::default());
            let DummyCtx { sync_ctx, non_sync_ctx } = &mut ctx;
            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            assert_eq!(
                UdpSocketHandler::listen_udp(
                    sync_ctx,
                    non_sync_ctx,
                    unbound,
                    None,
                    Some(NonZeroU16::new(1).unwrap())
                )
                .unwrap(),
                UdpListenerId::new(0)
            );

            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            assert_eq!(
                UdpSocketHandler::listen_udp(
                    sync_ctx,
                    non_sync_ctx,
                    unbound,
                    Some(ZonedAddr::Unzoned(local_ip::<I>())),
                    Some(NonZeroU16::new(2).unwrap())
                )
                .unwrap(),
                UdpListenerId::new(1)
            );
            let unbound = UdpSocketHandler::create_udp_unbound(sync_ctx);
            let listener = UdpSocketHandler::listen_udp(
                sync_ctx,
                non_sync_ctx,
                unbound,
                Some(ZonedAddr::Unzoned(local_ip::<I>())),
                Some(NonZeroU16::new(3).unwrap()),
            )
            .unwrap();
            assert_eq!(
                UdpSocketHandler::connect_udp_listener(
                    sync_ctx,
                    non_sync_ctx,
                    listener,
                    ZonedAddr::Unzoned(remote_ip::<I>()),
                    NonZeroU16::new(4).unwrap(),
                )
                .unwrap(),
                UdpConnId::new(0)
            );
            ctx
        }

        // Serialize a UDP-in-IP packet with the given values, and then receive
        // an ICMP error message with that packet as the original packet.
        fn receive_icmp_error<
            I: TestIpExt,
            F: Fn(&mut DummySyncCtx<I>, &mut DummyNonSyncCtx<I>, &[u8], I::ErrorCode),
        >(
            sync_ctx: &mut DummySyncCtx<I>,
            ctx: &mut DummyNonSyncCtx<I>,
            src_ip: I::Addr,
            dst_ip: I::Addr,
            src_port: u16,
            dst_port: u16,
            err: I::ErrorCode,
            f: F,
        ) where
            I::PacketBuilder: core::fmt::Debug,
        {
            let packet = (&[0u8][..])
                .into_serializer()
                .encapsulate(UdpPacketBuilder::new(
                    src_ip,
                    dst_ip,
                    NonZeroU16::new(src_port),
                    NonZeroU16::new(dst_port).unwrap(),
                ))
                .encapsulate(I::PacketBuilder::new(src_ip, dst_ip, 64, IpProto::Udp.into()))
                .serialize_vec_outer()
                .unwrap();
            f(sync_ctx, ctx, packet.as_ref(), err);
        }

        fn test<
            I: TestIpExt + PartialEq,
            F: Copy + Fn(&mut DummySyncCtx<I>, &mut DummyNonSyncCtx<I>, &[u8], I::ErrorCode),
        >(
            err: I::ErrorCode,
            f: F,
            other_remote_ip: I::Addr,
        ) where
            I::PacketBuilder: core::fmt::Debug,
            I::ErrorCode: Copy + core::fmt::Debug + PartialEq,
            DummySyncCtx<I>: DummySyncCtxBound<I>,
        {
            let DummyCtx { mut sync_ctx, mut non_sync_ctx } = initialize_context::<I>();

            let src_ip = local_ip::<I>();
            let dst_ip = remote_ip::<I>();

            // Test that we receive an error for the connection.
            receive_icmp_error(
                &mut sync_ctx,
                &mut non_sync_ctx,
                src_ip.get(),
                dst_ip.get(),
                3,
                4,
                err,
                f,
            );
            assert_eq!(
                non_sync_ctx.state().icmp_errors.as_slice(),
                [IcmpError { id: UdpConnId::new(0).into(), err }]
            );

            // Test that we receive an error for the listener.
            receive_icmp_error(
                &mut sync_ctx,
                &mut non_sync_ctx,
                src_ip.get(),
                dst_ip.get(),
                2,
                4,
                err,
                f,
            );
            assert_eq!(
                &non_sync_ctx.state().icmp_errors.as_slice()[1..],
                [IcmpError { id: UdpListenerId::new(1).into(), err }]
            );

            // Test that we receive an error for the wildcard listener.
            receive_icmp_error(
                &mut sync_ctx,
                &mut non_sync_ctx,
                src_ip.get(),
                dst_ip.get(),
                1,
                4,
                err,
                f,
            );
            assert_eq!(
                &non_sync_ctx.state().icmp_errors.as_slice()[2..],
                [IcmpError { id: UdpListenerId::new(0).into(), err }]
            );

            // Test that we receive an error for the wildcard listener even if
            // the original packet was sent to a different remote IP/port.
            receive_icmp_error(
                &mut sync_ctx,
                &mut non_sync_ctx,
                src_ip.get(),
                other_remote_ip,
                1,
                5,
                err,
                f,
            );
            assert_eq!(
                &non_sync_ctx.state().icmp_errors.as_slice()[3..],
                [IcmpError { id: UdpListenerId::new(0).into(), err }]
            );

            // Test that an error that doesn't correspond to any connection or
            // listener isn't received.
            receive_icmp_error(
                &mut sync_ctx,
                &mut non_sync_ctx,
                src_ip.get(),
                dst_ip.get(),
                3,
                5,
                err,
                f,
            );
            assert_eq!(non_sync_ctx.state().icmp_errors.len(), 4);
        }

        test(
            Icmpv4ErrorCode::DestUnreachable(Icmpv4DestUnreachableCode::DestNetworkUnreachable),
            |sync_ctx: &mut DummySyncCtx<Ipv4>,
             ctx: &mut DummyNonSyncCtx<Ipv4>,
             mut packet,
             error_code| {
                let packet = packet.parse::<Ipv4PacketRaw<_>>().unwrap();
                let device = DummyDeviceId;
                let src_ip = SpecifiedAddr::new(packet.src_ip());
                let dst_ip = SpecifiedAddr::new(packet.dst_ip()).unwrap();
                let body = packet.body().into_inner();
                <UdpIpTransportContext as IpTransportContext<Ipv4, _, _>>::receive_icmp_error(
                    sync_ctx, ctx, &device, src_ip, dst_ip, body, error_code,
                )
            },
            Ipv4Addr::new([1, 2, 3, 4]),
        );

        test(
            Icmpv6ErrorCode::DestUnreachable(Icmpv6DestUnreachableCode::NoRoute),
            |sync_ctx: &mut DummySyncCtx<Ipv6>,
             ctx: &mut DummyNonSyncCtx<Ipv6>,
             mut packet,
             error_code| {
                let packet = packet.parse::<Ipv6PacketRaw<_>>().unwrap();
                let device = DummyDeviceId;
                let src_ip = SpecifiedAddr::new(packet.src_ip());
                let dst_ip = SpecifiedAddr::new(packet.dst_ip()).unwrap();
                let body = packet.body().unwrap().into_inner();
                <UdpIpTransportContext as IpTransportContext<Ipv6, _, _>>::receive_icmp_error(
                    sync_ctx, ctx, &device, src_ip, dst_ip, body, error_code,
                )
            },
            Ipv6Addr::from_bytes([1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8]),
        );
    }
}
