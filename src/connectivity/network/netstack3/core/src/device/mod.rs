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
        CounterContext, DualStateContext, FrameContext, InstantContext, RecvFrameContext,
        RngContext, TimerContext,
    },
    data_structures::{IdMap, IdMapCollectionKey},
    device::{
        ethernet::{
            EthernetDeviceState, EthernetDeviceStateBuilder, EthernetLinkDevice, EthernetTimerId,
        },
        link::LinkDevice,
        loopback::LoopbackDeviceState,
        ndp::{NdpHandler, NdpPacketHandler},
        state::{CommonDeviceState, DeviceState, InitializationStatus, IpLinkDeviceState},
    },
    error::{ExistsError, NotFoundError, NotSupportedError},
    ip::{
        device::{
            state::{
                AddrConfig, AddressState, DualStackIpDeviceState, Ipv4DeviceConfiguration,
                Ipv4DeviceState, Ipv6AddressEntry, Ipv6DeviceConfiguration, Ipv6DeviceState,
            },
            BufferIpDeviceContext, IpDeviceContext, Ipv6DeviceContext,
        },
        IpDeviceId, IpDeviceIdContext,
    },
    BufferDispatcher, Ctx, EventDispatcher, Instant, StackState,
};

/// A timer ID for duplicate address detection.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) struct DadTimerId<D: LinkDevice, DeviceId> {
    device_id: DeviceId,
    addr: UnicastAddr<Ipv6Addr>,
    _marker: PhantomData<D>,
}

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
pub(crate) trait IpLinkDeviceContext<D: LinkDevice, TimerId>:
    DeviceIdContext<D>
    + CounterContext
    + RngContext
    + DualStateContext<
        IpLinkDeviceState<<Self as InstantContext>::Instant, D::State>,
        <Self as RngContext>::Rng,
        <Self as DeviceIdContext<D>>::DeviceId,
    > + TimerContext<TimerId>
    + FrameContext<EmptyBuf, <Self as DeviceIdContext<D>>::DeviceId>
    + FrameContext<Buf<Vec<u8>>, <Self as DeviceIdContext<D>>::DeviceId>
{
    /// Is `device` usable?
    ///
    /// That is, is it either initializing or initialized?
    fn is_device_usable(&self, device: <Self as DeviceIdContext<D>>::DeviceId) -> bool;
}

impl<D: EventDispatcher> IpLinkDeviceContext<EthernetLinkDevice, EthernetTimerId<EthernetDeviceId>>
    for Ctx<D>
{
    fn is_device_usable(&self, device: EthernetDeviceId) -> bool {
        is_device_usable(&self.state, device.into())
    }
}

/// `IpLinkDeviceContext` with an extra `B: BufferMut` parameter.
///
/// `BufferIpLinkDeviceContext` is used when sending a frame is required.
trait BufferIpLinkDeviceContext<D: LinkDevice, TimerId, B: BufferMut>:
    IpLinkDeviceContext<D, TimerId>
    + FrameContext<B, <Self as DeviceIdContext<D>>::DeviceId>
    + RecvFrameContext<B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv4>>
    + RecvFrameContext<B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv6>>
{
}

impl<
        D: LinkDevice,
        TimerId,
        B: BufferMut,
        C: IpLinkDeviceContext<D, TimerId>
            + FrameContext<B, <Self as DeviceIdContext<D>>::DeviceId>
            + RecvFrameContext<B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv4>>
            + RecvFrameContext<B, RecvIpFrameMeta<<Self as DeviceIdContext<D>>::DeviceId, Ipv6>>,
    > BufferIpLinkDeviceContext<D, TimerId, B> for C
{
}

