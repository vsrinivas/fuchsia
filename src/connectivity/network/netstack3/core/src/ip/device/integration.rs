// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The integrations for protocols built on top of an IP device.

use alloc::boxed::Box;
use core::{num::NonZeroU8, time::Duration};

use net_types::{
    ip::{AddrSubnet, Ip as _, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
    LinkLocalUnicastAddr, MulticastAddr, SpecifiedAddr, UnicastAddr, Witness as _,
};
use packet::{BufferMut, EmptyBuf, Serializer};
use packet_formats::{
    icmp::{
        ndp::{NeighborSolicitation, RouterSolicitation},
        IcmpMessage, IcmpPacketBuilder, IcmpUnusedCode,
    },
    ip::Ipv6Proto,
};

use crate::{
    context::{CounterContext, FrameContext, InstantContext},
    error::ExistsError,
    ip::{
        self,
        device::{
            self, add_ipv6_addr_subnet,
            dad::{DadHandler, Ipv6DeviceDadContext, Ipv6LayerDadContext},
            del_ipv6_addr_with_reason, get_ipv4_addr_subnet, get_ipv4_device_state,
            get_ipv6_device_state, get_ipv6_hop_limit, is_ipv4_routing_enabled,
            is_ipv6_routing_enabled, iter_ipv4_devices, iter_ipv6_devices,
            route_discovery::{Ipv6RouteDiscoveryState, Ipv6RouteDiscoveryStateContext},
            router_solicitation::{Ipv6DeviceRsContext, Ipv6LayerRsContext},
            send_ip_frame,
            slaac::{
                SlaacAddressEntry, SlaacAddressEntryMut, SlaacConfiguration, SlaacStateContext,
            },
            state::{
                AddrConfig, AddressState, DelIpv6AddrReason, IpDeviceConfiguration, IpDeviceState,
                Ipv4DeviceConfiguration, Ipv6AddressEntry, Ipv6DeviceConfiguration, SlaacConfig,
            },
            IpDeviceIpExt,
        },
        gmp::{
            igmp::{IgmpContext, IgmpGroupState, IgmpPacketMetadata},
            mld::{MldContext, MldFrameMetadata, MldGroupState},
            GmpHandler, MulticastGroupSet,
        },
        send_ipv6_packet_from_device, AddressStatus, IpLayerIpExt, Ipv6PresentAddressStatus,
        SendIpPacketMeta, DEFAULT_TTL,
    },
};

/// The IP packet hop limit for all NDP packets.
///
/// See [RFC 4861 section 4.1], [RFC 4861 section 4.2], [RFC 4861 section 4.2],
/// [RFC 4861 section 4.3], [RFC 4861 section 4.4], and [RFC 4861 section 4.5]
/// for more information.
///
/// [RFC 4861 section 4.1]: https://tools.ietf.org/html/rfc4861#section-4.1
/// [RFC 4861 section 4.2]: https://tools.ietf.org/html/rfc4861#section-4.2
/// [RFC 4861 section 4.3]: https://tools.ietf.org/html/rfc4861#section-4.3
/// [RFC 4861 section 4.4]: https://tools.ietf.org/html/rfc4861#section-4.4
/// [RFC 4861 section 4.5]: https://tools.ietf.org/html/rfc4861#section-4.5
pub(super) const REQUIRED_NDP_IP_PACKET_HOP_LIMIT: u8 = 255;

impl<C: device::Ipv6DeviceContext + GmpHandler<Ipv6> + DadHandler + CounterContext>
    SlaacStateContext for C
{
    fn get_config(&self, device_id: Self::DeviceId) -> SlaacConfiguration {
        let Ipv6DeviceConfiguration {
            dad_transmits: _,
            max_router_solicitations: _,
            slaac_config,
            ip_config: _,
        } = C::get_ip_device_state(self, device_id).config;
        slaac_config
    }

    fn dad_transmits(&self, device_id: Self::DeviceId) -> Option<NonZeroU8> {
        let Ipv6DeviceConfiguration {
            dad_transmits,
            max_router_solicitations: _,
            slaac_config: _,
            ip_config: _,
        } = C::get_ip_device_state(self, device_id).config;

        dad_transmits
    }

    fn retrans_timer(&self, device_id: C::DeviceId) -> Duration {
        C::get_ip_device_state(self, device_id).retrans_timer.get()
    }

    fn get_interface_identifier(&self, device_id: Self::DeviceId) -> [u8; 8] {
        C::get_eui64_iid(self, device_id).unwrap_or_else(Default::default)
    }

    fn iter_slaac_addrs(
        &self,
        device_id: Self::DeviceId,
    ) -> Box<dyn Iterator<Item = SlaacAddressEntry<Self::Instant>> + '_> {
        Box::new(C::get_ip_device_state(self, device_id).ip_state.iter_addrs().cloned().filter_map(
            |Ipv6AddressEntry { addr_sub, state: _, config, deprecated }| match config {
                AddrConfig::Slaac(config) => {
                    Some(SlaacAddressEntry { addr_sub, config, deprecated })
                }
                AddrConfig::Manual => None,
            },
        ))
    }

    fn iter_slaac_addrs_mut(
        &mut self,
        device_id: Self::DeviceId,
    ) -> Box<dyn Iterator<Item = SlaacAddressEntryMut<'_, Self::Instant>> + '_> {
        Box::new(C::get_ip_device_state_mut(self, device_id).ip_state.iter_addrs_mut().filter_map(
            |Ipv6AddressEntry { addr_sub, state: _, config, deprecated }| match config {
                AddrConfig::Slaac(config) => {
                    Some(SlaacAddressEntryMut { addr_sub: *addr_sub, config, deprecated })
                }
                AddrConfig::Manual => None,
            },
        ))
    }

    fn add_slaac_addr_sub(
        &mut self,
        device_id: Self::DeviceId,
        addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
        slaac_config: SlaacConfig<Self::Instant>,
    ) -> Result<(), ExistsError> {
        add_ipv6_addr_subnet(
            self,
            &mut (),
            device_id,
            addr_sub.to_witness(),
            AddrConfig::Slaac(slaac_config),
        )
    }

    fn remove_slaac_addr(&mut self, device_id: Self::DeviceId, addr: &UnicastAddr<Ipv6Addr>) {
        del_ipv6_addr_with_reason(
            self,
            &mut (),
            device_id,
            &addr.into_specified(),
            DelIpv6AddrReason::ManualAction,
        )
        .unwrap()
    }
}

