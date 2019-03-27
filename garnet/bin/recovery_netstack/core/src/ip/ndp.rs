// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Neighboor Discovery Protocol (NDP).
//!
//! Neighboor Discovery for IPv6 as defined in [RFC 4861] defines mechanisms for
//! solving the following problems:
//! - Router Discovery
//! - Prefix Discovery
//! - Parameter Discovery
//! - Address Autoconfiguration
//! - Address resolution
//! - Next-hop determination
//! - Neighbor Unreachability Detection
//! - Duplicate Address Detection
//! - Redirect
//!
//! [RFC 4861]: https://tools.ietf.org/html/rfc4861

use crate::device::ethernet::EthernetNdpDevice;
use crate::device::DeviceId;
use crate::ip::types::IpProto;
use crate::ip::{send_ip_packet_from_device, IpAddress, IpLayerTimerId, Ipv6, Ipv6Addr};
use crate::wire::icmp::ndp::{
    self, options::NdpOption, NeighborAdvertisment, NeighborSolicitation, Options,
};
use crate::wire::icmp::{IcmpMessage, IcmpPacketBuilder, IcmpUnusedCode, Icmpv6Packet};
use crate::wire::ipv6::Ipv6PacketBuilder;
use crate::{Context, EventDispatcher, TimerId, TimerIdInner};
use log::debug;
use packet::{MtuError, Serializer};
use std::collections::HashMap;
use std::time::Duration;
use zerocopy::ByteSlice;

/// The the default value for *RetransTimer* as defined in
/// [RFC 4861 section 10].
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
const RETRANS_TIMER_DEFAULT: Duration = Duration::from_secs(1);

/// The maximum number of multicast solicitations as defined in
/// [RFC 4861 section 10].
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
const MAX_MULTICAST_SOLICIT: usize = 3;

/// A link layer address that can be discovered using NDP.
pub(crate) trait LinkLayerAddress: Copy + Clone {
    /// The length, in bytes, expected for the `LinkLayerAddress`
    const BYTES_LENGTH: usize;

    /// Returns the underlying bytes of a `LinkLayerAddress`
    fn bytes(&self) -> &[u8];
    /// Attemps to construct a `LinkLayerAddress` from the provided bytes.
    ///
    /// `bytes` is guaranteed to be **exactly** `BYTES_LENGTH` long.
    fn from_bytes(bytes: &[u8]) -> Self;
}

/// A device layer protocol which can support NDP.
///
/// An `NdpDevice` is a device layer protocol which can support NDP.
pub(crate) trait NdpDevice: Sized {
    /// The link-layer address type used by this device.
    type LinkAddress: LinkLayerAddress;
    /// The broadcast value for link addresses on this device.
    // NOTE(brunodalbo): RFC 4861 mentions the possibility of running NDP on
    // link types that do not support broadcasts, but this implementation does
    // not cover that for simplicity.
    const BROADCAST: Self::LinkAddress;

    /// Get a mutable reference to a device's NDP state.
    fn get_ndp_state<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
    ) -> &mut NdpState<Self>;

    /// Get the link layer address for a device.
    fn get_link_layer_addr<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
    ) -> Self::LinkAddress;

    /// Get *an* IPv6 address for this device.
    ///
    /// Any **unicast** IPv6 address is a valid return value. Violating this
    /// rule may result in incorrect IP packets being sent.
    fn get_ipv6_addr<D: EventDispatcher>(ctx: &mut Context<D>, device_id: u64) -> Option<Ipv6Addr>;

    /// Checks whether this device has the provided `address`.
    ///
    /// `address` is guaranteed to be a valid unicast address.
    fn has_ipv6_addr<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
        address: &Ipv6Addr,
    ) -> bool;

    /// Send a packet in a device layer frame.
    ///
    /// `send_ipv6_frame` accepts a device ID, a destination hardware address,
    /// and a `Serializer`. It computes the routing information, serializes the
    /// request in a device layer frame, and sends it.
    fn send_ipv6_frame<D: EventDispatcher, S: Serializer>(
        ctx: &mut Context<D>,
        device_id: u64,
        dst: Self::LinkAddress,
        body: S,
    ) -> Result<(), MtuError<S::InnerError>>;

    /// Retrieves the complete `DeviceId` for a given `id`.
    fn get_device_id(id: u64) -> DeviceId;

    /// Notifies device layer that the link-layer address for the neighbor in
    /// `address` has been resolved to `link_address`.
    ///
    /// Implementers may use this signal to dispatch any packets that
    /// were queued waiting for address resolution.
    fn address_resolved<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
        address: &Ipv6Addr,
        link_address: Self::LinkAddress,
    );
}

