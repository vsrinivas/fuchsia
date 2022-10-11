// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The device layer.

pub(crate) mod arp;
pub(crate) mod ethernet;
pub(crate) mod link;
pub(crate) mod loopback;
pub(crate) mod ndp;
pub mod queue;
mod state;

use alloc::boxed::Box;
use core::{
    fmt::{self, Debug, Display, Formatter},
    marker::PhantomData,
    num::NonZeroU32,
};

use derivative::Derivative;
use log::{debug, trace};
use net_types::{
    ethernet::Mac,
    ip::{
        AddrSubnet, AddrSubnetEither, Ip, IpAddr, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, Subnet,
    },
    MulticastAddr, SpecifiedAddr, UnicastAddr, Witness as _,
};
use packet::{BufferMut, Serializer};
use packet_formats::ethernet::EthernetIpExt;

use crate::{
    context::{FrameContext, RecvFrameContext},
    data_structures::{id_map::IdMap, id_map_collection::IdMapCollectionKey},
    device::{
        ethernet::{
            EthernetDeviceState, EthernetDeviceStateBuilder, EthernetLinkDevice, EthernetTimerId,
        },
        loopback::{LoopbackDevice, LoopbackDeviceId, LoopbackDeviceState},
        queue::ReceiveQueueHandler,
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
            BufferIpDeviceContext, DualStackDeviceContext, DualStackDeviceStateRef,
            IpDeviceContext, Ipv6DeviceContext,
        },
        types::{AddableEntry, AddableEntryEither},
        DualStackDeviceIdContext, IpDeviceId, IpDeviceIdContext,
    },
    sync::{ReferenceCounted, RwLock},
    BufferNonSyncContext, Instant, NonSyncContext, SyncCtx,
};

/// A device.
///
/// `Device` is used to identify a particular device implementation. It
/// is only intended to exist at the type level, never instantiated at runtime.
pub(crate) trait Device: 'static {}

/// An execution context which provides a `DeviceId` type for various device
/// layer internals to share.
pub(crate) trait DeviceIdContext<D: Device> {
    type DeviceId: Clone + Display + Debug + Eq + Send + Sync + 'static;
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

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>
    RecvFrameContext<NonSyncCtx, B, RecvIpFrameMeta<EthernetDeviceId, Ipv4>>
    for &'_ SyncCtx<NonSyncCtx>
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
            &metadata.device.into(),
            metadata.frame_dst,
            frame,
        );
    }
}

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>
    RecvFrameContext<NonSyncCtx, B, RecvIpFrameMeta<EthernetDeviceId, Ipv6>>
    for &'_ SyncCtx<NonSyncCtx>
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
            &metadata.device.into(),
            metadata.frame_dst,
            frame,
        );
    }
}

fn with_ip_device_state<
    NonSyncCtx: NonSyncContext,
    O,
    F: FnOnce(&DualStackIpDeviceState<NonSyncCtx::Instant>) -> O,
>(
    ctx: &SyncCtx<NonSyncCtx>,
    device: &DeviceId,
    cb: F,
) -> O {
    match device.inner() {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)) => {
            let eth = {
                let devices = ctx.state.device.devices.read();
                ReferenceCounted::clone(devices.ethernet.get(*id).unwrap())
            };

            cb(&eth.ip)
        }
        DeviceIdInner::Loopback(LoopbackDeviceId) => {
            let loopback = {
                let devices = ctx.state.device.devices.read();
                ReferenceCounted::clone(devices.loopback.as_ref().unwrap())
            };

            cb(&loopback.ip)
        }
    }
}

fn with_devices<
    NonSyncCtx: NonSyncContext,
    O,
    F: FnOnce(Box<dyn Iterator<Item = DeviceId> + '_>) -> O,
>(
    ctx: &SyncCtx<NonSyncCtx>,
    cb: F,
) -> O {
    let Devices { ethernet, loopback } = &*ctx.state.device.devices.read();

    cb(Box::new(
        ethernet
            .iter()
            .map(|(id, _state)| DeviceId::new_ethernet(id))
            .chain(loopback.iter().map(|_state| DeviceIdInner::Loopback(LoopbackDeviceId).into())),
    ))
}

fn get_mtu<NonSyncCtx: NonSyncContext>(mut ctx: &SyncCtx<NonSyncCtx>, device: &DeviceId) -> u32 {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::get_mtu(&mut ctx, &id),
        DeviceIdInner::Loopback(id) => self::loopback::get_mtu(ctx, id),
    }
}