impl<C: device::IpDeviceContext<Ipv6>> Ipv6RouteDiscoveryStateContext for C {
    fn get_discovered_routes_mut(
        &mut self,
        device_id: C::DeviceId,
    ) -> &mut Ipv6RouteDiscoveryState {
        &mut C::get_ip_device_state_mut(self, device_id).route_discovery
    }
}

impl<C: device::BufferIpDeviceContext<Ipv4, EmptyBuf>> IgmpContext for C {
    fn get_ip_addr_subnet(&self, device: C::DeviceId) -> Option<AddrSubnet<Ipv4Addr>> {
        get_ipv4_addr_subnet(self, &mut (), device)
    }

    fn igmp_enabled(&self, device: C::DeviceId) -> bool {
        let Ipv4DeviceConfiguration {
            ip_config: IpDeviceConfiguration { ip_enabled, gmp_enabled },
        } = C::get_ip_device_state(self, device).config;

        ip_enabled && gmp_enabled
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

    fn get_state(
        &self,
        device: C::DeviceId,
    ) -> &MulticastGroupSet<Ipv4Addr, IgmpGroupState<<C as InstantContext>::Instant>> {
        &self.get_ip_device_state(device).ip_state.multicast_groups
    }
}

impl<C: device::BufferIpDeviceContext<Ipv4, EmptyBuf>>
    FrameContext<(), EmptyBuf, IgmpPacketMetadata<C::DeviceId>> for C
{
    fn send_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        _ctx: &mut (),
        meta: IgmpPacketMetadata<C::DeviceId>,
        body: S,
    ) -> Result<(), S> {
        C::send_ip_frame(self, meta.device, meta.dst_ip.into_specified(), body)
    }
}

impl<C: device::BufferIpDeviceContext<Ipv6, EmptyBuf>> MldContext for C {
    fn get_ipv6_link_local_addr(
        &self,
        device: C::DeviceId,
    ) -> Option<LinkLocalUnicastAddr<Ipv6Addr>> {
        get_ipv6_device_state(self, &mut (), device).iter_addrs().find_map(|a| {
            if a.state.is_assigned() {
                LinkLocalUnicastAddr::new(a.addr_sub().addr())
            } else {
                None
            }
        })
    }

