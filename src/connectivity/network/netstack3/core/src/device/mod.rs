// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The device layer.

pub(crate) mod arp;
pub(crate) mod ethernet;
pub(crate) mod link;
pub(crate) mod loopback;
pub(crate) mod ndp;
mod state;

use alloc::{boxed::Box, vec::Vec};
use core::{
    fmt::{self, Debug, Display, Formatter},
    marker::PhantomData,
};

use derivative::Derivative;
use log::{debug, trace};
use net_types::{
    ethernet::Mac,
    ip::{
        AddrSubnet, AddrSubnetEither, Ip, IpAddr, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr,
        Ipv6SourceAddr, Subnet,
    },
    MulticastAddr, SpecifiedAddr, UnicastAddr,
};
use packet::{Buf, BufferMut, EmptyBuf, Serializer};
use packet_formats::icmp::ndp::NdpPacket;
use zerocopy::ByteSlice;

use crate::{
    context::{
        CounterContext, FrameContext, NonTestCtxMarker, RecvFrameContext, StateContext,
        TimerContext,
    },
    data_structures::{IdMap, IdMapCollectionKey},
    device::{
        ethernet::{
            EthernetDeviceState, EthernetDeviceStateBuilder, EthernetLinkDevice, EthernetTimerId,
        },
        link::LinkDevice,
        loopback::LoopbackDeviceState,
        ndp::NdpPacketHandler,
        state::IpLinkDeviceState,
    },
    error::{ExistsError, NotFoundError, NotSupportedError},
    ip::{
        device::{
            nud::{DynamicNeighborUpdateSource, NudHandler, NudIpHandler},
            state::{
                AddrConfig, DualStackIpDeviceState, Ipv4DeviceConfiguration, Ipv4DeviceState,
                Ipv6DeviceConfiguration, Ipv6DeviceState,
            },
            BufferIpDeviceContext, IpDeviceContext, Ipv6DeviceContext,
        },
        IpDeviceId, IpDeviceIdContext,
    },
    BufferNonSyncContext, Instant, NonSyncContext, SyncCtx,
};

/// An execution context which provides a `DeviceId` type for various device
/// layer internals to share.
pub(crate) trait DeviceIdContext<D: LinkDevice> {
    type DeviceId: Copy + Display + Debug + Eq + Send + Sync + 'static;
}

struct RecvIpFrameMeta<D, I: Ip> {
    device: D,
    frame_dst: FrameDestination,
    _marker: PhantomData<I>,
}

impl<D, I: Ip> RecvIpFrameMeta<D, I> {
    fn new(device: D, frame_dst: FrameDestination) -> RecvIpFrameMeta<D, I> {
        RecvIpFrameMeta { device, frame_dst, _marker: PhantomData }
    }
}

/// The non-synchronized execution context for an IP device.
pub(crate) trait IpLinkDeviceNonSyncContext<TimerId>:
    TimerContext<TimerId> + CounterContext
{
}
impl<TimerId, C: TimerContext<TimerId> + CounterContext> IpLinkDeviceNonSyncContext<TimerId> for C {}

/// The context provided by the device layer to a particular IP device
/// implementation.
///
/// A blanket implementation is provided for all types that implement
/// the inherited traits.
pub(crate) trait IpLinkDeviceContext<D: LinkDevice, C: IpLinkDeviceNonSyncContext<TimerId>, TimerId>:
    DeviceIdContext<D>
    + StateContext<C, IpLinkDeviceState<C::Instant, D::State>, Self::DeviceId>
    + FrameContext<C, EmptyBuf, Self::DeviceId>
    + FrameContext<C, Buf<Vec<u8>>, Self::DeviceId>
{
}

impl<
        D: LinkDevice,
        C: IpLinkDeviceNonSyncContext<TimerId>,
        TimerId,
        SC: NonTestCtxMarker
            + DeviceIdContext<D>
            + StateContext<C, IpLinkDeviceState<C::Instant, D::State>, Self::DeviceId>
            + FrameContext<C, EmptyBuf, Self::DeviceId>
            + FrameContext<C, Buf<Vec<u8>>, Self::DeviceId>,
    > IpLinkDeviceContext<D, C, TimerId> for SC
{
}

