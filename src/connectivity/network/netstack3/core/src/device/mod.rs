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
        Ipv6SourceAddr,
    },
    MulticastAddr, SpecifiedAddr, UnicastAddr,
};
use packet::{Buf, BufferMut, EmptyBuf, Serializer};
use packet_formats::icmp::ndp::NdpPacket;
use zerocopy::ByteSlice;

use crate::{
    context::{
        CounterContext, DualStateContext, EventContext, FrameContext, InstantContext,
        RecvFrameContext, RngContext, TimerContext,
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
            state::{
                AddrConfig, DualStackIpDeviceState, Ipv4DeviceConfiguration, Ipv4DeviceState,
                Ipv6DeviceConfiguration, Ipv6DeviceState,
            },
            BufferIpDeviceContext, IpDeviceContext, Ipv6DeviceContext,
        },
        IpDeviceId, IpDeviceIdContext,
    },
    BlanketCoreContext, BufferDispatcher, Ctx, EventDispatcher, Instant,
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

/// The context provided by the device layer to a particular IP device
/// implementation.
///
/// A blanket implementation is provided for all types that implement
/// the inherited traits.
pub(crate) trait IpLinkDeviceContext<D: LinkDevice, TimerId>:
    DeviceIdContext<D>
    + CounterContext
    + RngContext
    + DualStateContext<
        IpLinkDeviceState<<Self as InstantContext>::Instant, D::State>,
        <Self as RngContext>::Rng,
        <Self as DeviceIdContext<D>>::DeviceId,
    > + TimerContext<TimerId>
    + FrameContext<(), EmptyBuf, <Self as DeviceIdContext<D>>::DeviceId>
    + FrameContext<(), Buf<Vec<u8>>, <Self as DeviceIdContext<D>>::DeviceId>
{
}

impl<
        D: LinkDevice,
        TimerId,
        C: DeviceIdContext<D>
            + CounterContext
            + RngContext
            + DualStateContext<
                IpLinkDeviceState<<Self as InstantContext>::Instant, D::State>,
                <Self as RngContext>::Rng,
                <Self as DeviceIdContext<D>>::DeviceId,
            > + TimerContext<TimerId>
            + FrameContext<(), EmptyBuf, <Self as DeviceIdContext<D>>::DeviceId>
            + FrameContext<(), Buf<Vec<u8>>, <Self as DeviceIdContext<D>>::DeviceId>,
    > IpLinkDeviceContext<D, TimerId> for C
{
}

/// `IpLinkDeviceContext` with an extra `B: BufferMut` parameter.
///
/// `BufferIpLinkDeviceContext` is used when sending a frame is required.
trait BufferIpLinkDeviceContext<D: LinkDevice, TimerId, B: BufferMut>:
    IpLinkDeviceContext<D, TimerId>
    + FrameContext<(), B, <Self as DeviceIdContext<D>>::DeviceId>
    + RecvFrameContext<B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv4>>
    + RecvFrameContext<B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv6>>
{
}

impl<
        D: LinkDevice,
        TimerId,
        B: BufferMut,
        C: IpLinkDeviceContext<D, TimerId>
            + FrameContext<(), B, <Self as DeviceIdContext<D>>::DeviceId>
            + RecvFrameContext<B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv4>>
            + RecvFrameContext<B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv6>>,
    > BufferIpLinkDeviceContext<D, TimerId, B> for C
{
}

impl<B: BufferMut, D: BufferDispatcher<B>, C: BlanketCoreContext>
    RecvFrameContext<B, RecvIpFrameMeta<EthernetDeviceId, Ipv4>> for Ctx<D, C>
{
    fn receive_frame(&mut self, metadata: RecvIpFrameMeta<EthernetDeviceId, Ipv4>, frame: B) {
        crate::ip::receive_ipv4_packet(self, metadata.device.into(), metadata.frame_dst, frame);
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>, C: BlanketCoreContext>
    RecvFrameContext<B, RecvIpFrameMeta<EthernetDeviceId, Ipv6>> for Ctx<D, C>
{
    fn receive_frame(&mut self, metadata: RecvIpFrameMeta<EthernetDeviceId, Ipv6>, frame: B) {
        crate::ip::receive_ipv6_packet(self, metadata.device.into(), metadata.frame_dst, frame);
    }
}

impl<D: EventDispatcher, C: BlanketCoreContext>
    DualStateContext<IpLinkDeviceState<C::Instant, EthernetDeviceState>, C::Rng, EthernetDeviceId>
    for Ctx<D, C>
{
    fn get_states_with(
        &self,
        id0: EthernetDeviceId,
        _id1: (),
    ) -> (&IpLinkDeviceState<C::Instant, EthernetDeviceState>, &C::Rng) {
        (&self.state.device.ethernet.get(id0.0).unwrap(), self.ctx.rng())
    }

    fn get_states_mut_with(
        &mut self,
        id0: EthernetDeviceId,
        _id1: (),
    ) -> (&mut IpLinkDeviceState<C::Instant, EthernetDeviceState>, &mut C::Rng) {
        let Ctx { state, dispatcher: _, ctx } = self;
        (state.device.ethernet.get_mut(id0.0).unwrap(), ctx.rng_mut())
    }
}

fn get_ip_device_state<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &Ctx<D, C>,
    device: DeviceId,
) -> &DualStackIpDeviceState<C::Instant> {
    match device.inner() {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)) => {
            &ctx.state.device.ethernet.get(id).unwrap().ip
        }
        DeviceIdInner::Loopback => &ctx.state.device.loopback.as_ref().unwrap().ip,
    }
}