    fn mld_enabled(&self, device: C::DeviceId) -> bool {
        let Ipv6DeviceConfiguration {
            dad_transmits: _,
            max_router_solicitations: _,
            slaac_config: _,
            ip_config: IpDeviceConfiguration { ip_enabled, gmp_enabled },
        } = C::get_ip_device_state(self, device).config;

        ip_enabled && gmp_enabled
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

    fn get_state(
        &self,
        device: C::DeviceId,
    ) -> &MulticastGroupSet<Ipv6Addr, MldGroupState<<C as InstantContext>::Instant>> {
        &self.get_ip_device_state(device).ip_state.multicast_groups
    }
}

impl<C: device::Ipv6DeviceContext> Ipv6DeviceDadContext for C {
    fn get_address_state_mut(
        &mut self,
        device_id: C::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    ) -> Option<&mut AddressState> {
        self.get_ip_device_state_mut(device_id)
            .ip_state
            .find_addr_mut(&addr)
            .map(|Ipv6AddressEntry { addr_sub: _, state, config: _, deprecated: _ }| state)
    }

    fn retrans_timer(&self, device_id: C::DeviceId) -> Duration {
        C::get_ip_device_state(self, device_id).retrans_timer.get()
    }

    fn get_link_layer_addr_bytes(&self, device_id: C::DeviceId) -> Option<&[u8]> {
        device::Ipv6DeviceContext::get_link_layer_addr_bytes(self, device_id)
    }
}

fn send_ndp_packet<
    SC: ip::BufferIpDeviceContext<Ipv6, EmptyBuf>,
    C,
    S: Serializer<Buffer = EmptyBuf>,
    M: IcmpMessage<Ipv6, &'static [u8]>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    src_ip: Ipv6Addr,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
    body: S,
    code: M::Code,
    message: M,
) -> Result<(), S> {
    // TODO(https://fxbug.dev/95359): Send through ICMPv6 send path.
    send_ipv6_packet_from_device(
        sync_ctx,
        ctx,
        SendIpPacketMeta {
            device: device_id,
            src_ip: SpecifiedAddr::new(src_ip),
            dst_ip,
            next_hop: dst_ip,
            ttl: NonZeroU8::new(REQUIRED_NDP_IP_PACKET_HOP_LIMIT),
            proto: Ipv6Proto::Icmpv6,
            mtu: None,
        },
        body.encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
            src_ip,
            dst_ip.get(),
            code,
            message,
        )),
    )
    .map_err(|s| s.into_inner())
}

impl<C: ip::BufferIpDeviceContext<Ipv6, EmptyBuf>> Ipv6LayerDadContext for C {
    fn send_dad_packet<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        device_id: Self::DeviceId,
        dst_ip: MulticastAddr<Ipv6Addr>,
        message: NeighborSolicitation,
        body: S,
    ) -> Result<(), S> {
        send_ndp_packet(
            self,
            &mut (),
            device_id,
            Ipv6::UNSPECIFIED_ADDRESS,
            dst_ip.into_specified(),
            body,
            IcmpUnusedCode,
            message,
        )
    }
}

impl<C: device::Ipv6DeviceContext> Ipv6DeviceRsContext for C {
    fn get_max_router_solicitations(&self, device_id: C::DeviceId) -> Option<NonZeroU8> {
        self.get_ip_device_state(device_id).config.max_router_solicitations
    }

    fn get_router_soliciations_remaining_mut(
        &mut self,
        device_id: C::DeviceId,
    ) -> &mut Option<NonZeroU8> {
        &mut self.get_ip_device_state_mut(device_id).router_soliciations_remaining
    }

    fn get_link_layer_addr_bytes(&self, device_id: C::DeviceId) -> Option<&[u8]> {
        device::Ipv6DeviceContext::get_link_layer_addr_bytes(self, device_id)
    }
}

impl<C: ip::BufferIpDeviceContext<Ipv6, EmptyBuf> + device::Ipv6DeviceContext> Ipv6LayerRsContext
    for C
{
    fn send_rs_packet<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        device_id: Self::DeviceId,
        message: RouterSolicitation,
        body: S,
    ) -> Result<(), S> {
        let dst_ip = Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS.into_specified();
        send_ndp_packet(
            self,
            &mut (),
            device_id,
            crate::ip::socket::ipv6_source_address_selection::select_ipv6_source_address(
                dst_ip,
                device_id,
                get_ipv6_device_state(self, &mut (), device_id)
                    .iter_addrs()
                    .map(move |a| (a, device_id)),
            )
            .map_or(Ipv6::UNSPECIFIED_ADDRESS, |a| a.get()),
            dst_ip,
            body,
            IcmpUnusedCode,
            message,
        )
    }
}