/// `IpLinkDeviceContext` with an extra `B: BufferMut` parameter.
///
/// `BufferIpLinkDeviceContext` is used when sending a frame is required.
trait BufferIpLinkDeviceContext<
    D: LinkDevice,
    C: IpLinkDeviceNonSyncContext<TimerId>,
    TimerId,
    B: BufferMut,
>:
    IpLinkDeviceContext<D, C, TimerId>
    + FrameContext<C, B, <Self as DeviceIdContext<D>>::DeviceId>
    + RecvFrameContext<C, B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv4>>
    + RecvFrameContext<C, B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv6>>
{
}

impl<
        D: LinkDevice,
        C: IpLinkDeviceNonSyncContext<TimerId>,
        TimerId,
        B: BufferMut,
        SC: IpLinkDeviceContext<D, C, TimerId>
            + FrameContext<C, B, <Self as DeviceIdContext<D>>::DeviceId>
            + RecvFrameContext<C, B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv4>>
            + RecvFrameContext<C, B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv6>>,
    > BufferIpLinkDeviceContext<D, C, TimerId, B> for SC
{
}

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>
    RecvFrameContext<NonSyncCtx, B, RecvIpFrameMeta<EthernetDeviceId, Ipv4>>
    for SyncCtx<NonSyncCtx>
{
    fn receive_frame(
        &mut self,
        ctx: &mut NonSyncCtx,
        metadata: RecvIpFrameMeta<EthernetDeviceId, Ipv4>,
        frame: B,
    ) {
        crate::ip::receive_ipv4_packet(
            self,
            ctx,
            metadata.device.into(),
            metadata.frame_dst,
            frame,
        );
    }
}

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>
    RecvFrameContext<NonSyncCtx, B, RecvIpFrameMeta<EthernetDeviceId, Ipv6>>
    for SyncCtx<NonSyncCtx>
{
    fn receive_frame(
        &mut self,
        ctx: &mut NonSyncCtx,
        metadata: RecvIpFrameMeta<EthernetDeviceId, Ipv6>,
        frame: B,
    ) {
        crate::ip::receive_ipv6_packet(
            self,
            ctx,
            metadata.device.into(),
            metadata.frame_dst,
            frame,
        );
    }
}

impl<NonSyncCtx: NonSyncContext>
    StateContext<
        NonSyncCtx,
        IpLinkDeviceState<NonSyncCtx::Instant, EthernetDeviceState>,
        EthernetDeviceId,
    > for SyncCtx<NonSyncCtx>
{
    fn get_state_with(
        &self,
        id0: EthernetDeviceId,
    ) -> &IpLinkDeviceState<NonSyncCtx::Instant, EthernetDeviceState> {
        &self.state.device.ethernet.get(id0.0).unwrap()
    }

    fn get_state_mut_with(
        &mut self,
        id0: EthernetDeviceId,
    ) -> &mut IpLinkDeviceState<NonSyncCtx::Instant, EthernetDeviceState> {
        self.state.device.ethernet.get_mut(id0.0).unwrap()
    }
}

fn get_ip_device_state<NonSyncCtx: NonSyncContext>(
    ctx: &SyncCtx<NonSyncCtx>,
    device: DeviceId,
) -> &DualStackIpDeviceState<NonSyncCtx::Instant> {
    match device.inner() {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)) => {
            &ctx.state.device.ethernet.get(id).unwrap().ip
        }
        DeviceIdInner::Loopback => &ctx.state.device.loopback.as_ref().unwrap().ip,
    }
}

fn get_ip_device_state_mut<NonSyncCtx: NonSyncContext>(
    ctx: &mut SyncCtx<NonSyncCtx>,
    device: DeviceId,
) -> &mut DualStackIpDeviceState<NonSyncCtx::Instant> {
    match device.inner() {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)) => {
            &mut ctx.state.device.ethernet.get_mut(id).unwrap().ip
        }
        DeviceIdInner::Loopback => &mut ctx.state.device.loopback.as_mut().unwrap().ip,
    }
}

fn iter_devices<NonSyncCtx: NonSyncContext>(
    ctx: &SyncCtx<NonSyncCtx>,
) -> impl Iterator<Item = DeviceId> + '_ {
    let DeviceLayerState { ethernet, loopback } = &ctx.state.device;

    ethernet
        .iter()
        .map(|(id, _state)| DeviceId::new_ethernet(id))
        .chain(loopback.iter().map(|_state| DeviceIdInner::Loopback.into()))
}