fn join_link_multicast_group<NonSyncCtx: NonSyncContext, A: IpAddress>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device_id: &DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    match device_id.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::join_link_multicast(
            &mut sync_ctx,
            ctx,
            &id,
            MulticastAddr::from(&multicast_addr),
        ),
        DeviceIdInner::Loopback(LoopbackDeviceId) => {}
    }
}

fn leave_link_multicast_group<NonSyncCtx: NonSyncContext, A: IpAddress>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device_id: &DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    match device_id.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::leave_link_multicast(
            &mut sync_ctx,
            ctx,
            &id,
            MulticastAddr::from(&multicast_addr),
        ),
        DeviceIdInner::Loopback(LoopbackDeviceId) => {}
    }
}

impl<NonSyncCtx: NonSyncContext> DualStackDeviceContext<NonSyncCtx> for &'_ SyncCtx<NonSyncCtx> {
    fn with_dual_stack_device_state<
        O,
        F: FnOnce(DualStackDeviceStateRef<'_, NonSyncCtx::Instant>) -> O,
    >(
        &self,
        device_id: &Self::DualStackDeviceId,
        cb: F,
    ) -> O {
        with_ip_device_state(self, device_id, |DualStackIpDeviceState { ipv4, ipv6 }| {
            let ipv4 = ipv4.read();
            let ipv6 = ipv6.read();
            cb(DualStackDeviceStateRef { ipv4: &ipv4, ipv6: &ipv6 })
        })
    }
}

impl<NonSyncCtx: NonSyncContext> IpDeviceContext<Ipv4, NonSyncCtx> for &'_ SyncCtx<NonSyncCtx> {
    fn with_ip_device_state<O, F: FnOnce(&Ipv4DeviceState<NonSyncCtx::Instant>) -> O>(
        &self,
        device: &Self::DeviceId,
        cb: F,
    ) -> O {
        with_ip_device_state(self, device, |state| cb(&state.ipv4.read()))
    }

    fn with_ip_device_state_mut<O, F: FnOnce(&mut Ipv4DeviceState<NonSyncCtx::Instant>) -> O>(
        &mut self,
        device: &Self::DeviceId,
        cb: F,
    ) -> O {
        with_ip_device_state(self, device, |state| cb(&mut state.ipv4.write()))
    }

    fn with_devices<O, F: FnOnce(Box<dyn Iterator<Item = DeviceId> + '_>) -> O>(&self, cb: F) -> O {
        with_devices(self, cb)
    }

    fn get_mtu(&self, device_id: &Self::DeviceId) -> u32 {
        get_mtu(self, device_id)
    }

    fn join_link_multicast_group(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: &Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv4Addr>,
    ) {
        join_link_multicast_group(self, ctx, device_id, multicast_addr)
    }

    fn leave_link_multicast_group(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: &Self::DeviceId,
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
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
    local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S>
where
    A::Version: EthernetIpExt,
{
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            self::ethernet::send_ip_frame(&mut sync_ctx, ctx, &id, local_addr, body)
        }
        DeviceIdInner::Loopback(id) => {
            self::loopback::send_ip_frame(sync_ctx, ctx, id, local_addr, body)
        }
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

impl<I: Ip, C: NonSyncContext> NudIpHandler<I, C> for &'_ SyncCtx<C>
where
    Self: NudHandler<I, EthernetLinkDevice, C>
        + DeviceIdContext<EthernetLinkDevice, DeviceId = EthernetDeviceId>,
{
    fn handle_neighbor_probe(
        &mut self,
        ctx: &mut C,
        device_id: &DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_addr: &[u8],
    ) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                if let Some(link_addr) = bytes_to_mac(link_addr) {
                    NudHandler::<I, EthernetLinkDevice, _>::set_dynamic_neighbor(
                        self,
                        ctx,
                        &id,
                        neighbor,
                        link_addr,
                        DynamicNeighborUpdateSource::Probe,
                    )
                }
            }
            DeviceIdInner::Loopback(LoopbackDeviceId) => {}
        }
    }

    fn handle_neighbor_confirmation(
        &mut self,
        ctx: &mut C,
        device_id: &DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_addr: &[u8],
    ) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                if let Some(link_addr) = bytes_to_mac(link_addr) {
                    NudHandler::<I, EthernetLinkDevice, _>::set_dynamic_neighbor(
                        self,
                        ctx,
                        &id,
                        neighbor,
                        link_addr,
                        DynamicNeighborUpdateSource::Confirmation,
                    )
                }
            }
            DeviceIdInner::Loopback(LoopbackDeviceId) => {}
        }
    }

    fn flush_neighbor_table(&mut self, ctx: &mut C, device_id: &DeviceId) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                NudHandler::<I, EthernetLinkDevice, _>::flush(self, ctx, &id)
            }
            DeviceIdInner::Loopback(LoopbackDeviceId) => {}
        }
    }
}

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>> BufferIpDeviceContext<Ipv4, NonSyncCtx, B>
    for &'_ SyncCtx<NonSyncCtx>
{
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut NonSyncCtx,
        device: &DeviceId,
        local_addr: SpecifiedAddr<Ipv4Addr>,
        body: S,
    ) -> Result<(), S> {
        send_ip_frame(self, ctx, device, local_addr, body)
    }
}