impl<B: BufferMut, D: BufferDispatcher<B>>
    RecvFrameContext<B, RecvIpFrameMeta<EthernetDeviceId, Ipv4>> for Ctx<D>
{
    fn receive_frame(&mut self, metadata: RecvIpFrameMeta<EthernetDeviceId, Ipv4>, frame: B) {
        crate::ip::receive_ipv4_packet(self, metadata.device.into(), metadata.frame_dst, frame);
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>>
    RecvFrameContext<B, RecvIpFrameMeta<EthernetDeviceId, Ipv6>> for Ctx<D>
{
    fn receive_frame(&mut self, metadata: RecvIpFrameMeta<EthernetDeviceId, Ipv6>, frame: B) {
        crate::ip::receive_ipv6_packet(self, metadata.device.into(), metadata.frame_dst, frame);
    }
}

impl<D: EventDispatcher>
    DualStateContext<IpLinkDeviceState<D::Instant, EthernetDeviceState>, D::Rng, EthernetDeviceId>
    for Ctx<D>
{
    fn get_states_with(
        &self,
        id0: EthernetDeviceId,
        _id1: (),
    ) -> (&IpLinkDeviceState<D::Instant, EthernetDeviceState>, &D::Rng) {
        (&self.state.device.ethernet.get(id0.0).unwrap().device, self.dispatcher.rng())
    }

    fn get_states_mut_with(
        &mut self,
        id0: EthernetDeviceId,
        _id1: (),
    ) -> (&mut IpLinkDeviceState<D::Instant, EthernetDeviceState>, &mut D::Rng) {
        let Ctx { state, dispatcher } = self;
        (&mut state.device.ethernet.get_mut(id0.0).unwrap().device, dispatcher.rng_mut())
    }
}

fn get_ip_device_state<D: EventDispatcher>(
    ctx: &Ctx<D>,
    device: DeviceId,
) -> &DualStackIpDeviceState<D::Instant> {
    match device.inner() {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)) => {
            &ctx.state.device.ethernet.get(id).unwrap().device.ip
        }
        DeviceIdInner::Loopback => &ctx.state.device.loopback.as_ref().unwrap().device.ip,
    }
}

fn get_ip_device_state_mut_and_rng<D: EventDispatcher>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
) -> (&mut DualStackIpDeviceState<D::Instant>, &mut D::Rng) {
    let state = match device.inner() {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)) => {
            &mut ctx.state.device.ethernet.get_mut(id).unwrap().device.ip
        }
        DeviceIdInner::Loopback => &mut ctx.state.device.loopback.as_mut().unwrap().device.ip,
    };

    (state, ctx.dispatcher.rng_mut())
}

fn iter_devices<D: EventDispatcher>(ctx: &Ctx<D>) -> impl Iterator<Item = DeviceId> + '_ {
    let DeviceLayerState { ethernet, loopback, default_ndp_config: _, default_ipv6_config: _ } =
        &ctx.state.device;

    ethernet
        .iter()
        .filter_map(|(id, state)| state.common.is_initialized().then(|| DeviceId::new_ethernet(id)))
        .chain(loopback.iter().filter_map(|state| {
            state.common.is_initialized().then(|| DeviceIdInner::Loopback.into())
        }))
}

fn get_mtu<D: EventDispatcher>(ctx: &Ctx<D>, device: DeviceId) -> u32 {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::get_mtu(ctx, id),
        DeviceIdInner::Loopback => self::loopback::get_mtu(ctx),
    }
}

fn join_link_multicast_group<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Ctx<D>,
    device_id: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    match device_id.inner() {
        DeviceIdInner::Ethernet(id) => {
            self::ethernet::join_link_multicast(ctx, id, MulticastAddr::from(&multicast_addr))
        }
        DeviceIdInner::Loopback => {}
    }
}

fn leave_link_multicast_group<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Ctx<D>,
    device_id: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    match device_id.inner() {
        DeviceIdInner::Ethernet(id) => {
            self::ethernet::leave_link_multicast(ctx, id, MulticastAddr::from(&multicast_addr))
        }
        DeviceIdInner::Loopback => {}
    }
}

impl<D: EventDispatcher> IpDeviceContext<Ipv4> for Ctx<D> {
    fn get_ip_device_state(&self, device: DeviceId) -> &Ipv4DeviceState<Self::Instant> {
        &get_ip_device_state(self, device).ipv4
    }