fn get_mtu<NonSyncCtx: NonSyncContext>(ctx: &SyncCtx<NonSyncCtx>, device: DeviceId) -> u32 {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::get_mtu(ctx, id),
        DeviceIdInner::Loopback => self::loopback::get_mtu(ctx),
    }
}

fn join_link_multicast_group<NonSyncCtx: NonSyncContext, A: IpAddress>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device_id: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    match device_id.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::join_link_multicast(
            sync_ctx,
            ctx,
            id,
            MulticastAddr::from(&multicast_addr),
        ),
        DeviceIdInner::Loopback => {}
    }
}

fn leave_link_multicast_group<NonSyncCtx: NonSyncContext, A: IpAddress>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device_id: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    match device_id.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::leave_link_multicast(
            sync_ctx,
            ctx,
            id,
            MulticastAddr::from(&multicast_addr),
        ),
        DeviceIdInner::Loopback => {}
    }
}

impl<NonSyncCtx: NonSyncContext> IpDeviceContext<Ipv4, NonSyncCtx> for SyncCtx<NonSyncCtx> {
    fn with_ip_device_state<O, F: FnOnce(&Ipv4DeviceState<NonSyncCtx::Instant>) -> O>(
        &self,
        device: Self::DeviceId,
        cb: F,
    ) -> O {
        cb(&get_ip_device_state(self, device).ipv4)
    }

    fn with_ip_device_state_mut<O, F: FnOnce(&mut Ipv4DeviceState<NonSyncCtx::Instant>) -> O>(
        &mut self,
        device: Self::DeviceId,
        cb: F,
    ) -> O {
        cb(&mut get_ip_device_state_mut(self, device).ipv4)
    }

    fn iter_devices(&self) -> Box<dyn Iterator<Item = DeviceId> + '_> {
        Box::new(iter_devices(self))
    }

    fn get_mtu(&self, device_id: Self::DeviceId) -> u32 {
        get_mtu(self, device_id)
    }

    fn join_link_multicast_group(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv4Addr>,
    ) {
        join_link_multicast_group(self, ctx, device_id, multicast_addr)
    }

    fn leave_link_multicast_group(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv4Addr>,
    ) {
        leave_link_multicast_group(self, ctx, device_id, multicast_addr)
    }
}

fn send_ip_frame<
    B: BufferMut,
    NonSyncCtx: BufferNonSyncContext<B>,
    S: Serializer<Buffer = B>,
    A: IpAddress,
>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            self::ethernet::send_ip_frame(sync_ctx, ctx, id, local_addr, body)
        }
        DeviceIdInner::Loopback => self::loopback::send_ip_frame(sync_ctx, ctx, local_addr, body),
    }
}

fn bytes_to_mac(b: &[u8]) -> Option<Mac> {
    (b.len() >= Mac::BYTES).then(|| {
        Mac::new({
            let mut bytes = [0; Mac::BYTES];
            bytes.copy_from_slice(&b[..Mac::BYTES]);
            bytes
        })
    })
}

impl<C: NonSyncContext> NudIpHandler<Ipv6, C> for SyncCtx<C> {
    fn handle_neighbor_probe(
        &mut self,
        ctx: &mut C,
        device_id: DeviceId,
        neighbor: SpecifiedAddr<Ipv6Addr>,
        link_addr: &[u8],
    ) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                if let Some(link_addr) = bytes_to_mac(link_addr) {
                    NudHandler::<Ipv6, EthernetLinkDevice, _>::set_dynamic_neighbor(
                        self,
                        ctx,
                        id,
                        neighbor,
                        link_addr,
                        DynamicNeighborUpdateSource::Probe,
                    )
                }
            }
            DeviceIdInner::Loopback => {}
        }
    }

    fn handle_neighbor_confirmation(
        &mut self,
        ctx: &mut C,
        device_id: DeviceId,
        neighbor: SpecifiedAddr<Ipv6Addr>,
        link_addr: &[u8],
    ) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                if let Some(link_addr) = bytes_to_mac(link_addr) {
                    NudHandler::<Ipv6, EthernetLinkDevice, _>::set_dynamic_neighbor(
                        self,
                        ctx,
                        id,
                        neighbor,
                        link_addr,
                        DynamicNeighborUpdateSource::Confirmation,
                    )
                }
            }
            DeviceIdInner::Loopback => {}
        }
    }

    fn flush_neighbor_table(&mut self, ctx: &mut C, device_id: DeviceId) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                NudHandler::<Ipv6, EthernetLinkDevice, _>::flush(self, ctx, id)
            }
            DeviceIdInner::Loopback => {}
        }
    }
}

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>> BufferIpDeviceContext<Ipv4, NonSyncCtx, B>
    for SyncCtx<NonSyncCtx>
{
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut NonSyncCtx,
        device: DeviceId,
        local_addr: SpecifiedAddr<Ipv4Addr>,
        body: S,
    ) -> Result<(), S> {
        send_ip_frame(self, ctx, device, local_addr, body)
    }
}

