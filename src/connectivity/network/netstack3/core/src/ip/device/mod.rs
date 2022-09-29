// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An IP device.

pub mod dad;
mod integration;
pub(crate) mod nud;
pub mod route_discovery;
pub(crate) mod router_solicitation;
pub mod slaac;
pub mod state;

use alloc::{boxed::Box, vec::Vec};
use core::num::{NonZeroU32, NonZeroU8};

#[cfg(test)]
use net_types::ip::IpVersion;
use net_types::{
    ip::{AddrSubnet, AddrSubnetEither, Ip, IpAddress as _, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    MulticastAddr, SpecifiedAddr, UnicastAddr,
};
use packet::{BufferMut, EmptyBuf, Serializer};
use packet_formats::utils::NonZeroDuration;

use crate::{
    context::{
        CounterContext, EventContext, InstantContext, RngContext, TimerContext, TimerHandler,
    },
    error::{ExistsError, NotFoundError},
    ip::{
        device::{
            dad::{DadHandler, DadTimerId},
            nud::NudIpHandler,
            route_discovery::{Ipv6DiscoveredRouteTimerId, RouteDiscoveryHandler},
            router_solicitation::{RsHandler, RsTimerId},
            slaac::{SlaacHandler, SlaacTimerId},
            state::{
                AddrConfig, AddressState, DelIpv6AddrReason, IpDeviceConfiguration, IpDeviceState,
                IpDeviceStateIpExt, Ipv4DeviceConfiguration, Ipv4DeviceState, Ipv6AddressEntry,
                Ipv6DeviceConfiguration, Ipv6DeviceState,
            },
        },
        gmp::{
            igmp::IgmpTimerId, mld::MldDelayedReportTimerId, GmpHandler, GroupJoinResult,
            GroupLeaveResult,
        },
        DualStackDeviceIdContext, IpDeviceIdContext,
    },
    Instant,
};
#[cfg(test)]
use crate::{error::NotSupportedError, ip::IpDeviceId as _};

/// A timer ID for IPv4 devices.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) struct Ipv4DeviceTimerId<DeviceId>(IgmpTimerId<DeviceId>);

impl<DeviceId> From<IgmpTimerId<DeviceId>> for Ipv4DeviceTimerId<DeviceId> {
    fn from(id: IgmpTimerId<DeviceId>) -> Ipv4DeviceTimerId<DeviceId> {
        Ipv4DeviceTimerId(id)
    }
}

// If we are provided with an impl of `TimerContext<Ipv4DeviceTimerId<_>>`, then
// we can in turn provide an impl of `TimerContext` for IGMP.
impl_timer_context!(
    DeviceId,
    Ipv4DeviceTimerId<DeviceId>,
    IgmpTimerId<DeviceId>,
    Ipv4DeviceTimerId(id),
    id
);

/// Handle an IPv4 device timer firing.
pub(crate) fn handle_ipv4_timer<
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
    SC: BufferIpDeviceContext<Ipv4, C, EmptyBuf> + TimerHandler<C, IgmpTimerId<SC::DeviceId>>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    Ipv4DeviceTimerId(id): Ipv4DeviceTimerId<SC::DeviceId>,
) {
    TimerHandler::handle_timer(sync_ctx, ctx, id)
}

/// A timer ID for IPv6 devices.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) enum Ipv6DeviceTimerId<DeviceId> {
    Mld(MldDelayedReportTimerId<DeviceId>),
    Dad(DadTimerId<DeviceId>),
    Rs(RsTimerId<DeviceId>),
    RouteDiscovery(Ipv6DiscoveredRouteTimerId<DeviceId>),
    Slaac(SlaacTimerId<DeviceId>),
}

impl<DeviceId> From<MldDelayedReportTimerId<DeviceId>> for Ipv6DeviceTimerId<DeviceId> {
    fn from(id: MldDelayedReportTimerId<DeviceId>) -> Ipv6DeviceTimerId<DeviceId> {
        Ipv6DeviceTimerId::Mld(id)
    }
}

impl<DeviceId> From<DadTimerId<DeviceId>> for Ipv6DeviceTimerId<DeviceId> {
    fn from(id: DadTimerId<DeviceId>) -> Ipv6DeviceTimerId<DeviceId> {
        Ipv6DeviceTimerId::Dad(id)
    }
}

impl<DeviceId> From<RsTimerId<DeviceId>> for Ipv6DeviceTimerId<DeviceId> {
    fn from(id: RsTimerId<DeviceId>) -> Ipv6DeviceTimerId<DeviceId> {
        Ipv6DeviceTimerId::Rs(id)
    }
}

impl<DeviceId> From<Ipv6DiscoveredRouteTimerId<DeviceId>> for Ipv6DeviceTimerId<DeviceId> {
    fn from(id: Ipv6DiscoveredRouteTimerId<DeviceId>) -> Ipv6DeviceTimerId<DeviceId> {
        Ipv6DeviceTimerId::RouteDiscovery(id)
    }
}

impl<DeviceId> From<SlaacTimerId<DeviceId>> for Ipv6DeviceTimerId<DeviceId> {
    fn from(id: SlaacTimerId<DeviceId>) -> Ipv6DeviceTimerId<DeviceId> {
        Ipv6DeviceTimerId::Slaac(id)
    }
}

// If we are provided with an impl of `TimerContext<Ipv6DeviceTimerId<_>>`, then
// we can in turn provide an impl of `TimerContext` for MLD and DAD.
impl_timer_context!(
    DeviceId,
    Ipv6DeviceTimerId<DeviceId>,
    MldDelayedReportTimerId<DeviceId>,
    Ipv6DeviceTimerId::Mld(id),
    id
);
impl_timer_context!(
    DeviceId,
    Ipv6DeviceTimerId<DeviceId>,
    DadTimerId<DeviceId>,
    Ipv6DeviceTimerId::Dad(id),
    id
);
impl_timer_context!(
    DeviceId,
    Ipv6DeviceTimerId<DeviceId>,
    RsTimerId<DeviceId>,
    Ipv6DeviceTimerId::Rs(id),
    id
);
impl_timer_context!(
    DeviceId,
    Ipv6DeviceTimerId<DeviceId>,
    Ipv6DiscoveredRouteTimerId<DeviceId>,
    Ipv6DeviceTimerId::RouteDiscovery(id),
    id
);
impl_timer_context!(
    DeviceId,
    Ipv6DeviceTimerId<DeviceId>,
    SlaacTimerId<DeviceId>,
    Ipv6DeviceTimerId::Slaac(id),
    id
);

/// Handle an IPv6 device timer firing.
pub(crate) fn handle_ipv6_timer<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: BufferIpDeviceContext<Ipv6, C, EmptyBuf>
        + DadHandler<C>
        + RsHandler<C>
        + TimerHandler<C, Ipv6DiscoveredRouteTimerId<SC::DeviceId>>
        + TimerHandler<C, MldDelayedReportTimerId<SC::DeviceId>>
        + TimerHandler<C, SlaacTimerId<SC::DeviceId>>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: Ipv6DeviceTimerId<SC::DeviceId>,
) {
    match id {
        Ipv6DeviceTimerId::Mld(id) => TimerHandler::handle_timer(sync_ctx, ctx, id),
        Ipv6DeviceTimerId::Dad(id) => DadHandler::handle_timer(sync_ctx, ctx, id),
        Ipv6DeviceTimerId::Rs(id) => RsHandler::handle_timer(sync_ctx, ctx, id),
        Ipv6DeviceTimerId::RouteDiscovery(id) => TimerHandler::handle_timer(sync_ctx, ctx, id),
        Ipv6DeviceTimerId::Slaac(id) => TimerHandler::handle_timer(sync_ctx, ctx, id),
    }
}