/// The state associated with an instance of the Neighbor Discovery Protocol
/// (NDP).
///
/// Each device will contain an `NdpState` object to keep track of discovery
/// operations.
pub(crate) struct NdpState<D: NdpDevice> {
    neighbors: NeighborTable<D::LinkAddress>,
}

impl<D: NdpDevice> Default for NdpState<D> {
    fn default() -> Self {
        NdpState { neighbors: NeighborTable::default() }
    }
}

/// The identifier for timer events in NDP operations.
///
/// This is used to retry sending Neighbor Discovery Protocol requests.
#[derive(Copy, Clone, PartialEq)]
pub(crate) struct NdpTimerId {
    device_id: u64,
    neighbor_addr: Ipv6Addr,
}

impl NdpTimerId {
    /// Creates a new `NdpTimerId` wrapped inside a `TimerId` with the provided
    /// `device_id` and `neighbor_addr`.
    pub(crate) fn new_neighbor_solicitation_timer_id(
        device_id: u64,
        neighbor_addr: Ipv6Addr,
    ) -> TimerId {
        Self { device_id, neighbor_addr }.into()
    }
}

impl From<NdpTimerId> for TimerId {
    fn from(v: NdpTimerId) -> Self {
        TimerId(TimerIdInner::IpLayer(IpLayerTimerId::Ndp(v)))
    }
}

/// Handles a timeout event.
///
/// This currently only supports Ethernet NDP, since we know that that is
/// the only case that the netstack currently handles. In the future, this may
/// be extended to support other hardware types.
pub(crate) fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: NdpTimerId) {
    handle_timeout_inner::<D, EthernetNdpDevice>(ctx, id);
}

fn handle_timeout_inner<D: EventDispatcher, ND: NdpDevice>(ctx: &mut Context<D>, id: NdpTimerId) {
    let ndp_state = ND::get_ndp_state(ctx, id.device_id);
    match ndp_state.neighbors.get_neighbor_state_mut(&id.neighbor_addr) {
        Some(NeighborState {
            link_address: LinkAddressResolutionValue::Waiting { ref mut transmit_counter },
        }) => {
            if *transmit_counter < MAX_MULTICAST_SOLICIT {
                // Increase the transmit counter and send the solicitation again
                *transmit_counter += 1;
                send_neighbor_solicitation::<_, ND>(ctx, id.device_id, id.neighbor_addr);
                ctx.dispatcher.schedule_timeout(RETRANS_TIMER_DEFAULT, id.into());
            } else {
                // To make sure we don't get stuck in this neighbor unreachable
                // state forever, remove the neighbor from the database:
                ndp_state.neighbors.delete_neighbor_state(&id.neighbor_addr);
                increment_counter!(ctx, "ndp::neighbor_solicitation_timeout");

                // TODO(brunodalbo) maximum number of retries reached, RFC
                //  says: "If no Neighbor Advertisement is received after
                //  MAX_MULTICAST_SOLICIT solicitations, address resolution has
                //  failed. The sender MUST return ICMP destination unreachable
                //  indications with code 3 (Address Unreachable) for each
                //  packet queued awaiting address resolution."
                log_unimplemented!((), "Maximum number of neighbor solicitations reached");
            }
        }
        _ => debug!("ndp timeout fired for invalid neighbor state"),
    }
}

