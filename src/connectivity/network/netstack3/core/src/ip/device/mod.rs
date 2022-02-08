// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An IP device.

mod integration;
pub(crate) mod state;

use crate::{
    context::{RngContext, TimerContext, TimerHandler},
    device::DeviceId,
    error::{ExistsError, NotFoundError},
    ip::{
        device::state::{
            AddrConfig, AddressState, DualStackIpDeviceState, IpDeviceConfiguration, IpDeviceState,
            Ipv4DeviceConfiguration, Ipv6AddressEntry, Ipv6DeviceConfiguration, Ipv6DeviceState,
        },
        gmp::{
            igmp::IgmpTimerId, mld::MldReportDelay, GmpHandler, GroupJoinResult, GroupLeaveResult,
        },
        Destination, IpDeviceIdContext,
    },
    Ctx, EventDispatcher,
};
use alloc::boxed::Box;
use core::num::NonZeroU8;
use net_types::{
    ip::{
        AddrSubnet, AddrSubnetEither, Ip, IpAddr, IpAddress, IpVersion, Ipv4, Ipv4Addr, Ipv6,
        Ipv6Addr,
    },
    LinkLocalAddress as _, MulticastAddr, SpecifiedAddr, UnicastAddr,
};
use packet::{BufferMut, EmptyBuf, Serializer};
use specialize_ip_macro::specialize_ip_address;

/// A timer ID for IP devices.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) enum IpDeviceTimerId<DeviceId> {
    Igmp(IgmpTimerId<DeviceId>),
    Mld(MldReportDelay<DeviceId>),
}

impl<DeviceId> From<IgmpTimerId<DeviceId>> for IpDeviceTimerId<DeviceId> {
    fn from(id: IgmpTimerId<DeviceId>) -> IpDeviceTimerId<DeviceId> {
        IpDeviceTimerId::Igmp(id)
    }
}

impl<DeviceId> From<MldReportDelay<DeviceId>> for IpDeviceTimerId<DeviceId> {
    fn from(id: MldReportDelay<DeviceId>) -> IpDeviceTimerId<DeviceId> {
        IpDeviceTimerId::Mld(id)
    }
}

// If we are provided with an impl of `TimerContext<IpDeviceTimerId<_>>`, then
// we can in turn provide impls of `TimerContext` for IGMP and MLD timers.
impl_timer_context!(
    IpDeviceIdContext,
    IpDeviceTimerId<C::DeviceId>,
    IgmpTimerId<C::DeviceId>,
    IpDeviceTimerId::Igmp(id),
    id
);
impl_timer_context!(
    IpDeviceIdContext,
    IpDeviceTimerId<C::DeviceId>,
    MldReportDelay<C::DeviceId>,
    IpDeviceTimerId::Mld(id),
    id
);

/// Handle an IP device timer firing.
pub(crate) fn handle_timer<C: BufferIpDeviceContext<EmptyBuf>>(
    ctx: &mut C,
    id: IpDeviceTimerId<C::DeviceId>,
) {
    match id {
        IpDeviceTimerId::Igmp(id) => TimerHandler::handle_timer(ctx, id),
        IpDeviceTimerId::Mld(id) => TimerHandler::handle_timer(ctx, id),
    }
}