/// An extension trait adding IP device properties.
pub(crate) trait IpDeviceIpExt<Instant, DeviceId>: IpDeviceStateIpExt<Instant> {
    type State: AsRef<IpDeviceState<Instant, Self>>
        + AsMut<IpDeviceState<Instant, Self>>
        + AsRef<IpDeviceConfiguration>;
    type Timer;
}

impl<I: Instant, DeviceId> IpDeviceIpExt<I, DeviceId> for Ipv4 {
    type State = Ipv4DeviceState<I>;
    type Timer = Ipv4DeviceTimerId<DeviceId>;
}

impl<I: Instant, DeviceId> IpDeviceIpExt<I, DeviceId> for Ipv6 {
    type State = Ipv6DeviceState<I>;
    type Timer = Ipv6DeviceTimerId<DeviceId>;
}

/// IP address assignment states.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum IpAddressState {
    /// The address is unavailable because it's interface is not IP enabled.
    Unavailable,
    /// The address is assigned to an interface and can be considered bound to
    /// it (all packets destined to the address will be accepted).
    Assigned,
    /// The address is considered unassigned to an interface for normal
    /// operations, but has the intention of being assigned in the future (e.g.
    /// once Duplicate Address Detection is completed).
    Tentative,
}

#[derive(Debug, Eq, Hash, PartialEq)]
/// Events emitted from IP devices.
pub enum IpDeviceEvent<DeviceId, I: Ip> {
    /// Address was assigned.
    AddressAdded {
        /// The device.
        device: DeviceId,
        /// The new address.
        addr: AddrSubnet<I::Addr>,
        /// Initial address state.
        state: IpAddressState,
    },
    /// Address was unassigned.
    AddressRemoved {
        /// The device.
        device: DeviceId,
        /// The removed address.
        addr: SpecifiedAddr<I::Addr>,
    },
    /// Address state changed.
    AddressStateChanged {
        /// The device.
        device: DeviceId,
        /// The address whose state was changed.
        addr: SpecifiedAddr<I::Addr>,
        /// The new address state.
        state: IpAddressState,
    },
    /// IP was enabled/disabled on the device
    EnabledChanged {
        /// The device.
        device: DeviceId,
        /// `true` if IP was enabled on the device; `false` if IP was disabled.
        ip_enabled: bool,
    },
}

pub(crate) struct DualStackDeviceStateRef<'a, I: Instant> {
    pub(crate) ipv4: &'a Ipv4DeviceState<I>,
    pub(crate) ipv6: &'a Ipv6DeviceState<I>,
}

/// The non-synchronized execution context for dual-stack devices.
pub(crate) trait DualStackDeviceNonSyncContext: InstantContext {}
impl<C: InstantContext> DualStackDeviceNonSyncContext for C {}

/// The synchronized execution context for dual-stack devices.
pub(crate) trait DualStackDeviceContext<C: DualStackDeviceNonSyncContext>:
    DualStackDeviceIdContext
{
    /// Calls the function with an immutable view into the dual-stack device's
    /// state.
    fn with_dual_stack_device_state<O, F: FnOnce(DualStackDeviceStateRef<'_, C::Instant>) -> O>(
        &self,
        device_id: Self::DualStackDeviceId,
        cb: F,
    ) -> O;
}

/// An implementation of dual-stack devices.
pub(crate) trait DualStackDeviceHandler<C>: DualStackDeviceIdContext {
    /// Get all IPv4 and IPv6 address/subnet pairs configured on a device.
    fn get_all_ip_addr_subnets(&self, device_id: Self::DualStackDeviceId) -> Vec<AddrSubnetEither>;
}

impl<C: DualStackDeviceNonSyncContext, SC: DualStackDeviceContext<C>> DualStackDeviceHandler<C>
    for SC
{
    fn get_all_ip_addr_subnets(&self, device_id: Self::DualStackDeviceId) -> Vec<AddrSubnetEither> {
        self.with_dual_stack_device_state(device_id, |DualStackDeviceStateRef { ipv4, ipv6 }| {
            let addrs_v4 = ipv4
                .ip_state
                .iter_addrs()
                .filter_map(<Ipv4 as IpDeviceStateIpExt<C::Instant>>::assigned_addr);
            let addrs_v6 = ipv6
                .ip_state
                .iter_addrs()
                .filter_map(<Ipv6 as IpDeviceStateIpExt<C::Instant>>::assigned_addr);

            addrs_v4.map(AddrSubnetEither::V4).chain(addrs_v6.map(AddrSubnetEither::V6)).collect()
        })
    }
}

/// The non-synchronized execution context for IP devices.
pub(crate) trait IpDeviceNonSyncContext<
    I: IpDeviceIpExt<<Self as InstantContext>::Instant, DeviceId>,
    DeviceId,
>:
    RngContext + TimerContext<I::Timer> + EventContext<IpDeviceEvent<DeviceId, I>> + CounterContext
{
}
impl<
        DeviceId,
        I: IpDeviceIpExt<<C as InstantContext>::Instant, DeviceId>,
        C: RngContext
            + TimerContext<I::Timer>
            + EventContext<IpDeviceEvent<DeviceId, I>>
            + CounterContext,
    > IpDeviceNonSyncContext<I, DeviceId> for C
{
}

/// The execution context for IP devices.
pub(crate) trait IpDeviceContext<
    I: IpDeviceIpExt<C::Instant, Self::DeviceId>,
    C: IpDeviceNonSyncContext<I, Self::DeviceId>,
>: IpDeviceIdContext<I> where
    I::State: AsRef<IpDeviceState<C::Instant, I>>,
{
    /// Calls the function with an immutable reference to IP device state.
    fn with_ip_device_state<O, F: FnOnce(&I::State) -> O>(
        &self,
        device_id: Self::DeviceId,
        cb: F,
    ) -> O;

    /// Calls the function with a mutable reference to IP device state.
    fn with_ip_device_state_mut<O, F: FnOnce(&mut I::State) -> O>(
        &mut self,
        device_id: Self::DeviceId,
        cb: F,
    ) -> O;

    /// Calls the function with an [`Iterator`] of IDs for all initialized
    /// devices.
    fn with_devices<O, F: FnOnce(Box<dyn Iterator<Item = Self::DeviceId> + '_>) -> O>(
        &self,
        cb: F,
    ) -> O;

    /// Gets the MTU for a device.
    ///
    /// The MTU is the maximum size of an IP packet.
    fn get_mtu(&self, device_id: Self::DeviceId) -> u32;

    /// Joins the link-layer multicast group associated with the given IP
    /// multicast group.
    fn join_link_multicast_group(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<I::Addr>,
    );

    /// Leaves the link-layer multicast group associated with the given IP
    /// multicast group.
    fn leave_link_multicast_group(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<I::Addr>,
    );
}