impl<NonSyncCtx: NonSyncContext> IpDeviceContext<Ipv6, NonSyncCtx> for SyncCtx<NonSyncCtx> {
    fn with_ip_device_state<O, F: FnOnce(&Ipv6DeviceState<NonSyncCtx::Instant>) -> O>(
        &self,
        device: Self::DeviceId,
        cb: F,
    ) -> O {
        cb(&get_ip_device_state(self, device).ipv6)
    }

    fn with_ip_device_state_mut<O, F: FnOnce(&mut Ipv6DeviceState<NonSyncCtx::Instant>) -> O>(
        &mut self,
        device: Self::DeviceId,
        cb: F,
    ) -> O {
        cb(&mut get_ip_device_state_mut(self, device).ipv6)
    }

    fn iter_devices(&self) -> Box<dyn Iterator<Item = DeviceId> + '_> {
        Box::new(iter_devices(self))
    }

    fn get_mtu(&self, device_id: Self::DeviceId) -> u32 {
        get_mtu(self, device_id)
    }

    fn join_link_multicast_group(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    ) {
        join_link_multicast_group(self, ctx, device_id, multicast_addr)
    }

    fn leave_link_multicast_group(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    ) {
        leave_link_multicast_group(self, ctx, device_id, multicast_addr)
    }
}

impl<NonSyncCtx: NonSyncContext> Ipv6DeviceContext<NonSyncCtx> for SyncCtx<NonSyncCtx> {
    fn get_link_layer_addr_bytes(&self, device_id: Self::DeviceId) -> Option<&[u8]> {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => Some(ethernet::get_mac(self, id).as_ref().as_ref()),
            DeviceIdInner::Loopback => None,
        }
    }

    fn get_eui64_iid(&self, device_id: Self::DeviceId) -> Option<[u8; 8]> {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                Some(ethernet::get_mac(self, id).to_eui64_with_magic(Mac::DEFAULT_EUI_MAGIC))
            }
            DeviceIdInner::Loopback => None,
        }
    }
}

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>> BufferIpDeviceContext<Ipv6, NonSyncCtx, B>
    for SyncCtx<NonSyncCtx>
{
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut NonSyncCtx,
        device: DeviceId,
        local_addr: SpecifiedAddr<Ipv6Addr>,
        body: S,
    ) -> Result<(), S> {
        send_ip_frame(self, ctx, device, local_addr, body)
    }
}

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>
    FrameContext<NonSyncCtx, B, EthernetDeviceId> for SyncCtx<NonSyncCtx>
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut NonSyncCtx,
        device: EthernetDeviceId,
        frame: S,
    ) -> Result<(), S> {
        DeviceLayerEventDispatcher::send_frame(ctx, device.into(), frame)
    }
}

/// Device IDs identifying Ethernet devices.
#[derive(Copy, Clone, Eq, PartialEq, Hash)]
pub(crate) struct EthernetDeviceId(usize);

impl Debug for EthernetDeviceId {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        let device: DeviceId = self.clone().into();
        write!(f, "{:?}", device)
    }
}

impl Display for EthernetDeviceId {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        let device: DeviceId = self.clone().into();
        write!(f, "{}", device)
    }
}

impl From<usize> for EthernetDeviceId {
    fn from(id: usize) -> EthernetDeviceId {
        EthernetDeviceId(id)
    }
}

/// The identifier for timer events in the device layer.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) struct DeviceLayerTimerId(DeviceLayerTimerIdInner);

#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
enum DeviceLayerTimerIdInner {
    /// A timer event for an Ethernet device.
    Ethernet(EthernetTimerId<EthernetDeviceId>),
}