    fn get_ip_device_state_mut_and_rng(
        &mut self,
        device: DeviceId,
    ) -> (&mut Ipv4DeviceState<Self::Instant>, &mut D::Rng) {
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

fn send_ip_frame<B: BufferMut, D: BufferDispatcher<B>, S: Serializer<Buffer = B>, A: IpAddress>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    // `device` must not be uninitialized.
    assert!(is_device_usable(&ctx.state, device));

    match device.inner() {
        DeviceIdInner::Ethernet(id) => self::ethernet::send_ip_frame(ctx, id, local_addr, body),
        DeviceIdInner::Loopback => self::loopback::send_ip_frame(ctx, local_addr, body),
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> BufferIpDeviceContext<Ipv4, B> for Ctx<D> {
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        local_addr: SpecifiedAddr<Ipv4Addr>,
        body: S,
    ) -> Result<(), S> {
        send_ip_frame(self, device, local_addr, body)
    }
}

impl<D: EventDispatcher> IpDeviceContext<Ipv6> for Ctx<D> {
    fn get_ip_device_state(&self, device: DeviceId) -> &Ipv6DeviceState<Self::Instant> {
        &get_ip_device_state(self, device).ipv6
    }

    fn get_ip_device_state_mut_and_rng(
        &mut self,
        device: DeviceId,
    ) -> (&mut Ipv6DeviceState<Self::Instant>, &mut D::Rng) {
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

impl<D: EventDispatcher> Ipv6DeviceContext for Ctx<D> {
    fn start_duplicate_address_detection(
        &mut self,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    ) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                self::ethernet::do_duplicate_address_detection(self, id, addr)
            }
            DeviceIdInner::Loopback => {
                let Ipv6AddressEntry { addr_sub: _, state, config: _ } =
                    IpDeviceContext::<Ipv6>::get_ip_device_state_mut(self, device_id)
                        .ip_state
                        .iter_addrs_mut()
                        .find(|e| e.addr_sub().addr() == addr)
                        .expect("should find an address we are performing DAD on");
                match state {
                    AddressState::Tentative { dad_transmits_remaining } => {
                        assert_eq!(
                            dad_transmits_remaining, &None,
                            "TODO(https://fxbug.dev/72378): loopback does not handle DAD yet"
                        );
                    }
                    AddressState::Assigned | AddressState::Deprecated => {
                        panic!("expected address to be tentative")
                    }
                }

                *state = AddressState::Assigned;
            }
        }
    }

    fn stop_duplicate_address_detection(
        &mut self,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    ) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                let _: Option<D::Instant> = self.cancel_timer(EthernetTimerId::Dad(DadTimerId {
                    device_id: id,
                    addr,
                    _marker: core::marker::PhantomData,
                }));
            }
            DeviceIdInner::Loopback => {}
        }
    }

    fn start_soliciting_routers(&mut self, device_id: Self::DeviceId) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                NdpHandler::<EthernetLinkDevice>::start_soliciting_routers(self, id)
            }
            DeviceIdInner::Loopback => {}
        }
    }

    fn stop_soliciting_routers(&mut self, device_id: Self::DeviceId) {
        match device_id.inner() {
            DeviceIdInner::Ethernet(id) => {
                NdpHandler::<EthernetLinkDevice>::stop_soliciting_routers(self, id)
            }
            DeviceIdInner::Loopback => {}
        }
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> BufferIpDeviceContext<Ipv6, B> for Ctx<D> {
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        local_addr: SpecifiedAddr<Ipv6Addr>,
        body: S,
    ) -> Result<(), S> {
        send_ip_frame(self, device, local_addr, body)
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> FrameContext<B, EthernetDeviceId> for Ctx<D> {
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
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

impl<D: EventDispatcher> DeviceIdContext<EthernetLinkDevice> for Ctx<D> {
    type DeviceId = EthernetDeviceId;
}

impl_timer_context!(
    DeviceLayerTimerId,
    EthernetTimerId<EthernetDeviceId>,
    DeviceLayerTimerId(DeviceLayerTimerIdInner::Ethernet(id)),
    id
);

/// Handle a timer event firing in the device layer.
pub(crate) fn handle_timer<D: EventDispatcher>(ctx: &mut Ctx<D>, id: DeviceLayerTimerId) {
    match id.0 {
        DeviceLayerTimerIdInner::Ethernet(id) => ethernet::handle_timer(ctx, id),
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
    pub(crate) fn new_ethernet(id: usize) -> DeviceId {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)).into()
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

/// Builder for a [`DeviceLayerState`].
#[derive(Clone, Default)]
pub struct DeviceStateBuilder {
    /// Default values for NDP's configuration for new interfaces.
    ///
    /// See [`ndp::NdpConfiguration`].
    default_ndp_config: ndp::NdpConfiguration,
    default_ipv6_config: Ipv6DeviceConfiguration,
}

impl DeviceStateBuilder {
    /// Set the default values for NDP's configuration for new interfaces.
    ///
    /// See [`ndp::NdpConfiguration`] for more details.
    pub fn set_default_ndp_config(&mut self, v: ndp::NdpConfiguration) {
        self.default_ndp_config = v;
    }

    /// Set the default IPv6 device configuration or new interfaces.
    pub fn set_default_ipv6_config(&mut self, v: Ipv6DeviceConfiguration) {
        self.default_ipv6_config = v;
    }

    /// Build the [`DeviceLayerState`].
    pub(crate) fn build<I: Instant>(self) -> DeviceLayerState<I> {
        let Self { default_ndp_config, default_ipv6_config } = self;
        DeviceLayerState {
            ethernet: IdMap::new(),
            loopback: None,
            default_ndp_config,
            default_ipv6_config,
        }
    }
}

/// The state associated with the device layer.
pub(crate) struct DeviceLayerState<I: Instant> {
    ethernet: IdMap<DeviceState<IpLinkDeviceState<I, EthernetDeviceState>>>,
    loopback: Option<DeviceState<IpLinkDeviceState<I, LoopbackDeviceState>>>,
    default_ndp_config: ndp::NdpConfiguration,
    default_ipv6_config: Ipv6DeviceConfiguration,
}

impl<I: Instant> DeviceLayerState<I> {
    /// Add a new ethernet device to the device layer.
    ///
    /// `add` adds a new `EthernetDeviceState` with the given MAC address and
    /// MTU. The MTU will be taken as a limit on the size of Ethernet payloads -
    /// the Ethernet header is not counted towards the MTU.
    pub(crate) fn add_ethernet_device(&mut self, mac: UnicastAddr<Mac>, mtu: u32) -> DeviceId {
        let Self { ethernet, loopback: _, default_ndp_config, default_ipv6_config } = self;

        let mut builder = EthernetDeviceStateBuilder::new(mac, mtu);
        builder.set_ndp_config(default_ndp_config.clone());
        let mut ethernet_state = IpLinkDeviceState::new(builder.build());
        ethernet_state.ip.ipv6.config = default_ipv6_config.clone();
        let ethernet_state = DeviceState::new(ethernet_state);
        let id = ethernet.push(ethernet_state);
        debug!("adding Ethernet device with ID {} and MTU {}", id, mtu);
        DeviceId::new_ethernet(id)
    }

    /// Adds a new loopback device to the device layer.
    pub(crate) fn add_loopback_device(&mut self, mtu: u32) -> Result<DeviceId, ExistsError> {
        let Self { ethernet: _, loopback, default_ndp_config: _, default_ipv6_config } = self;

        if let Some(DeviceState { .. }) = loopback {
            return Err(ExistsError);
        }

        let mut loopback_state = IpLinkDeviceState::new(LoopbackDeviceState::new(mtu));
        loopback_state.ip.ipv6.config = default_ipv6_config.clone();
        let loopback_state = DeviceState::new(loopback_state);
        *loopback = Some(loopback_state);

        debug!("added loopback device");

        Ok(DeviceIdInner::Loopback.into())
    }
}

/// Metadata describing an IP packet to be sent in a link-layer frame to a
/// locally-connected host.
pub struct IpFrameMeta<A: IpAddress, D> {
    pub(crate) device: D,
    pub(crate) local_addr: SpecifiedAddr<A>,
}

impl<A: IpAddress, D> IpFrameMeta<A, D> {
    pub(crate) fn new(device: D, local_addr: SpecifiedAddr<A>) -> IpFrameMeta<A, D> {
        IpFrameMeta { device, local_addr }
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> FrameContext<B, IpFrameMeta<Ipv4Addr, DeviceId>>
    for Ctx<D>
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        meta: IpFrameMeta<Ipv4Addr, DeviceId>,
        body: S,
    ) -> Result<(), S> {
        let IpFrameMeta { device, local_addr } = meta;
        BufferIpDeviceContext::<Ipv4, _>::send_ip_frame(self, device, local_addr, body)
    }
}

impl<B: BufferMut, D: BufferDispatcher<B>> FrameContext<B, IpFrameMeta<Ipv6Addr, DeviceId>>
    for Ctx<D>
{
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        meta: IpFrameMeta<Ipv6Addr, DeviceId>,
        body: S,
    ) -> Result<(), S> {
        let IpFrameMeta { device, local_addr } = meta;
        BufferIpDeviceContext::<Ipv6, _>::send_ip_frame(self, device, local_addr, body)
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
    ///
    /// Note, until `device` has been initialized, the netstack promises to not
    /// send any outbound traffic to it. See [`initialize_device`] for more
    /// information.
    fn send_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device: DeviceId,
        frame: S,
    ) -> Result<(), S>;
}

/// Is `device` usable?
///
/// That is, is it either initializing or initialized?
pub(crate) fn is_device_usable<D: EventDispatcher>(
    state: &StackState<D>,
    device: DeviceId,
) -> bool {
    !get_common_device_state(state, device).is_uninitialized()
}

/// Is `device` initialized?
pub(crate) fn is_device_initialized<D: EventDispatcher>(
    state: &StackState<D>,
    device: DeviceId,
) -> bool {
    get_common_device_state(state, device).is_initialized()
}

/// Initialize a device.
///
/// `initialize_device` will start soliciting IPv6 routers on the link if
/// `device` is configured to be a host. If it is configured to be an
/// advertising interface, it will start sending periodic router advertisements.
///
/// `initialize_device` MUST be called after adding the device to the netstack.
/// A device MUST NOT be used until it has been initialized.
///
/// This initialize step is kept separated from the device creation/allocation
/// step so that implementations have a chance to do some work (such as updating
/// implementation specific IDs or state, configure the device or driver, etc.)
/// before the device is actually initialized and used by this netstack.
///
/// See [`StackState::add_ethernet_device`] for information about adding
/// ethernet devices.
///
/// # Panics
///
/// Panics if `device` is already initialized.
pub fn initialize_device<D: EventDispatcher>(ctx: &mut Ctx<D>, device: DeviceId) {
    let state = get_common_device_state_mut(&mut ctx.state, device);

    // `device` must currently be uninitialized.
    assert!(state.is_uninitialized());

    state.set_initialization_status(InitializationStatus::Initializing);

    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            ethernet::initialize_device(ctx, id);
            // All nodes should join the all-nodes multicast group.
            join_ip_multicast(ctx, device, Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS);

            // RFC 4861 section 6.3.7, it implies only a host sends router
            // solicitation messages.
            if !crate::ip::device::is_ipv6_routing_enabled(ctx, device) {
                <Ctx<_> as NdpHandler<EthernetLinkDevice>>::start_soliciting_routers(ctx, id);
            }
        }
        DeviceIdInner::Loopback => {}
    }

    get_common_device_state_mut(&mut ctx.state, device)
        .set_initialization_status(InitializationStatus::Initialized);
}

/// Remove a device from the device layer.
///
/// This function returns frames for the bindings to send if the shutdown is
/// graceful - they can be safely ignored otherwise.
///
/// # Panics
///
/// Panics if `device` does not refer to an existing device.
pub fn remove_device<D: EventDispatcher>(ctx: &mut Ctx<D>, device: DeviceId) -> Option<Vec<usize>> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            // TODO(rheacock): Generate any final frames to send here.
            crate::device::ethernet::deinitialize(ctx, id);
            let EthernetDeviceId(id) = id;
            let _: DeviceState<_> = ctx
                .state
                .device
                .ethernet
                .remove(id)
                .unwrap_or_else(|| panic!("no such Ethernet device: {}", id));
            debug!("removing Ethernet device with ID {}", id);
            None
        }
        DeviceIdInner::Loopback => {
            let _: DeviceState<_> =
                ctx.state.device.loopback.take().expect("loopback device does not exist");
            debug!("removing Loopback device");
            None
        }
    }
}