/// The execution context for an IPv6 device.
pub(crate) trait Ipv6DeviceContext<C: IpDeviceNonSyncContext<Ipv6, Self::DeviceId>>:
    IpDeviceContext<Ipv6, C>
{
    /// A link-layer address.
    type LinkLayerAddr: AsRef<[u8]>;

    /// Gets the device's link-layer address bytes, if the device supports
    /// link-layer addressing.
    fn get_link_layer_addr_bytes(&self, device_id: Self::DeviceId) -> Option<Self::LinkLayerAddr>;

    /// Gets the device's EUI-64 based interface identifier.
    ///
    /// A `None` value indicates the device does not have an EUI-64 based
    /// interface identifier.
    fn get_eui64_iid(&self, device_id: Self::DeviceId) -> Option<[u8; 8]>;

    /// Sets the link MTU for the device.
    fn set_link_mtu(&mut self, device_id: Self::DeviceId, mtu: NonZeroU32);
}

/// An implementation of an IP device.
pub(crate) trait IpDeviceHandler<I: Ip, C>: IpDeviceIdContext<I> {
    fn is_router_device(&self, device_id: Self::DeviceId) -> bool;

    fn set_default_hop_limit(&mut self, device_id: Self::DeviceId, hop_limit: NonZeroU8);
}

impl<
        I: IpDeviceIpExt<C::Instant, SC::DeviceId>,
        C: IpDeviceNonSyncContext<I, SC::DeviceId>,
        SC: IpDeviceContext<I, C>,
    > IpDeviceHandler<I, C> for SC
{
    fn is_router_device(&self, device_id: Self::DeviceId) -> bool {
        is_ip_routing_enabled(self, device_id)
    }

    fn set_default_hop_limit(&mut self, device_id: Self::DeviceId, hop_limit: NonZeroU8) {
        self.with_ip_device_state_mut(device_id, |state| {
            AsMut::<IpDeviceState<_, _>>::as_mut(state).default_hop_limit = hop_limit;
        })
    }
}

/// An implementation of an IPv6 device.
pub(crate) trait Ipv6DeviceHandler<C>: IpDeviceHandler<Ipv6, C> {
    /// A link-layer address.
    type LinkLayerAddr: AsRef<[u8]>;

    /// Gets the device's link-layer address bytes, if the device supports
    /// link-layer addressing.
    fn get_link_layer_addr_bytes(&self, device_id: Self::DeviceId) -> Option<Self::LinkLayerAddr>;

    /// Sets the discovered retransmit timer for the device.
    fn set_discovered_retrans_timer(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        retrans_timer: NonZeroDuration,
    );

    /// Removes a tentative address as a result of duplicate address detection.
    ///
    /// Returns whether or not the address was removed. That is, this method
    /// returns `Ok(true)` when the address was tentatively assigned and
    /// `Ok(false)` if the address is fully assigned.
    fn remove_duplicate_tentative_address(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    ) -> Result<bool, NotFoundError>;

    /// Sets the link MTU for the device.
    fn set_link_mtu(&mut self, device_id: Self::DeviceId, mtu: NonZeroU32);
}

impl<
        C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
        SC: Ipv6DeviceContext<C> + GmpHandler<Ipv6, C> + DadHandler<C> + SlaacHandler<C>,
    > Ipv6DeviceHandler<C> for SC
{
    type LinkLayerAddr = SC::LinkLayerAddr;

    fn get_link_layer_addr_bytes(&self, device_id: Self::DeviceId) -> Option<SC::LinkLayerAddr> {
        Ipv6DeviceContext::get_link_layer_addr_bytes(self, device_id)
    }

    fn set_discovered_retrans_timer(
        &mut self,
        _ctx: &mut C,
        device_id: Self::DeviceId,
        retrans_timer: NonZeroDuration,
    ) {
        self.with_ip_device_state_mut(device_id, |state| state.retrans_timer = retrans_timer)
    }

    fn remove_duplicate_tentative_address(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    ) -> Result<bool, NotFoundError> {
        let address_state = self.with_ip_device_state(device_id, |state| {
            state
                .ip_state
                .iter_addrs()
                .find_map(
                    |Ipv6AddressEntry {
                         addr_sub,
                         state: address_state,
                         config: _,
                         deprecated: _,
                     }| { (addr_sub.addr() == addr).then(|| *address_state) },
                )
                .ok_or(NotFoundError)
        })?;

        Ok(match address_state {
            AddressState::Tentative { dad_transmits_remaining: _ } => {
                del_ipv6_addr_with_reason(
                    self,
                    ctx,
                    device_id,
                    &addr.into_specified(),
                    DelIpv6AddrReason::DadFailed,
                )
                .unwrap();
                true
            }
            AddressState::Assigned => false,
        })
    }

    fn set_link_mtu(&mut self, device_id: Self::DeviceId, mtu: NonZeroU32) {
        Ipv6DeviceContext::set_link_mtu(self, device_id, mtu)
    }
}

/// The execution context for an IP device with a buffer.
pub(crate) trait BufferIpDeviceContext<
    I: IpDeviceIpExt<C::Instant, Self::DeviceId>,
    C: IpDeviceNonSyncContext<I, Self::DeviceId>,
    B: BufferMut,
>: IpDeviceContext<I, C>
{
    /// Sends an IP packet through the device.
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        local_addr: SpecifiedAddr<I::Addr>,
        body: S,
    ) -> Result<(), S>;
}

fn enable_ipv6_device<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: Ipv6DeviceContext<C> + GmpHandler<Ipv6, C> + RsHandler<C> + DadHandler<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
) {
    // All nodes should join the all-nodes multicast group.
    join_ip_multicast(sync_ctx, ctx, device_id, Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS);
    GmpHandler::gmp_handle_maybe_enabled(sync_ctx, ctx, device_id);

    // Perform DAD for all addresses when enabling a device.
    //
    // We have to do this for all addresses (including ones that had DAD
    // performed) as while the device was disabled, another node could have
    // assigned the address and we wouldn't have responded to its DAD
    // solicitations.
    sync_ctx
        .with_ip_device_state_mut(device_id, |state| {
            let Ipv6DeviceState {
                ip_state,
                config:
                    Ipv6DeviceConfiguration {
                        dad_transmits,
                        max_router_solicitations: _,
                        slaac_config: _,
                        ip_config: IpDeviceConfiguration { ip_enabled: _, gmp_enabled: _ },
                    },
                retrans_timer: _,
                router_soliciations_remaining: _,
                route_discovery: _,
            } = state;
            let dad_transmits = *dad_transmits;

            ip_state
                .iter_addrs_mut()
                .map(|Ipv6AddressEntry { addr_sub, state, config: _, deprecated: _ }| {
                    *state = AddressState::Tentative { dad_transmits_remaining: dad_transmits };
                    addr_sub.ipv6_unicast_addr()
                })
                .collect::<Vec<_>>()
        })
        .into_iter()
        .for_each(|addr| {
            ctx.on_event(IpDeviceEvent::AddressStateChanged {
                device: device_id,
                addr: addr.into_specified(),
                state: IpAddressState::Tentative,
            });
            DadHandler::do_duplicate_address_detection(sync_ctx, ctx, device_id, addr);
        });

    // TODO(https://fxbug.dev/95946): Generate link-local address with opaque
    // IIDs.
    if let Some(iid) = sync_ctx.get_eui64_iid(device_id) {
        let link_local_addr_sub = {
            let mut addr = [0; 16];
            addr[0..2].copy_from_slice(&[0xfe, 0x80]);
            addr[(Ipv6::UNICAST_INTERFACE_IDENTIFIER_BITS / 8) as usize..].copy_from_slice(&iid);

            AddrSubnet::new(
                Ipv6Addr::from(addr),
                Ipv6Addr::BYTES * 8 - Ipv6::UNICAST_INTERFACE_IDENTIFIER_BITS,
            )
            .expect("valid link-local address")
        };

        match add_ipv6_addr_subnet(
            sync_ctx,
            ctx,
            device_id,
            link_local_addr_sub,
            AddrConfig::SLAAC_LINK_LOCAL,
        ) {
            Ok(()) => {}
            Err(ExistsError) => {
                // The address may have been added by admin action so it is safe
                // to swallow the exists error.
            }
        }
    }

    // As per RFC 4861 section 6.3.7,
    //
    //    A host sends Router Solicitations to the all-routers multicast
    //    address.
    //
    // If we are operating as a router, we do not solicit routers.
    if !is_ip_routing_enabled(sync_ctx, device_id) {
        RsHandler::start_router_solicitation(sync_ctx, ctx, device_id);
    }
    ctx.on_event(IpDeviceEvent::EnabledChanged { device: device_id, ip_enabled: true });
}

