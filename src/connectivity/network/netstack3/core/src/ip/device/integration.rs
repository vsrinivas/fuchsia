// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The integrations for protocols built on top of an IP device.

use crate::{
    context::{FrameContext, InstantContext},
    ip::{
        device::{get_ip_addr_subnet, get_ipv6_device_state, BufferIpDeviceContext},
        gmp::{
            igmp::{IgmpContext, IgmpGroupState, IgmpPacketMetadata},
            mld::{MldContext, MldFrameMetadata, MldGroupState},
            MulticastGroupSet,
        },
    },
};
use net_types::{
    ip::{AddrSubnet, Ipv4Addr, Ipv6Addr},
    LinkLocalUnicastAddr,
};
use packet::{EmptyBuf, Serializer};

impl<C: BufferIpDeviceContext<EmptyBuf>> IgmpContext for C {
    fn get_ip_addr_subnet(&self, device: C::DeviceId) -> Option<AddrSubnet<Ipv4Addr>> {
        get_ip_addr_subnet(self, device)
    }

    fn igmp_enabled(&self, device: C::DeviceId) -> bool {
        C::get_ip_device_state(self, device).ipv4.config.ip_config.gmp_enabled
    }

    fn get_state_mut_and_rng(
        &mut self,
        device: C::DeviceId,
    ) -> (
        &mut MulticastGroupSet<Ipv4Addr, IgmpGroupState<<C as InstantContext>::Instant>>,
        &mut C::Rng,
    ) {
        let (state, rng) = self.get_ip_device_state_mut_and_rng(device);
        (&mut state.ipv4.ip_state.multicast_groups, rng)
    }
}

impl<C: BufferIpDeviceContext<EmptyBuf>> FrameContext<EmptyBuf, IgmpPacketMetadata<C::DeviceId>>
    for C
{
    fn send_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        meta: IgmpPacketMetadata<C::DeviceId>,
        body: S,
    ) -> Result<(), S> {
        C::send_ip_frame(self, meta.device, meta.dst_ip.into_specified(), body)
    }
}

impl<C: BufferIpDeviceContext<EmptyBuf>> MldContext for C {
    fn get_ipv6_link_local_addr(
        &self,
        device: C::DeviceId,
    ) -> Option<LinkLocalUnicastAddr<Ipv6Addr>> {
        get_ipv6_device_state(self, device).iter_addrs().find_map(|a| {
            if a.state.is_assigned() {
                LinkLocalUnicastAddr::new(a.addr_sub().addr())
            } else {
                None
            }
        })
    }

    fn mld_enabled(&self, device: C::DeviceId) -> bool {
        C::get_ip_device_state(self, device).ipv6.config.ip_config.gmp_enabled
    }

    fn get_state_mut_and_rng(
        &mut self,
        device: C::DeviceId,
    ) -> (
        &mut MulticastGroupSet<Ipv6Addr, MldGroupState<<C as InstantContext>::Instant>>,
        &mut C::Rng,
    ) {
        let (state, rng) = self.get_ip_device_state_mut_and_rng(device);
        (&mut state.ipv6.ip_state.multicast_groups, rng)
    }
}

impl<C: BufferIpDeviceContext<EmptyBuf>> FrameContext<EmptyBuf, MldFrameMetadata<C::DeviceId>>
    for C
{
    fn send_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        meta: MldFrameMetadata<C::DeviceId>,
        body: S,
    ) -> Result<(), S> {
        C::send_ip_frame(self, meta.device, meta.dst_ip.into_specified(), body)
    }
}
