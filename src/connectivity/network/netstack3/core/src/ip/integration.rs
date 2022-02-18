// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The integrations for protocols built on top of IP.

use alloc::boxed::Box;

use net_types::{
    ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    SpecifiedAddr,
};

use crate::{
    context::StateContext,
    ip::{
        device::{
            get_ipv4_device_state, get_ipv6_device_state, iter_ipv4_devices, iter_ipv6_devices,
            state::IpDeviceState, IpDeviceContext,
        },
        icmp::{Icmpv4State, Icmpv6State},
        lookup_route,
        socket::{IpSock, IpSocketContext},
        Destination, IpLayerContext, IpLayerStateIpExt, IpPacketFragmentCache, IpStateInner,
    },
};

impl<C: IpLayerContext<Ipv4> + IpDeviceContext<Ipv4>> IpSocketContext<Ipv4> for C {
    fn lookup_route(
        &self,
        addr: SpecifiedAddr<Ipv4Addr>,
    ) -> Option<Destination<Ipv4Addr, C::DeviceId>> {
        lookup_route(self, addr)
    }

    fn get_ip_device_state(&self, device: C::DeviceId) -> &IpDeviceState<C::Instant, Ipv4> {
        get_ipv4_device_state(self, device)
    }

    fn iter_devices(
        &self,
    ) -> Box<dyn Iterator<Item = (C::DeviceId, &IpDeviceState<C::Instant, Ipv4>)> + '_> {
        Box::new(iter_ipv4_devices(self))
    }
}

impl<C: IpLayerContext<Ipv6> + IpDeviceContext<Ipv6>> IpSocketContext<Ipv6> for C {
    fn lookup_route(
        &self,
        addr: SpecifiedAddr<Ipv6Addr>,
    ) -> Option<Destination<Ipv6Addr, C::DeviceId>> {
        lookup_route(self, addr)
    }

    fn get_ip_device_state(&self, device: C::DeviceId) -> &IpDeviceState<C::Instant, Ipv6> {
        get_ipv6_device_state(self, device)
    }

    fn iter_devices(
        &self,
    ) -> Box<dyn Iterator<Item = (C::DeviceId, &IpDeviceState<C::Instant, Ipv6>)> + '_> {
        Box::new(iter_ipv6_devices(self))
    }
}

impl<I: IpLayerStateIpExt<C::Instant, C::DeviceId>, C: IpLayerContext<I>>
    StateContext<IpPacketFragmentCache<I>> for C
{
    fn get_state_with(&self, _id: ()) -> &IpPacketFragmentCache<I> {
        &AsRef::<IpStateInner<_, _, _>>::as_ref(self.get_ip_layer_state()).fragment_cache
    }

    fn get_state_mut_with(&mut self, _id: ()) -> &mut IpPacketFragmentCache<I> {
        &mut AsMut::<IpStateInner<_, _, _>>::as_mut(self.get_ip_layer_state_mut()).fragment_cache
    }
}

impl<C: IpLayerContext<Ipv4>> StateContext<Icmpv4State<C::Instant, IpSock<Ipv4, C::DeviceId>>>
    for C
{
    fn get_state_with(&self, _id: ()) -> &Icmpv4State<C::Instant, IpSock<Ipv4, C::DeviceId>> {
        &self.get_ip_layer_state().icmp
    }

    fn get_state_mut_with(
        &mut self,
        _id: (),
    ) -> &mut Icmpv4State<C::Instant, IpSock<Ipv4, C::DeviceId>> {
        &mut self.get_ip_layer_state_mut().icmp
    }
}

impl<C: IpLayerContext<Ipv6>> StateContext<Icmpv6State<C::Instant, IpSock<Ipv6, C::DeviceId>>>
    for C
{
    fn get_state_with(&self, _id: ()) -> &Icmpv6State<C::Instant, IpSock<Ipv6, C::DeviceId>> {
        &self.get_ip_layer_state().icmp
    }

    fn get_state_mut_with(
        &mut self,
        _id: (),
    ) -> &mut Icmpv6State<C::Instant, IpSock<Ipv6, C::DeviceId>> {
        &mut self.get_ip_layer_state_mut().icmp
    }
}