fn disable_ipv6_device<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: Ipv6DeviceContext<C>
        + GmpHandler<Ipv6, C>
        + RsHandler<C>
        + DadHandler<C>
        + RouteDiscoveryHandler<C>
        + SlaacHandler<C>
        + NudIpHandler<Ipv6, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
) {
    NudIpHandler::flush_neighbor_table(sync_ctx, ctx, device_id);

    SlaacHandler::remove_all_slaac_addresses(sync_ctx, ctx, device_id);

    RouteDiscoveryHandler::invalidate_routes(sync_ctx, ctx, device_id);

    RsHandler::stop_router_solicitation(sync_ctx, ctx, device_id);

    // Delete the link-local address generated when enabling the device and stop
    // DAD on the other addresses.
    sync_ctx
        .with_ip_device_state(device_id, |state| {
            state
                .ip_state
                .iter_addrs()
                .map(|Ipv6AddressEntry { addr_sub, state: _, config, deprecated: _ }| {
                    (addr_sub.ipv6_unicast_addr(), *config)
                })
                .collect::<Vec<_>>()
        })
        .into_iter()
        .for_each(|(addr, config)| {
            if config == AddrConfig::SLAAC_LINK_LOCAL {
                del_ipv6_addr_with_reason(
                    sync_ctx,
                    ctx,
                    device_id,
                    &addr.into_specified(),
                    DelIpv6AddrReason::ManualAction,
                )
                .expect("delete listed address")
            } else {
                DadHandler::stop_duplicate_address_detection(sync_ctx, ctx, device_id, addr);
                ctx.on_event(IpDeviceEvent::AddressStateChanged {
                    device: device_id,
                    addr: addr.into_specified(),
                    state: IpAddressState::Unavailable,
                });
            }
        });

    GmpHandler::gmp_handle_disabled(sync_ctx, ctx, device_id);
    leave_ip_multicast(sync_ctx, ctx, device_id, Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS);
    ctx.on_event(IpDeviceEvent::EnabledChanged { device: device_id, ip_enabled: false });
}

fn enable_ipv4_device<
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
    SC: IpDeviceContext<Ipv4, C> + GmpHandler<Ipv4, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
) {
    // All systems should join the all-systems multicast group.
    join_ip_multicast(sync_ctx, ctx, device_id, Ipv4::ALL_SYSTEMS_MULTICAST_ADDRESS);
    GmpHandler::gmp_handle_maybe_enabled(sync_ctx, ctx, device_id);
    ctx.on_event(IpDeviceEvent::EnabledChanged { device: device_id, ip_enabled: true });
    sync_ctx.with_ip_device_state(device_id, |state| {
        state.ip_state.iter_addrs().for_each(|addr| {
            ctx.on_event(IpDeviceEvent::AddressStateChanged {
                device: device_id,
                addr: addr.addr(),
                state: IpAddressState::Assigned,
            });
        })
    });
}

fn disable_ipv4_device<
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
    SC: IpDeviceContext<Ipv4, C> + GmpHandler<Ipv4, C> + NudIpHandler<Ipv4, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
) {
    NudIpHandler::flush_neighbor_table(sync_ctx, ctx, device_id);
    GmpHandler::gmp_handle_disabled(sync_ctx, ctx, device_id);
    leave_ip_multicast(sync_ctx, ctx, device_id, Ipv4::ALL_SYSTEMS_MULTICAST_ADDRESS);
    sync_ctx.with_ip_device_state(device_id, |state| {
        state.ip_state.iter_addrs().for_each(|addr| {
            ctx.on_event(IpDeviceEvent::AddressStateChanged {
                device: device_id,
                addr: addr.addr(),
                state: IpAddressState::Unavailable,
            });
        })
    });
    ctx.on_event(IpDeviceEvent::EnabledChanged { device: device_id, ip_enabled: false });
}

