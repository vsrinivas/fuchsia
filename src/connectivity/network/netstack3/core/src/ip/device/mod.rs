// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An IP device.

mod integration;
pub(crate) mod state;

use alloc::boxed::Box;
use core::num::NonZeroU8;

#[cfg(test)]
use net_types::ip::{Ip, IpVersion};
use net_types::{
    ip::{AddrSubnet, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    LinkLocalAddress as _, MulticastAddr, SpecifiedAddr, UnicastAddr, Witness as _,
};
use packet::{BufferMut, EmptyBuf, Serializer};

use crate::{
    context::{InstantContext, RngContext, TimerContext, TimerHandler},
    error::{ExistsError, NotFoundError},
    ip::{
        device::state::{
            AddrConfig, AddressState, IpDeviceConfiguration, IpDeviceState, IpDeviceStateIpExt,
            Ipv4DeviceConfiguration, Ipv4DeviceState, Ipv6AddressEntry, Ipv6DeviceConfiguration,
            Ipv6DeviceState,
        },
        gmp::{
            igmp::IgmpTimerId, mld::MldReportDelay, GmpHandler, GroupJoinResult, GroupLeaveResult,
        },
        IpDeviceIdContext,
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
    IpDeviceIdContext<Ipv4>,
    Ipv4DeviceTimerId<C::DeviceId>,
    IgmpTimerId<C::DeviceId>,
    Ipv4DeviceTimerId(id),
    id
);

/// Handle an IPv4 device timer firing.
pub(crate) fn handle_ipv4_timer<C: BufferIpDeviceContext<Ipv4, EmptyBuf>>(
    ctx: &mut C,
    Ipv4DeviceTimerId(id): Ipv4DeviceTimerId<C::DeviceId>,
) {
    TimerHandler::handle_timer(ctx, id)
}

/// A timer ID for IPv6 devices.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) enum Ipv6DeviceTimerId<DeviceId> {
    Mld(MldReportDelay<DeviceId>),
}

impl<DeviceId> From<MldReportDelay<DeviceId>> for Ipv6DeviceTimerId<DeviceId> {
    fn from(id: MldReportDelay<DeviceId>) -> Ipv6DeviceTimerId<DeviceId> {
        Ipv6DeviceTimerId::Mld(id)
    }
}

// If we are provided with an impl of `TimerContext<Ipv6DeviceTimerId<_>>`, then
// we can in turn provide an impl of `TimerContext` for MLD.
impl_timer_context!(
    IpDeviceIdContext<Ipv6>,
    Ipv6DeviceTimerId<C::DeviceId>,
    MldReportDelay<C::DeviceId>,
    Ipv6DeviceTimerId::Mld(id),
    id
);

/// Handle an IPv6 device timer firing.
pub(crate) fn handle_ipv6_timer<C: BufferIpDeviceContext<Ipv6, EmptyBuf>>(
    ctx: &mut C,
    id: Ipv6DeviceTimerId<C::DeviceId>,
) {
    match id {
        Ipv6DeviceTimerId::Mld(id) => TimerHandler::handle_timer(ctx, id),
    }
}

/// An extension trait adding IP device properties.
pub(crate) trait IpDeviceIpExt<Instant, DeviceId>: IpDeviceStateIpExt<Instant> {
    type State;
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

/// The execution context for IP devices.
pub(crate) trait IpDeviceContext<
    I: IpDeviceIpExt<<Self as InstantContext>::Instant, Self::DeviceId>,
>: IpDeviceIdContext<I> + TimerContext<I::Timer> + RngContext
{
    /// Gets immutable access to an IP device's state.
    fn get_ip_device_state(&self, device_id: Self::DeviceId) -> &I::State;

    /// Gets mutable access to an IP device's state.
    fn get_ip_device_state_mut(&mut self, device_id: Self::DeviceId) -> &mut I::State {
        let (state, _rng) = self.get_ip_device_state_mut_and_rng(device_id);
        state
    }

    /// Get mutable access to an IP device's state.
    fn get_ip_device_state_mut_and_rng(
        &mut self,
        device_id: Self::DeviceId,
    ) -> (&mut I::State, &mut Self::Rng);

    /// Returns an [`Iterator`] of IDs for all initialized devices.
    fn iter_devices(&self) -> Box<dyn Iterator<Item = Self::DeviceId> + '_>;

    /// Gets the MTU for a device.
    ///
    /// The MTU is the maximum size of an IP packet.
    fn get_mtu(&self, device_id: Self::DeviceId) -> u32;

    /// Joins the link-layer multicast group associated with the given IP
    /// multicast group.
    fn join_link_multicast_group(
        &mut self,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<I::Addr>,
    );

    /// Leaves the link-layer multicast group associated with the given IP
    /// multicast group.
    fn leave_link_multicast_group(
        &mut self,
        device_id: Self::DeviceId,
        multicast_addr: MulticastAddr<I::Addr>,
    );
}

/// The execution context for an IPv6 device.
pub(crate) trait Ipv6DeviceContext: IpDeviceContext<Ipv6> {
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

    /// Starts soliciting routers.
    // TODO(https://fxbug.dev/72378): Remove this method once DAD operates at
    // L3.
    fn start_soliciting_routers(&mut self, device_id: Self::DeviceId);

    /// Stops soliciting routers.
    // TODO(https://fxbug.dev/72378): Remove this method once DAD operates at
    // L3.
    fn stop_soliciting_routers(&mut self, device_id: Self::DeviceId);
}

/// The execution context for an IP device with a buffer.
pub(crate) trait BufferIpDeviceContext<
    I: IpDeviceIpExt<Self::Instant, Self::DeviceId>,
    B: BufferMut,
>: IpDeviceContext<I>
{
    /// Sends an IP packet through the device.
    ///
    /// # Panics
    ///
    /// Panics if the device is not initialized.
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device_id: Self::DeviceId,
        local_addr: SpecifiedAddr<I::Addr>,
        body: S,
    ) -> Result<(), S>;
}

/// Gets the IPv4 address and subnet pairs associated with this device.
///
/// Returns an [`Iterator`] of `AddrSubnet`.
pub(crate) fn get_assigned_ipv4_addr_subnets<C: IpDeviceContext<Ipv4>>(
    ctx: &C,
    device_id: C::DeviceId,
) -> impl Iterator<Item = AddrSubnet<Ipv4Addr>> + '_ {
    ctx.get_ip_device_state(device_id).ip_state.iter_addrs().cloned()
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
pub(crate) fn get_assigned_ipv6_addr_subnets<C: IpDeviceContext<Ipv6>>(
    ctx: &C,
    device_id: C::DeviceId,
) -> impl Iterator<Item = AddrSubnet<Ipv6Addr>> + '_ {
    ctx.get_ip_device_state(device_id).ip_state.iter_addrs().filter_map(|a| {
        if a.state.is_assigned() {
            Some((*a.addr_sub()).to_witness())
        } else {
            None
        }
    })
}

