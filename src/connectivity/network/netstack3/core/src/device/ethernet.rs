// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Ethernet protocol.

use alloc::{collections::HashMap, collections::VecDeque, vec::Vec};
use core::fmt::Debug;

use log::{debug, trace};
use net_types::{
    ethernet::Mac,
    ip::{
        AddrSubnet, Ip, IpAddr, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr,
        UnicastOrMulticastIpv6Addr,
    },
    BroadcastAddress, MulticastAddr, MulticastAddress, SpecifiedAddr, UnicastAddr, UnicastAddress,
    Witness,
};
use packet::{Buf, BufferMut, EmptyBuf, Nested, Serializer};
use packet_formats::{
    arp::{peek_arp_types, ArpHardwareType, ArpNetworkType},
    ethernet::{
        EtherType, EthernetFrame, EthernetFrameBuilder, EthernetFrameLengthCheck, EthernetIpExt,
    },
    utils::NonZeroDuration,
};

use crate::{
    context::{FrameContext, RngContext, StateContext},
    data_structures::ref_counted_hash_map::{InsertResult, RefCountedHashSet, RemoveResult},
    device::{
        arp::{self, ArpContext, ArpDeviceIdContext, ArpFrameMetadata, ArpState, ArpTimerId},
        link::LinkDevice,
        ndp::{self, NdpContext, NdpHandler, NdpState, NdpTimerId},
        BufferIpLinkDeviceContext, DeviceIdContext, EthernetDeviceId, FrameDestination,
        IpLinkDeviceContext, IpLinkDeviceNonSyncContext, RecvIpFrameMeta,
    },
    error::ExistsError,
    ip::device::state::{AddrConfig, IpDeviceState},
    NonSyncContext, SyncCtx,
};

const ETHERNET_MAX_PENDING_FRAMES: usize = 10;

impl From<Mac> for FrameDestination {
    fn from(mac: Mac) -> FrameDestination {
        if mac.is_broadcast() {
            FrameDestination::Broadcast
        } else if mac.is_multicast() {
            FrameDestination::Multicast
        } else {
            debug_assert!(mac.is_unicast());
            FrameDestination::Unicast
        }
    }
}

/// The non-synchronized execution context for an Ethernet device.
pub(crate) trait EthernetIpLinkDeviceNonSyncContext<DeviceId>:
    RngContext + IpLinkDeviceNonSyncContext<EthernetTimerId<DeviceId>>
{
}
impl<DeviceId, C: RngContext + IpLinkDeviceNonSyncContext<EthernetTimerId<DeviceId>>>
    EthernetIpLinkDeviceNonSyncContext<DeviceId> for C
{
}

/// The execution context for an Ethernet device.
pub(crate) trait EthernetIpLinkDeviceContext<C: EthernetIpLinkDeviceNonSyncContext<Self::DeviceId>>:
    IpLinkDeviceContext<
    EthernetLinkDevice,
    C,
    EthernetTimerId<<Self as DeviceIdContext<EthernetLinkDevice>>::DeviceId>,
>
{
    /// Adds an IPv6 address to the device.
    // TODO(https://fxbug.dev/72378): Remove this method once NDP operates at
    // L3.
    fn add_ipv6_addr_subnet(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr_sub: AddrSubnet<Ipv6Addr>,
        config: AddrConfig<C::Instant>,
    ) -> Result<(), ExistsError>;

    /// Joins an IPv6 multicast group.
    // TODO(https://fxbug.dev/72378): Remove this method once NDP operates at
    // L3.
    fn join_ipv6_multicast(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    );

    /// Leaves an IPv6 multicast group.
    // TODO(https://fxbug.dev/72378): Remove this method once NDP operates at
    // L3.
    fn leave_ipv6_multicast(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    );
}

impl<NonSyncCtx: NonSyncContext> EthernetIpLinkDeviceContext<NonSyncCtx> for SyncCtx<NonSyncCtx> {
    fn add_ipv6_addr_subnet(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: EthernetDeviceId,
        addr_sub: AddrSubnet<Ipv6Addr>,
        config: AddrConfig<NonSyncCtx::Instant>,
    ) -> Result<(), ExistsError> {
        crate::ip::device::add_ipv6_addr_subnet(self, ctx, device_id.into(), addr_sub, config)
    }

    fn join_ipv6_multicast(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    ) {
        crate::ip::device::join_ip_multicast::<Ipv6, _, _>(
            self,
            ctx,
            device_id.into(),
            multicast_addr,
        )
    }

    fn leave_ipv6_multicast(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    ) {
        crate::ip::device::leave_ip_multicast::<Ipv6, _, _>(
            self,
            ctx,
            device_id.into(),
            multicast_addr,
        )
    }
}

/// A shorthand for `BufferIpLinkDeviceContext` with all of the appropriate type
/// arguments fixed to their Ethernet values.
pub(super) trait BufferEthernetIpLinkDeviceContext<
    C: EthernetIpLinkDeviceNonSyncContext<Self::DeviceId>,
    B: BufferMut,
>:
    EthernetIpLinkDeviceContext<C>
    + BufferIpLinkDeviceContext<
        EthernetLinkDevice,
        C,
        EthernetTimerId<<Self as DeviceIdContext<EthernetLinkDevice>>::DeviceId>,
        B,
    >
{
}

impl<
        C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
        B: BufferMut,
        SC: EthernetIpLinkDeviceContext<C>
            + BufferIpLinkDeviceContext<
                EthernetLinkDevice,
                C,
                EthernetTimerId<<SC as DeviceIdContext<EthernetLinkDevice>>::DeviceId>,
                B,
            >,
    > BufferEthernetIpLinkDeviceContext<C, B> for SC
{
}

/// Builder for [`EthernetDeviceState`].
pub(crate) struct EthernetDeviceStateBuilder {
    mac: UnicastAddr<Mac>,
    mtu: u32,
}

impl EthernetDeviceStateBuilder {
    /// Create a new `EthernetDeviceStateBuilder`.
    pub(crate) fn new(mac: UnicastAddr<Mac>, mtu: u32) -> Self {
        // TODO(joshlf): Add a minimum MTU for all Ethernet devices such that
        //  you cannot create an `EthernetDeviceState` with an MTU smaller than
        //  the minimum. The absolute minimum needs to be at least the minimum
        //  body size of an Ethernet frame. For IPv6-capable devices, the
        //  minimum needs to be higher - the IPv6 minimum MTU. The easy path is
        //  to simply use the IPv6 minimum MTU as the minimum in all cases,
        //  although we may at some point want to figure out how to configure
        //  devices which don't support IPv6, and allow smaller MTUs for those
        //  devices.
        //
        //  A few questions:
        //  - How do we wire error information back up the call stack? Should
        //    this just return a Result or something?
        Self { mac, mtu }
    }

    /// Build the `EthernetDeviceState` from this builder.
    pub(super) fn build(self) -> EthernetDeviceState {
        EthernetDeviceState {
            mac: self.mac,
            mtu: self.mtu,
            hw_mtu: self.mtu,
            link_multicast_groups: RefCountedHashSet::default(),
            ipv4_arp: ArpState::default(),
            ndp: NdpState::new(),
            pending_frames: HashMap::new(),
            promiscuous_mode: false,
        }
    }
}

/// The state associated with an Ethernet device.
pub(crate) struct EthernetDeviceState {
    /// Mac address of the device this state is for.
    mac: UnicastAddr<Mac>,

    /// The value this netstack assumes as the device's current MTU.
    mtu: u32,

    /// The maximum MTU allowed by the hardware.
    ///
    /// `mtu` MUST NEVER be greater than `hw_mtu`.
    hw_mtu: u32,

    /// Link multicast groups this device has joined.
    link_multicast_groups: RefCountedHashSet<MulticastAddr<Mac>>,

    /// IPv4 ARP state.
    ipv4_arp: ArpState<EthernetLinkDevice, Ipv4Addr>,

    /// (IPv6) NDP state.
    ndp: ndp::NdpState<EthernetLinkDevice>,