/// The execution context for IP devices.
pub(crate) trait IpDeviceContext:
    IpDeviceIdContext + TimerContext<IpDeviceTimerId<Self::DeviceId>> + RngContext
{
    /// Gets immutable access to an IP device's state.
    fn get_ip_device_state(
        &self,
        device_id: Self::DeviceId,
    ) -> &DualStackIpDeviceState<Self::Instant>;

    /// Gets mutable access to an IP device's state.
    fn get_ip_device_state_mut(
        &mut self,
        device_id: Self::DeviceId,
    ) -> &mut DualStackIpDeviceState<Self::Instant> {
        let (state, _rng) = self.get_ip_device_state_mut_and_rng(device_id);
        state
    }

    /// Get mutable access to an IP device's state.
    fn get_ip_device_state_mut_and_rng(
        &mut self,
        device_id: Self::DeviceId,
    ) -> (&mut DualStackIpDeviceState<Self::Instant>, &mut Self::Rng);

    /// Returns an [`Iterator`] of IDs for all initialized devices.
    fn iter_devices(&self) -> Box<dyn Iterator<Item = Self::DeviceId> + '_>;

    /// Gets the MTU for a device.
    ///
    /// The MTU is the maximum size of an IP packet.
    fn get_mtu(&self, device_id: Self::DeviceId) -> u32;

    /// Joins the link-layer multicast group associated with the given IP
    /// multicast group.
    fn join_link_multicast_group<A: IpAddress>(
        &mut self,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<A>,
    );

    /// Leaves the link-layer multicast group associated with the given IP
    /// multicast group.
    fn leave_link_multicast_group<A: IpAddress>(
        &mut self,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<A>,
    );

    /// Starts duplicate address detection.
    // TODO(https://fxbug.dev/72378): Remove this method once DAD operates at
    // L3.
    fn start_duplicate_address_detection(
        &mut self,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    );

    /// Stops duplicate address detection.
    // TODO(https://fxbug.dev/72378): Remove this method once DAD operates at
    // L3.
    fn stop_duplicate_address_detection(
        &mut self,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    );
}

pub(crate) trait BufferIpDeviceContext<B: BufferMut>: IpDeviceContext {
    /// Sends an IP packet through the device.
    ///
    /// # Panics
    ///
    /// Panics if the device is not initialized.
    fn send_ip_frame<S: Serializer<Buffer = B>, A: IpAddress>(
        &mut self,
        device_id: Self::DeviceId,
        local_addr: SpecifiedAddr<A>,
        body: S,
    ) -> Result<(), S>;
}

/// Gets the IP address and subnet pairs associated with this device which are in
/// the assigned state.
///
/// Tentative IP addresses (addresses which are not yet fully bound to a device)
/// and deprecated IP addresses (addresses which have been assigned but should
/// no longer be used for new connections) will not be returned by
/// `get_assigned_ip_addr_subnets`.
///
/// Returns an [`Iterator`] of `AddrSubnet`.
///
/// See [`Tentative`] and [`AddrSubnet`] for more information.
#[specialize_ip_address]
pub(crate) fn get_assigned_ip_addr_subnets<C: IpDeviceContext, A: IpAddress>(
    ctx: &C,
    device_id: C::DeviceId,
) -> Box<dyn Iterator<Item = AddrSubnet<A>> + '_> {
    let state = ctx.get_ip_device_state(device_id);

    #[ipv4addr]
    return Box::new(state.ipv4.ip_state.iter_addrs().cloned());

    #[ipv6addr]
    return Box::new(state.ipv6.ip_state.iter_addrs().filter_map(|a| {
        if a.state.is_assigned() {
            Some((*a.addr_sub()).to_witness())
        } else {
            None
        }
    }));
}

/// Gets a single IP address and subnet for a device.
///
/// Note, tentative IP addresses (addresses which are not yet fully bound to a
/// device) will not be returned by `get_ip_addr`.
///
/// For IPv6, this only returns global (not link-local) addresses.
#[specialize_ip_address]
pub(super) fn get_ip_addr_subnet<C: IpDeviceContext, A: IpAddress>(
    ctx: &C,
    device_id: C::DeviceId,
) -> Option<AddrSubnet<A>> {
    #[ipv4addr]
    return get_assigned_ip_addr_subnets(ctx, device_id).nth(0);
    #[ipv6addr]
    return get_assigned_ip_addr_subnets(ctx, device_id).find(|a| {
        let addr: SpecifiedAddr<Ipv6Addr> = a.addr();
        !addr.is_link_local()
    });
}

/// Gets the state associated with an IPv4 device.
pub(crate) fn get_ipv4_device_state<C: IpDeviceContext>(
    ctx: &C,
    device_id: C::DeviceId,
) -> &IpDeviceState<C::Instant, Ipv4> {
    &ctx.get_ip_device_state(device_id).ipv4.ip_state
}