/// Receive a device layer frame from the network.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub fn receive_frame<B: BufferMut, D: BufferDispatcher<B>>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    buffer: B,
) -> Result<(), NotSupportedError> {
    // `device` must be initialized.
    assert!(is_device_initialized(&ctx.state, device));

    match device.inner() {
        DeviceIdInner::Ethernet(id) => Ok(self::ethernet::receive_frame(ctx, id, buffer)),
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Set the promiscuous mode flag on `device`.
// TODO(rheacock): remove `allow(dead_code)` when this is used.
#[allow(dead_code)]
pub(crate) fn set_promiscuous_mode<D: EventDispatcher>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => Ok(self::ethernet::set_promiscuous_mode(ctx, id, enabled)),
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Adds an IP address and associated subnet to this device.
///
/// For IPv6, this function also joins the solicited-node multicast group and
/// begins performing Duplicate Address Detection (DAD).
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub(crate) fn add_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    addr_sub: AddrSubnet<A>,
) -> Result<(), ExistsError> {
    // `device` must be initialized.
    assert!(is_device_initialized(&ctx.state, device));

    trace!("add_ip_addr_subnet: adding addr {:?} to device {:?}", addr_sub, device);

    match addr_sub.into() {
        AddrSubnetEither::V4(addr_sub) => {
            crate::ip::device::add_ipv4_addr_subnet(ctx, device, addr_sub)
                .map(|()| crate::ip::on_routing_state_updated::<Ipv4, _>(ctx))
        }
        AddrSubnetEither::V6(addr_sub) => {
            crate::ip::device::add_ipv6_addr_subnet(ctx, device, addr_sub, AddrConfig::Manual)
                .map(|()| crate::ip::on_routing_state_updated::<Ipv6, _>(ctx))
        }
    }
}

/// Removes an IP address and associated subnet from this device.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub(crate) fn del_ip_addr<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    addr: &SpecifiedAddr<A>,
) -> Result<(), NotFoundError> {
    // `device` must be initialized.
    assert!(is_device_initialized(&ctx.state, device));

    trace!("del_ip_addr: removing addr {:?} from device {:?}", addr, device);

    match Into::into(*addr) {
        IpAddr::V4(addr) => crate::ip::device::del_ipv4_addr(ctx, device, &addr)
            .map(|()| crate::ip::on_routing_state_updated::<Ipv4, _>(ctx)),
        IpAddr::V6(addr) => crate::ip::device::del_ipv6_addr(ctx, device, &addr)
            .map(|()| crate::ip::on_routing_state_updated::<Ipv6, _>(ctx)),
    }
}

