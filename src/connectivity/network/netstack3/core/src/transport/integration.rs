// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use net_types::{
    ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    MulticastAddr,
};

use crate::{
    transport::udp::{UdpSockets, UdpStateContext},
    DeviceId, NonSyncContext, SyncCtx,
};

impl<C: NonSyncContext> UdpStateContext<Ipv4, C> for &'_ SyncCtx<C> {
    type IpSocketsCtx = Self;

    fn join_multicast_group(
        &mut self,
        ctx: &mut C,
        device: &DeviceId,
        addr: MulticastAddr<Ipv4Addr>,
    ) {
        crate::ip::device::join_ip_multicast::<Ipv4, _, _>(self, ctx, device, addr);
    }

    fn leave_multicast_group(
        &mut self,
        ctx: &mut C,
        device: &DeviceId,
        addr: MulticastAddr<Ipv4Addr>,
    ) {
        crate::ip::device::leave_ip_multicast::<Ipv4, _, _>(self, ctx, device, addr);
    }

    fn with_sockets<O, F: FnOnce(&Self::IpSocketsCtx, &UdpSockets<Ipv4, DeviceId>) -> O>(
        &self,
        cb: F,
    ) -> O {
        cb(self, &self.state.transport.udpv4.sockets.read())
    }

    fn with_sockets_mut<
        O,
        F: FnOnce(&mut Self::IpSocketsCtx, &mut UdpSockets<Ipv4, DeviceId>) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O {
        cb(self, &mut self.state.transport.udpv4.sockets.write())
    }

    fn should_send_port_unreachable(&self) -> bool {
        self.state.transport.udpv4.send_port_unreachable
    }
}

impl<C: NonSyncContext> UdpStateContext<Ipv6, C> for &'_ SyncCtx<C> {
    type IpSocketsCtx = Self;

    fn join_multicast_group(
        &mut self,
        ctx: &mut C,
        device: &DeviceId,
        addr: MulticastAddr<Ipv6Addr>,
    ) {
        crate::ip::device::join_ip_multicast::<Ipv6, _, _>(self, ctx, device, addr);
    }

    fn leave_multicast_group(
        &mut self,
        ctx: &mut C,
        device: &DeviceId,
        addr: MulticastAddr<Ipv6Addr>,
    ) {
        crate::ip::device::leave_ip_multicast::<Ipv6, _, _>(self, ctx, device, addr);
    }

    fn with_sockets<O, F: FnOnce(&Self::IpSocketsCtx, &UdpSockets<Ipv6, DeviceId>) -> O>(
        &self,
        cb: F,
    ) -> O {
        cb(self, &self.state.transport.udpv6.sockets.read())
    }

    fn with_sockets_mut<
        O,
        F: FnOnce(&mut Self::IpSocketsCtx, &mut UdpSockets<Ipv6, DeviceId>) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O {
        cb(self, &mut self.state.transport.udpv6.sockets.write())
    }

    fn should_send_port_unreachable(&self) -> bool {
        self.state.transport.udpv6.send_port_unreachable
    }
}
