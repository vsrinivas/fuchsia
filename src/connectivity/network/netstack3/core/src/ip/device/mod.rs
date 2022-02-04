// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An IP device.

pub(crate) mod state;

use alloc::boxed::Box;
use core::num::NonZeroU8;

use net_types::{
    ip::{AddrSubnet, Ip, IpAddress, IpVersion, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    LinkLocalAddress as _, SpecifiedAddr,
};
use specialize_ip_macro::specialize_ip_address;

use crate::{
    context::InstantContext,
    device::DeviceId,
    ip::{
        device::state::{DualStackIpDeviceState, IpDeviceState},
        forwarding::Destination,
        IpDeviceIdContext,
    },
    Ctx, EventDispatcher,
};

/// The execution context for IP devices.
pub(crate) trait IpDeviceContext: InstantContext + IpDeviceIdContext {
    /// Gets immutable access to an IP device's state.
    fn get_ip_device_state(
        &self,
        device_id: Self::DeviceId,
    ) -> &DualStackIpDeviceState<Self::Instant>;

    /// Returns an [`Iterator`] of IDs for all initialized devices.
    fn iter_devices(&self) -> Box<dyn Iterator<Item = Self::DeviceId> + '_>;

    /// Gets the MTU for a device.
    ///
    /// The MTU is the maximum size of an IP packet.
    fn get_mtu(&self, device_id: Self::DeviceId) -> u32;
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