pub(crate) fn with_assigned_addr_subnets<
    I: Ip + IpDeviceIpExt<C::Instant, SC::DeviceId>,
    C: IpDeviceNonSyncContext<I, SC::DeviceId>,
    SC: IpDeviceContext<I, C>,
    O,
    F: FnOnce(Box<dyn Iterator<Item = AddrSubnet<I::Addr>> + '_>) -> O,
>(
    sync_ctx: &SC,
    device_id: SC::DeviceId,
    cb: F,
) -> O {
    sync_ctx.with_ip_device_state(device_id, |state| {
        cb(Box::new(
            AsRef::<IpDeviceState<_, I>>::as_ref(state).iter_addrs().filter_map(I::assigned_addr),
        ))
    })
}

/// Gets the IPv4 address and subnet pairs associated with this device.
///
/// Returns an [`Iterator`] of `AddrSubnet`.
pub(crate) fn with_assigned_ipv4_addr_subnets<
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
    SC: IpDeviceContext<Ipv4, C>,
    O,
    F: FnOnce(Box<dyn Iterator<Item = AddrSubnet<Ipv4Addr>> + '_>) -> O,
>(
    sync_ctx: &SC,
    device_id: SC::DeviceId,
    cb: F,
) -> O {
    with_assigned_addr_subnets::<Ipv4, _, _, _, _>(sync_ctx, device_id, cb)
}

/// Gets a single IPv4 address and subnet for a device.
pub(super) fn get_ipv4_addr_subnet<
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
    SC: IpDeviceContext<Ipv4, C>,
>(
    sync_ctx: &SC,
    device_id: SC::DeviceId,
) -> Option<AddrSubnet<Ipv4Addr>> {
    with_assigned_ipv4_addr_subnets(sync_ctx, device_id, |mut addrs| addrs.nth(0))
}

/// Gets the hop limit for new IPv6 packets that will be sent out from `device`.
pub(crate) fn get_ipv6_hop_limit<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: IpDeviceContext<Ipv6, C>,
>(
    sync_ctx: &SC,
    device: SC::DeviceId,
) -> NonZeroU8 {
    sync_ctx.with_ip_device_state(device, |state| state.ip_state.default_hop_limit)
}

/// Is IP packet routing enabled?
pub(crate) fn is_ip_routing_enabled<
    I: IpDeviceIpExt<C::Instant, SC::DeviceId>,
    C: IpDeviceNonSyncContext<I, SC::DeviceId>,
    SC: IpDeviceContext<I, C>,
>(
    sync_ctx: &SC,
    device_id: SC::DeviceId,
) -> bool {
    sync_ctx.with_ip_device_state(device_id, |state| {
        AsRef::<IpDeviceState<_, _>>::as_ref(state).routing_enabled
    })
}

/// Enables or disables IP packet routing on `device`.
#[cfg(test)]
pub(crate) fn set_routing_enabled<
    C: IpDeviceNonSyncContext<Ipv4, <SC as IpDeviceIdContext<Ipv4>>::DeviceId>
        + IpDeviceNonSyncContext<Ipv6, <SC as IpDeviceIdContext<Ipv6>>::DeviceId>,
    SC: IpDeviceContext<Ipv4, C> + Ipv6DeviceContext<C> + GmpHandler<Ipv6, C> + RsHandler<C>,
    I: Ip,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device: <SC as IpDeviceIdContext<Ipv6>>::DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError>
where
    SC: IpDeviceIdContext<Ipv6, DeviceId = <SC as IpDeviceIdContext<Ipv4>>::DeviceId>,
{
    match I::VERSION {
        IpVersion::V4 => set_ipv4_routing_enabled(sync_ctx, ctx, device, enabled),
        IpVersion::V6 => set_ipv6_routing_enabled(sync_ctx, ctx, device, enabled),
    }
}

/// Enables or disables IPv4 packet routing on `device_id`.
#[cfg(test)]
fn set_ipv4_routing_enabled<
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
    SC: IpDeviceContext<Ipv4, C>,
>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    device_id: SC::DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError> {
    if device_id.is_loopback() {
        return Err(NotSupportedError);
    }

    sync_ctx.with_ip_device_state_mut(device_id, |state| state.ip_state.routing_enabled = enabled);
    Ok(())
}

/// Enables or disables IPv4 packet routing on `device_id`.
///
/// When routing is enabled/disabled, the interface will leave/join the all
/// routers link-local multicast group and stop/start soliciting routers.
///
/// Does nothing if the routing status does not change as a consequence of this
/// call.
#[cfg(test)]
pub(crate) fn set_ipv6_routing_enabled<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: Ipv6DeviceContext<C> + GmpHandler<Ipv6, C> + RsHandler<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError> {
    if device_id.is_loopback() {
        return Err(NotSupportedError);
    }

    let prev = sync_ctx.with_ip_device_state_mut(device_id, |state| {
        let prev = state.ip_state.routing_enabled;
        state.ip_state.routing_enabled = enabled;
        prev
    });

    if prev == enabled {
        return Ok(());
    }

    if enabled {
        RsHandler::stop_router_solicitation(sync_ctx, ctx, device_id);
        join_ip_multicast(sync_ctx, ctx, device_id, Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS);
    } else {
        leave_ip_multicast(
            sync_ctx,
            ctx,
            device_id,
            Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS,
        );
        RsHandler::start_router_solicitation(sync_ctx, ctx, device_id);
    }

    Ok(())
}

/// Adds `device_id` to a multicast group `multicast_addr`.
///
/// Calling `join_ip_multicast` multiple times is completely safe. A counter
/// will be kept for the number of times `join_ip_multicast` has been called
/// with the same `device_id` and `multicast_addr` pair. To completely leave a
/// multicast group, [`leave_ip_multicast`] must be called the same number of
/// times `join_ip_multicast` has been called for the same `device_id` and
/// `multicast_addr` pair. The first time `join_ip_multicast` is called for a
/// new `device` and `multicast_addr` pair, the device will actually join the
/// multicast group.
pub(crate) fn join_ip_multicast<
    I: IpDeviceIpExt<C::Instant, SC::DeviceId>,
    C: IpDeviceNonSyncContext<I, SC::DeviceId>,
    SC: IpDeviceContext<I, C> + GmpHandler<I, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    multicast_addr: MulticastAddr<I::Addr>,
) {
    match sync_ctx.gmp_join_group(ctx, device_id, multicast_addr) {
        GroupJoinResult::Joined(()) => {
            sync_ctx.join_link_multicast_group(ctx, device_id, multicast_addr)
        }
        GroupJoinResult::AlreadyMember => {}
    }
}

/// Removes `device_id` from a multicast group `multicast_addr`.
///
/// `leave_ip_multicast` will attempt to remove `device_id` from a multicast
/// group `multicast_addr`. `device_id` may have "joined" the same multicast
/// address multiple times, so `device_id` will only leave the multicast group
/// once `leave_ip_multicast` has been called for each corresponding
/// [`join_ip_multicast`]. That is, if `join_ip_multicast` gets called 3
/// times and `leave_ip_multicast` gets called two times (after all 3
/// `join_ip_multicast` calls), `device_id` will still be in the multicast
/// group until the next (final) call to `leave_ip_multicast`.
///
/// # Panics
///
/// If `device_id` is not currently in the multicast group `multicast_addr`.
pub(crate) fn leave_ip_multicast<
    I: IpDeviceIpExt<C::Instant, SC::DeviceId>,
    C: IpDeviceNonSyncContext<I, SC::DeviceId>,
    SC: IpDeviceContext<I, C> + GmpHandler<I, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    multicast_addr: MulticastAddr<I::Addr>,
) {
    match sync_ctx.gmp_leave_group(ctx, device_id, multicast_addr) {
        GroupLeaveResult::Left(()) => {
            sync_ctx.leave_link_multicast_group(ctx, device_id, multicast_addr)
        }
        GroupLeaveResult::StillMember => {}
        GroupLeaveResult::NotMember => panic!(
            "attempted to leave IP multicast group we were not a member of: {}",
            multicast_addr,
        ),
    }
}

/// Adds an IPv4 address and associated subnet to this device.
pub(crate) fn add_ipv4_addr_subnet<
    SC: BufferIpDeviceContext<Ipv4, C, EmptyBuf>,
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    addr_sub: AddrSubnet<Ipv4Addr>,
) -> Result<(), ExistsError> {
    sync_ctx.with_ip_device_state_mut(device_id, |Ipv4DeviceState { ip_state, config }| {
        let address_state = match config.ip_config.ip_enabled {
            true => IpAddressState::Assigned,
            false => IpAddressState::Unavailable,
        };
        ip_state.add_addr(addr_sub).map(|()| {
            ctx.on_event(IpDeviceEvent::AddressAdded {
                device: device_id,
                addr: addr_sub,
                state: address_state,
            })
        })
    })
}