impl From<EthernetTimerId<EthernetDeviceId>> for DeviceLayerTimerId {
    fn from(id: EthernetTimerId<EthernetDeviceId>) -> DeviceLayerTimerId {
        DeviceLayerTimerId(DeviceLayerTimerIdInner::Ethernet(id))
    }
}

impl<NonSyncCtx: NonSyncContext> DeviceIdContext<EthernetLinkDevice> for SyncCtx<NonSyncCtx> {
    type DeviceId = EthernetDeviceId;
}

impl_timer_context!(
    DeviceLayerTimerId,
    EthernetTimerId<EthernetDeviceId>,
    DeviceLayerTimerId(DeviceLayerTimerIdInner::Ethernet(id)),
    id
);

/// Handle a timer event firing in the device layer.
pub(crate) fn handle_timer<NonSyncCtx: NonSyncContext>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    id: DeviceLayerTimerId,
) {
    match id.0 {
        DeviceLayerTimerIdInner::Ethernet(id) => ethernet::handle_timer(sync_ctx, ctx, id),
    }
}

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
pub(crate) enum DeviceIdInner {
    Ethernet(EthernetDeviceId),
    Loopback,
}

/// An ID identifying a device.
#[derive(Copy, Clone, Eq, PartialEq, Hash)]
pub struct DeviceId(pub(crate) DeviceIdInner);

impl From<DeviceIdInner> for DeviceId {
    fn from(id: DeviceIdInner) -> DeviceId {
        DeviceId(id)
    }
}

impl From<EthernetDeviceId> for DeviceId {
    fn from(id: EthernetDeviceId) -> DeviceId {
        DeviceIdInner::Ethernet(id).into()
    }
}

impl DeviceId {
    /// Construct a new `DeviceId` for an Ethernet device.
    pub(crate) const fn new_ethernet(id: usize) -> DeviceId {
        DeviceId(DeviceIdInner::Ethernet(EthernetDeviceId(id)))
    }

    pub(crate) fn inner(self) -> DeviceIdInner {
        let DeviceId(id) = self;
        id
    }
}

impl IpDeviceId for DeviceId {
    fn is_loopback(&self) -> bool {
        match self.inner() {
            DeviceIdInner::Loopback => true,
            DeviceIdInner::Ethernet(_) => false,
        }
    }
}

impl Display for DeviceId {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        match self.inner() {
            DeviceIdInner::Ethernet(EthernetDeviceId(id)) => write!(f, "Ethernet({})", id),
            DeviceIdInner::Loopback => write!(f, "Loopback"),
        }
    }
}

impl Debug for DeviceId {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        Display::fmt(self, f)
    }
}

impl IdMapCollectionKey for DeviceId {
    const VARIANT_COUNT: usize = 2;

    fn get_id(&self) -> usize {
        match self.inner() {
            DeviceIdInner::Ethernet(EthernetDeviceId(id)) => id,
            DeviceIdInner::Loopback => 0,
        }
    }

    fn get_variant(&self) -> usize {
        match self.inner() {
            DeviceIdInner::Ethernet(_) => 0,
            DeviceIdInner::Loopback => 1,
        }
    }
}

// TODO(joshlf): Does the IP layer ever need to distinguish between broadcast
// and multicast frames?

/// The type of address used as the source address in a device-layer frame:
/// unicast or broadcast.
///
/// `FrameDestination` is used to implement RFC 1122 section 3.2.2 and RFC 4443
/// section 2.4.e, which govern when to avoid sending an ICMP error message for
/// ICMP and ICMPv6 respectively.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub(crate) enum FrameDestination {
    /// A unicast address - one which is neither multicast nor broadcast.
    Unicast,
    /// A multicast address; if the addressing scheme supports overlap between
    /// multicast and broadcast, then broadcast addresses should use the
    /// `Broadcast` variant.
    Multicast,
    /// A broadcast address; if the addressing scheme supports overlap between
    /// multicast and broadcast, then broadcast addresses should use the
    /// `Broadcast` variant.
    Broadcast,
}

impl FrameDestination {
    /// Is this `FrameDestination::Multicast`?
    pub(crate) fn is_multicast(self) -> bool {
        self == FrameDestination::Multicast
    }