    // pending_frames stores a list of serialized frames indexed by their
    // destination IP addresses. The frames contain an entire EthernetFrame
    // body and the MTU check is performed before queueing them here.
    pending_frames: HashMap<IpAddr, VecDeque<Buf<Vec<u8>>>>,

    /// A flag indicating whether the device will accept all ethernet frames
    /// that it receives, regardless of the ethernet frame's destination MAC
    /// address.
    promiscuous_mode: bool,
}

impl EthernetDeviceState {
    /// Adds a pending frame `frame` associated with `local_addr` to the list of
    /// pending frames in the current device state.
    ///
    /// If an older frame had to be dropped because it exceeds the maximum
    /// allowed number of pending frames, it is returned.
    fn add_pending_frame(
        &mut self,
        local_addr: IpAddr,
        frame: Buf<Vec<u8>>,
    ) -> Option<Buf<Vec<u8>>> {
        let buff = self.pending_frames.entry(local_addr).or_insert_with(Default::default);
        buff.push_back(frame);
        if buff.len() > ETHERNET_MAX_PENDING_FRAMES {
            buff.pop_front()
        } else {
            None
        }
    }

    /// Takes all pending frames associated with address `local_addr`.
    fn take_pending_frames(
        &mut self,
        local_addr: IpAddr,
    ) -> Option<impl Iterator<Item = Buf<Vec<u8>>>> {
        match self.pending_frames.remove(&local_addr) {
            Some(buff) => Some(buff.into_iter()),
            None => None,
        }
    }

    /// Is a packet with a destination MAC address, `dst`, destined for this
    /// device?
    ///
    /// Returns `true` if this device is has `dst_mac` as its assigned MAC
    /// address, `dst_mac` is the broadcast MAC address, or it is one of the
    /// multicast MAC addresses the device has joined.
    fn should_accept(&self, dst_mac: &Mac) -> bool {
        (self.mac.get() == *dst_mac)
            || dst_mac.is_broadcast()
            || (MulticastAddr::new(*dst_mac)
                .map(|a| self.link_multicast_groups.contains(&a))
                .unwrap_or(false))
    }

    /// Should a packet with destination MAC address, `dst`, be accepted by this
    /// device?
    ///
    /// Returns `true` if this device is in promiscuous mode or the frame is
    /// destined for this device.
    fn should_deliver(&self, dst_mac: &Mac) -> bool {
        self.promiscuous_mode || self.should_accept(dst_mac)
    }
}

/// A timer ID for Ethernet devices.
///
/// `D` is the type of device ID that identifies different Ethernet devices.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) enum EthernetTimerId<D> {
    Arp(ArpTimerId<EthernetLinkDevice, Ipv4Addr, D>),
    Ndp(NdpTimerId<EthernetLinkDevice, D>),
}

impl<D> From<ArpTimerId<EthernetLinkDevice, Ipv4Addr, D>> for EthernetTimerId<D> {
    fn from(id: ArpTimerId<EthernetLinkDevice, Ipv4Addr, D>) -> EthernetTimerId<D> {
        EthernetTimerId::Arp(id)
    }
}

impl<D> From<NdpTimerId<EthernetLinkDevice, D>> for EthernetTimerId<D> {
    fn from(id: NdpTimerId<EthernetLinkDevice, D>) -> EthernetTimerId<D> {
        EthernetTimerId::Ndp(id)
    }
}

/// Handle an Ethernet timer firing.
pub(super) fn handle_timer<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: EthernetTimerId<SC::DeviceId>,
) {
    match id {
        EthernetTimerId::Arp(id) => arp::handle_timer(sync_ctx, ctx, id.into()),
        EthernetTimerId::Ndp(id) => <SC as NdpHandler<_, _>>::handle_timer(sync_ctx, ctx, id),
    }
}

// If we are provided with an impl of `TimerContext<EthernetTimerId<_>>`, then
// we can in turn provide impls of `TimerContext` for ARP, NDP, IGMP, and MLD
// timers.
impl_timer_context!(
    DeviceId,
    EthernetTimerId<DeviceId>,
    ArpTimerId<EthernetLinkDevice, Ipv4Addr, DeviceId>,
    EthernetTimerId::Arp(id),
    id
);
impl_timer_context!(
    DeviceId,
    EthernetTimerId<DeviceId>,
    NdpTimerId<EthernetLinkDevice, DeviceId>,
    EthernetTimerId::Ndp(id),
    id
);

/// Send an IP packet in an Ethernet frame.
///
/// `send_ip_frame` accepts a device ID, a local IP address, and a
/// serializer. It computes the routing information, serializes
/// the serializer, and sends the resulting buffer in a new Ethernet
/// frame.
pub(super) fn send_ip_frame<
    B: BufferMut,
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>
        + FrameContext<C, B, <SC as DeviceIdContext<EthernetLinkDevice>>::DeviceId>,
    A: IpAddress,
    S: Serializer<Buffer = B>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    ctx.increment_counter("ethernet::send_ip_frame");

    trace!("ethernet::send_ip_frame: local_addr = {:?}; device = {:?}", local_addr, device_id);

    let state = &mut sync_ctx.get_state_mut_with(device_id).link;
    let (local_mac, mtu) = (state.mac, state.mtu);

    let dst_mac = match local_addr.into() {
        IpAddr::V4(local_addr) => match MulticastAddr::from_witness(local_addr) {
            Some(multicast) => Ok(Mac::from(&multicast)),
            None => arp::lookup(sync_ctx, ctx, device_id, local_mac.get(), local_addr.get())
                .ok_or(IpAddr::V4(local_addr)),
        },
        IpAddr::V6(local_addr) => match UnicastOrMulticastIpv6Addr::from_specified(local_addr) {
            UnicastOrMulticastIpv6Addr::Multicast(addr) => Ok(Mac::from(&addr)),
            UnicastOrMulticastIpv6Addr::Unicast(addr) => {
                <SC as NdpHandler<_, _>>::lookup(sync_ctx, ctx, device_id, addr)
                    .ok_or(IpAddr::V6(local_addr))
            }
        },
    };

    match dst_mac {
        Ok(dst_mac) => sync_ctx
            .send_frame(
                ctx,
                device_id.into(),
                body.with_mtu(mtu as usize).encapsulate(EthernetFrameBuilder::new(
                    local_mac.get(),
                    dst_mac,
                    A::Version::ETHER_TYPE,
                )),
            )
            .map_err(|ser| ser.into_inner().into_inner()),
        Err(local_addr) => {
            let state = &mut sync_ctx.get_state_mut_with(device_id).link;
            // The `serialize_vec_outer` call returns an `Either<B,
            // Buf<Vec<u8>>`. We could naively call `.as_ref().to_vec()` on it,
            // but if it were the `Buf<Vec<u8>>` variant, we'd be unnecessarily
            // allocating a new `Vec` when we already have one. Instead, we
            // leave the `Buf<Vec<u8>>` variant as it is, and only convert the
            // `B` variant by calling `map_a`. That gives us an
            // `Either<Buf<Vec<u8>>, Buf<Vec<u8>>`, which we call `into_inner`
            // on to get a `Buf<Vec<u8>>`.
            let frame = body
                .with_mtu(mtu as usize)
                .serialize_vec_outer()
                .map_err(|ser| ser.1.into_inner())?
                .map_a(|buffer| Buf::new(buffer.as_ref().to_vec(), ..))
                .into_inner();
            let dropped = state
                .add_pending_frame(local_addr.transpose::<SpecifiedAddr<IpAddr>>().get(), frame);
            if let Some(_dropped) = dropped {
                // TODO(brunodalbo): Is it ok to silently just let this drop? Or
                //  should the IP layer be notified in any way?
                log_unimplemented!((), "Ethernet dropped frame because ran out of allowable space");
            }
            Ok(())
        }
    }
}

