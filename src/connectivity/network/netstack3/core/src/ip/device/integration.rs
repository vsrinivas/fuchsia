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
    context::FrameContext,
    error::ExistsError,
    ip::{
        self,
        device::{
            self, add_ipv6_addr_subnet,
            dad::{DadHandler, Ipv6DeviceDadContext, Ipv6LayerDadContext},
            del_ipv6_addr_with_reason, get_ipv4_addr_subnet, get_ipv4_device_state,
            get_ipv6_device_state, get_ipv6_hop_limit, is_ip_device_enabled,
            is_ipv4_routing_enabled, is_ipv6_routing_enabled,
            route_discovery::{Ipv6RouteDiscoveryState, Ipv6RouteDiscoveryStateContext},
            router_solicitation::{Ipv6DeviceRsContext, Ipv6LayerRsContext},
            send_ip_frame,
            slaac::{
                SlaacAddressEntry, SlaacAddressEntryMut, SlaacConfiguration, SlaacStateContext,
            },
            state::{
                AddrConfig, AddressState, DelIpv6AddrReason, IpDeviceConfiguration,
                Ipv4DeviceConfiguration, Ipv6AddressEntry, Ipv6DeviceConfiguration, SlaacConfig,
            },
            IpDeviceIpExt, IpDeviceNonSyncContext,
        },
        gmp::{
            igmp::{IgmpContext, IgmpGroupState, IgmpPacketMetadata},
            mld::{MldContext, MldFrameMetadata, MldGroupState},
            GmpHandler, MulticastGroupSet,
        },
        send_ipv6_packet_from_device, AddressStatus, IpLayerIpExt, IpLayerNonSyncContext,
        Ipv4PresentAddressStatus, Ipv6PresentAddressStatus, SendIpPacketMeta, DEFAULT_TTL,
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

impl<
        C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
        SC: device::Ipv6DeviceContext<C> + GmpHandler<Ipv6, C> + DadHandler<C>,
    > SlaacStateContext<C> for SC
{
    fn get_config(&self, device_id: Self::DeviceId) -> SlaacConfiguration {
        let Ipv6DeviceConfiguration {
            dad_transmits: _,
            max_router_solicitations: _,
            slaac_config,
            ip_config: _,
        } = SC::get_ip_device_state(self, device_id).config;
        slaac_config
    }

    fn dad_transmits(&self, device_id: Self::DeviceId) -> Option<NonZeroU8> {
        let Ipv6DeviceConfiguration {
            dad_transmits,
            max_router_solicitations: _,
            slaac_config: _,
            ip_config: _,
        } = SC::get_ip_device_state(self, device_id).config;

        dad_transmits
    }

    fn retrans_timer(&self, device_id: SC::DeviceId) -> Duration {
        SC::get_ip_device_state(self, device_id).retrans_timer.get()
    }

    fn get_interface_identifier(&self, device_id: Self::DeviceId) -> [u8; 8] {
        SC::get_eui64_iid(self, device_id).unwrap_or_else(Default::default)
    }

    fn iter_slaac_addrs(
        &self,

        device_id: Self::DeviceId,
    ) -> Box<dyn Iterator<Item = SlaacAddressEntry<C::Instant>> + '_> {
        Box::new(
            SC::get_ip_device_state(self, device_id).ip_state.iter_addrs().cloned().filter_map(
                |Ipv6AddressEntry { addr_sub, state: _, config, deprecated }| match config {
                    AddrConfig::Slaac(config) => {
                        Some(SlaacAddressEntry { addr_sub, config, deprecated })
                    }
                    AddrConfig::Manual => None,
                },
            ),
        )
    }

    fn iter_slaac_addrs_mut(
        &mut self,

        device_id: Self::DeviceId,
    ) -> Box<dyn Iterator<Item = SlaacAddressEntryMut<'_, C::Instant>> + '_> {
        Box::new(SC::get_ip_device_state_mut(self, device_id).ip_state.iter_addrs_mut().filter_map(
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
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
        slaac_config: SlaacConfig<C::Instant>,
    ) -> Result<(), ExistsError> {
        add_ipv6_addr_subnet(
            self,
            ctx,
            device_id,
            addr_sub.to_witness(),
            AddrConfig::Slaac(slaac_config),
        )
    }

    fn remove_slaac_addr(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr: &UnicastAddr<Ipv6Addr>,
    ) {
        del_ipv6_addr_with_reason(
            self,
            ctx,
            device_id,
            &addr.into_specified(),
            DelIpv6AddrReason::ManualAction,
        )
        .unwrap()
    }
}

impl<C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>, SC: device::IpDeviceContext<Ipv6, C>>
    Ipv6RouteDiscoveryStateContext<C> for SC
{
    fn with_discovered_routes_mut<F: FnOnce(&mut Ipv6RouteDiscoveryState)>(
        &mut self,
        device_id: SC::DeviceId,
        cb: F,
    ) {
        cb(&mut SC::get_ip_device_state_mut(self, device_id).route_discovery)
    }
}

impl<
        C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
        SC: device::BufferIpDeviceContext<Ipv4, C, EmptyBuf>,
    > IgmpContext<C> for SC
{
    fn get_ip_addr_subnet(&self, device: SC::DeviceId) -> Option<AddrSubnet<Ipv4Addr>> {
        get_ipv4_addr_subnet(self, device)
    }

    fn igmp_enabled(&self, device: SC::DeviceId) -> bool {
        let Ipv4DeviceConfiguration {
            ip_config: IpDeviceConfiguration { ip_enabled, gmp_enabled },
        } = SC::get_ip_device_state(self, device).config;

        ip_enabled && gmp_enabled
    }

    fn get_state_mut(
        &mut self,
        device: SC::DeviceId,
    ) -> &mut MulticastGroupSet<Ipv4Addr, IgmpGroupState<C::Instant>> {
        &mut self.get_ip_device_state_mut(device).ip_state.multicast_groups
    }

    fn get_state(
        &self,
        device: SC::DeviceId,
    ) -> &MulticastGroupSet<Ipv4Addr, IgmpGroupState<C::Instant>> {
        &self.get_ip_device_state(device).ip_state.multicast_groups
    }
}

impl<
        C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>,
        SC: device::BufferIpDeviceContext<Ipv4, C, EmptyBuf>,
    > FrameContext<C, EmptyBuf, IgmpPacketMetadata<SC::DeviceId>> for SC
{
    fn send_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        ctx: &mut C,
        meta: IgmpPacketMetadata<SC::DeviceId>,
        body: S,
    ) -> Result<(), S> {
        SC::send_ip_frame(self, ctx, meta.device, meta.dst_ip.into_specified(), body)
    }
}

impl<
        C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
        SC: device::BufferIpDeviceContext<Ipv6, C, EmptyBuf>,
    > MldContext<C> for SC
{
    fn get_ipv6_link_local_addr(
        &self,
        device: SC::DeviceId,
    ) -> Option<LinkLocalUnicastAddr<Ipv6Addr>> {
        get_ipv6_device_state(self, device).iter_addrs().find_map(|a| {
            if a.state.is_assigned() {
                LinkLocalUnicastAddr::new(a.addr_sub().addr())
            } else {
                None
            }
        })
    }

    fn mld_enabled(&self, device: SC::DeviceId) -> bool {
        let Ipv6DeviceConfiguration {
            dad_transmits: _,
            max_router_solicitations: _,
            slaac_config: _,
            ip_config: IpDeviceConfiguration { ip_enabled, gmp_enabled },
        } = SC::get_ip_device_state(self, device).config;

        ip_enabled && gmp_enabled
    }

    fn get_state_mut(
        &mut self,
        device: SC::DeviceId,
    ) -> &mut MulticastGroupSet<Ipv6Addr, MldGroupState<C::Instant>> {
        &mut self.get_ip_device_state_mut(device).ip_state.multicast_groups
    }

    fn get_state(
        &self,
        device: SC::DeviceId,
    ) -> &MulticastGroupSet<Ipv6Addr, MldGroupState<C::Instant>> {
        &self.get_ip_device_state(device).ip_state.multicast_groups
    }
}

impl<C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>, SC: device::Ipv6DeviceContext<C>>
    Ipv6DeviceDadContext<C> for SC
{
    fn with_address_state_and_retrans_timer<
        O,
        F: FnOnce(Option<(&mut AddressState, Duration)>) -> O,
    >(
        &mut self,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
        cb: F,
    ) -> O {
        let retrans_timer = SC::get_ip_device_state(self, device_id).retrans_timer.get();
        cb(self.get_ip_device_state_mut(device_id).ip_state.find_addr_mut(&addr).map(
            |Ipv6AddressEntry { addr_sub: _, state, config: _, deprecated: _ }| {
                (state, retrans_timer)
            },
        ))
    }
}

fn send_ndp_packet<
    C: IpLayerNonSyncContext<Ipv6, SC::DeviceId>,
    SC: ip::BufferIpDeviceContext<Ipv6, C, EmptyBuf>,
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

impl<
        C: IpLayerNonSyncContext<Ipv6, SC::DeviceId>,
        SC: ip::BufferIpDeviceContext<Ipv6, C, EmptyBuf>,
    > Ipv6LayerDadContext<C> for SC
{
    fn send_dad_packet(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        dst_ip: MulticastAddr<Ipv6Addr>,
        message: NeighborSolicitation,
    ) -> Result<(), ()> {
        send_ndp_packet(
            self,
            ctx,
            device_id,
            Ipv6::UNSPECIFIED_ADDRESS,
            dst_ip.into_specified(),
            EmptyBuf,
            IcmpUnusedCode,
            message,
        )
        .map_err(|EmptyBuf| ())
    }
}

impl<C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>, SC: device::Ipv6DeviceContext<C>>
    Ipv6DeviceRsContext<C> for SC
{
    fn with_rs_remaining_mut_and_max<
        O,
        F: FnOnce(&mut Option<NonZeroU8>, Option<NonZeroU8>) -> O,
    >(
        &mut self,
        device_id: Self::DeviceId,
        cb: F,
    ) -> O {
        let state = self.get_ip_device_state_mut(device_id);
        cb(&mut state.router_soliciations_remaining, state.config.max_router_solicitations)
    }

    fn get_link_layer_addr_bytes(&self, device_id: SC::DeviceId) -> Option<&[u8]> {
        device::Ipv6DeviceContext::get_link_layer_addr_bytes(self, device_id)
    }
}

impl<
        C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId> + IpLayerNonSyncContext<Ipv6, SC::DeviceId>,
        SC: ip::BufferIpDeviceContext<Ipv6, C, EmptyBuf> + device::Ipv6DeviceContext<C>,
    > Ipv6LayerRsContext<C> for SC
{
    fn send_rs_packet<
        S: Serializer<Buffer = EmptyBuf>,
        F: FnOnce(Option<UnicastAddr<Ipv6Addr>>) -> S,
    >(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        message: RouterSolicitation,
        body: F,
    ) -> Result<(), S> {
        let dst_ip = Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS.into_specified();
        let src_ip = crate::ip::socket::ipv6_source_address_selection::select_ipv6_source_address(
            dst_ip,
            device_id,
            get_ipv6_device_state(self, device_id).iter_addrs().map(move |a| (a, device_id)),
        );
        send_ndp_packet(
            self,
            ctx,
            device_id,
            src_ip.map_or(Ipv6::UNSPECIFIED_ADDRESS, |a| a.get()),
            dst_ip,
            body(src_ip),
            IcmpUnusedCode,
            message,
        )
    }
}

impl<C: IpDeviceNonSyncContext<Ipv4, SC::DeviceId>, SC: device::IpDeviceContext<Ipv4, C>>
    ip::IpDeviceContext<Ipv4, C> for SC
{
    fn is_ip_device_enabled(&self, device_id: SC::DeviceId) -> bool {
        is_ip_device_enabled(self, device_id)
    }

    fn address_status(
        &self,
        dst_ip: SpecifiedAddr<Ipv4Addr>,
    ) -> AddressStatus<(SC::DeviceId, Ipv4PresentAddressStatus)> {
        self.iter_devices()
            .find_map(|device_id| match self.address_status_for_device(dst_ip, device_id) {
                AddressStatus::Present(a) => Some(AddressStatus::Present((device_id, a))),
                AddressStatus::Unassigned => None,
            })
            .unwrap_or(AddressStatus::Unassigned)
    }

    fn address_status_for_device(
        &self,
        dst_ip: SpecifiedAddr<Ipv4Addr>,
        device_id: Self::DeviceId,
    ) -> AddressStatus<Ipv4PresentAddressStatus> {
        let dev_state = get_ipv4_device_state(self, device_id);

        if dst_ip.is_limited_broadcast() {
            return AddressStatus::Present(Ipv4PresentAddressStatus::LimitedBroadcast);
        }

        if MulticastAddr::new(dst_ip.get())
            .map_or(false, |addr| dev_state.multicast_groups.contains(&addr))
        {
            return AddressStatus::Present(Ipv4PresentAddressStatus::Multicast);
        }

        dev_state
            .iter_addrs()
            .find_map(|addr| {
                let (addr, subnet) = addr.addr_subnet();

                if addr == dst_ip {
                    Some(AddressStatus::Present(Ipv4PresentAddressStatus::Unicast))
                } else if dst_ip.get() == subnet.broadcast() {
                    Some(AddressStatus::Present(Ipv4PresentAddressStatus::SubnetBroadcast))
                } else {
                    None
                }
            })
            .unwrap_or(AddressStatus::Unassigned)
    }

    fn is_device_routing_enabled(&self, device_id: SC::DeviceId) -> bool {
        is_ipv4_routing_enabled(self, device_id)
    }

    fn get_hop_limit(&self, _device_id: SC::DeviceId) -> NonZeroU8 {
        DEFAULT_TTL
    }

    fn get_mtu(&self, device_id: Self::DeviceId) -> u32 {
        self.get_mtu(device_id)
    }
}

impl<C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>, SC: device::IpDeviceContext<Ipv6, C>>
    ip::IpDeviceContext<Ipv6, C> for SC
{
    fn is_ip_device_enabled(&self, device_id: SC::DeviceId) -> bool {
        is_ip_device_enabled(self, device_id)
    }

    fn address_status(
        &self,
        addr: SpecifiedAddr<Ipv6Addr>,
    ) -> AddressStatus<(SC::DeviceId, Ipv6PresentAddressStatus)> {
        self.iter_devices()
            .find_map(|device_id| match self.address_status_for_device(addr, device_id) {
                AddressStatus::Present(a) => Some(AddressStatus::Present((device_id, a))),
                AddressStatus::Unassigned => None,
            })
            .unwrap_or(AddressStatus::Unassigned)
    }

    fn address_status_for_device(
        &self,
        addr: SpecifiedAddr<Ipv6Addr>,
        device_id: Self::DeviceId,
    ) -> AddressStatus<<Ipv6 as IpLayerIpExt>::AddressStatus> {
        let dev_state = get_ipv6_device_state(self, device_id);

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
    }

    fn is_device_routing_enabled(&self, device_id: SC::DeviceId) -> bool {
        is_ipv6_routing_enabled(self, device_id)
    }

    fn get_hop_limit(&self, device_id: SC::DeviceId) -> NonZeroU8 {
        get_ipv6_hop_limit(self, device_id)
    }

    fn get_mtu(&self, device_id: Self::DeviceId) -> u32 {
        self.get_mtu(device_id)
    }
}

impl<
        C: IpDeviceNonSyncContext<Ipv6, SC::DeviceId>,
        SC: device::BufferIpDeviceContext<Ipv6, C, EmptyBuf>,
    > FrameContext<C, EmptyBuf, MldFrameMetadata<SC::DeviceId>> for SC
{
    fn send_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        ctx: &mut C,
        meta: MldFrameMetadata<SC::DeviceId>,
        body: S,
    ) -> Result<(), S> {
        SC::send_ip_frame(self, ctx, meta.device, meta.dst_ip.into_specified(), body)
    }
}

impl<
        I: IpLayerIpExt + IpDeviceIpExt<C::Instant, SC::DeviceId>,
        B: BufferMut,
        C: IpDeviceNonSyncContext<I, SC::DeviceId>,
        SC: device::BufferIpDeviceContext<I, C, B> + ip::IpDeviceContext<I, C>,
    > ip::BufferIpDeviceContext<I, C, B> for SC
{
    fn send_ip_frame<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        device_id: SC::DeviceId,
        next_hop: SpecifiedAddr<I::Addr>,
        packet: S,
    ) -> Result<(), S> {
        send_ip_frame(self, ctx, device_id, next_hop, packet)
    }
}