/// Look up the link layer address
pub(crate) fn lookup<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: u64,
    lookup_addr: Ipv6Addr,
) -> Option<ND::LinkAddress> {
    // An IPv6 multicast address should always be sent on a broadcast
    // link address.
    if lookup_addr.is_multicast() {
        return Some(ND::BROADCAST);
    }
    // TODO(brunodalbo): Figure out what to do if a frame can't be sent
    let ndpstate = ND::get_ndp_state(ctx, device_id);
    let result = ndpstate.neighbors.get_neighbor_state(&lookup_addr);

    match result {
        Some(NeighborState {
            link_address: LinkAddressResolutionValue::Known { address }, ..
        }) => Some(address.clone()),
        None => {
            // if we're not already waiting for a neighbor solicitation
            // response, mark it as waiting and send a neighbor solicitation,
            // also setting the transmission count to 1.
            ndpstate.neighbors.set_waiting_link_address(lookup_addr, 1);
            send_neighbor_solicitation::<_, ND>(ctx, device_id, lookup_addr);
            // also schedule a timer to retransmit in case we don't get
            // neighbor advertisements back.
            ctx.dispatcher.schedule_timeout(
                RETRANS_TIMER_DEFAULT,
                NdpTimerId::new_neighbor_solicitation_timer_id(device_id, lookup_addr),
            );
            None
        }
        _ => None,
    }
}

/// Insert a neighbor to the known neihbors table.
pub(crate) fn insert_neighbor<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: u64,
    net: Ipv6Addr,
    hw: ND::LinkAddress,
) {
    ND::get_ndp_state(ctx, device_id).neighbors.set_link_address(net, hw)
}

/// `NeighborState` keeps all state that NDP may want to keep about neighbors,
/// like link address resolution and reachability information, for example.
struct NeighborState<H> {
    link_address: LinkAddressResolutionValue<H>,
}

impl<H> NeighborState<H> {
    fn new() -> Self {
        Self { link_address: LinkAddressResolutionValue::Unknown }
    }
}

#[derive(Debug, Eq, PartialEq)]
enum LinkAddressResolutionValue<H> {
    Unknown,
    Known { address: H },
    Waiting { transmit_counter: usize },
}

impl<H> LinkAddressResolutionValue<H> {
    fn is_waiting(&self) -> bool {
        match self {
            LinkAddressResolutionValue::Waiting { .. } => true,
            _ => false,
        }
    }
}

struct NeighborTable<H> {
    table: HashMap<Ipv6Addr, NeighborState<H>>,
}

impl<H> NeighborTable<H> {
    fn set_link_address(&mut self, neighbor: Ipv6Addr, address: H) {
        self.table.entry(neighbor).or_insert_with(|| NeighborState::new()).link_address =
            LinkAddressResolutionValue::Known { address };
    }

    fn set_waiting_link_address(&mut self, neighbor: Ipv6Addr, transmit_counter: usize) {
        self.table.entry(neighbor).or_insert_with(|| NeighborState::new()).link_address =
            LinkAddressResolutionValue::Waiting { transmit_counter };
    }

    fn get_neighbor_state(&self, neighbor: &Ipv6Addr) -> Option<&NeighborState<H>> {
        self.table.get(neighbor)
    }

    fn get_neighbor_state_mut(&mut self, neighbor: &Ipv6Addr) -> Option<&mut NeighborState<H>> {
        self.table.get_mut(neighbor)
    }

    fn delete_neighbor_state(&mut self, neighbor: &Ipv6Addr) {
        self.table.remove(neighbor);
    }
}

impl<H> Default for NeighborTable<H> {
    fn default() -> Self {
        NeighborTable { table: HashMap::default() }
    }
}