fn get_ip_device_state_mut_and_rng<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
) -> (&mut DualStackIpDeviceState<C::Instant>, &mut C::Rng) {
    let state = match device.inner() {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)) => {
            &mut ctx.state.device.ethernet.get_mut(id).unwrap().ip
        }
        DeviceIdInner::Loopback => &mut ctx.state.device.loopback.as_mut().unwrap().ip,
    };

    (state, ctx.ctx.rng_mut())
}

fn iter_devices<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &Ctx<D, C>,
) -> impl Iterator<Item = DeviceId> + '_ {
    let DeviceLayerState { ethernet, loopback } = &ctx.state.device;

    ethernet
        .iter()
        .map(|(id, _state)| DeviceId::new_ethernet(id))
        .chain(loopback.iter().map(|_state| DeviceIdInner::Loopback.into()))
}

fn get_mtu<D: EventDispatcher, C: BlanketCoreContext>(ctx: &Ctx<D, C>, device: DeviceId) -> u32 {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::get_mtu(ctx, &mut (), id),
        DeviceIdInner::Loopback => self::loopback::get_mtu(ctx),
    }
}

fn join_link_multicast_group<D: EventDispatcher, C: BlanketCoreContext, A: IpAddress>(
    ctx: &mut Ctx<D, C>,
    device_id: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    match device_id.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::join_link_multicast(
            ctx,
            &mut (),
            id,
            MulticastAddr::from(&multicast_addr),
        ),
        DeviceIdInner::Loopback => {}
    }
}

fn leave_link_multicast_group<D: EventDispatcher, C: BlanketCoreContext, A: IpAddress>(
    ctx: &mut Ctx<D, C>,
    device_id: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    match device_id.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::leave_link_multicast(
            ctx,
            &mut (),
            id,
            MulticastAddr::from(&multicast_addr),
        ),
        DeviceIdInner::Loopback => {}
    }
}

impl<D: EventDispatcher, C: BlanketCoreContext, T> EventContext<T> for Ctx<D, C>
where
    D: EventContext<T>,
{
    fn on_event(&mut self, event: T) {
        self.dispatcher.on_event(event)
    }
}

impl<D: EventDispatcher, C: BlanketCoreContext> IpDeviceContext<Ipv4> for Ctx<D, C> {
    fn get_ip_device_state(&self, device: DeviceId) -> &Ipv4DeviceState<Self::Instant> {
        &get_ip_device_state(self, device).ipv4
    }

    fn get_ip_device_state_mut_and_rng(
        &mut self,
        device: DeviceId,
    ) -> (&mut Ipv4DeviceState<Self::Instant>, &mut C::Rng) {
        let (state, rng) = get_ip_device_state_mut_and_rng(self, device);
        (&mut state.ipv4, rng)
    }

    fn iter_devices(&self) -> Box<dyn Iterator<Item = DeviceId> + '_> {
        Box::new(iter_devices(self))
    }

    fn get_mtu(&self, device_id: Self::DeviceId) -> u32 {
        get_mtu(self, device_id)
    }

    fn join_link_multicast_group(
        &mut self,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv4Addr>,
    ) {
        join_link_multicast_group(self, device_id, multicast_addr)
    }

    fn leave_link_multicast_group(
        &mut self,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv4Addr>,
    ) {
        leave_link_multicast_group(self, device_id, multicast_addr)
    }
}

fn send_ip_frame<
    B: BufferMut,
    D: BufferDispatcher<B>,
    C: BlanketCoreContext,
    S: Serializer<Buffer = B>,
    A: IpAddress,