// Temporary blanket impl until we switch over entirely to the traits defined in
// the `context` module.
impl<D: EventDispatcher, I: Ip> IpDeviceIdContext<I> for Ctx<D> {
    type DeviceId = DeviceId;
}

/// Add `device` to a multicast group `multicast_addr`.
///
/// Calling `join_ip_multicast` with the same `device` and `multicast_addr` is
/// completely safe. A counter will be kept for the number of times
/// `join_ip_multicast` has been called with the same `device` and
/// `multicast_addr` pair. To completely leave a multicast group,
/// [`leave_ip_multicast`] must be called the same number of times
/// `join_ip_multicast` has been called for the same `device` and
/// `multicast_addr` pair. The first time `join_ip_multicast` is called for a
/// new `device` and `multicast_addr` pair, the device will actually join the
/// multicast group.
///
/// # Panics
///
/// Panics if `device` is not initialized.
pub(crate) fn join_ip_multicast<D: BufferDispatcher<EmptyBuf> + EventDispatcher, A: IpAddress>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    // `device` must not be uninitialized.
    assert!(is_device_usable(&ctx.state, device));

    trace!("join_ip_multicast: device {:?} joining multicast {:?}", device, multicast_addr);

    match multicast_addr.into() {
        IpAddr::V4(addr) => crate::ip::device::join_ip_multicast::<Ipv4, _>(ctx, device, addr),
        IpAddr::V6(addr) => crate::ip::device::join_ip_multicast::<Ipv6, _>(ctx, device, addr),
    }
}

