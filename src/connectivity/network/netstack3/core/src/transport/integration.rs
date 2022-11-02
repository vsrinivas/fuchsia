// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use net_types::ip::{Ipv4, Ipv6};

use crate::{
    transport::udp::{UdpSockets, UdpStateContext},
    DeviceId, NonSyncContext, SyncCtx,
};

impl<C: NonSyncContext> UdpStateContext<Ipv4, C> for &'_ SyncCtx<C> {
    type IpSocketsCtx = Self;

    fn with_sockets<
        O,
        F: FnOnce(&Self::IpSocketsCtx, &UdpSockets<Ipv4, DeviceId<C::Instant>>) -> O,
    >(
        &self,
        cb: F,
    ) -> O {
        cb(self, &self.state.transport.udpv4.sockets.read())
    }

    fn with_sockets_mut<
        O,
        F: FnOnce(&mut Self::IpSocketsCtx, &mut UdpSockets<Ipv4, DeviceId<C::Instant>>) -> O,
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

    fn with_sockets<
        O,
        F: FnOnce(&Self::IpSocketsCtx, &UdpSockets<Ipv6, DeviceId<C::Instant>>) -> O,
    >(
        &self,
        cb: F,
    ) -> O {
        cb(self, &self.state.transport.udpv6.sockets.read())
    }

    fn with_sockets_mut<
        O,
        F: FnOnce(&mut Self::IpSocketsCtx, &mut UdpSockets<Ipv6, DeviceId<C::Instant>>) -> O,
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