impl<C: device::IpDeviceContext<Ipv4>> ip::IpDeviceContext<Ipv4> for C {
    fn address_status(
        &self,
        device_id: Option<C::DeviceId>,
        dst_ip: SpecifiedAddr<Ipv4Addr>,
    ) -> AddressStatus<()> {
        let get_status = |dev_state: &IpDeviceState<C::Instant, Ipv4>| {
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
        };

        if let Some(device_id) = device_id {
            get_status(get_ipv4_device_state(self, &mut (), device_id))
        } else {
            iter_ipv4_devices(self, &mut ())
                .find_map(|(_device_id, state)| match get_status(state) {
                    AddressStatus::Present(()) => Some(AddressStatus::Present(())),
                    AddressStatus::Unassigned => None,
                })
                .unwrap_or(AddressStatus::Unassigned)
        }
    }

    fn is_device_routing_enabled(&self, device_id: C::DeviceId) -> bool {
        is_ipv4_routing_enabled(self, &mut (), device_id)
    }

    fn get_hop_limit(&self, _device_id: C::DeviceId) -> NonZeroU8 {
        DEFAULT_TTL
    }
}

impl<C: device::IpDeviceContext<Ipv6>> ip::IpDeviceContext<Ipv6> for C {
    fn address_status(
        &self,
        device_id: Option<C::DeviceId>,
        addr: SpecifiedAddr<Ipv6Addr>,
    ) -> AddressStatus<Ipv6PresentAddressStatus> {
        let get_status = |dev_state: &IpDeviceState<C::Instant, Ipv6>| {
            if MulticastAddr::new(addr.get())
                .map_or(false, |addr| dev_state.multicast_groups.contains(&addr))
            {
                AddressStatus::Present(Ipv6PresentAddressStatus::Multicast)
            } else {
                dev_state.find_addr(&addr).map(|addr| addr.state).map_or(
                    AddressStatus::Unassigned,
                    |state| match state {
                        AddressState::Assigned => {
                            AddressStatus::Present(Ipv6PresentAddressStatus::UnicastAssigned)
                        }
                        AddressState::Tentative { dad_transmits_remaining: _ } => {
                            AddressStatus::Present(Ipv6PresentAddressStatus::UnicastTentative)
                        }
                    },
                )
            }
        };

        if let Some(device_id) = device_id {
            get_status(get_ipv6_device_state(self, &mut (), device_id))
        } else {
            iter_ipv6_devices(self, &mut ())
                .find_map(|(_device_id, state)| match get_status(state) {
                    AddressStatus::Present(a) => Some(AddressStatus::Present(a)),
                    AddressStatus::Unassigned => None,
                })
                .unwrap_or(AddressStatus::Unassigned)
        }
    }

    fn is_device_routing_enabled(&self, device_id: C::DeviceId) -> bool {
        is_ipv6_routing_enabled(self, &mut (), device_id)
    }

    fn get_hop_limit(&self, device_id: C::DeviceId) -> NonZeroU8 {
        get_ipv6_hop_limit(self, &mut (), device_id)
    }
}

impl<C: device::BufferIpDeviceContext<Ipv6, EmptyBuf>>
    FrameContext<(), EmptyBuf, MldFrameMetadata<C::DeviceId>> for C
{
    fn send_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        _ctx: &mut (),
        meta: MldFrameMetadata<C::DeviceId>,
        body: S,
    ) -> Result<(), S> {
        C::send_ip_frame(self, meta.device, meta.dst_ip.into_specified(), body)
    }
}

impl<
        I: IpLayerIpExt + IpDeviceIpExt<C::Instant, C::DeviceId>,
        B: BufferMut,
        C: device::BufferIpDeviceContext<I, B> + ip::IpDeviceContext<I>,
    > ip::BufferIpDeviceContext<I, B> for C
{
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        device_id: C::DeviceId,
        next_hop: SpecifiedAddr<I::Addr>,
        packet: S,
    ) -> Result<(), S> {
        send_ip_frame(self, &mut (), device_id, next_hop, packet)
    }
}