/// Adds an IPv6 address (with duplicate address detection) and associated
/// subnet to this device and joins the address's solicited-node multicast
/// group.
///
/// `config` is the way this address is being configured. See [`AddrConfig`]
/// for more details.
pub(crate) fn add_ipv6_addr_subnet<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: Ipv6DeviceContext<C> + GmpHandler<Ipv6, C> + DadHandler<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    addr_sub: AddrSubnet<Ipv6Addr>,
    config: AddrConfig<C::Instant>,
) -> Result<(), ExistsError> {
    let addr_sub = addr_sub.to_unicast();
    sync_ctx
        .with_ip_device_state_mut(device_id, |state| {
            let Ipv6DeviceState {
                ref mut ip_state,
                config:
                    Ipv6DeviceConfiguration {
                        dad_transmits,
                        max_router_solicitations: _,
                        slaac_config: _,
                        ip_config: IpDeviceConfiguration { ip_enabled, gmp_enabled: _ },
                    },
                retrans_timer: _,
                router_soliciations_remaining: _,
                route_discovery: _,
            } = state;

            ip_state
                .add_addr(Ipv6AddressEntry::new(
                    addr_sub,
                    AddressState::Tentative { dad_transmits_remaining: *dad_transmits },
                    config,
                ))
                .map(|()| *ip_enabled)
        })
        .map(|ip_enabled| {
            // As per RFC 4861 section 5.6.2,
            //
            //   Before sending a Neighbor Solicitation, an interface MUST join
            //   the all-nodes multicast address and the solicited-node
            //   multicast address of the tentative address.
            //
            // Note that we join the all-nodes multicast address on interface
            // enable.
            join_ip_multicast(
                sync_ctx,
                ctx,
                device_id,
                addr_sub.addr().to_solicited_node_address(),
            );

            let state = match ip_enabled {
                true => IpAddressState::Tentative,
                false => IpAddressState::Unavailable,
            };
            ctx.on_event(IpDeviceEvent::AddressAdded {
                device: device_id,
                addr: addr_sub.to_witness(),
                state: state,
            });

            // NB: We don't start DAD if the device is disabled. DAD will be
            // performed when the device is enabled for all addressed.
            if ip_enabled {
                DadHandler::do_duplicate_address_detection(
                    sync_ctx,
                    ctx,
                    device_id,
                    addr_sub.addr(),
                );
            }
        })
}

/// Removes an IPv4 address and associated subnet from this device.
pub(crate) fn del_ipv4_addr<
    SC: BufferIpDeviceContext<Ipv4, C, EmptyBuf>,
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    addr: &SpecifiedAddr<Ipv4Addr>,
) -> Result<(), NotFoundError> {
    sync_ctx.with_ip_device_state_mut(device_id, |state| {
        state.ip_state.remove_addr(&addr).map(|addr| {
            ctx.on_event(IpDeviceEvent::AddressRemoved { device: device_id, addr: addr.addr() })
        })
    })
}

fn del_ipv6_addr<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: Ipv6DeviceContext<C> + GmpHandler<Ipv6, C> + DadHandler<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    addr: &SpecifiedAddr<Ipv6Addr>,
) -> Result<Ipv6AddressEntry<C::Instant>, NotFoundError> {
    let entry =
        sync_ctx.with_ip_device_state_mut(device_id, |state| state.ip_state.remove_addr(&addr))?;
    let addr = entry.addr_sub.addr();
    DadHandler::stop_duplicate_address_detection(sync_ctx, ctx, device_id, addr);
    leave_ip_multicast(sync_ctx, ctx, device_id, addr.to_solicited_node_address());

    ctx.on_event(IpDeviceEvent::AddressRemoved { device: device_id, addr: addr.into_specified() });

    Ok(entry)
}

/// Removes an IPv6 address and associated subnet from this device.
pub(crate) fn del_ipv6_addr_with_reason<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: Ipv6DeviceContext<C> + GmpHandler<Ipv6, C> + DadHandler<C> + SlaacHandler<C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    addr: &SpecifiedAddr<Ipv6Addr>,
    reason: DelIpv6AddrReason,
) -> Result<(), NotFoundError> {
    del_ipv6_addr(sync_ctx, ctx, device_id, addr).map(
        |Ipv6AddressEntry { addr_sub, state: _, config, deprecated: _ }| match config {
            AddrConfig::Slaac(s) => {
                SlaacHandler::on_address_removed(sync_ctx, ctx, device_id, addr_sub, s, reason)
            }
            AddrConfig::Manual => {}
        },
    )
}

/// Sends an IP packet through the device.
pub(crate) fn send_ip_frame<
    I: IpDeviceIpExt<C::Instant, SC::DeviceId>,
    C: IpDeviceNonSyncContext<I, SC::DeviceId>,
    SC: BufferIpDeviceContext<I, C, B>,
    B: BufferMut,
    S: Serializer<Buffer = B>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    local_addr: SpecifiedAddr<I::Addr>,
    body: S,
) -> Result<(), S> {
    is_ip_device_enabled(sync_ctx, device_id)
        .then(|| sync_ctx.send_ip_frame(ctx, device_id, local_addr, body))
        .unwrap_or(Ok(()))
}

pub(crate) fn get_ipv4_configuration<
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
    SC: IpDeviceContext<Ipv4, C>,
>(
    sync_ctx: &SC,
    device_id: SC::DeviceId,
) -> Ipv4DeviceConfiguration {
    sync_ctx.with_ip_device_state(device_id, |state| state.config.clone())
}

pub(crate) fn get_ipv6_configuration<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: IpDeviceContext<Ipv6, C>,
>(
    sync_ctx: &SC,
    device_id: SC::DeviceId,
) -> Ipv6DeviceConfiguration {
    sync_ctx.with_ip_device_state(device_id, |state| state.config.clone())
}

/// Updates the IPv4 Configuration for the device.
pub(crate) fn update_ipv4_configuration<
    C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
    SC: IpDeviceContext<Ipv4, C> + GmpHandler<Ipv4, C> + NudIpHandler<Ipv4, C>,
    F: FnOnce(&mut Ipv4DeviceConfiguration),
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    update_cb: F,
) {
    let (prev_config, new_config) = sync_ctx.with_ip_device_state_mut(device_id, |state| {
        let config = &mut state.config;
        let prev_config = *config;

        update_cb(config);

        (prev_config, *config)
    });

    let Ipv4DeviceConfiguration {
        ip_config:
            IpDeviceConfiguration { ip_enabled: prev_ip_enabled, gmp_enabled: prev_gmp_enabled },
    } = prev_config;
    let Ipv4DeviceConfiguration {
        ip_config:
            IpDeviceConfiguration { ip_enabled: next_ip_enabled, gmp_enabled: next_gmp_enabled },
    } = new_config;

    if !prev_ip_enabled && next_ip_enabled {
        enable_ipv4_device(sync_ctx, ctx, device_id);
    } else if prev_ip_enabled && !next_ip_enabled {
        disable_ipv4_device(sync_ctx, ctx, device_id);
    }

    if !prev_gmp_enabled && next_gmp_enabled {
        GmpHandler::gmp_handle_maybe_enabled(sync_ctx, ctx, device_id);
    } else if prev_gmp_enabled && !next_gmp_enabled {
        GmpHandler::gmp_handle_disabled(sync_ctx, ctx, device_id);
    }
}

pub(super) fn is_ip_device_enabled<
    I: IpDeviceIpExt<C::Instant, SC::DeviceId>,
    C: IpDeviceNonSyncContext<I, SC::DeviceId>,
    SC: IpDeviceContext<I, C>,
>(
    sync_ctx: &SC,
    device_id: SC::DeviceId,
) -> bool {
    sync_ctx.with_ip_device_state(device_id, |state| {
        AsRef::<IpDeviceConfiguration>::as_ref(state).ip_enabled
    })
}