/// Receive an Ethernet frame from the network.
pub(super) fn receive_frame<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    B: BufferMut,
    SC: BufferEthernetIpLinkDeviceContext<C, B>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    mut buffer: B,
) {
    trace!("ethernet::receive_frame: device_id = {:?}", device_id);
    // NOTE(joshlf): We do not currently validate that the Ethernet frame
    // satisfies the minimum length requirement. We expect that if this
    // requirement is necessary (due to requirements of the physical medium),
    // the driver or hardware will have checked it, and that if this requirement
    // is not necessary, it is acceptable for us to operate on a smaller
    // Ethernet frame. If this becomes insufficient in the future, we may want
    // to consider making this behavior configurable (at compile time, at
    // runtime on a global basis, or at runtime on a per-device basis).
    let frame = if let Ok(frame) =
        buffer.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::NoCheck)
    {
        frame
    } else {
        trace!("ethernet::receive_frame: failed to parse ethernet frame");
        // TODO(joshlf): Do something else?
        return;
    };

    let (_, dst) = (frame.src_mac(), frame.dst_mac());

    if !sync_ctx.get_state_with(device_id).link.should_deliver(&dst) {
        trace!("ethernet::receive_frame: destination mac {:?} not for device {:?}", dst, device_id);
        return;
    }

    let frame_dst = FrameDestination::from(dst);

    match frame.ethertype() {
        Some(EtherType::Arp) => {
            let types = if let Ok(types) = peek_arp_types(buffer.as_ref()) {
                types
            } else {
                // TODO(joshlf): Do something else here?
                return;
            };
            match types {
                (ArpHardwareType::Ethernet, ArpNetworkType::Ipv4) => {
                    arp::handle_packet(sync_ctx, ctx, device_id, buffer)
                }
            }
        }
        Some(EtherType::Ipv4) => sync_ctx.receive_frame(
            ctx,
            RecvIpFrameMeta::<_, Ipv4>::new(device_id, frame_dst),
            buffer,
        ),
        Some(EtherType::Ipv6) => sync_ctx.receive_frame(
            ctx,
            RecvIpFrameMeta::<_, Ipv6>::new(device_id, frame_dst),
            buffer,
        ),
        Some(EtherType::Other(_)) | None => {} // TODO(joshlf)
    }
}

/// Set the promiscuous mode flag on `device_id`.
pub(super) fn set_promiscuous_mode<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    device_id: SC::DeviceId,
    enabled: bool,
) {
    sync_ctx.get_state_mut_with(device_id).link.promiscuous_mode = enabled;
}

/// Add `device_id` to a link multicast group `multicast_addr`.
///
/// Calling `join_link_multicast` with the same `device_id` and `multicast_addr`
/// is completely safe. A counter will be kept for the number of times
/// `join_link_multicast` has been called with the same `device_id` and
/// `multicast_addr` pair. To completely leave a multicast group,
/// [`leave_link_multicast`] must be called the same number of times
/// `join_link_multicast` has been called for the same `device_id` and
/// `multicast_addr` pair. The first time `join_link_multicast` is called for a
/// new `device` and `multicast_addr` pair, the device will actually join the
/// multicast group.
///
/// `join_link_multicast` is different from [`join_ip_multicast`] as
/// `join_link_multicast` joins an L2 multicast group, whereas
/// `join_ip_multicast` joins an L3 multicast group.
pub(super) fn join_link_multicast<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    device_id: SC::DeviceId,
    multicast_addr: MulticastAddr<Mac>,
) {
    let device_state = &mut sync_ctx.get_state_mut_with(device_id).link;

    let groups = &mut device_state.link_multicast_groups;

    match groups.insert(multicast_addr) {
        InsertResult::Inserted(()) => {
            trace!("ethernet::join_link_multicast: joining link multicast {:?}", multicast_addr);
        }
        InsertResult::AlreadyPresent => {
            trace!(
                "ethernet::join_link_multicast: already joined link multicast {:?}",
                multicast_addr,
            );
        }
    }
}

/// Remove `device_id` from a link multicast group `multicast_addr`.
///
/// `leave_link_multicast` will attempt to remove `device_id` from the multicast
/// group `multicast_addr`. `device_id` may have "joined" the same multicast
/// address multiple times, so `device_id` will only leave the multicast group
/// once `leave_ip_multicast` has been called for each corresponding
/// [`join_link_multicast`]. That is, if `join_link_multicast` gets called 3
/// times and `leave_link_multicast` gets called two times (after all 3
/// `join_link_multicast` calls), `device_id` will still be in the multicast
/// group until the next (final) call to `leave_link_multicast`.
///
/// `leave_link_multicast` is different from [`leave_ip_multicast`] as
/// `leave_link_multicast` leaves an L2 multicast group, whereas
/// `leave_ip_multicast` leaves an L3 multicast group.
///
/// # Panics
///
/// If `device_id` is not in the multicast group `multicast_addr`.
pub(super) fn leave_link_multicast<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    device_id: SC::DeviceId,
    multicast_addr: MulticastAddr<Mac>,
) {
    let device_state = &mut sync_ctx.get_state_mut_with(device_id).link;

    let groups = &mut device_state.link_multicast_groups;

    match groups.remove(multicast_addr) {
        RemoveResult::Removed(()) => {
            trace!("ethernet::leave_link_multicast: leaving link multicast {:?}", multicast_addr);
        }
        RemoveResult::StillPresent => {
            trace!(
                "ethernet::leave_link_multicast: not leaving link multicast {:?} as there are still listeners for it",
                multicast_addr,
            );
        }
        RemoveResult::NotPresent => {
            panic!(
                "ethernet::leave_link_multicast: device {:?} has not yet joined link multicast {:?}",
                device_id,
                multicast_addr,
            );
        }
    }
}

/// Get the MTU associated with this device.
pub(super) fn get_mtu<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &SC,
    device_id: SC::DeviceId,
) -> u32 {
    sync_ctx.get_state_with(device_id).link.mtu
}

/// Insert a static entry into this device's ARP table.
///
/// This will cause any conflicting dynamic entry to be removed, and
/// any future conflicting gratuitous ARPs to be ignored.
// TODO(rheacock): remove `cfg(test)` when this is used. Will probably be called
// by a pub fn in the device mod.
#[cfg(test)]
pub(super) fn insert_static_arp_table_entry<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    addr: Ipv4Addr,
    mac: Mac,
) {
    arp::insert_static_neighbor(sync_ctx, ctx, device_id, addr, mac)
}

/// Insert an entry into this device's NDP table.
///
/// This method only gets called when testing to force set a neighbor's link
/// address so that lookups succeed immediately, without doing address
/// resolution.
// TODO(rheacock): Remove when this is called from non-test code.
#[cfg(test)]
pub(super) fn insert_ndp_table_entry<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    addr: UnicastAddr<Ipv6Addr>,
    mac: Mac,
) {
    <SC as NdpHandler<_, _>>::insert_static_neighbor(sync_ctx, ctx, device_id, addr, mac)
}

/// Deinitializes and cleans up state for ethernet devices
///
/// After this function is called, the ethernet device should not be used and
/// nothing else should be done with the state.
pub(super) fn deinitialize<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
) {
    arp::deinitialize(sync_ctx, ctx, device_id);
    <SC as NdpHandler<_, _>>::deinitialize(sync_ctx, ctx, device_id);
}

impl<C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>, SC: EthernetIpLinkDeviceContext<C>>
    StateContext<C, ArpState<EthernetLinkDevice, Ipv4Addr>, SC::DeviceId> for SC
{
    fn get_state_with(&self, id: SC::DeviceId) -> &ArpState<EthernetLinkDevice, Ipv4Addr> {
        &self.get_state_with(id).link.ipv4_arp
    }

    fn get_state_mut_with(
        &mut self,
        id: SC::DeviceId,
    ) -> &mut ArpState<EthernetLinkDevice, Ipv4Addr> {
        &mut self.get_state_mut_with(id).link.ipv4_arp
    }
}

impl<
        C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
        B: BufferMut,
        SC: EthernetIpLinkDeviceContext<C>
            + FrameContext<C, B, <SC as DeviceIdContext<EthernetLinkDevice>>::DeviceId>,
    > FrameContext<C, B, ArpFrameMetadata<EthernetLinkDevice, SC::DeviceId>> for SC
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        meta: ArpFrameMetadata<EthernetLinkDevice, SC::DeviceId>,
        body: S,
    ) -> Result<(), S> {
        let src = self.get_state_with(meta.device_id).link.mac;
        self.send_frame(
            ctx,
            meta.device_id,
            body.encapsulate(EthernetFrameBuilder::new(src.get(), meta.dst_addr, EtherType::Arp)),
        )
        .map_err(Nested::into_inner)
    }
}

