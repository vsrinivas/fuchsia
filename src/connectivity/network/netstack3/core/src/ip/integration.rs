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
        path_mtu::{PmtuCache, PmtuStateContext},
        reassembly::FragmentStateContext,
        send_ipv4_packet_from_device, send_ipv6_packet_from_device,
        socket::{BufferIpSocketContext, IpSock, IpSocketContext},
        IpDeviceIdContext, IpPacketFragmentCache, IpStateContext, SendIpPacketMeta,
    },
};

// Note: we can't provide a single impl for `FragmentStateContext` which is
// generic over all `I: IpLayerStateIpExt<SC::Instant, SC::DeviceId>` because
// we have a `DummyCtx` type in the reassembly tests module which implements
// `FragmentStateContext` resulting in a conflicting implementations error.
// Manually implementing `FragmentStateContext` for each `I: Ip` we support
// seems to workaround this issue.
//
// See https://doc.rust-lang.org/error-index.html#E0119 for more details.

impl<SC: IpStateContext<Ipv4>> FragmentStateContext<Ipv4> for SC {
    fn get_state_mut(&mut self) -> &mut IpPacketFragmentCache<Ipv4> {
        &mut self.get_ip_layer_state_mut().as_mut().fragment_cache
    }
}

impl<SC: IpStateContext<Ipv6>> FragmentStateContext<Ipv6> for SC {
    fn get_state_mut(&mut self) -> &mut IpPacketFragmentCache<Ipv6> {
        &mut self.get_ip_layer_state_mut().as_mut().fragment_cache
    }
}

// We can't provide a single impl for `PmtuStateContext` which is generic over
// `Ip` for the same reason as `FragmentStateContext` above.

impl<SC: IpStateContext<Ipv4>> PmtuStateContext<Ipv4> for SC {
    fn get_state_mut(&mut self) -> &mut PmtuCache<Ipv4, SC::Instant> {
        &mut self.get_ip_layer_state_mut().as_mut().pmtu_cache
    }
}

impl<SC: IpStateContext<Ipv6>> PmtuStateContext<Ipv6> for SC {
    fn get_state_mut(&mut self) -> &mut PmtuCache<Ipv6, SC::Instant> {
        &mut self.get_ip_layer_state_mut().as_mut().pmtu_cache
    }
}

impl<C: IpStateContext<Ipv4>> StateContext<Icmpv4State<C::Instant, IpSock<Ipv4, C::DeviceId>>>
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

impl<C: IpStateContext<Ipv6>> StateContext<Icmpv6State<C::Instant, IpSock<Ipv6, C::DeviceId>>>
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
        C,
        SC: ip::BufferIpDeviceContext<Ipv4, C, B> + IpStateContext<Ipv4> + IpSocketContext<Ipv4, C>,
    > BufferIpSocketContext<Ipv4, C, B> for SC
{
    fn send_ip_packet<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        meta: SendIpPacketMeta<
            Ipv4,
            <SC as IpDeviceIdContext<Ipv4>>::DeviceId,
            SpecifiedAddr<Ipv4Addr>,
        >,
        body: S,
    ) -> Result<(), S> {
        send_ipv4_packet_from_device(self, ctx, meta.into(), body)
    }
}

impl<
        B: BufferMut,
        C,
        SC: ip::BufferIpDeviceContext<Ipv6, C, B> + IpStateContext<Ipv6> + IpSocketContext<Ipv6, C>,
    > BufferIpSocketContext<Ipv6, C, B> for SC
{
    fn send_ip_packet<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        meta: SendIpPacketMeta<
            Ipv6,
            <SC as IpDeviceIdContext<Ipv6>>::DeviceId,
            SpecifiedAddr<Ipv6Addr>,
        >,
        body: S,
    ) -> Result<(), S> {
        send_ipv6_packet_from_device(self, ctx, meta.into(), body)
    }
}