/// Gets a single IPv4 address and subnet for a device.
pub(super) fn get_ipv4_addr_subnet<C: IpDeviceContext<Ipv4>>(
    ctx: &C,
    device_id: C::DeviceId,
) -> Option<AddrSubnet<Ipv4Addr>> {
    get_assigned_ipv4_addr_subnets(ctx, device_id).nth(0)
}

/// Gets a single non-link-local IPv6 address and subnet for a device.
///
/// Note, tentative IP addresses (addresses which are not yet fully bound to a
/// device) will not be returned by this method.
pub(super) fn get_ipv6_addr_subnet<C: IpDeviceContext<Ipv6>>(
    ctx: &C,
    device_id: C::DeviceId,
) -> Option<AddrSubnet<Ipv6Addr>> {
    get_assigned_ipv6_addr_subnets(ctx, device_id).find(|a| {
        let addr: SpecifiedAddr<Ipv6Addr> = a.addr();
        !addr.is_link_local()
    })
}

/// Gets the state associated with an IPv4 device.
pub(crate) fn get_ipv4_device_state<C: IpDeviceContext<Ipv4>>(
    ctx: &C,
    device_id: C::DeviceId,
) -> &IpDeviceState<C::Instant, Ipv4> {
    &ctx.get_ip_device_state(device_id).ip_state
}