    /// Is this `FrameDestination::Broadcast`?
    pub(crate) fn is_broadcast(self) -> bool {
        self == FrameDestination::Broadcast
    }
}

/// The state associated with the device layer.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct DeviceLayerState<I: Instant> {
    ethernet: IdMap<IpLinkDeviceState<I, EthernetDeviceState>>,
    loopback: Option<IpLinkDeviceState<I, LoopbackDeviceState>>,
}

impl<I: Instant> DeviceLayerState<I> {
    /// Add a new ethernet device to the device layer.
    ///
    /// `add` adds a new `EthernetDeviceState` with the given MAC address and
    /// MTU. The MTU will be taken as a limit on the size of Ethernet payloads -
    /// the Ethernet header is not counted towards the MTU.
    pub(crate) fn add_ethernet_device(&mut self, mac: UnicastAddr<Mac>, mtu: u32) -> DeviceId {
        let Self { ethernet, loopback: _ } = self;

        let id = ethernet
            .push(IpLinkDeviceState::new(EthernetDeviceStateBuilder::new(mac, mtu).build()));
        debug!("adding Ethernet device with ID {} and MTU {}", id, mtu);
        DeviceId::new_ethernet(id)
    }

    /// Adds a new loopback device to the device layer.
    pub(crate) fn add_loopback_device(&mut self, mtu: u32) -> Result<DeviceId, ExistsError> {
        let Self { ethernet: _, loopback } = self;

        if let Some(IpLinkDeviceState { .. }) = loopback {
            return Err(ExistsError);
        }

        *loopback = Some(IpLinkDeviceState::new(LoopbackDeviceState::new(mtu)));

        debug!("added loopback device");

        Ok(DeviceIdInner::Loopback.into())
    }
}

/// An event dispatcher for the device layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait DeviceLayerEventDispatcher<B: BufferMut> {
    /// Send a frame to a device driver.
    ///
    /// If there was an MTU error while attempting to serialize the frame, the
    /// original serializer is returned in the `Err` variant. All other errors
    /// (for example, errors in allocating a buffer) are silently ignored and
    /// reported as success.
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        frame: S,
    ) -> Result<(), S>;
}

/// Remove a device from the device layer.
///
/// # Panics
///
/// Panics if `device` does not refer to an existing device.
pub fn remove_device<NonSyncCtx: NonSyncContext>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
) {
    // Uninstall all routes associated with the device.
    crate::ip::del_device_routes::<Ipv4, _, _>(sync_ctx, ctx, &device);
    crate::ip::del_device_routes::<Ipv6, _, _>(sync_ctx, ctx, &device);
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            crate::device::ethernet::deinitialize(sync_ctx, ctx, id);
            let EthernetDeviceId(id) = id;
            let _: IpLinkDeviceState<_, _> = sync_ctx
                .state
                .device
                .ethernet
                .remove(id)
                .unwrap_or_else(|| panic!("no such Ethernet device: {}", id));
            debug!("removing Ethernet device with ID {}", id);
        }
        DeviceIdInner::Loopback => {
            let _: IpLinkDeviceState<_, _> =
                sync_ctx.state.device.loopback.take().expect("loopback device does not exist");
            debug!("removing Loopback device");
        }
    }
}

/// Adds a new Ethernet device to the stack.
pub fn add_ethernet_device<NonSyncCtx: NonSyncContext>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    mac: UnicastAddr<Mac>,
    mtu: u32,
) -> DeviceId {
    let id = sync_ctx.state.device.add_ethernet_device(mac, mtu);

    const LINK_LOCAL_SUBNET: Subnet<Ipv6Addr> = net_declare::net_subnet_v6!("fe80::/64");
    crate::add_route(
        sync_ctx,
        ctx,
        crate::AddableEntryEither::new(LINK_LOCAL_SUBNET.into(), Some(id), None)
            .expect("create link local entry"),
    )
    // Adding the link local route must succeed: we're providing correct
    // arguments and the device has just been created.
    .expect("add link local route");

    id
}

/// Adds a new device loopback device to the stack.
///
/// Only one loopback device may be installed at any point in time.
pub fn add_loopback_device<NonSyncCtx: NonSyncContext>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    mtu: u32,
) -> Result<DeviceId, crate::error::ExistsError> {
    sync_ctx.state.device.add_loopback_device(mtu)
}