>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
    local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            self::ethernet::send_ip_frame(ctx, &mut (), id, local_addr, body)
        }
        DeviceIdInner::Loopback => self::loopback::send_ip_frame(ctx, local_addr, body),
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>, C: BlanketCoreContext> BufferIpDeviceContext<Ipv4, B>
    for Ctx<D, C>
{
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        local_addr: SpecifiedAddr<Ipv4Addr>,
        body: S,
    ) -> Result<(), S> {
        send_ip_frame(self, device, local_addr, body)
    }
}

impl<D: EventDispatcher, C: BlanketCoreContext> IpDeviceContext<Ipv6> for Ctx<D, C> {
    fn get_ip_device_state(&self, device: DeviceId) -> &Ipv6DeviceState<Self::Instant> {
        &get_ip_device_state(self, device).ipv6
    }

    fn get_ip_device_state_mut_and_rng(
        &mut self,
        device: DeviceId,
    ) -> (&mut Ipv6DeviceState<Self::Instant>, &mut C::Rng) {
        let (state, rng) = get_ip_device_state_mut_and_rng(self, device);
        (&mut state.ipv6, rng)
    }

    fn iter_devices(&self) -> Box<dyn Iterator<Item = DeviceId> + '_> {
        Box::new(iter_devices(self))
    }

    fn get_mtu(&self, device_id: Self::DeviceId) -> u32 {
        get_mtu(self, device_id)
    }

    fn join_link_multicast_group(
        &mut self,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    ) {
        join_link_multicast_group(self, device_id, multicast_addr)
    }

    fn leave_link_multicast_group(
        &mut self,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<Ipv6Addr>,
    ) {
        leave_link_multicast_group(self, device_id, multicast_addr)
    }
}

impl<D: EventDispatcher, C: BlanketCoreContext> Ipv6DeviceContext for Ctx<D, C> {
    fn get_link_layer_addr_bytes(&self, device_id: Self::DeviceId) -> Option<&[u8]> {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                Some(ethernet::get_mac(self, &mut (), id).as_ref().as_ref())
            }
            DeviceIdInner::Loopback => None,
        }
    }

    fn get_eui64_iid(&self, device_id: Self::DeviceId) -> Option<[u8; 8]> {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => Some(
                ethernet::get_mac(self, &mut (), id).to_eui64_with_magic(Mac::DEFAULT_EUI_MAGIC),
            ),
            DeviceIdInner::Loopback => None,
        }
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>, C: BlanketCoreContext> BufferIpDeviceContext<Ipv6, B>
    for Ctx<D, C>
{
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        local_addr: SpecifiedAddr<Ipv6Addr>,
        body: S,
    ) -> Result<(), S> {
        send_ip_frame(self, device, local_addr, body)
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>, C: BlanketCoreContext>
    FrameContext<(), B, EthernetDeviceId> for Ctx<D, C>
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        _ctx: &mut (),
        device: EthernetDeviceId,
        frame: S,
    ) -> Result<(), S> {
        DeviceLayerEventDispatcher::send_frame(&mut self.dispatcher, device.into(), frame)
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

impl<D: EventDispatcher, C: BlanketCoreContext> DeviceIdContext<EthernetLinkDevice> for Ctx<D, C> {
    type DeviceId = EthernetDeviceId;
}

impl_timer_context!(
    DeviceLayerTimerId,
    EthernetTimerId<EthernetDeviceId>,
    DeviceLayerTimerId(DeviceLayerTimerIdInner::Ethernet(id)),
    id
);

/// Handle a timer event firing in the device layer.
pub(crate) fn handle_timer<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &mut Ctx<D, C>,
    id: DeviceLayerTimerId,
) {
    match id.0 {
        DeviceLayerTimerIdInner::Ethernet(id) => ethernet::handle_timer(ctx, &mut (), id),
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
/// This function returns frames for the bindings to send if the shutdown is
/// graceful - they can be safely ignored otherwise.
///
/// # Panics
///
/// Panics if `device` does not refer to an existing device.
pub fn remove_device<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
) -> Option<Vec<usize>> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            // TODO(rheacock): Generate any final frames to send here.
            crate::device::ethernet::deinitialize(ctx, &mut (), id);
            let EthernetDeviceId(id) = id;
            let _: IpLinkDeviceState<_, _> = ctx
                .state
                .device
                .ethernet
                .remove(id)
                .unwrap_or_else(|| panic!("no such Ethernet device: {}", id));
            debug!("removing Ethernet device with ID {}", id);
            None
        }
        DeviceIdInner::Loopback => {
            let _: IpLinkDeviceState<_, _> =
                ctx.state.device.loopback.take().expect("loopback device does not exist");
            debug!("removing Loopback device");
            None
        }
    }
}