/// Gets the state associated with an IPv6 device.
pub(crate) fn get_ipv6_device_state<C: IpDeviceContext>(
    ctx: &C,
    device_id: C::DeviceId,
) -> &IpDeviceState<C::Instant, Ipv6> {
    &ctx.get_ip_device_state(device_id).ipv6.ip_state
}

/// Gets the hop limit for new IPv6 packets that will be sent out from `device`.
pub(crate) fn get_ipv6_hop_limit<C: IpDeviceContext>(ctx: &C, device: C::DeviceId) -> NonZeroU8 {
    get_ipv6_device_state(ctx, device).default_hop_limit
}

/// Return an [`Iterator`] of IDs for all initialized devices.
pub(crate) fn iter_devices<C: IpDeviceContext>(ctx: &C) -> impl Iterator<Item = C::DeviceId> + '_ {
    ctx.iter_devices()
}

/// Iterates over all of the IPv4 devices in the stack.
pub(super) fn iter_ipv4_devices<C: IpDeviceContext>(
    ctx: &C,
) -> impl Iterator<Item = (C::DeviceId, &IpDeviceState<C::Instant, Ipv4>)> + '_ {
    iter_devices(ctx).map(move |device| (device, get_ipv4_device_state(ctx, device)))
}

/// Iterates over all of the IPv6 devices in the stack.
pub(super) fn iter_ipv6_devices<C: IpDeviceContext>(
    ctx: &C,
) -> impl Iterator<Item = (C::DeviceId, &IpDeviceState<C::Instant, Ipv6>)> + '_ {
    iter_devices(ctx).map(move |device| (device, get_ipv6_device_state(ctx, device)))
}

/// Is IP packet routing enabled on `device`?
pub(crate) fn is_routing_enabled<C: IpDeviceContext, I: Ip>(
    ctx: &C,
    device_id: C::DeviceId,
) -> bool {
    match I::VERSION {
        IpVersion::V4 => get_ipv4_device_state(ctx, device_id).routing_enabled,
        IpVersion::V6 => get_ipv6_device_state(ctx, device_id).routing_enabled,
    }
}

/// Gets the MTU for a device.
///
/// The MTU is the maximum size of an IP packet.
pub(crate) fn get_mtu<C: IpDeviceContext>(ctx: &C, device_id: C::DeviceId) -> u32 {
    ctx.get_mtu(device_id)
}

impl<D: EventDispatcher> crate::ip::socket::IpSocketContext<Ipv4> for Ctx<D> {
    fn lookup_route(
        &self,
        addr: SpecifiedAddr<Ipv4Addr>,
    ) -> Option<Destination<Ipv4Addr, DeviceId>> {
        crate::ip::lookup_route(self, addr)
    }

    fn get_ip_device_state(&self, device: DeviceId) -> &IpDeviceState<D::Instant, Ipv4> {
        get_ipv4_device_state(self, device)
    }

    fn iter_devices(
        &self,
    ) -> Box<dyn Iterator<Item = (DeviceId, &IpDeviceState<D::Instant, Ipv4>)> + '_> {
        Box::new(
            iter_devices(self).map(move |device| (device, get_ipv4_device_state(self, device))),
        )
    }
}

impl<D: EventDispatcher> crate::ip::socket::IpSocketContext<Ipv6> for Ctx<D> {
    fn lookup_route(
        &self,
        addr: SpecifiedAddr<Ipv6Addr>,
    ) -> Option<Destination<Ipv6Addr, DeviceId>> {
        crate::ip::lookup_route(self, addr)
    }

    fn get_ip_device_state(&self, device: DeviceId) -> &IpDeviceState<D::Instant, Ipv6> {
        get_ipv6_device_state(self, device)
    }