impl<C: DeviceIdContext<EthernetLinkDevice>> ArpDeviceIdContext<EthernetLinkDevice> for C {
    type DeviceId = <C as DeviceIdContext<EthernetLinkDevice>>::DeviceId;
}

impl<C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>, SC: EthernetIpLinkDeviceContext<C>>
    ArpContext<EthernetLinkDevice, Ipv4Addr, C> for SC
{
    fn get_protocol_addr(
        &self,
        _ctx: &mut C,
        device_id: <SC as ArpDeviceIdContext<EthernetLinkDevice>>::DeviceId,
    ) -> Option<Ipv4Addr> {
        self.get_state_with(device_id.into())
            .ip
            .ipv4
            .ip_state
            .iter_addrs()
            .next()
            .cloned()
            .map(|addr| addr.addr().get())
    }
    fn get_hardware_addr(
        &self,
        _ctx: &mut C,
        device_id: <SC as ArpDeviceIdContext<EthernetLinkDevice>>::DeviceId,
    ) -> UnicastAddr<Mac> {
        self.get_state_with(device_id.into()).link.mac
    }
    fn address_resolved(
        &mut self,
        ctx: &mut C,
        device_id: <SC as ArpDeviceIdContext<EthernetLinkDevice>>::DeviceId,
        proto_addr: Ipv4Addr,
        hw_addr: Mac,
    ) {
        mac_resolved(self, ctx, device_id.into(), IpAddr::V4(proto_addr), hw_addr);
    }
    fn address_resolution_failed(
        &mut self,
        ctx: &mut C,
        device_id: <SC as ArpDeviceIdContext<EthernetLinkDevice>>::DeviceId,
        proto_addr: Ipv4Addr,
    ) {
        mac_resolution_failed(self, ctx, device_id.into(), IpAddr::V4(proto_addr));
    }
    fn address_resolution_expired(
        &mut self,
        _ctx: &mut C,
        _device_id: <SC as ArpDeviceIdContext<EthernetLinkDevice>>::DeviceId,
        _proto_addr: Ipv4Addr,
    ) {
        log_unimplemented!((), "ArpContext::address_resolution_expired");
    }
}

impl<C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>, SC: EthernetIpLinkDeviceContext<C>>
    StateContext<C, NdpState<EthernetLinkDevice>, SC::DeviceId> for SC
{
    fn get_state_with(&self, id: SC::DeviceId) -> &NdpState<EthernetLinkDevice> {
        &self.get_state_with(id).link.ndp
    }

    fn get_state_mut_with(&mut self, id: SC::DeviceId) -> &mut NdpState<EthernetLinkDevice> {
        &mut self.get_state_mut_with(id).link.ndp
    }
}

pub(super) fn get_mac<
    'a,
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &'a SC,
    device_id: SC::DeviceId,
) -> &'a UnicastAddr<Mac> {
    &sync_ctx.get_state_with(device_id).link.mac
}

impl<C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>, SC: EthernetIpLinkDeviceContext<C>>
    NdpContext<EthernetLinkDevice, C> for SC
{
    fn get_retrans_timer(&self, device_id: Self::DeviceId) -> NonZeroDuration {
        self.get_state_with(device_id).ip.ipv6.retrans_timer
    }

    fn get_link_layer_addr(&self, device_id: SC::DeviceId) -> UnicastAddr<Mac> {
        get_mac(self, device_id).clone()
    }

    fn get_ip_device_state(&self, device_id: Self::DeviceId) -> &IpDeviceState<C::Instant, Ipv6> {
        &self.get_state_with(device_id).ip.ipv6.ip_state
    }

    fn get_ip_device_state_mut(
        &mut self,
        device_id: Self::DeviceId,
    ) -> &mut IpDeviceState<C::Instant, Ipv6> {
        &mut self.get_state_mut_with(device_id).ip.ipv6.ip_state
    }

    fn send_ipv6_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        next_hop: SpecifiedAddr<Ipv6Addr>,
        body: S,
    ) -> Result<(), S> {
        // TODO(joshlf): Wire `SpecifiedAddr` through the `ndp` module.
        send_ip_frame(self, ctx, device_id, next_hop, body)
    }

    fn address_resolved(
        &mut self,
        ctx: &mut C,
        device_id: SC::DeviceId,
        address: &UnicastAddr<Ipv6Addr>,
        link_address: Mac,
    ) {
        mac_resolved(self, ctx, device_id, IpAddr::V6(address.get()), link_address);
    }

    fn address_resolution_failed(
        &mut self,
        ctx: &mut C,
        device_id: SC::DeviceId,
        address: &UnicastAddr<Ipv6Addr>,
    ) {
        mac_resolution_failed(self, ctx, device_id, IpAddr::V6(address.get()));
    }

    fn set_mtu(&mut self, _ctx: &mut C, device_id: SC::DeviceId, mut mtu: u32) {
        // TODO(ghanan): Should this new MTU be updated only from the netstack's
        //               perspective or be exposed to the device hardware?

        // `mtu` must not be less than the minimum IPv6 MTU.
        assert!(mtu >= Ipv6::MINIMUM_LINK_MTU.into());

        let dev_state = &mut self.get_state_mut_with(device_id).link;

        // If `mtu` is greater than what the device supports, set `mtu` to the
        // maximum MTU the device supports.
        if mtu > dev_state.hw_mtu {
            trace!("ethernet::ndp_device::set_mtu: MTU of {:?} is greater than the device {:?}'s max MTU of {:?}, using device's max MTU instead", mtu, device_id, dev_state.hw_mtu);
            mtu = dev_state.hw_mtu;
        }

        trace!("ethernet::ndp_device::set_mtu: setting link MTU to {:?}", mtu);
        dev_state.mtu = mtu;
    }
}

/// An implementation of the [`LinkDevice`] trait for Ethernet devices.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub(crate) struct EthernetLinkDevice;

impl LinkDevice for EthernetLinkDevice {
    type Address = Mac;
    type State = EthernetDeviceState;
}

/// Sends out any pending frames that are waiting for link layer address
/// resolution.
///
/// `mac_resolved` is the common logic used when a link layer address is
/// resolved either by ARP or NDP.
fn mac_resolved<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    address: IpAddr,
    dst_mac: Mac,
) {
    let state = &mut sync_ctx.get_state_mut_with(device_id).link;
    let src_mac = state.mac;
    let ether_type = match &address {
        IpAddr::V4(_) => EtherType::Ipv4,
        IpAddr::V6(_) => EtherType::Ipv6,
    };
    if let Some(pending) = state.take_pending_frames(address) {
        for frame in pending {
            // NOTE(brunodalbo): We already performed MTU checking when we saved
            //  the buffer waiting for address resolution. It should be noted
            //  that the MTU check back then didn't account for ethernet frame
            //  padding required by EthernetFrameBuilder, but that's fine (as it
            //  stands right now) because the MTU is guaranteed to be larger
            //  than an Ethernet minimum frame body size.
            let res = sync_ctx.send_frame(
                ctx,
                device_id.into(),
                frame.encapsulate(EthernetFrameBuilder::new(src_mac.get(), dst_mac, ether_type)),
            );
            if let Err(_) = res {
                // TODO(joshlf): Do we want to handle this differently?
                debug!("Failed to send pending frame; MTU changed since frame was queued");
            }
        }
    }
}

/// Clears out any pending frames that are waiting for link layer address
/// resolution.
///
/// `mac_resolution_failed` is the common logic used when a link layer address
/// fails to resolve either by ARP or NDP.
fn mac_resolution_failed<
    C: EthernetIpLinkDeviceNonSyncContext<SC::DeviceId>,
    SC: EthernetIpLinkDeviceContext<C>,