fn send_neighbor_solicitation<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: u64,
    lookup_addr: Ipv6Addr,
) {
    // TODO(brunodalbo) when we send neighbor solicitations, we SHOULD set
    //  the source IP to the same IP as the packet that triggered the
    //  solicitation, so that when we hit the neighbor they'll have us in their
    //  cache, reducing overall burden on the network.
    if let Some(device_addr) = ND::get_ipv6_addr(ctx, device_id) {
        debug_assert!(device_addr.is_valid_unicast());
        let src_ll = ND::get_link_layer_addr(ctx, device_id);
        let dst_ip = lookup_addr.to_solicited_node_address();
        send_ndp_packet::<_, ND, &u8, _>(
            ctx,
            device_id,
            device_addr,
            dst_ip,
            ND::BROADCAST,
            NeighborSolicitation::new(lookup_addr),
            &[NdpOption::SourceLinkLayerAddress(src_ll.bytes())],
        );
    } else {
        // Nothing can be done if we don't have any ipv6 addresses to send
        // packets out to.
        debug!("Not sending NDP request, since we don't know our IPv6 address");
    }
}

fn send_neighbor_advertisement<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: u64,
    solicited: bool,
    device_addr: Ipv6Addr,
    dst_ip: Ipv6Addr,
) {
    debug!("send_neighbor_advertisement from {:?} to {:?}", device_addr, dst_ip);
    debug_assert!(device_addr.is_valid_unicast());

    // TODO(brunodalbo) if we're a router, flags must also set FLAG_ROUTER.
    let flags = if solicited { NeighborAdvertisment::FLAG_SOLICITED } else { 0x00 };
    // We must call into the higher level send_ip_packet_from_device function
    // because it is not guaranteed that we have actually saved the link layer
    // address of the destination ip. Typically, the solicitation request will
    // carry that information, but it is not necessary. So it is perfectly valid
    // that trying to send this advertisement will end up triggering a neighbor
    // solicitation to be sent.
    let src_ll = ND::get_link_layer_addr(ctx, device_id);
    send_ip_packet_from_device(
        ctx,
        ND::get_device_id(device_id),
        device_addr,
        dst_ip,
        dst_ip,
        IpProto::Icmpv6,
        ndp::OptionsSerializer::<_>::new(
            [NdpOption::TargetLinkLayerAddress(src_ll.bytes())].iter(),
        )
        .encapsulate(IcmpPacketBuilder::<Ipv6, &u8, _>::new(
            device_addr,
            dst_ip,
            IcmpUnusedCode,
            NeighborAdvertisment::new(flags, device_addr),
        )),
    )
    .unwrap_or_else(|e| debug!("Failed to send neighbor advertisement: {:?}", e));
}

/// Helper function to send ndp packet over an NdpDevice
fn send_ndp_packet<D: EventDispatcher, ND: NdpDevice, B, M>(
    ctx: &mut Context<D>,
    device_id: u64,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    link_addr: ND::LinkAddress,
    message: M,
    options: &[NdpOption],
) where
    M: IcmpMessage<Ipv6, B, Code = IcmpUnusedCode>,
{
    ND::send_ipv6_frame(
        ctx,
        device_id,
        link_addr,
        ndp::OptionsSerializer::<_>::new(options.iter())
            .encapsulate(IcmpPacketBuilder::<Ipv6, B, M>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                message,
            ))
            .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, 1, IpProto::Icmpv6)),
    );
}

pub(crate) fn receive_ndp_packet<D: EventDispatcher, B>(
    ctx: &mut Context<D>,
    device: Option<DeviceId>,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    packet: Icmpv6Packet<B>,
) where
    B: ByteSlice,
{
    match device {
        Some(d) => {
            // TODO(brunodalbo) we're assuming the device Id is for an ethernet
            //  device, but it could be for another protocol.
            receive_ndp_packet_inner::<_, EthernetNdpDevice, _>(
                ctx,
                d.id(),
                src_ip,
                dst_ip,
                packet,
            );
        }
        None => {
            // NDP needs a device identifier context to operate on.
            debug!("Got NDP packet without device identifier. Ignoring it.");
        }
    }
}