    fn iter_devices(
        &self,
    ) -> Box<dyn Iterator<Item = (DeviceId, &IpDeviceState<D::Instant, Ipv6>)> + '_> {
        Box::new(
            iter_devices(self).map(move |device| (device, get_ipv6_device_state(self, device))),
        )
    }
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
#[specialize_ip_address]
pub(crate) fn join_ip_multicast<C: BufferIpDeviceContext<EmptyBuf>, A: IpAddress>(
    ctx: &mut C,
    device_id: C::DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    #[ipv4addr]
    let res = <C as GmpHandler<Ipv4>>::gmp_join_group(ctx, device_id, multicast_addr);

    #[ipv6addr]
    let res = <C as GmpHandler<Ipv6>>::gmp_join_group(ctx, device_id, multicast_addr);

    match res {
        GroupJoinResult::Joined(()) => ctx.join_link_multicast_group(device_id, multicast_addr),
        GroupJoinResult::AlreadyMember => {}
    }
}

/// Removes `device_id` from a multicast group `multicast_addr`.
///
/// `leave_ip_multicast` will attempt to remove `device_id` from a multicast
/// group `multicast_addr`. `device_id` may have "joined" the same multicast
/// address multiple times, so `device_id` will only leave the multicast group
/// once `leave_ip_multicast` has been called for each corresponding
/// [`join_ip_multicast`]. That is, if `join_ip_multicast` gets called 3 times
/// and `leave_ip_multicast` gets called two times (after all 3
/// `join_ip_multicast` calls), `device_id` will still be in the multicast group
/// until the next (final) call to `leave_ip_multicast`.
///
/// # Panics
///
/// If `device_id` is not currently in the multicast group `multicast_addr`.
#[specialize_ip_address]
pub(crate) fn leave_ip_multicast<C: BufferIpDeviceContext<EmptyBuf>, A: IpAddress>(
    ctx: &mut C,
    device_id: C::DeviceId,
    multicast_addr: MulticastAddr<A>,
) {
    #[ipv4addr]
    let res = <C as GmpHandler<Ipv4>>::gmp_leave_group(ctx, device_id, multicast_addr);

    #[ipv6addr]
    let res = <C as GmpHandler<Ipv6>>::gmp_leave_group(ctx, device_id, multicast_addr);

    match res {
        GroupLeaveResult::Left(()) => ctx.leave_link_multicast_group(device_id, multicast_addr),
        GroupLeaveResult::StillMember => {}
        GroupLeaveResult::NotMember => panic!(
            "attempted to leave IP multicast group we were not a member of: {}",
            multicast_addr,
        ),
    }
}

/// Adds an IP address and associated subnet to this device.
///
/// `config` is the way this address is being configured. See [`AddrConfig`]
/// for more details.
///
/// For IPv6, this function also joins the solicited-node multicast group and
/// begins performing Duplicate Address Detection (DAD).
///
/// # Panics
///
/// Panics if `A = Ipv4Addr` and `config != AddrConfig::Manual`.
pub(crate) fn add_ip_addr_subnet<C: BufferIpDeviceContext<EmptyBuf>, A: IpAddress>(
    ctx: &mut C,
    device_id: C::DeviceId,
    addr_sub: AddrSubnet<A>,
    config: AddrConfig<C::Instant>,
) -> Result<(), ExistsError> {
    let state = ctx.get_ip_device_state_mut(device_id);
    match addr_sub.into() {
        AddrSubnetEither::V4(addr_sub) => state.ipv4.ip_state.add_addr(addr_sub),
        AddrSubnetEither::V6(addr_sub) => {
            let Ipv6DeviceState {
                ref mut ip_state,
                config:
                    Ipv6DeviceConfiguration {
                        dad_transmits,
                        ip_config: IpDeviceConfiguration { gmp_enabled: _ },
                    },
            } = state.ipv6;

            let addr_sub = addr_sub.to_unicast();
            ip_state
                .add_addr(Ipv6AddressEntry::new(
                    addr_sub,
                    AddressState::Tentative { dad_transmits_remaining: dad_transmits },
                    config,
                ))
                .map(|()| {
                    join_ip_multicast(ctx, device_id, addr_sub.addr().to_solicited_node_address());
                    ctx.start_duplicate_address_detection(device_id, addr_sub.addr());
                })
        }
    }
}