>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    device_id: SC::DeviceId,
    address: IpAddr,
) {
    // TODO(brunodalbo) what do we do here in regards to the pending frames?
    //  NDP's RFC explicitly states unreachable ICMP messages must be generated:
    //  "If no Neighbor Advertisement is received after MAX_MULTICAST_SOLICIT
    //  solicitations, address resolution has failed. The sender MUST return
    //  ICMP destination unreachable indications with code 3
    //  (Address Unreachable) for each packet queued awaiting address
    //  resolution."
    //  For ARP, we don't have such a clear statement on the RFC, it would make
    //  sense to do the same thing though.
    let state = &mut sync_ctx.get_state_mut_with(device_id).link;
    if let Some(_) = state.take_pending_frames(address) {
        log_unimplemented!((), "ethernet mac resolution failed not implemented");
    }
}

#[cfg(test)]
mod tests {
    use alloc::vec;

    use assert_matches::assert_matches;
    use net_types::ip::IpVersion;
    use packet::Buf;
    use packet_formats::{
        icmp::{IcmpDestUnreachable, IcmpIpExt},
        ip::{IpExt, IpPacketBuilder, IpProto},
        testdata::{dns_request_v4, dns_request_v6},
        testutil::{
            parse_icmp_packet_in_ip_packet_in_ethernet_frame, parse_ip_packet_in_ethernet_frame,
        },
    };
    use rand::Rng;
    use specialize_ip_macro::ip_test;
    use test_case::test_case;

    use super::*;
    use crate::{
        context::testutil::DummyInstant,
        device::{arp::ArpHandler, DeviceId, DeviceIdInner, EthernetDeviceId, IpLinkDeviceState},
        error::NotFoundError,
        ip::{
            device::{is_ip_routing_enabled, set_routing_enabled, state::AssignedAddress},
            dispatch_receive_ip_packet_name, receive_ip_packet,
            testutil::is_in_ip_multicast,
            DummyDeviceId,
        },
        testutil::{
            add_arp_or_ndp_table_entry, assert_empty, get_counter_val, new_rng,
            DummyEventDispatcherBuilder, TestIpExt, DUMMY_CONFIG_V4,
        },
        Ctx,
    };

    struct DummyEthernetCtx {
        state: IpLinkDeviceState<DummyInstant, EthernetDeviceState>,
    }

    impl DummyEthernetCtx {
        fn new(mac: UnicastAddr<Mac>, mtu: u32) -> DummyEthernetCtx {
            DummyEthernetCtx {
                state: IpLinkDeviceState::new(EthernetDeviceStateBuilder::new(mac, mtu).build()),
            }
        }
    }

    type DummyNonSyncCtx =
        crate::context::testutil::DummyNonSyncCtx<EthernetTimerId<DummyDeviceId>, (), ()>;

    type DummyCtx =
        crate::context::testutil::DummySyncCtx<DummyEthernetCtx, DummyDeviceId, DummyDeviceId>;

    impl
        StateContext<
            DummyNonSyncCtx,
            IpLinkDeviceState<DummyInstant, EthernetDeviceState>,
            DummyDeviceId,
        > for DummyCtx
    {
        fn get_state_with(
            &self,
            _id0: DummyDeviceId,
        ) -> &IpLinkDeviceState<DummyInstant, EthernetDeviceState> {
            &self.get_ref().state
        }

        fn get_state_mut_with(
            &mut self,
            _id0: DummyDeviceId,
        ) -> &mut IpLinkDeviceState<DummyInstant, EthernetDeviceState> {
            &mut self.get_mut().state
        }
    }

    impl EthernetIpLinkDeviceContext<DummyNonSyncCtx> for DummyCtx {
        fn add_ipv6_addr_subnet(
            &mut self,
            _ctx: &mut DummyNonSyncCtx,
            _device_id: DummyDeviceId,
            _addr_sub: AddrSubnet<Ipv6Addr>,
            _config: AddrConfig<DummyInstant>,
        ) -> Result<(), ExistsError> {
            unimplemented!()
        }

        fn join_ipv6_multicast(
            &mut self,
            _ctx: &mut DummyNonSyncCtx,
            _device_id: DummyDeviceId,
            _multicast_addr: MulticastAddr<Ipv6Addr>,
        ) {
            unimplemented!()
        }

        fn leave_ipv6_multicast(
            &mut self,
            _ctx: &mut DummyNonSyncCtx,
            _device_id: DummyDeviceId,
            _multicast_addr: MulticastAddr<Ipv6Addr>,
        ) {
            unimplemented!()
        }
    }

    impl DeviceIdContext<EthernetLinkDevice> for DummyCtx {
        type DeviceId = DummyDeviceId;
    }

    impl IpLinkDeviceContext<EthernetLinkDevice, DummyNonSyncCtx, EthernetTimerId<DummyDeviceId>>
        for DummyCtx
    {
    }

    fn contains_addr<A: IpAddress>(
        sync_ctx: &crate::testutil::DummySyncCtx,
        device: DeviceId,
        addr: SpecifiedAddr<A>,
    ) -> bool {
        match addr.into() {
            IpAddr::V4(addr) => {
                crate::ip::device::IpDeviceContext::<Ipv4, _>::with_ip_device_state(
                    sync_ctx,
                    device,
                    |state| state.ip_state.iter_addrs().any(|a| a.addr() == addr),
                )
            }
            IpAddr::V6(addr) => {
                crate::ip::device::IpDeviceContext::<Ipv6, _>::with_ip_device_state(
                    sync_ctx,
                    device,
                    |state| state.ip_state.iter_addrs().any(|a| a.addr() == addr),
                )
            }
        }
    }

    #[test]
    fn test_mtu() {
        // Test that we send an Ethernet frame whose size is less than the MTU,
        // and that we don't send an Ethernet frame whose size is greater than
        // the MTU.
        fn test(size: usize, expect_frames_sent: usize) {
            let crate::context::testutil::DummyCtx { mut sync_ctx, mut non_sync_ctx } =
                crate::context::testutil::DummyCtx::with_sync_ctx(DummyCtx::with_state(
                    DummyEthernetCtx::new(DUMMY_CONFIG_V4.local_mac, Ipv6::MINIMUM_LINK_MTU.into()),
                ));
            <DummyCtx as ArpHandler<_, _, _>>::insert_static_neighbor(
                &mut sync_ctx,
                &mut non_sync_ctx,
                DummyDeviceId,
                DUMMY_CONFIG_V4.remote_ip.get(),
                DUMMY_CONFIG_V4.remote_mac.get(),
            );
            let _ = send_ip_frame(
                &mut sync_ctx,
                &mut non_sync_ctx,
                DummyDeviceId,
                DUMMY_CONFIG_V4.remote_ip,
                Buf::new(&mut vec![0; size], ..),
            );
            assert_eq!(sync_ctx.frames().len(), expect_frames_sent);
        }

        test(Ipv6::MINIMUM_LINK_MTU.into(), 1);
        test(usize::from(Ipv6::MINIMUM_LINK_MTU) + 1, 0);
    }

    #[test]
    fn test_pending_frames() {
        let mut state = EthernetDeviceStateBuilder::new(
            DUMMY_CONFIG_V4.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        )
        .build();
        let ip = IpAddr::V4(DUMMY_CONFIG_V4.local_ip.get());
        assert_matches!(state.add_pending_frame(ip, Buf::new(vec![1], ..)), None);
        assert_matches!(state.add_pending_frame(ip, Buf::new(vec![2], ..)), None);
        assert_matches!(state.add_pending_frame(ip, Buf::new(vec![3], ..)), None);

        // Check that we're accumulating correctly...
        assert_eq!(3, state.take_pending_frames(ip).unwrap().count());
        // ...and that take_pending_frames clears all the buffered data.
        assert!(state.take_pending_frames(ip).is_none());

        for i in 0..ETHERNET_MAX_PENDING_FRAMES {
            assert!(state.add_pending_frame(ip, Buf::new(vec![i as u8], ..)).is_none());
        }
        // Check that adding more than capacity will drop the older buffers as
        // a proper FIFO queue.
        assert_eq!(0, state.add_pending_frame(ip, Buf::new(vec![255], ..)).unwrap().as_ref()[0]);
        assert_eq!(1, state.add_pending_frame(ip, Buf::new(vec![255], ..)).unwrap().as_ref()[0]);
        assert_eq!(2, state.add_pending_frame(ip, Buf::new(vec![255], ..)).unwrap().as_ref()[0]);
    }