/// Receive a device layer frame from the network.
pub fn receive_frame<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    buffer: B,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => Ok(self::ethernet::receive_frame(sync_ctx, ctx, id, buffer)),
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Set the promiscuous mode flag on `device`.
// TODO(rheacock): remove `allow(dead_code)` when this is used.
#[allow(dead_code)]
pub(crate) fn set_promiscuous_mode<NonSyncCtx: NonSyncContext>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::set_promiscuous_mode(sync_ctx, ctx, id, enabled))
        }
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Adds an IP address and associated subnet to this device.
///
/// For IPv6, this function also joins the solicited-node multicast group and
/// begins performing Duplicate Address Detection (DAD).
pub(crate) fn add_ip_addr_subnet<NonSyncCtx: NonSyncContext, A: IpAddress>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    addr_sub: AddrSubnet<A>,
) -> Result<(), ExistsError> {
    trace!("add_ip_addr_subnet: adding addr {:?} to device {:?}", addr_sub, device);

    match addr_sub.into() {
        AddrSubnetEither::V4(addr_sub) => {
            crate::ip::device::add_ipv4_addr_subnet(sync_ctx, ctx, device, addr_sub)
        }
        AddrSubnetEither::V6(addr_sub) => crate::ip::device::add_ipv6_addr_subnet(
            sync_ctx,
            ctx,
            device,
            addr_sub,
            AddrConfig::Manual,
        ),
    }
}

/// Removes an IP address and associated subnet from this device.
///
/// Should only be called on user action.
pub(crate) fn del_ip_addr<NonSyncCtx: NonSyncContext, A: IpAddress>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    addr: &SpecifiedAddr<A>,
) -> Result<(), NotFoundError> {
    trace!("del_ip_addr: removing addr {:?} from device {:?}", addr, device);

    match Into::into(*addr) {
        IpAddr::V4(addr) => crate::ip::device::del_ipv4_addr(sync_ctx, ctx, device, &addr),
        IpAddr::V6(addr) => crate::ip::device::del_ipv6_addr_with_reason(
            sync_ctx,
            ctx,
            device,
            &addr,
            crate::ip::device::state::DelIpv6AddrReason::ManualAction,
        ),
    }
}

// Temporary blanket impl until we switch over entirely to the traits defined in
// the `context` module.
impl<NonSyncCtx: NonSyncContext, I: Ip> IpDeviceIdContext<I> for SyncCtx<NonSyncCtx> {
    type DeviceId = DeviceId;

    fn loopback_id(&self) -> Option<DeviceId> {
        self.state.device.loopback.as_ref().map(|_state| DeviceIdInner::Loopback.into())
    }
}

/// Insert a static entry into this device's ARP table.
///
/// This will cause any conflicting dynamic entry to be removed, and
/// any future conflicting gratuitous ARPs to be ignored.
// TODO(rheacock): remove `cfg(test)` when this is used. Will probably be
// called by a pub fn in the device mod.
#[cfg(test)]
pub(super) fn insert_static_arp_table_entry<NonSyncCtx: NonSyncContext>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    addr: Ipv4Addr,
    mac: UnicastAddr<Mac>,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::insert_static_arp_table_entry(sync_ctx, ctx, id, addr, mac.into()))
        }
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Insert an entry into this device's NDP table.
///
/// This method only gets called when testing to force set a neighbor's link
/// address so that lookups succeed immediately, without doing address
/// resolution.
// TODO(rheacock): Remove when this is called from non-test code.
#[cfg(test)]
pub(crate) fn insert_ndp_table_entry<NonSyncCtx: NonSyncContext>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    addr: UnicastAddr<Ipv6Addr>,
    mac: Mac,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::insert_ndp_table_entry(sync_ctx, ctx, id, addr, mac))
        }
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Gets the IPv4 Configuration for a `device`.
pub fn get_ipv4_configuration<NonSyncCtx: NonSyncContext>(
    ctx: &SyncCtx<NonSyncCtx>,
    device: DeviceId,
) -> Ipv4DeviceConfiguration {
    crate::ip::device::get_ipv4_configuration(ctx, device)
}

/// Gets the IPv6 Configuration for a `device`.
pub fn get_ipv6_configuration<NonSyncCtx: NonSyncContext>(
    ctx: &SyncCtx<NonSyncCtx>,
    device: DeviceId,
) -> Ipv6DeviceConfiguration {
    crate::ip::device::get_ipv6_configuration(ctx, device)
}