/// Removes an IP address and associated subnet from this device.
///
/// If `config_type` is provided, then only an address of that
/// configuration type will be removed.
///
/// # Panics
///
/// `del_ip_addr_inner` panics if `A = Ipv4Addr` and `config_type != None`.
pub(crate) fn del_ip_addr<C: BufferIpDeviceContext<EmptyBuf>, A: IpAddress>(
    ctx: &mut C,
    device_id: C::DeviceId,
    addr: &A,
) -> Result<(), NotFoundError> {
    let state = ctx.get_ip_device_state_mut(device_id);
    match addr.to_ip_addr() {
        IpAddr::V4(addr) => state.ipv4.ip_state.remove_addr(&addr),
        IpAddr::V6(addr) => {
            state.ipv6.ip_state.remove_addr(&addr).map(|()| {
                // TODO(https://fxbug.dev/69196): Give `addr` the type
                // `UnicastAddr<Ipv6Addr>` for IPv6 instead of doing this
                // dynamic check here and statically guarantee only unicast
                // addresses are added for IPv6.
                if let Some(addr) = UnicastAddr::new(addr) {
                    // Leave the the solicited-node multicast group.
                    leave_ip_multicast(ctx, device_id, addr.to_solicited_node_address());

                    ctx.stop_duplicate_address_detection(device_id, addr);
                }
            })
        }
    }
}

/// Sends an IP packet through the device.
///
/// # Panics
///
/// Panics if the device is not initialized.
pub(crate) fn send_ip_frame<
    C: BufferIpDeviceContext<B>,
    B: BufferMut,
    S: Serializer<Buffer = B>,
    A: IpAddress,
>(
    ctx: &mut C,
    device_id: C::DeviceId,
    local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    ctx.send_ip_frame(device_id, local_addr, body)
}

/// Updates the IPv4 Configuration for the device.
pub(crate) fn set_ipv4_configuration<C: IpDeviceContext>(
    ctx: &mut C,
    device_id: C::DeviceId,
    config: Ipv4DeviceConfiguration,
) {
    ctx.get_ip_device_state_mut(device_id).ipv4.config = config;
}

/// Updates the IPv6 Configuration for the device.
pub(crate) fn set_ipv6_configuration<C: IpDeviceContext>(
    ctx: &mut C,
    device_id: C::DeviceId,
    config: Ipv6DeviceConfiguration,
) {
    ctx.get_ip_device_state_mut(device_id).ipv6.config = config;
}

#[cfg(test)]
mod tests {
    use alloc::vec::Vec;

    use crate::testutil::{DummyEventDispatcher, DummyEventDispatcherBuilder, TestIpExt as _};
    use net_types::{
        ip::{Ip as _, Ipv6},
        Witness,
    };

    use super::*;

    /// Test that `get_ip_addr_subnet` only returns non-local IPv6 addresses.
    #[test]
    fn test_get_ip_addr_subnet() {
        let config = Ipv6::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::default().build::<DummyEventDispatcher>();
        let device = ctx.state.add_ethernet_device(config.local_mac, Ipv6::MINIMUM_LINK_MTU.into());

        // `initialize_device` adds the MAC-derived link-local IPv6 address.
        crate::device::initialize_device(&mut ctx, device);
        // Verify that there is a single assigned address - the MAC-derived
        // link-local.
        let state = ctx.get_ip_device_state(device);
        assert_eq!(
            state
                .ipv6
                .ip_state
                .iter_addrs()
                .map(|entry| entry.addr_sub().addr())
                .collect::<Vec<_>>(),
            [config.local_mac.to_ipv6_link_local().addr().get()]
        );
        assert_eq!(get_ip_addr_subnet::<_, Ipv6Addr>(&ctx, device.into()), None);
    }
}
