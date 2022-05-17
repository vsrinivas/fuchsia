// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The integrations for protocols built on top of IP.

use net_types::{
    ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    SpecifiedAddr,
};
use packet::{BufferMut, Serializer};

use crate::{
    context::StateContext,
    ip::{
        self,
        icmp::{Icmpv4State, Icmpv6State},
        send_ipv4_packet_from_device, send_ipv6_packet_from_device,
        socket::{BufferIpSocketContext, IpSock, IpSocketContext},
        IpLayerContext, IpLayerStateIpExt, IpPacketFragmentCache, IpStateContext, IpStateInner,
        SendIpPacketMeta,
    },
};

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

impl<
        B: BufferMut,
        C: ip::BufferIpDeviceContext<Ipv4, B> + IpStateContext<Ipv4> + IpSocketContext<Ipv4>,
    > BufferIpSocketContext<Ipv4, B> for C
{
    fn send_ip_packet<S: Serializer<Buffer = B>>(
        &mut self,
        meta: SendIpPacketMeta<Ipv4, C::DeviceId, SpecifiedAddr<Ipv4Addr>>,
        body: S,
    ) -> Result<(), S> {
        send_ipv4_packet_from_device(self, &mut (), meta.into(), body)
    }
}

impl<
        B: BufferMut,
        C: ip::BufferIpDeviceContext<Ipv6, B> + IpStateContext<Ipv6> + IpSocketContext<Ipv6>,
    > BufferIpSocketContext<Ipv6, B> for C
{
    fn send_ip_packet<S: Serializer<Buffer = B>>(
        &mut self,
        meta: SendIpPacketMeta<Ipv6, C::DeviceId, SpecifiedAddr<Ipv6Addr>>,
        body: S,
    ) -> Result<(), S> {
        send_ipv6_packet_from_device(self, &mut (), meta.into(), body)
    }
}