impl<NonSyncCtx: NonSyncContext> IpDeviceContext<Ipv6, NonSyncCtx> for &'_ SyncCtx<NonSyncCtx> {
    fn with_ip_device_state<O, F: FnOnce(&Ipv6DeviceState<NonSyncCtx::Instant>) -> O>(
        &self,
        device: &Self::DeviceId,
        cb: F,
    ) -> O {
        with_ip_device_state(self, device, |state| cb(&state.ipv6.read()))
    }

    fn with_ip_device_state_mut<O, F: FnOnce(&mut Ipv6DeviceState<NonSyncCtx::Instant>) -> O>(
        &mut self,
        device: &Self::DeviceId,
        cb: F,
    ) -> O {
        with_ip_device_state(self, device, |state| cb(&mut state.ipv6.write()))
    }

    fn with_devices<O, F: FnOnce(Box<dyn Iterator<Item = DeviceId> + '_>) -> O>(&self, cb: F) -> O {
        with_devices(self, cb)
    }

    fn get_mtu(&self, device_id: &Self::DeviceId) -> u32 {
        get_mtu(self, device_id)
    }

    fn join_link_multicast_group(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: &Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    ) {
        join_link_multicast_group(self, ctx, device_id, multicast_addr)
    }

    fn leave_link_multicast_group(
        &mut self,
        ctx: &mut NonSyncCtx,
        device_id: &Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    ) {
        leave_link_multicast_group(self, ctx, device_id, multicast_addr)
    }
}

pub(crate) enum Ipv6DeviceLinkLayerAddr {
    Mac(Mac),
    // Add other link-layer address types as needed.
}

impl AsRef<[u8]> for Ipv6DeviceLinkLayerAddr {
    fn as_ref(&self) -> &[u8] {
        match self {
            Ipv6DeviceLinkLayerAddr::Mac(a) => a.as_ref(),
        }
    }
}

