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
        socket::{BufferIpSocketContext, IpSock, IpSocketContext, IpSocketNonSyncContext},
        IpDeviceIdContext, IpLayerNonSyncContext, IpLayerStateIpExt, IpPacketFragmentCache,
        IpStateContext, SendIpPacketMeta,
    },
    Instant,
};

impl<
        Instant: crate::Instant,
        I: IpLayerStateIpExt<Instant, SC::DeviceId>,
        SC: IpStateContext<I, Instant> + NonTestCtxMarker,
    > FragmentStateContext<I, Instant> for SC
{
    fn with_state_mut<O, F: FnOnce(&mut IpPacketFragmentCache<I, Instant>) -> O>(
        &mut self,
        cb: F,
    ) -> O {
        cb(&mut self.get_ip_layer_state_mut().as_mut().fragment_cache)
    }
}

impl<
        Instant: crate::Instant,
        I: IpLayerStateIpExt<Instant, SC::DeviceId>,
        SC: IpStateContext<I, Instant> + NonTestCtxMarker,
    > PmtuStateContext<I, Instant> for SC
{
    fn with_state_mut<F: FnOnce(&mut PmtuCache<I, Instant>)>(&mut self, cb: F) {
        cb(&mut self.get_ip_layer_state_mut().as_mut().pmtu_cache)
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
        C: IpSocketNonSyncContext
            + IpLayerNonSyncContext<Ipv4, <SC as IpDeviceIdContext<Ipv4>>::DeviceId>,
        SC: ip::BufferIpDeviceContext<Ipv4, C, B>
            + IpStateContext<Ipv4, C::Instant>
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
        C: IpSocketNonSyncContext
            + IpLayerNonSyncContext<Ipv6, <SC as IpDeviceIdContext<Ipv6>>::DeviceId>,
        SC: ip::BufferIpDeviceContext<Ipv6, C, B>
            + IpStateContext<Ipv6, C::Instant>
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