    #[ip_test]
    #[test_case(true; "enabled")]
    #[test_case(false; "disabled")]
    fn test_receive_ip_frame<I: Ip + TestIpExt>(enable: bool) {
        // Should only receive a frame if the device is enabled.

        let config = I::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );

        let mut bytes = match I::VERSION {
            IpVersion::V4 => dns_request_v4::ETHERNET_FRAME,
            IpVersion::V6 => dns_request_v6::ETHERNET_FRAME,
        }
        .bytes
        .to_vec();

        let mac_bytes = config.local_mac.bytes();
        bytes[0..6].copy_from_slice(&mac_bytes);

        let expected_received = if enable {
            crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);
            1
        } else {
            0
        };

        crate::device::receive_frame(&mut sync_ctx, &mut non_sync_ctx, device, Buf::new(bytes, ..))
            .expect("error receiving frame");

        let counter = match I::VERSION {
            IpVersion::V4 => "receive_ipv4_packet",
            IpVersion::V6 => "receive_ipv6_packet",
        };
        assert_eq!(get_counter_val(&non_sync_ctx, counter), expected_received);
    }

    #[ip_test]
    #[test_case(true; "enabled")]
    #[test_case(false; "disabled")]
    fn test_send_ip_frame<I: Ip + TestIpExt>(enable: bool) {
        // Should only send a frame if the device is enabled.

        let config = I::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = crate::testutil::DummyCtx::default();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );

        let expected_sent = if enable {
            crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);
            1
        } else {
            0
        };

        match I::VERSION {
            IpVersion::V4 => {
                let addr = SpecifiedAddr::new(dns_request_v4::IPV4_PACKET.metadata.dst_ip).unwrap();
                crate::device::insert_static_arp_table_entry(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    device,
                    addr.get(),
                    config.remote_mac,
                )
                .expect("insert static ARP entry");

                crate::ip::device::send_ip_frame::<Ipv4, _, _, _, _>(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    device,
                    addr,
                    Buf::new(dns_request_v4::IPV4_PACKET.bytes.to_vec(), ..),
                )
                .expect("error sending IPv4 frame")
            }

            IpVersion::V6 => {
                let addr = UnicastAddr::new(dns_request_v6::IPV6_PACKET.metadata.dst_ip).unwrap();
                crate::device::insert_ndp_table_entry(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    device,
                    addr,
                    config.remote_mac.get(),
                )
                .expect("insert static NDP entry");
                crate::ip::device::send_ip_frame::<Ipv6, _, _, _, _>(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    device,
                    addr.into_specified(),
                    Buf::new(dns_request_v6::IPV6_PACKET.bytes.to_vec(), ..),
                )
                .expect("error sending IPv6 frame")
            }
        }

        assert_eq!(get_counter_val(&non_sync_ctx, "ethernet::send_ip_frame"), expected_sent);
    }

    #[test]
    fn initialize_once() {
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DUMMY_CONFIG_V4.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);
    }

    fn is_routing_enabled<I: Ip>(
        sync_ctx: &crate::testutil::DummySyncCtx,
        device: DeviceId,
    ) -> bool {
        match I::VERSION {
            IpVersion::V4 => is_ip_routing_enabled::<Ipv4, _, _>(sync_ctx, device),
            IpVersion::V6 => is_ip_routing_enabled::<Ipv6, _, _>(sync_ctx, device),
        }
    }

    #[ip_test]
    fn test_set_ip_routing<I: Ip + TestIpExt + IcmpIpExt + IpExt>() {
        fn check_other_is_routing_enabled<I: Ip>(
            sync_ctx: &crate::testutil::DummySyncCtx,
            device: DeviceId,
            expected: bool,
        ) {
            let enabled = match I::VERSION {
                IpVersion::V4 => is_routing_enabled::<Ipv6>(sync_ctx, device),
                IpVersion::V6 => is_routing_enabled::<Ipv4>(sync_ctx, device),
            };

            assert_eq!(enabled, expected);
        }

        fn check_icmp<I: Ip>(buf: &[u8]) {
            match I::VERSION {
                IpVersion::V4 => {
                    let _ = parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                        Ipv4,
                        _,
                        IcmpDestUnreachable,
                        _,
                    >(buf, |_| {})
                    .unwrap();
                }
                IpVersion::V6 => {
                    let _ = parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                        Ipv6,
                        _,
                        IcmpDestUnreachable,
                        _,
                    >(buf, |_| {})
                    .unwrap();
                }
            }
        }

        let src_ip = I::get_other_ip_address(3);
        let src_mac = UnicastAddr::new(Mac::new([10, 11, 12, 13, 14, 15])).unwrap();
        let config = I::DUMMY_CONFIG;
        let device = DeviceId::new_ethernet(0);
        let frame_dst = FrameDestination::Unicast;
        let mut rng = new_rng(70812476915813);
        let mut body: Vec<u8> = core::iter::repeat_with(|| rng.gen()).take(100).collect();
        let buf = Buf::new(&mut body[..], ..)
            .encapsulate(I::PacketBuilder::new(
                src_ip.get(),
                config.remote_ip.get(),
                64,
                IpProto::Tcp.into(),
            ))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .unwrap_b();

        // Test with netstack no forwarding

        let mut builder = DummyEventDispatcherBuilder::from_config(config.clone());
        let device_builder_id = 0;
        add_arp_or_ndp_table_entry(&mut builder, device_builder_id, src_ip.get(), src_mac);
        let Ctx { mut sync_ctx, mut non_sync_ctx } = builder.build();

        // Should not be a router (default).
        assert!(!is_routing_enabled::<I>(&sync_ctx, device));
        check_other_is_routing_enabled::<I>(&sync_ctx, device, false);

        // Receiving a packet not destined for the node should only result in a
        // dest unreachable message if routing is enabled.
        receive_ip_packet::<_, _, I>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            frame_dst,
            buf.clone(),
        );
        assert_empty(non_sync_ctx.frames_sent().iter());

        // Set routing and expect packets to be forwarded.
        set_routing_enabled::<_, _, I>(&mut sync_ctx, &mut non_sync_ctx, device, true)
            .expect("error setting routing enabled");
        assert!(is_routing_enabled::<I>(&sync_ctx, device));
        // Should not update other Ip routing status.
        check_other_is_routing_enabled::<I>(&sync_ctx, device, false);

        // Should route the packet since routing fully enabled (netstack &
        // device).
        receive_ip_packet::<_, _, I>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            frame_dst,
            buf.clone(),
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        let (packet_buf, _, _, packet_src_ip, packet_dst_ip, proto, ttl) =
            parse_ip_packet_in_ethernet_frame::<I>(&non_sync_ctx.frames_sent()[0].1[..]).unwrap();
        assert_eq!(src_ip.get(), packet_src_ip);
        assert_eq!(config.remote_ip.get(), packet_dst_ip);
        assert_eq!(proto, IpProto::Tcp.into());
        assert_eq!(body, packet_buf);
        assert_eq!(ttl, 63);

        // Test routing a packet to an unknown address.
        let buf_unknown_dest = Buf::new(&mut body[..], ..)
            .encapsulate(I::PacketBuilder::new(
                src_ip.get(),
                // Addr must be remote, otherwise this will cause an NDP/ARP
                // request rather than ICMP unreachable.
                I::get_other_remote_ip_address(10).get(),
                64,
                IpProto::Tcp.into(),
            ))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .unwrap_b();
        receive_ip_packet::<_, _, I>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            frame_dst,
            buf_unknown_dest,
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);
        check_icmp::<I>(&non_sync_ctx.frames_sent()[1].1);

        // Attempt to unset router
        set_routing_enabled::<_, _, I>(&mut sync_ctx, &mut non_sync_ctx, device, false)
            .expect("error setting routing enabled");
        assert!(!is_routing_enabled::<I>(&sync_ctx, device));
        check_other_is_routing_enabled::<I>(&sync_ctx, device, false);

        // Should not route packets anymore
        receive_ip_packet::<_, _, I>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            frame_dst,
            buf.clone(),
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);
    }

    #[ip_test]
    fn test_promiscuous_mode<I: Ip + TestIpExt + IpExt>() {
        // Test that frames not destined for a device will still be accepted
        // when the device is put into promiscuous mode. In all cases, frames
        // that are destined for a device must always be accepted.

        let config = I::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(config.clone()).build();
        let device = DeviceId::new_ethernet(0);
        let other_mac = Mac::new([13, 14, 15, 16, 17, 18]);

        let buf = Buf::new(Vec::new(), ..)
            .encapsulate(I::PacketBuilder::new(
                config.remote_ip.get(),
                config.local_ip.get(),
                64,
                IpProto::Tcp.into(),
            ))
            .encapsulate(EthernetFrameBuilder::new(
                config.remote_mac.get(),
                config.local_mac.get(),
                I::ETHER_TYPE,
            ))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .unwrap_b();

        // Accept packet destined for this device if promiscuous mode is off.
        crate::device::set_promiscuous_mode(&mut sync_ctx, &mut non_sync_ctx, device, false)
            .expect("error setting promiscuous mode");
        crate::device::receive_frame(&mut sync_ctx, &mut non_sync_ctx, device, buf.clone())
            .expect("error receiving frame");
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 1);

        // Accept packet destined for this device if promiscuous mode is on.
        crate::device::set_promiscuous_mode(&mut sync_ctx, &mut non_sync_ctx, device, true)
            .expect("error setting promiscuous mode");
        crate::device::receive_frame(&mut sync_ctx, &mut non_sync_ctx, device, buf.clone())
            .expect("error receiving frame");
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 2);

        let buf = Buf::new(Vec::new(), ..)
            .encapsulate(I::PacketBuilder::new(
                config.remote_ip.get(),
                config.local_ip.get(),
                64,
                IpProto::Tcp.into(),
            ))
            .encapsulate(EthernetFrameBuilder::new(
                config.remote_mac.get(),
                other_mac,
                I::ETHER_TYPE,
            ))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .unwrap_b();

        // Reject packet not destined for this device if promiscuous mode is
        // off.
        crate::device::set_promiscuous_mode(&mut sync_ctx, &mut non_sync_ctx, device, false)
            .expect("error setting promiscuous mode");
        crate::device::receive_frame(&mut sync_ctx, &mut non_sync_ctx, device, buf.clone())
            .expect("error receiving frame");
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 2);

        // Accept packet not destined for this device if promiscuous mode is on.
        crate::device::set_promiscuous_mode(&mut sync_ctx, &mut non_sync_ctx, device, true)
            .expect("error setting promiscuous mode");
        crate::device::receive_frame(&mut sync_ctx, &mut non_sync_ctx, device, buf.clone())
            .expect("error receiving frame");
        assert_eq!(get_counter_val(&non_sync_ctx, dispatch_receive_ip_packet_name::<I>()), 3);
    }

    #[ip_test]
    fn test_add_remove_ip_addresses<I: Ip + TestIpExt>() {
        let config = I::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        let ip1 = I::get_other_ip_address(1);
        let ip2 = I::get_other_ip_address(2);
        let ip3 = I::get_other_ip_address(3);

        let prefix = I::Addr::BYTES * 8;
        let as1 = AddrSubnet::new(ip1.get(), prefix).unwrap();
        let as2 = AddrSubnet::new(ip2.get(), prefix).unwrap();

        assert!(!contains_addr(&sync_ctx, device, ip1));
        assert!(!contains_addr(&sync_ctx, device, ip2));
        assert!(!contains_addr(&sync_ctx, device, ip3));

        // Add ip1 (ok)
        crate::device::add_ip_addr_subnet(&mut sync_ctx, &mut non_sync_ctx, device, as1).unwrap();
        assert!(contains_addr(&sync_ctx, device, ip1));
        assert!(!contains_addr(&sync_ctx, device, ip2));
        assert!(!contains_addr(&sync_ctx, device, ip3));

        // Add ip2 (ok)
        crate::device::add_ip_addr_subnet(&mut sync_ctx, &mut non_sync_ctx, device, as2).unwrap();
        assert!(contains_addr(&sync_ctx, device, ip1));
        assert!(contains_addr(&sync_ctx, device, ip2));
        assert!(!contains_addr(&sync_ctx, device, ip3));

        // Del ip1 (ok)
        crate::device::del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, device, &ip1).unwrap();
        assert!(!contains_addr(&sync_ctx, device, ip1));
        assert!(contains_addr(&sync_ctx, device, ip2));
        assert!(!contains_addr(&sync_ctx, device, ip3));

        // Del ip1 again (ip1 not found)
        assert_eq!(
            crate::device::del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, device, &ip1),
            Err(NotFoundError)
        );
        assert!(!contains_addr(&sync_ctx, device, ip1));
        assert!(contains_addr(&sync_ctx, device, ip2));
        assert!(!contains_addr(&sync_ctx, device, ip3));

        // Add ip2 again (ip2 already exists)
        assert_eq!(
            crate::device::add_ip_addr_subnet(&mut sync_ctx, &mut non_sync_ctx, device, as2)
                .unwrap_err(),
            ExistsError,
        );
        assert!(!contains_addr(&sync_ctx, device, ip1));
        assert!(contains_addr(&sync_ctx, device, ip2));
        assert!(!contains_addr(&sync_ctx, device, ip3));

        // Add ip2 with different subnet (ip2 already exists)
        assert_eq!(
            crate::device::add_ip_addr_subnet(
                &mut sync_ctx,
                &mut non_sync_ctx,
                device,
                AddrSubnet::new(ip2.get(), prefix - 1).unwrap()
            )
            .unwrap_err(),
            ExistsError,
        );
        assert!(!contains_addr(&sync_ctx, device, ip1));
        assert!(contains_addr(&sync_ctx, device, ip2));
        assert!(!contains_addr(&sync_ctx, device, ip3));
    }

    fn receive_simple_ip_packet_test<A: IpAddress>(
        sync_ctx: &mut crate::testutil::DummySyncCtx,
        non_sync_ctx: &mut crate::testutil::DummyNonSyncCtx,
        device: DeviceId,
        src_ip: A,
        dst_ip: A,
        expected: usize,
    ) {
        let buf = Buf::new(Vec::new(), ..)
            .encapsulate(<A::Version as IpExt>::PacketBuilder::new(
                src_ip,
                dst_ip,
                64,
                IpProto::Tcp.into(),
            ))
            .serialize_vec_outer()
            .ok()
            .unwrap()
            .into_inner();

        receive_ip_packet::<_, _, A::Version>(
            sync_ctx,
            non_sync_ctx,
            device,
            FrameDestination::Unicast,
            buf,
        );
        assert_eq!(
            get_counter_val(non_sync_ctx, dispatch_receive_ip_packet_name::<A::Version>()),
            expected
        );
    }

    #[ip_test]
    fn test_multiple_ip_addresses<I: Ip + TestIpExt>() {
        let config = I::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        let ip1 = I::get_other_ip_address(1);
        let ip2 = I::get_other_ip_address(2);
        let from_ip = I::get_other_ip_address(3).get();

        assert!(!contains_addr(&sync_ctx, device, ip1));
        assert!(!contains_addr(&sync_ctx, device, ip2));

        // Should not receive packets on any IP.
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip1.get(),
            0,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip2.get(),
            0,
        );

        // Add ip1 to device.
        crate::device::add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            AddrSubnet::new(ip1.get(), I::Addr::BYTES * 8).unwrap(),
        )
        .unwrap();
        assert!(contains_addr(&sync_ctx, device, ip1));
        assert!(!contains_addr(&sync_ctx, device, ip2));

        // Should receive packets on ip1 but not ip2
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip1.get(),
            1,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip2.get(),
            1,
        );

        // Add ip2 to device.
        crate::device::add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            AddrSubnet::new(ip2.get(), I::Addr::BYTES * 8).unwrap(),
        )
        .unwrap();
        assert!(contains_addr(&sync_ctx, device, ip1));
        assert!(contains_addr(&sync_ctx, device, ip2));

        // Should receive packets on both ips
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip1.get(),
            2,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip2.get(),
            3,
        );

        // Remove ip1
        crate::device::del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, device, &ip1).unwrap();
        assert!(!contains_addr(&sync_ctx, device, ip1));
        assert!(contains_addr(&sync_ctx, device, ip2));

        // Should receive packets on ip2
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip1.get(),
            3,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip2.get(),
            4,
        );
    }

    fn join_ip_multicast<A: IpAddress, NonSyncCtx: NonSyncContext>(
        sync_ctx: &mut SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        device: DeviceId,
        multicast_addr: MulticastAddr<A>,
    ) {
        match multicast_addr.into() {
            IpAddr::V4(multicast_addr) => crate::ip::device::join_ip_multicast::<Ipv4, _, _>(
                sync_ctx,
                ctx,
                device,
                multicast_addr,
            ),
            IpAddr::V6(multicast_addr) => crate::ip::device::join_ip_multicast::<Ipv6, _, _>(
                sync_ctx,
                ctx,
                device,
                multicast_addr,
            ),
        }
    }

    fn leave_ip_multicast<A: IpAddress, NonSyncCtx: NonSyncContext>(
        sync_ctx: &mut SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        device: DeviceId,
        multicast_addr: MulticastAddr<A>,
    ) {
        match multicast_addr.into() {
            IpAddr::V4(multicast_addr) => crate::ip::device::leave_ip_multicast::<Ipv4, _, _>(
                sync_ctx,
                ctx,
                device,
                multicast_addr,
            ),
            IpAddr::V6(multicast_addr) => crate::ip::device::leave_ip_multicast::<Ipv6, _, _>(
                sync_ctx,
                ctx,
                device,
                multicast_addr,
            ),
        }
    }

    /// Test that we can join and leave a multicast group, but we only truly
    /// leave it after calling `leave_ip_multicast` the same number of times as
    /// `join_ip_multicast`.
    #[ip_test]
    fn test_ip_join_leave_multicast_addr_ref_count<I: Ip + TestIpExt>() {
        let config = I::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        let multicast_addr = I::get_multicast_addr(3);

        // Should not be in the multicast group yet.
        assert!(!is_in_ip_multicast(&sync_ctx, device, multicast_addr));

        // Join the multicast group.
        join_ip_multicast(&mut sync_ctx, &mut non_sync_ctx, device, multicast_addr);
        assert!(is_in_ip_multicast(&sync_ctx, device, multicast_addr));

        // Leave the multicast group.
        leave_ip_multicast(&mut sync_ctx, &mut non_sync_ctx, device, multicast_addr);
        assert!(!is_in_ip_multicast(&sync_ctx, device, multicast_addr));

        // Join the multicst group.
        join_ip_multicast(&mut sync_ctx, &mut non_sync_ctx, device, multicast_addr);
        assert!(is_in_ip_multicast(&sync_ctx, device, multicast_addr));

        // Join it again...
        join_ip_multicast(&mut sync_ctx, &mut non_sync_ctx, device, multicast_addr);
        assert!(is_in_ip_multicast(&sync_ctx, device, multicast_addr));

        // Leave it (still in it because we joined twice).
        leave_ip_multicast(&mut sync_ctx, &mut non_sync_ctx, device, multicast_addr);
        assert!(is_in_ip_multicast(&sync_ctx, device, multicast_addr));

        // Leave it again... (actually left now).
        leave_ip_multicast(&mut sync_ctx, &mut non_sync_ctx, device, multicast_addr);
        assert!(!is_in_ip_multicast(&sync_ctx, device, multicast_addr));
    }

    /// Test leaving a multicast group a device has not yet joined.
    ///
    /// # Panics
    ///
    /// This method should always panic as leaving an unjoined multicast group
    /// is a panic condition.
    #[ip_test]
    #[should_panic(expected = "attempted to leave IP multicast group we were not a member of:")]
    fn test_ip_leave_unjoined_multicast<I: Ip + TestIpExt>() {
        let config = I::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        let multicast_addr = I::get_multicast_addr(3);

        // Should not be in the multicast group yet.
        assert!(!is_in_ip_multicast(&sync_ctx, device, multicast_addr));

        // Leave it (this should panic).
        leave_ip_multicast(&mut sync_ctx, &mut non_sync_ctx, device, multicast_addr);
    }

    #[test]
    fn test_ipv6_duplicate_solicited_node_address() {
        // Test that we still receive packets destined to a solicited-node
        // multicast address of an IP address we deleted because another
        // (distinct) IP address that is still assigned uses the same
        // solicited-node multicast address.

        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        let ip1 = SpecifiedAddr::new(Ipv6Addr::new([0, 0, 0, 1, 0, 0, 0, 1])).unwrap();
        let ip2 = SpecifiedAddr::new(Ipv6Addr::new([0, 0, 0, 2, 0, 0, 0, 1])).unwrap();
        let from_ip = Ipv6Addr::new([0, 0, 0, 3, 0, 0, 0, 1]);

        // ip1 and ip2 are not equal but their solicited node addresses are the
        // same.
        assert_ne!(ip1, ip2);
        assert_eq!(ip1.to_solicited_node_address(), ip2.to_solicited_node_address());
        let sn_addr = ip1.to_solicited_node_address().get();

        let addr_sub1 = AddrSubnet::new(ip1.get(), 64).unwrap();
        let addr_sub2 = AddrSubnet::new(ip2.get(), 64).unwrap();

        assert_eq!(get_counter_val(&non_sync_ctx, "dispatch_receive_ip_packet"), 0);

        // Add ip1 to the device.
        //
        // Should get packets destined for the solicited node address and ip1.
        crate::device::add_ip_addr_subnet(&mut sync_ctx, &mut non_sync_ctx, device, addr_sub1)
            .unwrap();
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip1.get(),
            1,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip2.get(),
            1,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            sn_addr,
            2,
        );

        // Add ip2 to the device.
        //
        // Should get packets destined for the solicited node address, ip1 and
        // ip2.
        crate::device::add_ip_addr_subnet(&mut sync_ctx, &mut non_sync_ctx, device, addr_sub2)
            .unwrap();
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip1.get(),
            3,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip2.get(),
            4,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            sn_addr,
            5,
        );

        // Remove ip1 from the device.
        //
        // Should get packets destined for the solicited node address and ip2.
        crate::device::del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, device, &ip1).unwrap();
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip1.get(),
            5,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            ip2.get(),
            6,
        );
        receive_simple_ip_packet_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            from_ip,
            sn_addr,
            7,
        );
    }

    #[test]
    fn test_add_ip_addr_subnet_link_local() {
        // Test that `add_ip_addr_subnet` allows link-local addresses.

        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = EthernetDeviceId(0);
        assert_eq!(
            crate::add_ethernet_device(
                &mut sync_ctx,
                &mut non_sync_ctx,
                config.local_mac,
                Ipv6::MINIMUM_LINK_MTU.into()
            ),
            DeviceIdInner::Ethernet(device).into()
        );

        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device.into());
        // Verify that there is a single assigned address.
        assert_eq!(
            sync_ctx
                .state
                .device
                .ethernet
                .get(0)
                .unwrap()
                .ip
                .ipv6
                .ip_state
                .iter_addrs()
                .map(|entry| entry.addr_sub().addr())
                .collect::<Vec<_>>(),
            [config.local_mac.to_ipv6_link_local().addr().get()]
        );
        crate::device::add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device.into(),
            AddrSubnet::new(Ipv6::LINK_LOCAL_UNICAST_SUBNET.network(), 128).unwrap(),
        )
        .unwrap();
        // Assert that the new address got added.
        let addr_subs: Vec<_> = sync_ctx
            .state
            .device
            .ethernet
            .get(0)
            .unwrap()
            .ip
            .ipv6
            .ip_state
            .iter_addrs()
            .map(|entry| entry.addr_sub().addr().get())
            .collect();
        assert_eq!(
            addr_subs,
            [
                config.local_mac.to_ipv6_link_local().addr().get(),
                Ipv6::LINK_LOCAL_UNICAST_SUBNET.network()
            ]
        );
    }
}