impl<NonSyncCtx: NonSyncContext> Ipv6DeviceContext<NonSyncCtx> for &'_ SyncCtx<NonSyncCtx> {
    type LinkLayerAddr = Ipv6DeviceLinkLayerAddr;

    fn get_link_layer_addr_bytes(
        &self,
        device_id: &Self::DeviceId,
    ) -> Option<Ipv6DeviceLinkLayerAddr> {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                Some(Ipv6DeviceLinkLayerAddr::Mac(ethernet::get_mac(self, &id).get()))
            }
            DeviceIdInner::Loopback(LoopbackDeviceId) => None,
        }
    }

    fn get_eui64_iid(&self, device_id: &Self::DeviceId) -> Option<[u8; 8]> {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                Some(ethernet::get_mac(self, &id).to_eui64_with_magic(Mac::DEFAULT_EUI_MAGIC))
            }
            DeviceIdInner::Loopback(LoopbackDeviceId) => None,
        }
    }

    fn set_link_mtu(&mut self, device_id: &Self::DeviceId, mtu: NonZeroU32) {
        if mtu.get() < Ipv6::MINIMUM_LINK_MTU.into() {
            return;
        }

        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => ethernet::set_mtu(self, &id, mtu),
            DeviceIdInner::Loopback(LoopbackDeviceId) => {}
        }
    }
}

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>> BufferIpDeviceContext<Ipv6, NonSyncCtx, B>
    for &'_ SyncCtx<NonSyncCtx>
{
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut NonSyncCtx,
        device: &DeviceId,
        local_addr: SpecifiedAddr<Ipv6Addr>,
        body: S,
    ) -> Result<(), S> {
        send_ip_frame(self, ctx, device, local_addr, body)
    }
}

impl<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>
    FrameContext<NonSyncCtx, B, EthernetDeviceId> for &'_ SyncCtx<NonSyncCtx>
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut NonSyncCtx,
        device: EthernetDeviceId,
        frame: S,
    ) -> Result<(), S> {
        BufferDeviceLayerEventDispatcher::send_frame(ctx, &device.into(), frame)
    }
}

/// Device IDs identifying Ethernet devices.
#[derive(Clone, Eq, PartialEq, Hash)]
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
#[derive(Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) struct DeviceLayerTimerId(DeviceLayerTimerIdInner);

#[derive(Clone, Eq, PartialEq, Debug, Hash)]
enum DeviceLayerTimerIdInner {
    /// A timer event for an Ethernet device.
    Ethernet(EthernetTimerId<EthernetDeviceId>),
}

impl From<EthernetTimerId<EthernetDeviceId>> for DeviceLayerTimerId {
    fn from(id: EthernetTimerId<EthernetDeviceId>) -> DeviceLayerTimerId {
        DeviceLayerTimerId(DeviceLayerTimerIdInner::Ethernet(id))
    }
}

impl<NonSyncCtx: NonSyncContext> DeviceIdContext<EthernetLinkDevice> for &'_ SyncCtx<NonSyncCtx> {
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
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    id: DeviceLayerTimerId,
) {
    match id.0 {
        DeviceLayerTimerIdInner::Ethernet(id) => ethernet::handle_timer(&mut sync_ctx, ctx, id),
    }
}

#[derive(Clone, Eq, PartialEq, Hash)]
pub(crate) enum DeviceIdInner {
    Ethernet(EthernetDeviceId),
    Loopback(LoopbackDeviceId),
}

/// An ID identifying a device.
#[derive(Clone, Eq, PartialEq, Hash)]
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

impl From<LoopbackDeviceId> for DeviceId {
    fn from(id: LoopbackDeviceId) -> DeviceId {
        DeviceIdInner::Loopback(id).into()
    }
}

impl DeviceId {
    /// Construct a new `DeviceId` for an Ethernet device.
    pub(crate) const fn new_ethernet(id: usize) -> DeviceId {
        DeviceId(DeviceIdInner::Ethernet(EthernetDeviceId(id)))
    }

    pub(crate) fn inner(&self) -> &DeviceIdInner {
        let DeviceId(id) = self;
        id
    }
}

impl IpDeviceId for DeviceId {
    fn is_loopback(&self) -> bool {
        match self.inner() {
            DeviceIdInner::Loopback(LoopbackDeviceId) => true,
            DeviceIdInner::Ethernet(_) => false,
        }
    }
}

