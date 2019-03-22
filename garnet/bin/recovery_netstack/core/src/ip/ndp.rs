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
use crate::ip::types::IpProto;
use crate::ip::{IpLayerTimerId, Ipv6, Ipv6Addr};
use crate::wire::icmp::ndp::{self, options::NdpOption, NeighborSolicitation};
use crate::wire::icmp::{IcmpMessage, IcmpPacketBuilder, IcmpUnusedCode};
use crate::wire::ipv6::Ipv6PacketBuilder;
use crate::{Context, EventDispatcher, TimerId, TimerIdInner};
use log::debug;
use packet::{MtuError, Serializer};
use std::collections::HashMap;
use std::time::Duration;

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
    fn bytes(&self) -> &[u8];
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::device::ethernet::{EthernetNdpDevice, Mac};
    use crate::ip::IPV6_MIN_MTU;
    use crate::testutil::{self, set_logger_for_test, DummyEventDispatcher};
    use crate::StackState;

    const TEST_LOCAL_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_REMOTE_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);

    #[test]
    fn test_send_neighbor_solicitation_on_cache_miss() {
        set_logger_for_test();
        let mut state = StackState::default();
        let dev_id = state.device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        let dispatcher = DummyEventDispatcher::default();
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);

        lookup::<DummyEventDispatcher, EthernetNdpDevice>(
            &mut ctx,
            dev_id.id(),
            TEST_REMOTE_MAC.to_ipv6_link_local(None),
        );

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
}