/// Gets the state associated with an IPv6 device.
pub(crate) fn get_ipv6_device_state<C: IpDeviceContext<Ipv6>>(
    ctx: &C,
    device_id: C::DeviceId,
) -> &IpDeviceState<C::Instant, Ipv6> {
    &ctx.get_ip_device_state(device_id).ip_state
}

/// Gets the hop limit for new IPv6 packets that will be sent out from `device`.
pub(crate) fn get_ipv6_hop_limit<C: IpDeviceContext<Ipv6>>(
    ctx: &C,
    device: C::DeviceId,
) -> NonZeroU8 {
    get_ipv6_device_state(ctx, device).default_hop_limit
}

/// Iterates over all of the IPv4 devices in the stack.
pub(super) fn iter_ipv4_devices<C: IpDeviceContext<Ipv4>>(
    ctx: &C,
) -> impl Iterator<Item = (C::DeviceId, &IpDeviceState<C::Instant, Ipv4>)> + '_ {
    ctx.iter_devices().map(move |device| (device, get_ipv4_device_state(ctx, device)))
}

/// Iterates over all of the IPv6 devices in the stack.
pub(super) fn iter_ipv6_devices<C: IpDeviceContext<Ipv6>>(
    ctx: &C,
) -> impl Iterator<Item = (C::DeviceId, &IpDeviceState<C::Instant, Ipv6>)> + '_ {
    ctx.iter_devices().map(move |device| (device, get_ipv6_device_state(ctx, device)))
}

/// Is IPv4 packet routing enabled on `device`?
pub(crate) fn is_ipv4_routing_enabled<C: IpDeviceContext<Ipv4>>(
    ctx: &C,
    device_id: C::DeviceId,
) -> bool {
    get_ipv4_device_state(ctx, device_id).routing_enabled
}

/// Is IPv6 packet routing enabled on `device`?
pub(crate) fn is_ipv6_routing_enabled<C: IpDeviceContext<Ipv6>>(
    ctx: &C,
    device_id: C::DeviceId,
) -> bool {
    get_ipv6_device_state(ctx, device_id).routing_enabled
}

/// Enables or disables IP packet routing on `device`.
#[cfg(test)]
pub(crate) fn set_routing_enabled<
    C: IpDeviceContext<Ipv4> + Ipv6DeviceContext + GmpHandler<Ipv6>,
    I: Ip,
