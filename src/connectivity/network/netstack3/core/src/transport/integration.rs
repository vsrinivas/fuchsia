// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use net_types::MulticastAddr;

use crate::{
    context::{NonTestCtxMarker, StateContext},
    ip::{
        device::{IpDeviceContext, IpDeviceIpExt, IpDeviceNonSyncContext},
        gmp::GmpHandler,
    },
    transport::udp::{UdpState, UdpStateContext, UdpStateNonSyncContext},
    IpExt, TransportIpContext,
};

impl<
        I: IpExt + IpDeviceIpExt<C::Instant, SC::DeviceId>,
        C: UdpStateNonSyncContext<I> + IpDeviceNonSyncContext<I, SC::DeviceId>,
        SC: TransportIpContext<I, C>
            + StateContext<C, UdpState<I, SC::DeviceId>>
            + IpDeviceContext<I, C>
            + GmpHandler<I, C>
            + NonTestCtxMarker,
    > UdpStateContext<I, C> for SC
{
    fn join_multicast_group(
        &mut self,
        ctx: &mut C,
        device: Self::DeviceId,
        addr: MulticastAddr<I::Addr>,
    ) {
        crate::ip::device::join_ip_multicast(self, ctx, device, addr);
    }

    fn leave_multicast_group(
        &mut self,
        ctx: &mut C,
        device: Self::DeviceId,
        addr: MulticastAddr<I::Addr>,
    ) {
        crate::ip::device::leave_ip_multicast(self, ctx, device, addr);
    }
}