/// Receive a device layer frame from the network.
pub fn receive_frame<B: BufferMut, D: BufferDispatcher<B>, C: BlanketCoreContext>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
    buffer: B,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => Ok(self::ethernet::receive_frame(ctx, &mut (), id, buffer)),
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Set the promiscuous mode flag on `device`.
// TODO(rheacock): remove `allow(dead_code)` when this is used.
#[allow(dead_code)]
pub(crate) fn set_promiscuous_mode<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::set_promiscuous_mode(ctx, &mut (), id, enabled))
        }
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Adds an IP address and associated subnet to this device.
///
/// For IPv6, this function also joins the solicited-node multicast group and
/// begins performing Duplicate Address Detection (DAD).
pub(crate) fn add_ip_addr_subnet<D: EventDispatcher, C: BlanketCoreContext, A: IpAddress>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
    addr_sub: AddrSubnet<A>,
) -> Result<(), ExistsError> {
    trace!("add_ip_addr_subnet: adding addr {:?} to device {:?}", addr_sub, device);

    match addr_sub.into() {
        AddrSubnetEither::V4(addr_sub) => {
            crate::ip::device::add_ipv4_addr_subnet(ctx, &mut (), device, addr_sub)
                .map(|()| crate::ip::on_routing_state_updated::<Ipv4, _, _>(ctx, &mut ()))
        }
        AddrSubnetEither::V6(addr_sub) => crate::ip::device::add_ipv6_addr_subnet(
            ctx,
            &mut (),
            device,
            addr_sub,
            AddrConfig::Manual,
        )
        .map(|()| crate::ip::on_routing_state_updated::<Ipv6, _, _>(ctx, &mut ())),
    }
}

/// Removes an IP address and associated subnet from this device.
///
/// Should only be called on user action.
pub(crate) fn del_ip_addr<D: EventDispatcher, C: BlanketCoreContext, A: IpAddress>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
    addr: &SpecifiedAddr<A>,
) -> Result<(), NotFoundError> {
    trace!("del_ip_addr: removing addr {:?} from device {:?}", addr, device);

    match Into::into(*addr) {
        IpAddr::V4(addr) => crate::ip::device::del_ipv4_addr(ctx, &mut (), device, &addr)
            .map(|()| crate::ip::on_routing_state_updated::<Ipv4, _, _>(ctx, &mut ())),
        IpAddr::V6(addr) => crate::ip::device::del_ipv6_addr_with_reason(
            ctx,
            &mut (),
            device,
            &addr,
            crate::ip::device::state::DelIpv6AddrReason::ManualAction,
        )
        .map(|()| crate::ip::on_routing_state_updated::<Ipv6, _, _>(ctx, &mut ())),
    }
}

// Temporary blanket impl until we switch over entirely to the traits defined in
// the `context` module.
impl<D: EventDispatcher, C: BlanketCoreContext, I: Ip> IpDeviceIdContext<I> for Ctx<D, C> {
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
pub(super) fn insert_static_arp_table_entry<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
    addr: Ipv4Addr,
    mac: UnicastAddr<Mac>,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::insert_static_arp_table_entry(ctx, &mut (), id, addr, mac.into()))
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
pub(crate) fn insert_ndp_table_entry<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
    addr: UnicastAddr<Ipv6Addr>,
    mac: Mac,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::insert_ndp_table_entry(ctx, &mut (), id, addr, mac))
        }
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Gets the IPv4 Configuration for a `device`.
pub fn get_ipv4_configuration<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &Ctx<D, C>,
    device: DeviceId,
) -> Ipv4DeviceConfiguration {
    crate::ip::device::get_ipv4_configuration(ctx, device)
}

/// Gets the IPv6 Configuration for a `device`.
pub fn get_ipv6_configuration<D: EventDispatcher, C: BlanketCoreContext>(
    ctx: &Ctx<D, C>,
    device: DeviceId,
) -> Ipv6DeviceConfiguration {
    crate::ip::device::get_ipv6_configuration(ctx, device)
}

/// Updates the IPv4 Configuration for a `device`.
pub fn update_ipv4_configuration<
    D: EventDispatcher,
    C: BlanketCoreContext,
    F: FnOnce(&mut Ipv4DeviceConfiguration),
>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
    update_cb: F,
) {
    crate::ip::device::update_ipv4_configuration(ctx, &mut (), device, update_cb)
}

/// Updates the IPv6 Configuration for a `device`.
pub fn update_ipv6_configuration<
    D: EventDispatcher,
    C: BlanketCoreContext,
    F: FnOnce(&mut Ipv6DeviceConfiguration),
