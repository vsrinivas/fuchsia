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
    context::{NonTestCtxMarker, StateContext},
    ip::{
        self,
        icmp::{Icmpv4State, Icmpv6State},
        path_mtu::{PmtuCache, PmtuStateContext},
        reassembly::FragmentStateContext,
        send_ipv4_packet_from_device, send_ipv6_packet_from_device,
        socket::{BufferIpSocketContext, IpSock, IpSocketContext},
        IpDeviceIdContext, IpPacketFragmentCache, IpStateContext, SendIpPacketMeta,
    },
    Instant,
};

// Note: we can't provide a single impl for `FragmentStateContext` which is
// generic over all `I: IpLayerStateIpExt<SC::Instant, SC::DeviceId>` because
// we have a `DummyCtx` type in the reassembly tests module which implements
// `FragmentStateContext` resulting in a conflicting implementations error.
// Manually implementing `FragmentStateContext` for each `I: Ip` we support
// seems to workaround this issue.
//
// See https://doc.rust-lang.org/error-index.html#E0119 for more details.

impl<I: Instant, SC: IpStateContext<Ipv4, I>> FragmentStateContext<Ipv4, I> for SC {
    fn get_state_mut(&mut self) -> &mut IpPacketFragmentCache<Ipv4, I> {
        &mut self.get_ip_layer_state_mut().as_mut().fragment_cache
    }
}

impl<I: Instant, SC: IpStateContext<Ipv6, I>> FragmentStateContext<Ipv6, I> for SC {
    fn get_state_mut(&mut self) -> &mut IpPacketFragmentCache<Ipv6, I> {
        &mut self.get_ip_layer_state_mut().as_mut().fragment_cache
    }
}

// We can't provide a single impl for `PmtuStateContext` which is generic over
// `Ip` for the same reason as `FragmentStateContext` above.

impl<I: Instant, SC: IpStateContext<Ipv4, I>> PmtuStateContext<Ipv4, I> for SC {
    fn get_state_mut(&mut self) -> &mut PmtuCache<Ipv4, I> {
        &mut self.get_ip_layer_state_mut().as_mut().pmtu_cache
    }
}

impl<I: Instant, SC: IpStateContext<Ipv6, I>> PmtuStateContext<Ipv6, I> for SC {
    fn get_state_mut(&mut self) -> &mut PmtuCache<Ipv6, I> {
        &mut self.get_ip_layer_state_mut().as_mut().pmtu_cache
    }
}

impl<C, I: Instant, SC: IpStateContext<Ipv4, I>>
    StateContext<C, Icmpv4State<I, IpSock<Ipv4, SC::DeviceId>>> for SC
{
    fn get_state_with(&self, _id: ()) -> &Icmpv4State<I, IpSock<Ipv4, SC::DeviceId>> {
        &self.get_ip_layer_state().icmp
    }

    fn get_state_mut_with(&mut self, _id: ()) -> &mut Icmpv4State<I, IpSock<Ipv4, SC::DeviceId>> {
        &mut self.get_ip_layer_state_mut().icmp
    }
}

impl<C, I: Instant, SC: IpStateContext<Ipv6, I>>
    StateContext<C, Icmpv6State<I, IpSock<Ipv6, SC::DeviceId>>> for SC
{
    fn get_state_with(&self, _id: ()) -> &Icmpv6State<I, IpSock<Ipv6, SC::DeviceId>> {
        &self.get_ip_layer_state().icmp
    }

    fn get_state_mut_with(&mut self, _id: ()) -> &mut Icmpv6State<I, IpSock<Ipv6, SC::DeviceId>> {
        &mut self.get_ip_layer_state_mut().icmp
    }
}

impl<
        B: BufferMut,
        C,
        SC: ip::BufferIpDeviceContext<Ipv4, C, B>
            + IpStateContext<Ipv4, SC::Instant>
            + IpSocketContext<Ipv4, C>
            + NonTestCtxMarker,
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
        SC: ip::BufferIpDeviceContext<Ipv6, C, B>
            + IpStateContext<Ipv6, SC::Instant>
            + IpSocketContext<Ipv6, C>
            + NonTestCtxMarker,
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
