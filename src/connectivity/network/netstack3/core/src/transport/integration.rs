// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use net_types::{
    ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    MulticastAddr,
};

use crate::{
    transport::udp::{UdpState, UdpStateContext},
    DeviceId, NonSyncContext, SyncCtx,
};

impl<C: NonSyncContext> UdpStateContext<Ipv4, C> for SyncCtx<C> {
    fn join_multicast_group(
        &mut self,
        ctx: &mut C,
        device: DeviceId,
        addr: MulticastAddr<Ipv4Addr>,
    ) {
        crate::ip::device::join_ip_multicast::<Ipv4, _, _>(self, ctx, device, addr);
    }

    fn leave_multicast_group(
        &mut self,
        ctx: &mut C,
        device: DeviceId,
        addr: MulticastAddr<Ipv4Addr>,
    ) {
        crate::ip::device::leave_ip_multicast::<Ipv4, _, _>(self, ctx, device, addr);
    }

    fn with_state<O, F: FnOnce(&UdpState<Ipv4, DeviceId>) -> O>(&self, cb: F) -> O {
        cb(&self.state.transport.udpv4)
    }

    fn with_state_mut<O, F: FnOnce(&mut UdpState<Ipv4, DeviceId>) -> O>(&mut self, cb: F) -> O {
        cb(&mut self.state.transport.udpv4)
    }
}

impl<C: NonSyncContext> UdpStateContext<Ipv6, C> for SyncCtx<C> {
    fn join_multicast_group(
        &mut self,
        ctx: &mut C,
        device: DeviceId,
        addr: MulticastAddr<Ipv6Addr>,
    ) {
        crate::ip::device::join_ip_multicast::<Ipv6, _, _>(self, ctx, device, addr);
    }

    fn leave_multicast_group(
        &mut self,
        ctx: &mut C,
        device: DeviceId,
        addr: MulticastAddr<Ipv6Addr>,
    ) {
        crate::ip::device::leave_ip_multicast::<Ipv6, _, _>(self, ctx, device, addr);
    }

    fn with_state<O, F: FnOnce(&UdpState<Ipv6, DeviceId>) -> O>(&self, cb: F) -> O {
        cb(&self.state.transport.udpv6)
    }

    fn with_state_mut<O, F: FnOnce(&mut UdpState<Ipv6, DeviceId>) -> O>(&mut self, cb: F) -> O {
        cb(&mut self.state.transport.udpv6)
    }
}
