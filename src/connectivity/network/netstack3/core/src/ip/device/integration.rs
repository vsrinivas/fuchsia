// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The integrations for protocols built on top of an IP device.

use net_types::{
    ip::{AddrSubnet, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    LinkLocalUnicastAddr, MulticastAddr, SpecifiedAddr, Witness as _,
};
use packet::{BufferMut, EmptyBuf, Serializer};

use crate::{
    context::{FrameContext, InstantContext},
    ip::{
        self,
        device::{
            self, get_ipv4_addr_subnet, get_ipv4_device_state, get_ipv6_device_state,
            is_ipv4_routing_enabled, is_ipv6_routing_enabled, send_ip_frame, state::AddressState,
            BufferIpDeviceContext, IpDeviceIpExt,
        },
        gmp::{
            igmp::{IgmpContext, IgmpGroupState, IgmpPacketMetadata},
            mld::{MldContext, MldFrameMetadata, MldGroupState},
            MulticastGroupSet,
        },
        AddressStatus, IpLayerIpExt, Ipv6PresentAddressStatus,
    },
};

impl<C: BufferIpDeviceContext<Ipv4, EmptyBuf>> IgmpContext for C {
    fn get_ip_addr_subnet(&self, device: C::DeviceId) -> Option<AddrSubnet<Ipv4Addr>> {
        get_ipv4_addr_subnet(self, device)
    }

    fn igmp_enabled(&self, device: C::DeviceId) -> bool {
        C::get_ip_device_state(self, device).config.ip_config.gmp_enabled
    }

    fn get_state_mut_and_rng(
        &mut self,
        device: C::DeviceId,
    ) -> (
        &mut MulticastGroupSet<Ipv4Addr, IgmpGroupState<<C as InstantContext>::Instant>>,
        &mut C::Rng,
    ) {
        let (state, rng) = self.get_ip_device_state_mut_and_rng(device);
        (&mut state.ip_state.multicast_groups, rng)
    }
}

impl<C: BufferIpDeviceContext<Ipv4, EmptyBuf>>
    FrameContext<EmptyBuf, IgmpPacketMetadata<C::DeviceId>> for C
{
    fn send_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        meta: IgmpPacketMetadata<C::DeviceId>,
        body: S,
    ) -> Result<(), S> {
        C::send_ip_frame(self, meta.device, meta.dst_ip.into_specified(), body)
    }
}

impl<C: BufferIpDeviceContext<Ipv6, EmptyBuf>> MldContext for C {
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
        C::get_ip_device_state(self, device).config.ip_config.gmp_enabled
    }

    fn get_state_mut_and_rng(
        &mut self,
        device: C::DeviceId,
    ) -> (
        &mut MulticastGroupSet<Ipv6Addr, MldGroupState<<C as InstantContext>::Instant>>,
        &mut C::Rng,
    ) {
        let (state, rng) = self.get_ip_device_state_mut_and_rng(device);
        (&mut state.ip_state.multicast_groups, rng)
    }
}

impl<C: device::IpDeviceContext<Ipv4>> ip::IpDeviceContext<Ipv4> for C {
    fn address_status(
        &self,
        device_id: C::DeviceId,
        dst_ip: SpecifiedAddr<Ipv4Addr>,
    ) -> AddressStatus<()> {
        let dev_state = get_ipv4_device_state(self, device_id);
        if dev_state
            .iter_addrs()
            .map(|addr| addr.addr_subnet())
            .any(|(addr, subnet)| addr == dst_ip || dst_ip.get() == subnet.broadcast())
            || dst_ip.is_limited_broadcast()
            || MulticastAddr::from_witness(dst_ip)
                .map_or(false, |addr| dev_state.multicast_groups.contains(&addr))
        {
            AddressStatus::Present(())
        } else {
            AddressStatus::Unassigned
        }
    }

    fn is_device_routing_enabled(&self, device_id: C::DeviceId) -> bool {
        is_ipv4_routing_enabled(self, device_id)
    }
}

impl<C: device::IpDeviceContext<Ipv6>> ip::IpDeviceContext<Ipv6> for C {
    fn address_status(
        &self,
        device_id: C::DeviceId,
        addr: SpecifiedAddr<Ipv6Addr>,
    ) -> AddressStatus<Ipv6PresentAddressStatus> {
        let dev_state = get_ipv6_device_state(&*self, device_id);
        if MulticastAddr::new(addr.get())
            .map_or(false, |addr| dev_state.multicast_groups.contains(&addr))
        {
            AddressStatus::Present(Ipv6PresentAddressStatus::Multicast)
        } else {
            dev_state.find_addr(&addr).map(|addr| addr.state).map_or(
                AddressStatus::Unassigned,
                |state| match state {
                    AddressState::Assigned | AddressState::Deprecated => {
                        AddressStatus::Present(Ipv6PresentAddressStatus::UnicastAssigned)
                    }
                    AddressState::Tentative { dad_transmits_remaining: _ } => {
                        AddressStatus::Present(Ipv6PresentAddressStatus::UnicastTentative)
                    }
                },
            )
        }
    }

    fn is_device_routing_enabled(&self, device_id: C::DeviceId) -> bool {
        is_ipv6_routing_enabled(self, device_id)
    }
}

impl<C: BufferIpDeviceContext<Ipv6, EmptyBuf>> FrameContext<EmptyBuf, MldFrameMetadata<C::DeviceId>>
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

impl<
        I: IpLayerIpExt + IpDeviceIpExt<C::Instant, C::DeviceId>,
        B: BufferMut,
        C: BufferIpDeviceContext<I, B>,
    > ip::BufferIpDeviceContext<I, B> for C
{
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device_id: C::DeviceId,
        next_hop: SpecifiedAddr<I::Addr>,
        packet: S,
    ) -> Result<(), S> {
        send_ip_frame(self, device_id, next_hop, packet)
    }
}
