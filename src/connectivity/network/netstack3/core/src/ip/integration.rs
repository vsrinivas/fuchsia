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
    context::NonTestCtxMarker,
    ip::{
        self,
        path_mtu::{PmtuCache, PmtuStateContext},
        reassembly::FragmentStateContext,
        send_ipv4_packet_from_device, send_ipv6_packet_from_device,
        socket::{BufferIpSocketContext, IpSocketContext, IpSocketNonSyncContext},
        IpDeviceIdContext, IpLayerIpExt, IpLayerNonSyncContext, IpPacketFragmentCache,
        IpStateContext, SendIpPacketMeta,
    },
};

impl<
        Instant: crate::Instant,
        I: IpLayerIpExt,
        SC: IpStateContext<I, Instant> + NonTestCtxMarker,
    > FragmentStateContext<I, Instant> for SC
{
    fn with_state_mut<O, F: FnOnce(&mut IpPacketFragmentCache<I, Instant>) -> O>(
        &mut self,
        cb: F,
    ) -> O {
        self.with_ip_layer_state(|state| cb(&mut state.as_ref().fragment_cache.lock()))
    }
}

impl<
        Instant: crate::Instant,
        I: IpLayerIpExt,
        SC: IpStateContext<I, Instant> + NonTestCtxMarker,
    > PmtuStateContext<I, Instant> for SC
{
    fn with_state_mut<F: FnOnce(&mut PmtuCache<I, Instant>)>(&mut self, cb: F) {
        self.with_ip_layer_state(|state| cb(&mut state.as_ref().pmtu_cache.lock()))
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