fn receive_ndp_packet_inner<D: EventDispatcher, ND: NdpDevice, B>(
    ctx: &mut Context<D>,
    device_id: u64,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    packet: Icmpv6Packet<B>,
) where
    B: ByteSlice,
{
    match packet {
        Icmpv6Packet::RouterSolicitation(p) => {
            log_unimplemented!((), "NDP Router Solicitation not implemented")
        }
        Icmpv6Packet::RouterAdvertisment(p) => {
            log_unimplemented!((), "NDP Router Advertisement not implemented")
        }
        Icmpv6Packet::NeighborSolicitation(p) => {
            let target_address = p.message().target_address();
            if !(target_address.is_valid_unicast()
                && ND::has_ipv6_addr(ctx, device_id, target_address))
            {
                // just ignore packet, either it was not really meant for us or
                // is malformed.
                return;
            }
            increment_counter!(ctx, "ndp::rx_neighbor_solicitation");
            // if we have a source link layer address option, we take it and
            // save to our cache:
            if let Some(ll) = get_source_link_layer_option::<ND, _>(p.body()) {
                // TODO(brunodalbo) the reachability state of the neighbor
                //  that is added to the cache by this route must be set to
                //  STALE.
                ND::get_ndp_state(ctx, device_id).neighbors.set_link_address(src_ip, ll);
            }

            // Finally we ought to reply to the Neighbor Solicitation with a
            // Neighbor Advertisement.
            send_neighbor_advertisement::<_, ND>(ctx, device_id, true, *target_address, src_ip);
        }
        Icmpv6Packet::NeighborAdvertisment(p) => {
            increment_counter!(ctx, "ndp::rx_neighbor_advertisement");
            let state = ND::get_ndp_state(ctx, device_id);
            match state.neighbors.get_neighbor_state_mut(&src_ip) {
                None => {
                    // If the neighbor is not in the cache, we just ignore the
                    // advertisement, as we're not interested in communicating
                    // with it.
                }
                Some(NeighborState { link_address }) if link_address.is_waiting() => {
                    if let Some(address) = get_target_link_layer_option::<ND, _>(p.body()) {
                        *link_address = LinkAddressResolutionValue::Known { address };
                        ND::address_resolved(ctx, device_id, &src_ip, address);
                    }
                }
                _ => {
                    // TODO(brunodalbo) In any other case, the neighbor
                    //  advertisement is used to update reachability and router
                    //  status in the cache.
                    log_unimplemented!((), "Received already known neighbor advertisement")
                }
            }
        }
        Icmpv6Packet::Redirect(p) => log_unimplemented!((), "NDP Redirect not implemented"),
        _ => debug_assert!(false, "Invalid ICMP packet passed to NDP"),
    }
}

fn get_source_link_layer_option<ND: NdpDevice, B>(options: &Options<B>) -> Option<ND::LinkAddress>
where
    B: ByteSlice,
{
    options.iter().find_map(|o| match o {
        NdpOption::SourceLinkLayerAddress(a) => {
            if a.len() >= ND::LinkAddress::BYTES_LENGTH {
                Some(ND::LinkAddress::from_bytes(&a[..ND::LinkAddress::BYTES_LENGTH]))
            } else {
                None
            }
        }
        _ => None,
    })
}