/// Attempt to remove `device` from a multicast group `multicast_addr`.
///
/// `leave_ip_multicast` will attempt to remove `device` from a multicast group
/// `multicast_addr`. `device` may have "joined" the same multicast address
/// multiple times, so `device` will only leave the multicast group once
/// `leave_ip_multicast` has been called for each corresponding
/// [`join_ip_multicast`]. That is, if `join_ip_multicast` gets called 3 times
/// and `leave_ip_multicast` gets called two times (after all 3
/// `join_ip_multicast` calls), `device` will still be in the multicast group
/// until the next (final) call to `leave_ip_multicast`.
///
/// # Panics
///
/// Panics if `device` is not initialized or `device` is not currently in the
/// multicast group.
// TODO(joshlf): remove `allow(dead_code)` when this is used.
#[cfg_attr(not(test), allow(dead_code))]
pub(crate) fn leave_ip_multicast<D: BufferDispatcher<EmptyBuf>, A: IpAddress>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    // `device` must not be uninitialized.
    assert!(is_device_usable(&ctx.state, device));

    trace!("join_ip_multicast: device {:?} leaving multicast {:?}", device, multicast_addr);

    match multicast_addr.into() {
        IpAddr::V4(addr) => crate::ip::device::leave_ip_multicast::<Ipv4, _>(ctx, device, addr),
        IpAddr::V6(addr) => crate::ip::device::leave_ip_multicast::<Ipv6, _>(ctx, device, addr),
    }
}