impl Display for DeviceId {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        match self.inner() {
            DeviceIdInner::Ethernet(EthernetDeviceId(id)) => write!(f, "Ethernet({})", id),
            DeviceIdInner::Loopback(LoopbackDeviceId) => write!(f, "Loopback"),
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
            DeviceIdInner::Ethernet(EthernetDeviceId(id)) => *id,
            DeviceIdInner::Loopback(LoopbackDeviceId) => 0,
        }
    }

    fn get_variant(&self) -> usize {
        match self.inner() {
            DeviceIdInner::Ethernet(_) => 0,
            DeviceIdInner::Loopback(LoopbackDeviceId) => 1,
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

#[derive(Derivative)]
#[derivative(Default(bound = ""))]
struct Devices<I: Instant> {
    ethernet: IdMap<ReferenceCounted<IpLinkDeviceState<I, EthernetDeviceState>>>,
    loopback: Option<ReferenceCounted<IpLinkDeviceState<I, LoopbackDeviceState>>>,
}

/// The state associated with the device layer.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct DeviceLayerState<I: Instant> {
    devices: RwLock<Devices<I>>,
}

impl<I: Instant> DeviceLayerState<I> {
    /// Add a new ethernet device to the device layer.
    ///
    /// `add` adds a new `EthernetDeviceState` with the given MAC address and
    /// MTU. The MTU will be taken as a limit on the size of Ethernet payloads -
    /// the Ethernet header is not counted towards the MTU.
    pub(crate) fn add_ethernet_device(&self, mac: UnicastAddr<Mac>, mtu: u32) -> DeviceId {
        let Devices { ethernet, loopback: _ } = &mut *self.devices.write();

        let id = ethernet
            .push(IpLinkDeviceState::new(EthernetDeviceStateBuilder::new(mac, mtu).build()).into());
        debug!("adding Ethernet device with ID {} and MTU {}", id, mtu);
        DeviceId::new_ethernet(id)
    }

    /// Adds a new loopback device to the device layer.
    pub(crate) fn add_loopback_device(&self, mtu: u32) -> Result<DeviceId, ExistsError> {
        let Devices { ethernet: _, loopback } = &mut *self.devices.write();

        if let Some(_) = loopback {
            return Err(ExistsError);
        }

        *loopback = Some(IpLinkDeviceState::new(LoopbackDeviceState::new(mtu)).into());

        debug!("added loopback device");

        Ok(DeviceIdInner::Loopback(LoopbackDeviceId).into())
    }
}

/// An event dispatcher for the device layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait DeviceLayerEventDispatcher {
    /// Signals to the dispatcher that RX frames are available and ready to be
    /// handled by [`handle_queued_rx_packets`].
    ///
    /// Implementations must make sure that [`handle_queued_rx_packets`] is
    /// scheduled to be called as soon as possible so that enqueued RX frames
    /// are promptly handled.
    fn wake_rx_task(&mut self, device: &DeviceId);
}

/// A [`DeviceLayerEventDispatcher`] with a buffer.
pub trait BufferDeviceLayerEventDispatcher<B: BufferMut>: DeviceLayerEventDispatcher {
    /// Send a frame to a device driver.
    ///
    /// If there was an MTU error while attempting to serialize the frame, the
    /// original serializer is returned in the `Err` variant. All other errors
    /// (for example, errors in allocating a buffer) are silently ignored and
    /// reported as success.
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: &DeviceId,
        frame: S,
    ) -> Result<(), S>;
}

/// Handle a batch of queued RX packets for the device.
///
/// If packets remain in the RX queue after a batch of RX packets has been
/// handled, the RX task will be scheduled to run again so the next batch of
/// RX packets may be handled. See [`DeviceLayerEventDispatcher::wake_rx_task`]
/// for more details.
pub fn handle_queued_rx_packets<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
) {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            panic!("ethernet device {} does not support RX queues", id)
        }
        DeviceIdInner::Loopback(id) => {
            ReceiveQueueHandler::<LoopbackDevice, _>::handle_queued_rx_packets(
                &mut sync_ctx,
                ctx,
                id,
            )
        }
    }
}