/// Updates the IPv6 Configuration for the device.
pub(crate) fn update_ipv6_configuration<
    C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
    SC: Ipv6DeviceContext<C>
        + GmpHandler<Ipv6, C>
        + RsHandler<C>
        + DadHandler<C>
        + RouteDiscoveryHandler<C>
        + SlaacHandler<C>
        + NudIpHandler<Ipv6, C>,
    F: FnOnce(&mut Ipv6DeviceConfiguration),
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    update_cb: F,
) {
    let (prev_config, new_config) = sync_ctx.with_ip_device_state_mut(device_id, |state| {
        let config = &mut state.config;
        let prev_config = *config;

        update_cb(config);

        (prev_config, *config)
    });

    let Ipv6DeviceConfiguration {
        dad_transmits: _,
        max_router_solicitations: _,
        slaac_config: _,
        ip_config:
            IpDeviceConfiguration { ip_enabled: prev_ip_enabled, gmp_enabled: prev_gmp_enabled },
    } = prev_config;
    let Ipv6DeviceConfiguration {
        dad_transmits: _,
        max_router_solicitations: _,
        slaac_config: _,
        ip_config:
            IpDeviceConfiguration { ip_enabled: next_ip_enabled, gmp_enabled: next_gmp_enabled },
    } = new_config;

    if !prev_ip_enabled && next_ip_enabled {
        enable_ipv6_device(sync_ctx, ctx, device_id);
    } else if prev_ip_enabled && !next_ip_enabled {
        disable_ipv6_device(sync_ctx, ctx, device_id);
    }

    if !prev_gmp_enabled && next_gmp_enabled {
        GmpHandler::gmp_handle_maybe_enabled(sync_ctx, ctx, device_id);
    } else if prev_gmp_enabled && !next_gmp_enabled {
        GmpHandler::gmp_handle_disabled(sync_ctx, ctx, device_id);
    }
}

#[cfg(test)]
pub(crate) mod testutil {
    use super::*;

    use net_types::{ip::Ipv6Scope, ScopeableAddress as _};

    pub(crate) fn get_global_ipv6_addrs<
        C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
        SC: IpDeviceContext<Ipv6, C>,
    >(
        sync_ctx: &SC,
        device_id: SC::DeviceId,
    ) -> Vec<Ipv6AddressEntry<C::Instant>> {
        sync_ctx.with_ip_device_state(device_id, |state| {
            state
                .ip_state
                .iter_addrs()
                .filter(|entry| match entry.addr_sub.addr().scope() {
                    Ipv6Scope::Global => true,
                    Ipv6Scope::InterfaceLocal
                    | Ipv6Scope::LinkLocal
                    | Ipv6Scope::AdminLocal
                    | Ipv6Scope::SiteLocal
                    | Ipv6Scope::OrganizationLocal
                    | Ipv6Scope::Reserved(_)
                    | Ipv6Scope::Unassigned(_) => false,
                })
                .cloned()
                .collect()
        })
    }

    /// Gets the IPv6 address and subnet pairs associated with this device which are
    /// in the assigned state.
    ///
    /// Tentative IP addresses (addresses which are not yet fully bound to a device)
    /// and deprecated IP addresses (addresses which have been assigned but should
    /// no longer be used for new connections) will not be returned by
    /// `get_assigned_ipv6_addr_subnets`.
    ///
    /// Returns an [`Iterator`] of `AddrSubnet`.
    ///
    /// See [`Tentative`] and [`AddrSubnet`] for more information.
    pub(crate) fn with_assigned_ipv6_addr_subnets<
        C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
        SC: IpDeviceContext<Ipv6, C>,
        O,
        F: FnOnce(Box<dyn Iterator<Item = AddrSubnet<Ipv6Addr>> + '_>) -> O,
    >(
        sync_ctx: &SC,
        device_id: SC::DeviceId,
        cb: F,
    ) -> O {
        with_assigned_addr_subnets::<Ipv6, _, _, _, _>(sync_ctx, device_id, cb)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use alloc::vec;
    use fakealloc::collections::HashSet;

    use net_types::ip::Ipv6;

    use crate::{
        ip::gmp::GmpDelayedReportTimerId,
        testutil::{
            assert_empty, DispatchedEvent, DummyCtx, DummyNonSyncCtx, DummySyncCtx, TestIpExt as _,
        },
        Ctx, StackStateBuilder, TimerId, TimerIdInner,
    };

    #[test]
    fn enable_disable_ipv4() {
        let DummyCtx { sync_ctx, mut non_sync_ctx } =
            Ctx::new_with_builder(StackStateBuilder::default());
        let mut sync_ctx = &sync_ctx;
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        let local_mac = Ipv4::DUMMY_CONFIG.local_mac;
        let device_id =
            sync_ctx.state.device.add_ethernet_device(local_mac, Ipv4::MINIMUM_LINK_MTU.into());

        assert_eq!(non_sync_ctx.take_events()[..], []);

        update_ipv4_configuration(&mut sync_ctx, &mut non_sync_ctx, device_id, |config| {
            config.ip_config.ip_enabled = true
        });
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [DispatchedEvent::IpDeviceIpv4(IpDeviceEvent::EnabledChanged {
                device: device_id,
                ip_enabled: true,
            })]
        );

        update_ipv4_configuration(&mut sync_ctx, &mut non_sync_ctx, device_id, |config| {
            config.ip_config.ip_enabled = false
        });
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [DispatchedEvent::IpDeviceIpv4(IpDeviceEvent::EnabledChanged {
                device: device_id,
                ip_enabled: false,
            })]
        );

        let ipv4_addr_subnet = AddrSubnet::new(Ipv4Addr::new([192, 168, 0, 1]), 24).unwrap();
        add_ipv4_addr_subnet(&mut sync_ctx, &mut non_sync_ctx, device_id, ipv4_addr_subnet.clone())
            .expect("failed to add IPv4 Address");
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [DispatchedEvent::IpDeviceIpv4(IpDeviceEvent::AddressAdded {
                device: device_id,
                addr: ipv4_addr_subnet.clone(),
                state: IpAddressState::Unavailable,
            })]
        );