/// Get a reference to the common device state for a `device`.
fn get_common_device_state<D: EventDispatcher>(
    state: &StackState<D>,
    device: DeviceId,
) -> &CommonDeviceState {
    match device.inner() {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)) => {
            &state
                .device
                .ethernet
                .get(id)
                .unwrap_or_else(|| panic!("no such Ethernet device: {}", id))
                .common
        }
        DeviceIdInner::Loopback => {
            &state.device.loopback.as_ref().expect("no loopback device").common
        }
    }
}

/// Get a mutable reference to the common device state for a `device`.
fn get_common_device_state_mut<D: EventDispatcher>(
    state: &mut StackState<D>,
    device: DeviceId,
) -> &mut CommonDeviceState {
    match device.inner() {
        DeviceIdInner::Ethernet(EthernetDeviceId(id)) => {
            &mut state
                .device
                .ethernet
                .get_mut(id)
                .unwrap_or_else(|| panic!("no such Ethernet device: {}", id))
                .common
        }
        DeviceIdInner::Loopback => {
            &mut state.device.loopback.as_mut().expect("no loopback device").common
        }
    }
}

/// Insert a static entry into this device's ARP table.
///
/// This will cause any conflicting dynamic entry to be removed, and
/// any future conflicting gratuitous ARPs to be ignored.
// TODO(rheacock): remove `cfg(test)` when this is used. Will probably be
// called by a pub fn in the device mod.
#[cfg(test)]
pub(super) fn insert_static_arp_table_entry<D: EventDispatcher>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    addr: Ipv4Addr,
    mac: UnicastAddr<Mac>,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::insert_static_arp_table_entry(ctx, id, addr, mac.into()))
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
pub(crate) fn insert_ndp_table_entry<D: EventDispatcher>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    addr: UnicastAddr<Ipv6Addr>,
    mac: Mac,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(self::ethernet::insert_ndp_table_entry(ctx, id, addr, mac))
        }
        DeviceIdInner::Loopback => Err(NotSupportedError),
    }
}