/// Remove a device from the device layer.
///
/// # Panics
///
/// Panics if `device` does not refer to an existing device.
pub fn remove_device<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: DeviceId,
) {
    // Uninstall all routes associated with the device.
    crate::ip::del_device_routes::<Ipv4, _, _>(&mut sync_ctx, ctx, &device);
    crate::ip::del_device_routes::<Ipv6, _, _>(&mut sync_ctx, ctx, &device);

    let mut devices = sync_ctx.state.device.devices.write();

    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            let EthernetDeviceId(id) = id;
            let _: ReferenceCounted<IpLinkDeviceState<_, _>> = devices
                .ethernet
                .remove(*id)
                .unwrap_or_else(|| panic!("no such Ethernet device: {}", id));
            debug!("removing Ethernet device with ID {}", id);
        }
        DeviceIdInner::Loopback(LoopbackDeviceId) => {
            let _: ReferenceCounted<IpLinkDeviceState<_, _>> =
                devices.loopback.take().expect("loopback device does not exist");
            debug!("removing Loopback device");
        }
    }
}

/// Adds a new Ethernet device to the stack and installs routes for it.
///
/// Adds a new Ethernet device and installs routes for the link-local subnet and
/// the multicast subnets.
pub fn add_ethernet_device<NonSyncCtx: NonSyncContext>(
    sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    mac: UnicastAddr<Mac>,
    mtu: u32,
) -> DeviceId {
    let id = sync_ctx.state.device.add_ethernet_device(mac, mtu);

    const LINK_LOCAL_SUBNET: Subnet<Ipv6Addr> = net_declare::net_subnet_v6!("fe80::/64");
    crate::add_route(
        sync_ctx,
        ctx,
        AddableEntry::without_gateway(LINK_LOCAL_SUBNET.into(), id.clone()).into(),
    )
    // Adding the link local route must succeed: we're providing correct
    // arguments and the device has just been created.
    .unwrap_or_else(|e| unreachable!("add link local route: {e}"));

    add_multicast_routes(sync_ctx, ctx, &id);

    id
}

/// Adds a new loopback device to the stack and installs routes for it.
///
/// Adds a new loopback device to the stack and installs routes for the loopback
/// subnet and multicast subnets for the device. Only one loopback device may be
/// installed at any point in time, so if there is one already, an error is
/// returned.
pub fn add_loopback_device<NonSyncCtx: NonSyncContext>(
    sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    mtu: u32,
) -> Result<DeviceId, crate::error::ExistsError> {
    let id = sync_ctx.state.device.add_loopback_device(mtu)?;

    for entry in [
        AddableEntryEither::from(AddableEntry::without_gateway(Ipv4::LOOPBACK_SUBNET, id.clone())),
        AddableEntryEither::from(AddableEntry::without_gateway(Ipv6::LOOPBACK_SUBNET, id.clone())),
    ] {
        crate::add_route(sync_ctx, ctx, entry.clone())
            // Adding the loopback route must succeed: we're providing correct
            // arguments and the device has just been created.
            .unwrap_or_else(|e| unreachable!("add loopback route for {:?} failed: {:?}", entry, e));
    }

    add_multicast_routes(sync_ctx, ctx, &id);

    Ok(id)
}

fn add_multicast_routes<NonSyncCtx: NonSyncContext>(
    sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    id: &DeviceId,
) {
    for entry in [
        AddableEntryEither::from(AddableEntry::without_gateway(Ipv4::MULTICAST_SUBNET, id.clone())),
        AddableEntryEither::from(AddableEntry::without_gateway(Ipv6::MULTICAST_SUBNET, id.clone())),
    ] {
        crate::add_route(sync_ctx, ctx, entry.clone())
            // Adding the multicast routes must succeed: we're providing correct
            // arguments and the device has just been created.
            .unwrap_or_else(|e| unreachable!("add multicast route {entry:?} for {id:?}: {e}"));
    }
}

/// Receive a device layer frame from the network.
pub fn receive_frame<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
    buffer: B,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::receive_frame(&mut sync_ctx, ctx, id, buffer))
        }
        DeviceIdInner::Loopback(LoopbackDeviceId) => Err(NotSupportedError),
    }
}

/// Set the promiscuous mode flag on `device`.
// TODO(rheacock): remove `allow(dead_code)` when this is used.
#[allow(dead_code)]
pub(crate) fn set_promiscuous_mode<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::set_promiscuous_mode(&mut sync_ctx, ctx, id, enabled))
        }
        DeviceIdInner::Loopback(LoopbackDeviceId) => Err(NotSupportedError),
    }
}