fn get_target_link_layer_option<ND: NdpDevice, B>(options: &Options<B>) -> Option<ND::LinkAddress>
where
    B: ByteSlice,
{
    options.iter().find_map(|o| match o {
        NdpOption::TargetLinkLayerAddress(a) => {
            if a.len() >= ND::LinkAddress::BYTES_LENGTH {
                Some(ND::LinkAddress::from_bytes(&a[..ND::LinkAddress::BYTES_LENGTH]))
            } else {
                None
            }
        }
        _ => None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::device::ethernet::{EthernetNdpDevice, Mac};
    use crate::ip::IPV6_MIN_MTU;
    use crate::testutil::{
        self, set_logger_for_test, DummyEventDispatcher, DummyEventDispatcherBuilder,
    };
    use crate::wire::icmp::IcmpEchoRequest;
    use crate::StackState;
    use packet::{Buf, BufferSerializer};

    const TEST_LOCAL_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_REMOTE_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);

    fn local_ip() -> Ipv6Addr {
        TEST_LOCAL_MAC.to_ipv6_link_local(None)
    }

    fn remote_ip() -> Ipv6Addr {
        TEST_REMOTE_MAC.to_ipv6_link_local(None)
    }

    #[test]
    fn test_send_neighbor_solicitation_on_cache_miss() {
        set_logger_for_test();
        let mut state = StackState::default();
        let dev_id = state.device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        let dispatcher = DummyEventDispatcher::default();
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);

        lookup::<DummyEventDispatcher, EthernetNdpDevice>(&mut ctx, dev_id.id(), remote_ip());

        // Check that we send the original neighboor solicitation,
        // then resend a few times if we don't receive a response.
        for packet_num in 0..MAX_MULTICAST_SOLICIT {
            assert_eq!(ctx.dispatcher.frames_sent().len(), packet_num + 1);

            testutil::trigger_next_timer(&mut ctx);
        }
        // check that we hit the timeout after MAX_MULTICAST_SOLICIT
        assert_eq!(
            *ctx.state().test_counters.get("ndp::neighbor_solicitation_timeout"),
            1,
            "timeout counter at zero"
        );
    }

    #[test]
    fn test_address_resolution() {
        set_logger_for_test();
        let mut builder = DummyEventDispatcherBuilder::default();
        builder.add_device(TEST_LOCAL_MAC);
        let mut local = builder.build();
        builder = DummyEventDispatcherBuilder::default();
        builder.add_device(TEST_REMOTE_MAC);
        let mut remote = builder.build();
        let device_id = DeviceId::new_ethernet(1);
        let mapper = |_: DeviceId| device_id;

        // let's try to ping the remote device from the local device:
        let req = IcmpEchoRequest::new(0, 0);
        let req_body = &[1, 2, 3, 4];
        let body = BufferSerializer::new_vec(Buf::new(req_body.to_vec(), ..)).encapsulate(
            IcmpPacketBuilder::<Ipv6, &[u8], _>::new(local_ip(), remote_ip(), IcmpUnusedCode, req),
        );
        send_ip_packet_from_device(
            &mut local,
            device_id,
            local_ip(),
            remote_ip(),
            remote_ip(),
            IpProto::Icmpv6,
            body,
        );
        // this should've triggered a neighbor solicitation to come out of local
        assert_eq!(local.dispatcher.frames_sent().len(), 1);
        local.dispatcher.forward_frames(&mut remote, mapper);
        assert_eq!(
            *remote.state().test_counters.get("ndp::rx_neighbor_solicitation"),
            1,
            "remote received solicitation"
        );
        assert_eq!(remote.dispatcher.frames_sent().len(), 1);
        // forward advertisement response back to local
        remote.dispatcher.forward_frames(&mut local, mapper);
        assert_eq!(
            *local.state().test_counters.get("ndp::rx_neighbor_advertisement"),
            1,
            "local received advertisement"
        );

        // at the end of the exchange, both sides should have each other on
        // their ndp tables:
        assert_eq!(
            EthernetNdpDevice::get_ndp_state::<_>(&mut local, device_id.id())
                .neighbors
                .get_neighbor_state(&remote_ip())
                .unwrap()
                .link_address,
            LinkAddressResolutionValue::<Mac>::Known { address: TEST_REMOTE_MAC }
        );
        assert_eq!(
            EthernetNdpDevice::get_ndp_state::<_>(&mut remote, device_id.id())
                .neighbors
                .get_neighbor_state(&local_ip())
                .unwrap()
                .link_address,
            LinkAddressResolutionValue::<Mac>::Known { address: TEST_LOCAL_MAC }
        );

        // TODO(brunodalbo) once we are able to dequeue the packets, we can
        //  assert that "remote" receives the original IcmpEchoRequest.
    }
}