/// Updates the NDP Configuration for a `device`.
///
/// Note, some values may not take effect immediately, and may only take effect
/// the next time they are used. These scenarios documented below:
///
///  - Updates to [`NdpConfiguration::dup_addr_detect_transmits`] will only take
///    effect the next time Duplicate Address Detection (DAD) is done. Any DAD
///    processes that have already started will continue using the old value.
///
///  - Updates to [`NdpConfiguration::max_router_solicitations`] will only take
///    effect the next time routers are explicitly solicited. Current router
///    solicitation will continue using the old value.
// TODO(rheacock): remove `allow(dead_code)` when this is used.
#[allow(dead_code)]
pub fn set_ndp_configuration<D: EventDispatcher>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    config: ndp::NdpConfiguration,
) -> Result<(), NotSupportedError> {
    match device.inner() {
        DeviceIdInner::Ethernet(id) => {
            Ok(<Ctx<_> as NdpHandler<EthernetLinkDevice>>::set_configuration(ctx, id, config))
        }
        DeviceIdInner::Loopback => {
            // TODO(https://fxbug.dev/72378): Support NDP configurations on
            // loopback?
            Err(NotSupportedError)
        }
    }
}

/// Updates the IPv4 Configuration for a `device`.
pub fn set_ipv4_configuration<D: EventDispatcher>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    config: Ipv4DeviceConfiguration,
) {
    crate::ip::device::set_ipv4_configuration(ctx, device, config)
}

/// Updates the IPv6 Configuration for a `device`.
pub fn set_ipv6_configuration<D: EventDispatcher>(
    ctx: &mut Ctx<D>,
    device: DeviceId,
    config: Ipv6DeviceConfiguration,
) {
    crate::ip::device::set_ipv6_configuration(ctx, device, config)
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
impl<D: EventDispatcher> NdpPacketHandler<DeviceId> for Ctx<D> {
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
                crate::device::ndp::receive_ndp_packet(self, id, src_ip, dst_ip, packet);
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

    use crate::{
        device::DeviceId,
        ip::device::state::{IpDeviceState, IpDeviceStateIpExt},
        Ctx, EventDispatcher,
    };

    pub(crate) trait DeviceTestIpExt<Instant: crate::Instant>:
        IpDeviceStateIpExt<Instant>
    {
        fn get_ip_device_state<D: EventDispatcher<Instant = Instant>>(
            ctx: &Ctx<D>,
            device: DeviceId,
        ) -> &IpDeviceState<D::Instant, Self>;
    }

    impl<Instant: crate::Instant> DeviceTestIpExt<Instant> for Ipv4 {
        fn get_ip_device_state<D: EventDispatcher<Instant = Instant>>(
            ctx: &Ctx<D>,
            device: DeviceId,
        ) -> &IpDeviceState<D::Instant, Ipv4> {
            crate::ip::device::get_ipv4_device_state(ctx, device)
        }
    }

    impl<Instant: crate::Instant> DeviceTestIpExt<Instant> for Ipv6 {
        fn get_ip_device_state<D: EventDispatcher<Instant = Instant>>(
            ctx: &Ctx<D>,
            device: DeviceId,
        ) -> &IpDeviceState<D::Instant, Ipv6> {
            crate::ip::device::get_ipv6_device_state(ctx, device)
        }
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
        let DummyEventDispatcherConfig {
            subnet: _,
            local_ip: _,
            local_mac,
            remote_ip: _,
            remote_mac: _,
        } = DUMMY_CONFIG_V4;
        let ethernet_device = ctx.state.add_ethernet_device(local_mac, 0 /* mtu */);
        check(&ctx, &[][..]);

        initialize_device(&mut ctx, loopback_device);
        check(&ctx, &[loopback_device][..]);

        initialize_device(&mut ctx, ethernet_device);
        check(&ctx, &[ethernet_device, loopback_device][..]);
    }
}