/// Adds an IP address and associated subnet to this device.
///
/// For IPv6, this function also joins the solicited-node multicast group and
/// begins performing Duplicate Address Detection (DAD).
pub(crate) fn add_ip_addr_subnet<NonSyncCtx: NonSyncContext, A: IpAddress>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
    addr_sub: AddrSubnet<A>,
) -> Result<(), ExistsError> {
    trace!("add_ip_addr_subnet: adding addr {:?} to device {:?}", addr_sub, device);

    match addr_sub.into() {
        AddrSubnetEither::V4(addr_sub) => {
            crate::ip::device::add_ipv4_addr_subnet(&mut sync_ctx, ctx, device, addr_sub)
        }
        AddrSubnetEither::V6(addr_sub) => crate::ip::device::add_ipv6_addr_subnet(
            &mut sync_ctx,
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
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
    addr: &SpecifiedAddr<A>,
) -> Result<(), NotFoundError> {
    trace!("del_ip_addr: removing addr {:?} from device {:?}", addr, device);

    match Into::into(*addr) {
        IpAddr::V4(addr) => crate::ip::device::del_ipv4_addr(&mut sync_ctx, ctx, &device, &addr),
        IpAddr::V6(addr) => crate::ip::device::del_ipv6_addr_with_reason(
            &mut sync_ctx,
            ctx,
            &device,
            &addr,
            crate::ip::device::state::DelIpv6AddrReason::ManualAction,
        ),
    }
}

impl<NonSyncCtx: NonSyncContext> DualStackDeviceIdContext for &'_ SyncCtx<NonSyncCtx> {
    type DualStackDeviceId = DeviceId;
}

// Temporary blanket impl until we switch over entirely to the traits defined in
// the `context` module.
impl<NonSyncCtx: NonSyncContext, I: Ip> IpDeviceIdContext<I> for &'_ SyncCtx<NonSyncCtx> {
    type DeviceId = DeviceId;

    fn loopback_id(&self) -> Option<DeviceId> {
        let devices = self.state.device.devices.read();
        devices.loopback.as_ref().map(|_state| DeviceIdInner::Loopback(LoopbackDeviceId).into())
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
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
    addr: Ipv4Addr,
    mac: UnicastAddr<Mac>,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => Ok(self::ethernet::insert_static_arp_table_entry(
            &mut sync_ctx,
            ctx,
            id,
            addr,
            mac.into(),
        )),
        DeviceIdInner::Loopback(LoopbackDeviceId) => Err(NotSupportedError),
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
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
    addr: UnicastAddr<Ipv6Addr>,
    mac: Mac,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::insert_ndp_table_entry(&mut sync_ctx, ctx, id, addr, mac))
        }
        DeviceIdInner::Loopback(LoopbackDeviceId) => Err(NotSupportedError),
    }
}

/// Gets the IPv4 Configuration for a `device`.
pub fn get_ipv4_configuration<NonSyncCtx: NonSyncContext>(
    mut ctx: &SyncCtx<NonSyncCtx>,
    device: &DeviceId,
) -> Ipv4DeviceConfiguration {
    crate::ip::device::get_ipv4_configuration(&mut ctx, device)
}

/// Gets the IPv6 Configuration for a `device`.
pub fn get_ipv6_configuration<NonSyncCtx: NonSyncContext>(
    mut ctx: &SyncCtx<NonSyncCtx>,
    device: &DeviceId,
) -> Ipv6DeviceConfiguration {
    crate::ip::device::get_ipv6_configuration(&mut ctx, device)
}

/// Updates the IPv4 Configuration for a `device`.
pub fn update_ipv4_configuration<
    NonSyncCtx: NonSyncContext,
    F: FnOnce(&mut Ipv4DeviceConfiguration),
>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
    update_cb: F,
) {
    crate::ip::device::update_ipv4_configuration(&mut sync_ctx, ctx, device, update_cb)
}

/// Updates the IPv6 Configuration for a `device`.
pub fn update_ipv6_configuration<
    NonSyncCtx: NonSyncContext,
    F: FnOnce(&mut Ipv6DeviceConfiguration),