        update_ipv4_configuration(&mut sync_ctx, &mut non_sync_ctx, device_id, |config| {
            config.ip_config.ip_enabled = true
        });
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [
                DispatchedEvent::IpDeviceIpv4(IpDeviceEvent::EnabledChanged {
                    device: device_id,
                    ip_enabled: true,
                }),
                DispatchedEvent::IpDeviceIpv4(IpDeviceEvent::AddressStateChanged {
                    device: device_id,
                    addr: ipv4_addr_subnet.addr().into(),
                    state: IpAddressState::Assigned,
                }),
            ]
        );
        // Verify that a redundant "enable" does not generate any events.
        update_ipv4_configuration(&mut sync_ctx, &mut non_sync_ctx, device_id, |config| {
            config.ip_config.ip_enabled = true
        });
        assert_eq!(non_sync_ctx.take_events()[..], []);

        update_ipv4_configuration(&mut sync_ctx, &mut non_sync_ctx, device_id, |config| {
            config.ip_config.ip_enabled = false
        });
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [
                DispatchedEvent::IpDeviceIpv4(IpDeviceEvent::AddressStateChanged {
                    device: device_id,
                    addr: ipv4_addr_subnet.addr().into(),
                    state: IpAddressState::Unavailable,
                }),
                DispatchedEvent::IpDeviceIpv4(IpDeviceEvent::EnabledChanged {
                    device: device_id,
                    ip_enabled: false,
                }),
            ]
        );
        // Verify that a redundant "disable" does not generate any events.
        update_ipv4_configuration(&mut sync_ctx, &mut non_sync_ctx, device_id, |config| {
            config.ip_config.ip_enabled = false
        });
        assert_eq!(non_sync_ctx.take_events()[..], []);
    }

    #[test]
    fn enable_disable_ipv6() {
        let DummyCtx { sync_ctx, mut non_sync_ctx } =
            Ctx::new_with_builder(StackStateBuilder::default());
        let mut sync_ctx = &sync_ctx;
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        let local_mac = Ipv6::DUMMY_CONFIG.local_mac;
        let device_id =
            sync_ctx.state.device.add_ethernet_device(local_mac, Ipv6::MINIMUM_LINK_MTU.into());
        update_ipv6_configuration(&mut sync_ctx, &mut non_sync_ctx, device_id, |config| {
            config.ip_config.gmp_enabled = true;

            // Doesn't matter as long as we perform DAD and router
            // solicitation.
            config.dad_transmits = NonZeroU8::new(1);
            config.max_router_solicitations = NonZeroU8::new(1);
        });
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(non_sync_ctx.take_events()[..], []);

        let ll_addr = local_mac.to_ipv6_link_local();

        // Enable the device and observe an auto-generated link-local address,
        // router solicitation and DAD for the auto-generated address.
        let test_enable_device =
            |sync_ctx: &mut &DummySyncCtx,
             non_sync_ctx: &mut DummyNonSyncCtx,
             extra_group: Option<MulticastAddr<Ipv6Addr>>| {
                update_ipv6_configuration(sync_ctx, non_sync_ctx, device_id, |config| {
                    config.ip_config.ip_enabled = true;
                });
                assert_eq!(
                    IpDeviceContext::<Ipv6, _>::with_ip_device_state(
                        sync_ctx,
                        device_id,
                        |state| {
                            state
                                .ip_state
                                .iter_addrs()
                                .map(
                                    |Ipv6AddressEntry {
                                         addr_sub,
                                         state: _,
                                         config: _,
                                         deprecated: _,
                                     }| {
                                        addr_sub.ipv6_unicast_addr()
                                    },
                                )
                                .collect::<HashSet<_>>()
                        }
                    ),
                    HashSet::from([ll_addr.ipv6_unicast_addr()]),
                    "enabled device expected to generate link-local address"
                );
                let mut timers = vec![
                    (
                        TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Rs(RsTimerId {
                            device_id,
                        }))),
                        ..,
                    ),
                    (
                        TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Dad(DadTimerId {
                            device_id,
                            addr: ll_addr.ipv6_unicast_addr(),
                        }))),
                        ..,
                    ),
                    (
                        TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Mld(
                            MldDelayedReportTimerId(GmpDelayedReportTimerId {
                                device: device_id,
                                group_addr: local_mac
                                    .to_ipv6_link_local()
                                    .addr()
                                    .to_solicited_node_address(),
                            })
                            .into(),
                        ))),
                        ..,
                    ),
                ];
                if let Some(group_addr) = extra_group {
                    timers.push((
                        TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Mld(
                            MldDelayedReportTimerId(GmpDelayedReportTimerId {
                                device: device_id,
                                group_addr,
                            })
                            .into(),
                        ))),
                        ..,
                    ))
                }
                non_sync_ctx.timer_ctx().assert_timers_installed(timers);
            };
        test_enable_device(&mut sync_ctx, &mut non_sync_ctx, None);
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::AddressAdded {
                    device: device_id,
                    addr: ll_addr.to_witness(),
                    state: IpAddressState::Tentative,
                }),
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::EnabledChanged {
                    device: device_id,
                    ip_enabled: true,
                })
            ]
        );

        let test_disable_device =
            |sync_ctx: &mut &DummySyncCtx, non_sync_ctx: &mut DummyNonSyncCtx| {
                update_ipv6_configuration(sync_ctx, non_sync_ctx, device_id, |config| {
                    config.ip_config.ip_enabled = false;
                });
                non_sync_ctx.timer_ctx().assert_no_timers_installed();
            };
        test_disable_device(&mut sync_ctx, &mut non_sync_ctx);
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::AddressRemoved {
                    device: device_id,
                    addr: ll_addr.addr().into(),
                }),
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::EnabledChanged {
                    device: device_id,
                    ip_enabled: false,
                })
            ]
        );

        IpDeviceContext::<Ipv6, _>::with_ip_device_state(&sync_ctx, device_id, |state| {
            assert_empty(state.ip_state.iter_addrs());
        });

        let multicast_addr = Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS;
        join_ip_multicast::<Ipv6, _, _>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            multicast_addr,
        );
        add_ipv6_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            ll_addr.to_witness(),
            AddrConfig::Manual,
        )
        .expect("add MAC based IPv6 link-local address");
        assert_eq!(
            IpDeviceContext::<Ipv6, _>::with_ip_device_state(&sync_ctx, device_id, |state| {
                state
                    .ip_state
                    .iter_addrs()
                    .map(|Ipv6AddressEntry { addr_sub, state: _, config: _, deprecated: _ }| {
                        addr_sub.ipv6_unicast_addr()
                    })
                    .collect::<HashSet<_>>()
            }),
            HashSet::from([ll_addr.ipv6_unicast_addr()])
        );
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::AddressAdded {
                device: device_id,
                addr: ll_addr.to_witness(),
                state: IpAddressState::Unavailable,
            })]
        );

        test_enable_device(&mut sync_ctx, &mut non_sync_ctx, Some(multicast_addr));
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::AddressStateChanged {
                    device: device_id,
                    addr: ll_addr.addr().into(),
                    state: IpAddressState::Tentative,
                }),
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::EnabledChanged {
                    device: device_id,
                    ip_enabled: true,
                })
            ]
        );
        test_disable_device(&mut sync_ctx, &mut non_sync_ctx);
        // The address was manually added, don't expect it to be removed.
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::AddressStateChanged {
                    device: device_id,
                    addr: ll_addr.addr().into(),
                    state: IpAddressState::Unavailable,
                }),
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::EnabledChanged {
                    device: device_id,
                    ip_enabled: false,
                })
            ]
        );

        // Verify that a redundant "disable" does not generate any events.
        test_disable_device(&mut sync_ctx, &mut non_sync_ctx);
        assert_eq!(non_sync_ctx.take_events()[..], []);

        assert_eq!(
            IpDeviceContext::<Ipv6, _>::with_ip_device_state(&sync_ctx, device_id, |state| {
                state
                    .ip_state
                    .iter_addrs()
                    .map(|Ipv6AddressEntry { addr_sub, state: _, config: _, deprecated: _ }| {
                        addr_sub.ipv6_unicast_addr()
                    })
                    .collect::<HashSet<_>>()
            }),
            HashSet::from([ll_addr.ipv6_unicast_addr()]),
            "manual addresses should not be removed on device disable"
        );

        leave_ip_multicast::<Ipv6, _, _>(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            multicast_addr,
        );
        test_enable_device(&mut sync_ctx, &mut non_sync_ctx, None);
        assert_eq!(
            non_sync_ctx.take_events()[..],
            [
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::AddressStateChanged {
                    device: device_id,
                    addr: ll_addr.addr().into(),
                    state: IpAddressState::Tentative,
                }),
                DispatchedEvent::IpDeviceIpv6(IpDeviceEvent::EnabledChanged {
                    device: device_id,
                    ip_enabled: true,
                })
            ]
        );

        // Verify that a redundant "enable" does not generate any events.
        test_enable_device(&mut sync_ctx, &mut non_sync_ctx, None);
        assert_eq!(non_sync_ctx.take_events()[..], []);
    }
}