/// Updates the IPv4 Configuration for a `device`.
pub fn update_ipv4_configuration<
    NonSyncCtx: NonSyncContext,
    F: FnOnce(&mut Ipv4DeviceConfiguration),
>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    update_cb: F,
) {
    crate::ip::device::update_ipv4_configuration(sync_ctx, ctx, device, update_cb)
}

/// Updates the IPv6 Configuration for a `device`.
pub fn update_ipv6_configuration<
    NonSyncCtx: NonSyncContext,
    F: FnOnce(&mut Ipv6DeviceConfiguration),
>(
    sync_ctx: &mut SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
    update_cb: F,
) {
    crate::ip::device::update_ipv6_configuration(sync_ctx, ctx, device, update_cb)
}

/// An address that may be "tentative" in that it has not yet passed duplicate
/// address detection (DAD).
///
/// A tentative address is one for which DAD is currently being performed. An
/// address is only considered assigned to an interface once DAD has completed
/// without detecting any duplicates. See [RFC 4862] for more details.
///
/// [RFC 4862]: https://tools.ietf.org/html/rfc4862
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Tentative<T>(T, bool);

/// This implementation of `NdpPacketHandler` is consumed by ICMPv6.
impl<NonSyncCtx: NonSyncContext> NdpPacketHandler<NonSyncCtx, DeviceId> for SyncCtx<NonSyncCtx> {
    fn receive_ndp_packet<B: ByteSlice>(
        &mut self,
        ctx: &mut NonSyncCtx,
        device: DeviceId,
        src_ip: Ipv6SourceAddr,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: NdpPacket<B>,
    ) {
        trace!("device::receive_ndp_packet");

        match device.inner() {
            DeviceIdInner::Ethernet(id) => {
                crate::device::ndp::receive_ndp_packet(self, ctx, id, src_ip, dst_ip, packet);
            }
            DeviceIdInner::Loopback => {
                unimplemented!("TODO(https://fxbug.dev/72378): Handle NDP on loopback")
            }
        }
    }
}

#[cfg(test)]
pub(crate) mod testutil {
    use super::*;
    use crate::Ctx;

    /// Calls [`receive_frame`], panicking on error.
    pub(crate) fn receive_frame_or_panic<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>(
        Ctx { sync_ctx, non_sync_ctx }: &mut Ctx<NonSyncCtx>,
        device: DeviceId,
        buffer: B,
    ) {
        crate::device::receive_frame(sync_ctx, non_sync_ctx, device, buffer).unwrap()
    }

    pub fn enable_device<NonSyncCtx: NonSyncContext>(
        sync_ctx: &mut SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        device: DeviceId,
    ) {
        crate::ip::device::update_ipv4_configuration(sync_ctx, ctx, device, |config| {
            config.ip_config.ip_enabled = true;
        });
        crate::ip::device::update_ipv6_configuration(sync_ctx, ctx, device, |config| {
            config.ip_config.ip_enabled = true;
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        testutil::{
            DummyEventDispatcherBuilder, DummyEventDispatcherConfig, DummySyncCtx, DUMMY_CONFIG_V4,
        },
        Ctx,
    };

    #[test]
    fn test_iter_devices() {
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();

        fn check(sync_ctx: &DummySyncCtx, expected: &[DeviceId]) {
            assert_eq!(
                IpDeviceContext::<Ipv4, _>::iter_devices(sync_ctx).collect::<Vec<_>>(),
                expected
            );
            assert_eq!(
                IpDeviceContext::<Ipv6, _>::iter_devices(sync_ctx).collect::<Vec<_>>(),
                expected
            );
        }
        check(&sync_ctx, &[][..]);

        let loopback_device = crate::add_loopback_device(&mut sync_ctx, 55 /* mtu */)
            .expect("error adding loopback device");
        check(&sync_ctx, &[loopback_device][..]);

        let DummyEventDispatcherConfig {
            subnet: _,
            local_ip: _,
            local_mac,
            remote_ip: _,
            remote_mac: _,
        } = DUMMY_CONFIG_V4;
        let ethernet_device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac,
            0, /* mtu */
        );
        check(&sync_ctx, &[ethernet_device, loopback_device][..]);
    }
}