>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device: &DeviceId,
    update_cb: F,
) {
    crate::ip::device::update_ipv6_configuration(&mut sync_ctx, ctx, device, update_cb)
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
        crate::device::receive_frame(sync_ctx, non_sync_ctx, &device, buffer).unwrap()
    }

    pub fn enable_device<NonSyncCtx: NonSyncContext>(
        mut sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        device: &DeviceId,
    ) {
        crate::ip::device::update_ipv4_configuration(&mut sync_ctx, ctx, device, |config| {
            config.ip_config.ip_enabled = true;
        });
        crate::ip::device::update_ipv6_configuration(&mut sync_ctx, ctx, device, |config| {
            config.ip_config.ip_enabled = true;
        });
    }
}

#[cfg(test)]
mod tests {
    use alloc::{collections::HashMap, vec::Vec};

    use net_declare::{net_mac, net_subnet_v4, net_subnet_v6};
    use net_types::ip::SubnetEither;

    use super::*;
    use crate::{
        testutil::{
            DummyEventDispatcherBuilder, DummyEventDispatcherConfig, DummySyncCtx, DUMMY_CONFIG_V4,
        },
        Ctx,
    };

    #[test]
    fn test_iter_devices() {
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;

        fn check(sync_ctx: &&DummySyncCtx, expected: &[DeviceId]) {
            assert_eq!(
                IpDeviceContext::<Ipv4, _>::with_devices(sync_ctx, |devices| devices
                    .collect::<Vec<_>>()),
                expected
            );
            assert_eq!(
                IpDeviceContext::<Ipv6, _>::with_devices(sync_ctx, |devices| devices
                    .collect::<Vec<_>>()),
                expected
            );
        }
        check(&sync_ctx, &[][..]);

        let loopback_device =
            crate::device::add_loopback_device(&mut sync_ctx, &mut non_sync_ctx, 55 /* mtu */)
                .expect("error adding loopback device");
        check(&sync_ctx, &[loopback_device.clone()][..]);

        let DummyEventDispatcherConfig {
            subnet: _,
            local_ip: _,
            local_mac,
            remote_ip: _,
            remote_mac: _,
        } = DUMMY_CONFIG_V4;
        let ethernet_device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac,
            0, /* mtu */
        );
        check(&sync_ctx, &[ethernet_device, loopback_device][..]);
    }

    fn get_routes<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        device: &DeviceId,
    ) -> HashMap<SubnetEither, Option<IpAddr<SpecifiedAddr<Ipv4Addr>, SpecifiedAddr<Ipv6Addr>>>>
    {
        crate::ip::get_all_routes(&sync_ctx)
            .into_iter()
            .map(|entry| {
                let (subnet, route_device, gateway) = entry.into_subnet_device_gateway();
                assert_eq!(&route_device, device);
                (subnet, gateway)
            })
            .collect::<HashMap<_, _>>()
    }

    #[test]
    fn test_add_loopback_device_routes() {
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();

        let loopback_device =
            crate::device::add_loopback_device(&mut sync_ctx, &mut non_sync_ctx, 55 /* mtu */)
                .expect("error adding loopback device");

        let expected = [
            net_subnet_v4!("127.0.0.0/8").into(),
            net_subnet_v6!("::1/128").into(),
            net_subnet_v4!("224.0.0.0/4").into(),
            net_subnet_v6!("ff00::/8").into(),
        ]
        .map(|s: SubnetEither| (s, None));
        assert_eq!(get_routes(&sync_ctx, &loopback_device), HashMap::from(expected));
    }

    #[test]
    fn test_add_ethernet_device_routes() {
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();

        let ethernet_device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            UnicastAddr::new(net_mac!("aa:bb:cc:dd:ee:ff")).expect("MAC is unicast"),
            55, /* mtu */
        );

        let expected = [
            net_subnet_v6!("fe80::/64").into(),
            net_subnet_v4!("224.0.0.0/4").into(),
            net_subnet_v6!("ff00::/8").into(),
        ]
        .map(|s: SubnetEither| (s, None));
        assert_eq!(get_routes(&sync_ctx, &ethernet_device), HashMap::from(expected));
    }
}