>(
    ctx: &mut C,
    device: <C as IpDeviceIdContext<Ipv6>>::DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError>
where
    C: IpDeviceIdContext<Ipv6, DeviceId = <C as IpDeviceIdContext<Ipv4>>::DeviceId>,
{
    match I::VERSION {
        IpVersion::V4 => set_ipv4_routing_enabled(ctx, device, enabled),
        IpVersion::V6 => set_ipv6_routing_enabled(ctx, device, enabled),
    }
}

/// Enables or disables IPv4 packet routing on `device_id`.
#[cfg(test)]
fn set_ipv4_routing_enabled<C: IpDeviceContext<Ipv4>>(
    ctx: &mut C,
    device_id: C::DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError> {
    if device_id.is_loopback() {
        return Err(NotSupportedError);
    }

    ctx.get_ip_device_state_mut(device_id).ip_state.routing_enabled = enabled;
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
pub(crate) fn set_ipv6_routing_enabled<C: Ipv6DeviceContext + GmpHandler<Ipv6>>(
    ctx: &mut C,
    device_id: C::DeviceId,
    enabled: bool,
) -> Result<(), NotSupportedError> {
    if device_id.is_loopback() {
        return Err(NotSupportedError);
    }

    if is_ipv6_routing_enabled(ctx, device_id) == enabled {
        return Ok(());
    }

    if enabled {
        ctx.stop_soliciting_routers(device_id);
        ctx.get_ip_device_state_mut(device_id).ip_state.routing_enabled = true;
        join_ip_multicast(ctx, device_id, Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS);
    } else {
        leave_ip_multicast(ctx, device_id, Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS);
        ctx.get_ip_device_state_mut(device_id).ip_state.routing_enabled = false;
        ctx.start_soliciting_routers(device_id);
    }

    Ok(())
}

/// Gets the MTU for a device.
///
/// The MTU is the maximum size of an IP packet.
pub(crate) fn get_mtu<I: IpDeviceIpExt<C::Instant, C::DeviceId>, C: IpDeviceContext<I>>(
    ctx: &C,
    device_id: C::DeviceId,
) -> u32 {
    ctx.get_mtu(device_id)
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
    I: IpDeviceIpExt<C::Instant, C::DeviceId>,
    C: IpDeviceContext<I> + GmpHandler<I>,
>(
    ctx: &mut C,
    device_id: C::DeviceId,
    multicast_addr: MulticastAddr<I::Addr>,
) {
    match ctx.gmp_join_group(device_id, multicast_addr) {
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
/// [`join_ip_multicast`]. That is, if `join_ip_multicast` gets called 3
/// times and `leave_ip_multicast` gets called two times (after all 3
/// `join_ip_multicast` calls), `device_id` will still be in the multicast
/// group until the next (final) call to `leave_ip_multicast`.
///
/// # Panics
///
/// If `device_id` is not currently in the multicast group `multicast_addr`.
pub(crate) fn leave_ip_multicast<
    I: IpDeviceIpExt<C::Instant, C::DeviceId>,
    C: IpDeviceContext<I> + GmpHandler<I>,
>(
    ctx: &mut C,
    device_id: C::DeviceId,
    multicast_addr: MulticastAddr<I::Addr>,
) {
    match ctx.gmp_leave_group(device_id, multicast_addr) {
        GroupLeaveResult::Left(()) => ctx.leave_link_multicast_group(device_id, multicast_addr),
        GroupLeaveResult::StillMember => {}
        GroupLeaveResult::NotMember => panic!(
            "attempted to leave IP multicast group we were not a member of: {}",
            multicast_addr,
        ),
    }
}

/// Adds an IPv4 address and associated subnet to this device.
pub(crate) fn add_ipv4_addr_subnet<
    C: IpDeviceContext<Ipv4> + BufferIpDeviceContext<Ipv4, EmptyBuf>,
>(
    ctx: &mut C,
    device_id: C::DeviceId,
    addr_sub: AddrSubnet<Ipv4Addr>,
) -> Result<(), ExistsError> {
    ctx.get_ip_device_state_mut(device_id).ip_state.add_addr(addr_sub)
}

/// Adds an IPv6 address (with duplicate address detection) and associated
/// subnet to this device and joins the address's solicited-node multicast
/// group.
///
/// `config` is the way this address is being configured. See [`AddrConfig`]
/// for more details.
pub(crate) fn add_ipv6_addr_subnet<C: Ipv6DeviceContext + BufferIpDeviceContext<Ipv6, EmptyBuf>>(
    ctx: &mut C,
    device_id: C::DeviceId,
    addr_sub: AddrSubnet<Ipv6Addr>,
    config: AddrConfig<C::Instant>,
) -> Result<(), ExistsError> {
    let Ipv6DeviceState {
        ref mut ip_state,
        config:
            Ipv6DeviceConfiguration {
                dad_transmits,
                ip_config: IpDeviceConfiguration { gmp_enabled: _ },
            },
    } = ctx.get_ip_device_state_mut(device_id);

    let addr_sub = addr_sub.to_unicast();
    ip_state
        .add_addr(Ipv6AddressEntry::new(
            addr_sub,
            AddressState::Tentative { dad_transmits_remaining: *dad_transmits },
            config,
        ))
        .map(|()| {
            join_ip_multicast(ctx, device_id, addr_sub.addr().to_solicited_node_address());
            ctx.start_duplicate_address_detection(device_id, addr_sub.addr());
        })
}

/// Removes an IPv4 address and associated subnet from this device.
pub(crate) fn del_ipv4_addr<C: IpDeviceContext<Ipv4> + BufferIpDeviceContext<Ipv4, EmptyBuf>>(
    ctx: &mut C,
    device_id: C::DeviceId,
    addr: &SpecifiedAddr<Ipv4Addr>,
) -> Result<(), NotFoundError> {
    ctx.get_ip_device_state_mut(device_id).ip_state.remove_addr(&addr)
}

/// Removes an IPv6 address and associated subnet from this device.
pub(crate) fn del_ipv6_addr<C: Ipv6DeviceContext + BufferIpDeviceContext<Ipv6, EmptyBuf>>(
    ctx: &mut C,
    device_id: C::DeviceId,
    addr: &SpecifiedAddr<Ipv6Addr>,
) -> Result<(), NotFoundError> {
    ctx.get_ip_device_state_mut(device_id).ip_state.remove_addr(&addr).map(|()| {
        // TODO(https://fxbug.dev/69196): Give `addr` the type
        // `UnicastAddr<Ipv6Addr>` for IPv6 instead of doing this
        // dynamic check here and statically guarantee only unicast
        // addresses are added for IPv6.
        if let Some(addr) = UnicastAddr::new(addr.get()) {
            // Leave the the solicited-node multicast group.
            leave_ip_multicast(ctx, device_id, addr.to_solicited_node_address());
            ctx.stop_duplicate_address_detection(device_id, addr);
        }
    })
}

/// Sends an IP packet through the device.
///
/// # Panics
///
/// Panics if the device is not initialized.
pub(crate) fn send_ip_frame<
    I: IpDeviceIpExt<C::Instant, C::DeviceId>,
    C: BufferIpDeviceContext<I, B>,
    B: BufferMut,
    S: Serializer<Buffer = B>,
>(
    ctx: &mut C,
    device_id: C::DeviceId,
    local_addr: SpecifiedAddr<I::Addr>,
    body: S,
) -> Result<(), S> {
    ctx.send_ip_frame(device_id, local_addr, body)
}

/// Updates the IPv4 Configuration for the device.
pub(crate) fn set_ipv4_configuration<C: IpDeviceContext<Ipv4>>(
    ctx: &mut C,
    device_id: C::DeviceId,
    config: Ipv4DeviceConfiguration,
) {
    ctx.get_ip_device_state_mut(device_id).config = config;
}

/// Updates the IPv6 Configuration for the device.
pub(crate) fn set_ipv6_configuration<C: IpDeviceContext<Ipv6>>(
    ctx: &mut C,
    device_id: C::DeviceId,
    config: Ipv6DeviceConfiguration,
) {
    ctx.get_ip_device_state_mut(device_id).config = config;
}

#[cfg(test)]
mod tests {
    use alloc::vec::Vec;

    use net_types::{
        ip::{Ip as _, Ipv6},
        Witness,
    };

    use super::*;
    use crate::testutil::{DummyEventDispatcherBuilder, TestIpExt as _};

    /// Test that `get_ipv6_addr_subnet` only returns non-local IPv6 addresses.
    #[test]
    fn test_get_ipv6_addr_subnet() {
        let config = Ipv6::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let device = ctx.state.add_ethernet_device(config.local_mac, Ipv6::MINIMUM_LINK_MTU.into());

        // `initialize_device` adds the MAC-derived link-local IPv6 address.
        crate::device::initialize_device(&mut ctx, device);
        // Verify that there is a single assigned address - the MAC-derived
        // link-local.
        let state = IpDeviceContext::<Ipv6>::get_ip_device_state(&ctx, device);
        assert_eq!(
            state.ip_state.iter_addrs().map(|entry| entry.addr_sub().addr()).collect::<Vec<_>>(),
            [config.local_mac.to_ipv6_link_local().addr().get()]
        );
        assert_eq!(get_ipv6_addr_subnet(&ctx, device.into()), None);
    }
}