>(
    ctx: &mut Ctx<D, C>,
    device: DeviceId,
    update_cb: F,
) {
    crate::ip::device::update_ipv6_configuration(ctx, &mut (), device, update_cb)
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
impl<D: EventDispatcher, C: BlanketCoreContext> NdpPacketHandler<DeviceId> for Ctx<D, C> {
    fn receive_ndp_packet<B: ByteSlice>(
        &mut self,
        device: DeviceId,
        src_ip: Ipv6SourceAddr,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: NdpPacket<B>,
    ) {
        trace!("device::receive_ndp_packet");

        match device.inner() {
            DeviceIdInner::Ethernet(id) => {
                crate::device::ndp::receive_ndp_packet(self, &mut (), id, src_ip, dst_ip, packet);
            }
            DeviceIdInner::Loopback => {
                unimplemented!("TODO(https://fxbug.dev/72378): Handle NDP on loopback")
            }
        }
    }
}

#[cfg(test)]
pub(crate) mod testutil {
    use net_types::ip::{Ipv4, Ipv6};

    use super::*;
    use crate::{
        ip::device::state::{IpDeviceState, IpDeviceStateIpExt},
        Ctx, EventDispatcher,
    };

    pub(crate) trait DeviceTestIpExt<Instant: crate::Instant>:
        IpDeviceStateIpExt<Instant>
    {
        fn get_ip_device_state<D: EventDispatcher, C: BlanketCoreContext<Instant = Instant>>(
            ctx: &Ctx<D, C>,
            device: DeviceId,
        ) -> &IpDeviceState<C::Instant, Self>;
    }

    impl<Instant: crate::Instant> DeviceTestIpExt<Instant> for Ipv4 {
        fn get_ip_device_state<D: EventDispatcher, C: BlanketCoreContext<Instant = Instant>>(
            ctx: &Ctx<D, C>,
            device: DeviceId,
        ) -> &IpDeviceState<C::Instant, Ipv4> {
            crate::ip::device::get_ipv4_device_state(ctx, &mut (), device)
        }
    }

    impl<Instant: crate::Instant> DeviceTestIpExt<Instant> for Ipv6 {
        fn get_ip_device_state<D: EventDispatcher, C: BlanketCoreContext<Instant = Instant>>(
            ctx: &Ctx<D, C>,
            device: DeviceId,
        ) -> &IpDeviceState<C::Instant, Ipv6> {
            crate::ip::device::get_ipv6_device_state(ctx, &mut (), device)
        }
    }

    /// Calls [`receive_frame`], panicking on error.
    pub(crate) fn receive_frame_or_panic<
        B: BufferMut,
        D: BufferDispatcher<B>,
        C: BlanketCoreContext,
    >(
        ctx: &mut Ctx<D, C>,
        device: DeviceId,
        buffer: B,
    ) {
        crate::device::receive_frame(ctx, device, buffer).unwrap()
    }

    pub fn enable_device<D: EventDispatcher, C: BlanketCoreContext>(
        ctx: &mut Ctx<D, C>,
        device: DeviceId,
    ) {
        crate::ip::device::update_ipv4_configuration(ctx, &mut (), device, |config| {
            config.ip_config.ip_enabled = true;
        });
        crate::ip::device::update_ipv6_configuration(ctx, &mut (), device, |config| {
            config.ip_config.ip_enabled = true;
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testutil::{
        DummyCtx, DummyEventDispatcherBuilder, DummyEventDispatcherConfig, DUMMY_CONFIG_V4,
    };

    #[test]
    fn test_iter_devices() {
        let mut ctx = DummyEventDispatcherBuilder::default().build();

        fn check(ctx: &DummyCtx, expected: &[DeviceId]) {
            assert_eq!(IpDeviceContext::<Ipv4>::iter_devices(ctx).collect::<Vec<_>>(), expected);
            assert_eq!(IpDeviceContext::<Ipv6>::iter_devices(ctx).collect::<Vec<_>>(), expected);
        }
        check(&ctx, &[][..]);

        let loopback_device =
            ctx.state.add_loopback_device(55 /* mtu */).expect("error adding loopback device");
        check(&ctx, &[loopback_device][..]);

        let DummyEventDispatcherConfig {
            subnet: _,
            local_ip: _,
            local_mac,
            remote_ip: _,
            remote_mac: _,
        } = DUMMY_CONFIG_V4;
        let ethernet_device = ctx.state.add_ethernet_device(local_mac, 0 /* mtu */);
        check(&ctx, &[ethernet_device, loopback_device][..]);
    }
}
