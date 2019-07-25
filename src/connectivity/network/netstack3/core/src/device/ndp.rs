// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Neighbor Discovery Protocol (NDP).
//!
//! Neighbor Discovery for IPv6 as defined in [RFC 4861] defines mechanisms for
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

use std::collections::HashMap;
use std::num::NonZeroUsize;
use std::time::Duration;

use log::{debug, error, trace};
use net_types::ip::{IpAddress, Ipv6, Ipv6Addr};
use net_types::{LinkLocalAddress, MulticastAddress};
use packet::{EmptyBuf, InnerPacketBuilder, Serializer};
use zerocopy::ByteSlice;

use crate::device::ethernet::EthernetNdpDevice;
use crate::device::{DeviceId, DeviceLayerTimerId, DeviceProtocol, Tentative};
use crate::ip::{is_router, IpProto};
use crate::wire::icmp::ndp::{
    self, options::NdpOption, NeighborAdvertisement, NeighborSolicitation, Options,
};
use crate::wire::icmp::{IcmpMessage, IcmpPacketBuilder, IcmpUnusedCode, Icmpv6Packet};
use crate::wire::ipv6::Ipv6PacketBuilder;
use crate::{Context, EventDispatcher, StackState, TimerId, TimerIdInner};

/// The default value for *RetransTimer* as defined in
/// [RFC 4861 section 10].
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
const RETRANS_TIMER_DEFAULT: Duration = Duration::from_secs(1);

/// The maximum number of multicast solicitations as defined in
/// [RFC 4861 section 10].
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
const MAX_MULTICAST_SOLICIT: usize = 3;

/// The number of NS messages to be sent to perform DAD
/// [RFC 4862 section 5.1]
///
/// [RFC 4862 section 5.1]: https://tools.ietf.org/html/rfc4862#section-5.1
pub(crate) const DUP_ADDR_DETECT_TRANSMITS: usize = 1;

/// A link layer address that can be discovered using NDP.
pub(crate) trait LinkLayerAddress: Copy + Clone {
    /// The length, in bytes, expected for the `LinkLayerAddress`
    const BYTES_LENGTH: usize;

    /// Returns the underlying bytes of a `LinkLayerAddress`
    fn bytes(&self) -> &[u8];
    /// Attempts to construct a `LinkLayerAddress` from the provided bytes.
    ///
    /// `bytes` is guaranteed to be **exactly** `BYTES_LENGTH` long.
    fn from_bytes(bytes: &[u8]) -> Self;
}

/// The various states an IP address can be on an interface.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum AddressState {
    /// The address is assigned to an interface and can be considered
    /// bound to it (all packets destined to the address will be
    /// accepted).
    Assigned,

    /// The address is unassigned to an interface. Packets destined to
    /// an unassigned address will be dropped, or forwarded if the
    /// interface is acting as a router and possible to forward.
    Unassigned,

    /// The address is considered unassigned to an interface for normal
    /// operations, but has the intention of being assigned in the future
    /// (e.g. once NDP's Duplicate Address Detection is completed).
    Tentative,
}

impl AddressState {
    /// Is this address unassigned?
    pub(crate) fn is_unassigned(self) -> bool {
        self == AddressState::Unassigned
    }
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
        state: &mut StackState<D>,
        device_id: usize,
    ) -> &mut NdpState<Self>;

    /// Get the link layer address for a device.
    fn get_link_layer_addr<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
    ) -> Self::LinkAddress;

    /// Get a (possibly tentative) IPv6 address for this device.
    ///
    /// Any **unicast** IPv6 address is a valid return value. Violating this
    /// rule may result in incorrect IP packets being sent.
    fn get_ipv6_addr<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
    ) -> Option<Tentative<Ipv6Addr>>;

    /// Returns the state of `address` on the device identified
    /// by `device_id`.
    ///
    /// `address` is guaranteed to be a valid unicast address.
    fn ipv6_addr_state<D: EventDispatcher>(
        state: &StackState<D>,
        device_id: usize,
        address: &Ipv6Addr,
    ) -> AddressState;

    /// Send a packet in a device layer frame to a destination `LinkAddress`.
    ///
    /// `send_ipv6_frame_to` accepts a device ID, a destination hardware
    /// address, and a `Serializer`. Implementers are expected simply to form
    /// a link-layer frame and encapsulate the provided IPv6 body.
    fn send_ipv6_frame_to<D: EventDispatcher, S: Serializer<Buffer = EmptyBuf>>(
        ctx: &mut Context<D>,
        device_id: usize,
        dst: Self::LinkAddress,
        body: S,
    ) -> Result<(), S>;

    /// Send a packet in a device layer frame.
    ///
    /// `send_ipv6_frame` accepts a device ID, a next hop IP address, and a
    /// `Serializer`. Implementers must resolve the destination link-layer
    /// address from the provided `next_hop` IPv6 address.
    fn send_ipv6_frame<D: EventDispatcher, S: Serializer<Buffer = EmptyBuf>>(
        ctx: &mut Context<D>,
        device_id: usize,
        next_hop: Ipv6Addr,
        body: S,
    ) -> Result<(), S>;

    /// Retrieves the complete `DeviceId` for a given `id`.
    fn get_device_id(id: usize) -> DeviceId;

    /// Notifies device layer that the link-layer address for the neighbor in
    /// `address` has been resolved to `link_address`.
    ///
    /// Implementers may use this signal to dispatch any packets that
    /// were queued waiting for address resolution.
    fn address_resolved<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: usize,
        address: &Ipv6Addr,
        link_address: Self::LinkAddress,
    );

    /// Notifies the device layer that the link-layer address resolution for
    /// the neighbor in `address` failed.
    fn address_resolution_failed<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: usize,
        address: &Ipv6Addr,
    );

    /// Notifies the device layer that a duplicate address has been detected. The
    /// device should want to remove the address.
    fn duplicate_address_detected<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: usize,
        addr: Ipv6Addr,
    );

    /// Notifies the device layer that the address is very likely (because DAD
    /// is not reliable) to be unique, it is time to mark it to be permanent.
    ///
    /// # Panics
    ///
    /// Panics if `addr` is not tentative on the devide identified by `device_id`.
    fn unique_address_determined<D: EventDispatcher>(
        state: &mut StackState<D>,
        device_id: usize,
        addr: Ipv6Addr,
    );
}

/// Per interface configurations for NDP.
#[derive(Debug, Clone)]
pub struct NdpConfigurations {
    /// Value for NDP's DUP_ADDR_DETECT_TRANSMITS parameter.
    ///
    /// As per [RFC 4862 section 5.1], the DUP_ADDR_DETECT_TRANSMITS
    /// is configurable per interface.
    ///
    /// A value of `None` means DAD will not be performed on the interface.
    ///
    /// [RFC 4862 section 5.1]: https://tools.ietf.org/html/rfc4862#section-5.1
    dup_addr_detect_transmits: Option<NonZeroUsize>,
}

impl Default for NdpConfigurations {
    fn default() -> Self {
        Self { dup_addr_detect_transmits: NonZeroUsize::new(DUP_ADDR_DETECT_TRANSMITS) }
    }
}

impl NdpConfigurations {
    /// Set the value for NDP's DUP_ADDR_DETECT_TRANSMITS parameter.
    ///
    /// A value of `None` means DAD wil not be performed on the interface.
    pub(crate) fn set_dup_addr_detect_transmits(&mut self, v: Option<NonZeroUsize>) {
        self.dup_addr_detect_transmits = v;
    }
}

/// The state associated with an instance of the Neighbor Discovery Protocol
/// (NDP).
///
/// Each device will contain an `NdpState` object to keep track of discovery
/// operations.
pub(crate) struct NdpState<D: NdpDevice> {
    neighbors: NeighborTable<D::LinkAddress>,
    dad_transmits_remaining: HashMap<Ipv6Addr, usize>,
    configs: NdpConfigurations,
}

impl<D: NdpDevice> NdpState<D> {
    pub(crate) fn new(configs: NdpConfigurations) -> Self {
        Self {
            neighbors: NeighborTable::default(),
            dad_transmits_remaining: HashMap::new(),
            configs,
        }
    }

    pub(crate) fn set_dad_transmits(&mut self, new_dad_transmits: Option<NonZeroUsize>) {
        self.configs.dup_addr_detect_transmits = new_dad_transmits;
    }
}

/// The identifier for timer events in NDP operations.
#[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
pub(crate) struct NdpTimerId {
    device_id: DeviceId,
    inner: InnerNdpTimerId,
}

/// The types of NDP timers.
#[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
pub(crate) enum InnerNdpTimerId {
    /// This is used to retry sending Neighbor Discovery Protocol requests.
    LinkAddressResolution { neighbor_addr: Ipv6Addr },
    /// This is used to resend Duplicate Address Detection Neighbor Solicitation
    /// messages if `DUP_ADDR_DETECTION_TRANSMITS` is greater than one.
    DadNsTransmit { addr: Ipv6Addr },
    // TODO: The RFC suggests that we SHOULD make a random delay to
    // join the solicitation group. When we support MLD, we probably
    // want one for that.
}

impl NdpTimerId {
    /// Creates a new `NdpTimerId` wrapped inside a `TimerId` with the provided
    /// `device_id` and `neighbor_addr`.
    pub(crate) fn new_link_address_resolution_timer_id<ND: NdpDevice>(
        device_id: usize,
        neighbor_addr: Ipv6Addr,
    ) -> TimerId {
        NdpTimerId {
            device_id: ND::get_device_id(device_id),
            inner: InnerNdpTimerId::LinkAddressResolution { neighbor_addr },
        }
        .into()
    }

    pub(crate) fn new_dad_ns_transmission_timer_id<ND: NdpDevice>(
        device_id: usize,
        tentative_addr: Ipv6Addr,
    ) -> TimerId {
        NdpTimerId {
            device_id: ND::get_device_id(device_id),
            inner: InnerNdpTimerId::DadNsTransmit { addr: tentative_addr },
        }
        .into()
    }
}

impl From<NdpTimerId> for TimerId {
    fn from(v: NdpTimerId) -> Self {
        TimerId(TimerIdInner::DeviceLayer(DeviceLayerTimerId::Ndp(v)))
    }
}

/// Handles a timeout event.
///
/// This currently only supports Ethernet NDP, since we know that that is
/// the only case that the netstack currently handles. In the future, this may
/// be extended to support other hardware types.
pub(crate) fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: NdpTimerId) {
    match id.device_id.protocol() {
        DeviceProtocol::Ethernet => {
            handle_timeout_inner::<_, EthernetNdpDevice>(ctx, id.device_id.id(), id.inner)
        }
    }
}

fn handle_timeout_inner<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: usize,
    inner_id: InnerNdpTimerId,
) {
    match inner_id {
        InnerNdpTimerId::LinkAddressResolution { neighbor_addr } => {
            let ndp_state = ND::get_ndp_state(ctx.state_mut(), device_id);
            match ndp_state.neighbors.get_neighbor_state_mut(&neighbor_addr) {
                Some(NeighborState {
                    link_address: LinkAddressResolutionValue::Waiting { ref mut transmit_counter },
                }) => {
                    if *transmit_counter < MAX_MULTICAST_SOLICIT {
                        // Increase the transmit counter and send the solicitation again
                        *transmit_counter += 1;
                        send_neighbor_solicitation::<_, ND>(ctx, device_id, neighbor_addr);
                        ctx.dispatcher.schedule_timeout(
                            RETRANS_TIMER_DEFAULT,
                            NdpTimerId::new_link_address_resolution_timer_id::<ND>(
                                device_id,
                                neighbor_addr,
                            ),
                        );
                    } else {
                        // To make sure we don't get stuck in this neighbor unreachable
                        // state forever, remove the neighbor from the database:
                        ndp_state.neighbors.delete_neighbor_state(&neighbor_addr);
                        increment_counter!(ctx, "ndp::neighbor_solicitation_timeout");

                        ND::address_resolution_failed(ctx, device_id, &neighbor_addr);
                    }
                }
                _ => debug!("ndp timeout fired for invalid neighbor state"),
            }
        }
        InnerNdpTimerId::DadNsTransmit { addr } => {
            // Get device NDP state.
            //
            // We know this call to unwrap will not fail because we will only reach here
            // if DAD has been started for some device - address pair. When we start DAD,
            // we setup the `NdpState` so we should have a valid entry.
            let ndp_state = ND::get_ndp_state(ctx.state_mut(), device_id);
            let remaining = *ndp_state.dad_transmits_remaining.get(&addr).unwrap();

            // We have finished.
            if remaining == 0 {
                // We know `unwrap` will not fail because we just succesfully
                // called `get` then `unwrap` earlier.
                ndp_state.dad_transmits_remaining.remove(&addr).unwrap();

                // `unique_address_determined` may panic if we attempt to resolve an `addr`
                // that is not tentative on the device with id `device_id`. However, we
                // can only reach here if `addr` was tentative on `device_id` and we are
                // performing DAD so we know `unique_address_determined` will not panic.
                ND::unique_address_determined(ctx.state_mut(), device_id, addr);
            } else {
                do_duplicate_address_detection::<D, ND>(ctx, device_id, addr, remaining);
            }
        }
    }
}

/// Look up the link layer address
pub(crate) fn lookup<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: usize,
    lookup_addr: Ipv6Addr,
) -> Option<ND::LinkAddress> {
    trace!("ndp::lookup: {:?}", lookup_addr);

    // An IPv6 multicast address should always be sent on a broadcast
    // link address.
    if lookup_addr.is_multicast() {
        // TODO(brunodalbo): this is currently out of spec, we need to form a
        //  MAC multicast from the lookup address conforming to RFC 2464.
        return Some(ND::BROADCAST);
    }
    // TODO(brunodalbo): Figure out what to do if a frame can't be sent
    let ndpstate = ND::get_ndp_state(ctx.state_mut(), device_id);
    let result = ndpstate.neighbors.get_neighbor_state(&lookup_addr);

    match result {
        Some(NeighborState {
            link_address: LinkAddressResolutionValue::Known { address }, ..
        }) => Some(*address),
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
                NdpTimerId::new_link_address_resolution_timer_id::<ND>(device_id, lookup_addr),
            );
            None
        }
        _ => None,
    }
}

/// Insert a neighbor to the known neighbors table.
pub(crate) fn insert_neighbor<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: usize,
    net: Ipv6Addr,
    hw: ND::LinkAddress,
) {
    ND::get_ndp_state(ctx.state_mut(), device_id).neighbors.set_link_address(net, hw)
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
        self.table.entry(neighbor).or_insert_with(NeighborState::new).link_address =
            LinkAddressResolutionValue::Known { address };
    }

    fn set_waiting_link_address(&mut self, neighbor: Ipv6Addr, transmit_counter: usize) {
        self.table.entry(neighbor).or_insert_with(NeighborState::new).link_address =
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

/// Begin the Duplicate Address Detection process.
///
/// If the device is configured to not do DAD, then this method will
/// immediately assign `tentative_addr` to the device.
pub(crate) fn start_duplicate_address_detection<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: usize,
    tentative_addr: Ipv6Addr,
) {
    let transmits = ND::get_ndp_state(ctx.state_mut(), device_id).configs.dup_addr_detect_transmits;

    if let Some(transmits) = transmits {
        do_duplicate_address_detection::<D, ND>(ctx, device_id, tentative_addr, transmits.get());
    } else {
        // DAD is turned off since the interface's DUP_ADDR_DETECT_TRANSMIT parameter
        // is `None`.
        ND::unique_address_determined(ctx.state_mut(), device_id, tentative_addr);
    }
}

/// Cancels the Duplicate Address Detection process.
///
/// Note, the address will now be in a tentative state forever unless the
/// caller assigns a new address to the device (DAD will restart), explicitly
/// restarts DAD, or the device receives a Neighbor Solicitation or Neighbor
/// Advertisement message (the address will be found to be a duplicate and
/// unassigned from the device).
///
/// # Panics
///
/// Panics if we are not currently performing DAD for `tentative_addr` on `device_id`.
pub(crate) fn cancel_duplicate_address_detection<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: usize,
    tentative_addr: Ipv6Addr,
) {
    ctx.dispatcher_mut().cancel_timeout(NdpTimerId::new_dad_ns_transmission_timer_id::<ND>(
        device_id,
        tentative_addr,
    ));

    // `unwrap` may panic if we have no entry in `dad_transmits_remaining` for
    // `tentative_addr` which means that we are not performing DAD on
    // `tentative_add`. This case is documented as a panic condition.
    ND::get_ndp_state(ctx.state_mut(), device_id)
        .dad_transmits_remaining
        .remove(&tentative_addr)
        .unwrap();
}

fn do_duplicate_address_detection<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: usize,
    tentative_addr: Ipv6Addr,
    remaining: usize,
) {
    trace!("do_duplicate_address_detection: tentative_addr {:?}", tentative_addr);

    assert!(remaining > 0);

    let src_ll = ND::get_link_layer_addr(ctx.state(), device_id);
    send_ndp_packet::<_, ND, &[u8], _>(
        ctx,
        device_id,
        Ipv6Addr::new([0; 16]),
        tentative_addr.to_solicited_node_address().get(),
        ND::BROADCAST,
        NeighborSolicitation::new(tentative_addr),
        &[NdpOption::SourceLinkLayerAddress(src_ll.bytes())],
    );
    ND::get_ndp_state(ctx.state_mut(), device_id)
        .dad_transmits_remaining
        .insert(tentative_addr, remaining - 1);
    // Uses same RETRANS_TIMER definition per RFC 4862 section-5.1
    ctx.dispatcher_mut().schedule_timeout(
        RETRANS_TIMER_DEFAULT,
        NdpTimerId::new_dad_ns_transmission_timer_id::<ND>(device_id, tentative_addr),
    );
}

fn send_neighbor_solicitation<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: usize,
    lookup_addr: Ipv6Addr,
) {
    trace!("send_neighbor_solicitation: lookip_addr {:?}", lookup_addr);

    // TODO(brunodalbo) when we send neighbor solicitations, we SHOULD set
    //  the source IP to the same IP as the packet that triggered the
    //  solicitation, so that when we hit the neighbor they'll have us in their
    //  cache, reducing overall burden on the network.
    if let Some(tentative_device_addr) = ND::get_ipv6_addr(ctx.state(), device_id) {
        let device_addr = tentative_device_addr.into_inner();
        debug_assert!(device_addr.is_valid_unicast());
        let src_ll = ND::get_link_layer_addr(ctx.state(), device_id);
        let dst_ip = lookup_addr.to_solicited_node_address().get();
        send_ndp_packet::<_, ND, &[u8], _>(
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
    device_id: usize,
    solicited: bool,
    device_addr: Ipv6Addr,
    dst_ip: Ipv6Addr,
) {
    debug!("send_neighbor_advertisement from {:?} to {:?}", device_addr, dst_ip);
    debug_assert!(device_addr.is_valid_unicast());
    // We currently only allow the destination address to be:
    // 1) a unicast address.
    // 2) a multicast destination but the message should be a unsolicited neighbor
    //    advertisement.
    // NOTE: this assertion may need change if more messages are to be allowed in the future.
    debug_assert!(dst_ip.is_valid_unicast() || (!solicited && dst_ip.is_multicast()));

    // TODO(brunodalbo) if we're a router, flags must also set FLAG_ROUTER.
    let flags = if solicited { NeighborAdvertisement::FLAG_SOLICITED } else { 0x00 };
    // We must call into the higher level send_ipv6_frame function because it is
    // not guaranteed that we have actually saved the link layer address of the
    // destination ip. Typically, the solicitation request will carry that
    // information, but it is not necessary. So it is perfectly valid that
    // trying to send this advertisement will end up triggering a neighbor
    // solicitation to be sent.
    let src_ll = ND::get_link_layer_addr(ctx.state(), device_id);
    let options = [NdpOption::TargetLinkLayerAddress(src_ll.bytes())];
    let body = ndp::OptionsSerializer::<_>::new(options.iter())
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
            device_addr,
            dst_ip,
            IcmpUnusedCode,
            NeighborAdvertisement::new(flags, device_addr),
        ))
        .encapsulate(Ipv6PacketBuilder::new(device_addr, dst_ip, 1, IpProto::Icmpv6));
    ND::send_ipv6_frame(ctx, device_id, dst_ip, body)
        .unwrap_or_else(|_| debug!("Failed to send neighbor advertisement: MTU exceeded"));
}

/// Helper function to send ndp packet over an NdpDevice
fn send_ndp_packet<D: EventDispatcher, ND: NdpDevice, B: ByteSlice, M>(
    ctx: &mut Context<D>,
    device_id: usize,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    link_addr: ND::LinkAddress,
    message: M,
    options: &[NdpOption],
) where
    M: IcmpMessage<Ipv6, B, Code = IcmpUnusedCode>,
{
    trace!("send_ndp_packet: src_ip={:?} dst_ip={:?}", src_ip, dst_ip);
    ND::send_ipv6_frame_to(
        ctx,
        device_id,
        link_addr,
        ndp::OptionsSerializer::<_>::new(options.iter())
            .into_serializer()
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
    trace!("receive_ndp_packet");

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

fn duplicate_address_detected<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: usize,
    addr: Ipv6Addr,
) {
    cancel_duplicate_address_detection::<_, ND>(ctx, device_id, addr);

    // let's notify our device
    ND::duplicate_address_detected(ctx, device_id, addr);
}

fn receive_ndp_packet_inner<D: EventDispatcher, ND: NdpDevice, B>(
    ctx: &mut Context<D>,
    device_id: usize,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    packet: Icmpv6Packet<B>,
) where
    B: ByteSlice,
{
    match packet {
        Icmpv6Packet::RouterSolicitation(p) => {
            trace!("receive_ndp_packet_inner: Received NDP RS");

            if !is_router::<_, Ipv6>(ctx) {
                // Hosts MUST silently discard Router Solicitation messages
                // as per RFC 4861 section 6.1.1.
                trace!("receive_ndp_packet_inner: not a router, discarding NDP RS");
                return;
            }

            // TODO(ghanan): Make sure IP's hop limit is set to 255 as per RFC 4861 section 6.1.1.

            let source_link_layer_option = get_source_link_layer_option::<ND, _>(p.body());

            if src_ip.is_unspecified() && source_link_layer_option.is_some() {
                // If the IP source address is the unspecified address and there is a
                // source link-layer address option in the message, we MUST silently
                // discard the Router Solicitation message as per RFC 4861 section 6.1.1.
                trace!("receive_ndp_packet_inner: source is unspcified but it has the source link-layer address option, discarding NDP RS");
                return;
            }

            increment_counter!(ctx, "ndp::rx_router_solicitation");

            log_unimplemented!((), "NDP Router Solicitation not implemented")
        }
        Icmpv6Packet::RouterAdvertisement(p) => {
            trace!("receive_ndp_packet_inner: Received NDP RA");

            if !src_ip.is_linklocal() {
                // Nodes MUST silently discard any received Router Advertisement message
                // where the IP source address is not a link-local address as routers must
                // use their link-local address as the source for Router Advertisements so
                // hosts can uniquely identify routers, as per RFC 4861 section 6.1.2.
                trace!("receive_ndp_packet_inner: source is not a link-local address, discarding NDP RA");
                return;
            }

            // TODO(ghanan): Make sure IP's hop limit is set to 255 as per RFC 4861 section 6.1.2.

            increment_counter!(ctx, "ndp::rx_router_advertisement");

            log_unimplemented!((), "NDP Router Advertisement not implemented")
        }
        Icmpv6Packet::NeighborSolicitation(p) => {
            trace!("receive_ndp_packet_inner: Received NDP NS");

            let target_address = p.message().target_address();
            if !target_address.is_valid_unicast()
                || ND::ipv6_addr_state(ctx.state(), device_id, target_address).is_unassigned()
            {
                // just ignore packet, either it was not really meant for us or
                // is malformed.
                trace!("receive_ndp_packet_inner: Dropping NDP NS packet that is not meant for us or malformed");
                return;
            }

            // this solicitation message is used for DAD
            if let Some(my_addr) = ND::get_ipv6_addr(ctx.state_mut(), device_id) {
                if my_addr.is_tentative() {
                    // we are not allowed to respond to the solicitation
                    if src_ip.is_unspecified() {
                        // If the source address of the packet is the unspecified address,
                        // the source of the packet is performing DAD for the same target
                        // address as our `my_addr`. A duplicate address has been detected.
                        duplicate_address_detected::<_, ND>(ctx, device_id, my_addr.into_inner());
                    }
                } else {
                    increment_counter!(ctx, "ndp::rx_neighbor_solicitation");
                    // if we have a source link layer address option, we take it and
                    // save to our cache:
                    if !src_ip.is_unspecified() {
                        // we only update the cache if it is not from an unspecified address,
                        // i.e., it is not a DAD message. (RFC 4861)
                        if let Some(ll) = get_source_link_layer_option::<ND, _>(p.body()) {
                            // TODO(brunodalbo) the reachability state of the neighbor
                            //  that is added to the cache by this route must be set to
                            //  STALE.
                            ND::get_ndp_state(ctx.state_mut(), device_id)
                                .neighbors
                                .set_link_address(src_ip, ll);
                        }

                        // Finally we ought to reply to the Neighbor Solicitation with a
                        // Neighbor Advertisement.
                        send_neighbor_advertisement::<_, ND>(
                            ctx,
                            device_id,
                            true,
                            *target_address,
                            src_ip,
                        );
                    } else {
                        // Send out Unsolicited Advertisement in response to neighbor who's
                        // performing DAD, as described in RFC 4861 and 4862
                        send_neighbor_advertisement::<_, ND>(
                            ctx,
                            device_id,
                            false,
                            *target_address,
                            Ipv6::ALL_NODES_LINK_LOCAL_ADDRESS,
                        )
                    }
                }
            }
        }
        Icmpv6Packet::NeighborAdvertisement(p) => {
            trace!("receive_ndp_packet_inner: Received NDP NA");

            let target_address = p.message().target_address();
            match ND::ipv6_addr_state(ctx.state(), device_id, target_address) {
                AddressState::Tentative => {
                    duplicate_address_detected::<_, ND>(ctx, device_id, *target_address);
                    return;
                }
                AddressState::Assigned => {
                    // RFC 4862 says this situation is out of the scope, so we
                    // just log out the situation.
                    //
                    // TODO(ghanan): Signal to bindings that a duplicate address is detected?
                    error!("A duplicated address found when we are not in DAD process!");
                    return;
                }
                // Do nothing
                AddressState::Unassigned => {}
            }

            increment_counter!(ctx, "ndp::rx_neighbor_advertisement");
            let state = ND::get_ndp_state(ctx.state_mut(), device_id);
            match state.neighbors.get_neighbor_state_mut(&src_ip) {
                None => {
                    // If the neighbor is not in the cache, we just ignore the
                    // advertisement, as we're not interested in communicating
                    // with it.
                }
                Some(NeighborState { link_address }) if link_address.is_waiting() => {
                    if let Some(address) = get_target_link_layer_option::<ND, _>(p.body()) {
                        *link_address = LinkAddressResolutionValue::Known { address };
                        // Cancel the resolution timeout.
                        ctx.dispatcher.cancel_timeout(
                            NdpTimerId::new_link_address_resolution_timer_id::<ND>(
                                device_id, src_ip,
                            ),
                        );
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

    use net_types::ethernet::Mac;
    use net_types::ip::AddrSubnet;
    use packet::{Buf, Buffer, ParseBuffer};

    use crate::device::{
        ethernet::EthernetNdpDevice, get_ip_addr_subnet, is_in_ip_multicast, set_ip_addr_subnet,
    };
    use crate::ip::IPV6_MIN_MTU;
    use crate::testutil::{
        self, get_counter_val, get_dummy_config, set_logger_for_test, DummyEventDispatcher,
        DummyEventDispatcherBuilder, DummyNetwork,
    };
    use crate::wire::icmp::ndp::{OptionsSerializer, RouterAdvertisement, RouterSolicitation};
    use crate::wire::icmp::{IcmpEchoRequest, IcmpParseArgs, Icmpv6Packet};
    use crate::StackStateBuilder;

    const TEST_LOCAL_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_REMOTE_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);

    fn local_ip() -> Ipv6Addr {
        TEST_LOCAL_MAC.to_ipv6_link_local().get()
    }

    fn remote_ip() -> Ipv6Addr {
        TEST_REMOTE_MAC.to_ipv6_link_local().get()
    }

    #[test]
    fn test_send_neighbor_solicitation_on_cache_miss() {
        set_logger_for_test();
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let dev_id = ctx.state_mut().device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        // Now we have to manually assign the ip addresses, see `EthernetNdpDevice::get_ipv6_addr`
        set_ip_addr_subnet(&mut ctx, dev_id, AddrSubnet::new(local_ip(), 128).unwrap());

        lookup::<DummyEventDispatcher, EthernetNdpDevice>(&mut ctx, dev_id.id(), remote_ip());

        // Check that we send the original neighbor solicitation,
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
        let mut local = DummyEventDispatcherBuilder::default();
        local.add_device(TEST_LOCAL_MAC);
        let mut remote = DummyEventDispatcherBuilder::default();
        remote.add_device(TEST_REMOTE_MAC);
        let device_id = DeviceId::new_ethernet(0);

        let mut net = DummyNetwork::new(
            vec![("local", local.build()), ("remote", remote.build())].into_iter(),
            |ctx, dev| {
                if *ctx == "local" {
                    ("remote", device_id, None)
                } else {
                    ("local", device_id, None)
                }
            },
        );

        // let's try to ping the remote device from the local device:
        let req = IcmpEchoRequest::new(0, 0);
        let req_body = &[1, 2, 3, 4];
        let body = Buf::new(req_body.to_vec(), ..).encapsulate(
            IcmpPacketBuilder::<Ipv6, &[u8], _>::new(local_ip(), remote_ip(), IcmpUnusedCode, req),
        );
        // Manually assigning the addresses
        set_ip_addr_subnet(
            net.context("local"),
            device_id,
            AddrSubnet::new(local_ip(), 128).unwrap(),
        );
        set_ip_addr_subnet(
            net.context("remote"),
            device_id,
            AddrSubnet::new(remote_ip(), 128).unwrap(),
        );
        assert_eq!(net.context("local").dispatcher.frames_sent().len(), 0);
        assert_eq!(net.context("remote").dispatcher.frames_sent().len(), 0);

        crate::ip::send_ip_packet_from_device(
            net.context("local"),
            device_id,
            local_ip(),
            remote_ip(),
            remote_ip(),
            IpProto::Icmpv6,
            body,
            None,
        );
        // this should've triggered a neighbor solicitation to come out of local
        assert_eq!(net.context("local").dispatcher.frames_sent().len(), 1);
        // and a timer should've been started.
        assert_eq!(net.context("local").dispatcher.timer_events().count(), 1);

        net.step();

        assert_eq!(
            *net.context("remote").state().test_counters.get("ndp::rx_neighbor_solicitation"),
            1,
            "remote received solicitation"
        );
        assert_eq!(net.context("remote").dispatcher.frames_sent().len(), 1);

        // forward advertisement response back to local
        net.step();

        assert_eq!(
            *net.context("local").state().test_counters.get("ndp::rx_neighbor_advertisement"),
            1,
            "local received advertisement"
        );

        // at the end of the exchange, both sides should have each other on
        // their ndp tables:
        assert_eq!(
            EthernetNdpDevice::get_ndp_state::<_>(net.context("local").state_mut(), device_id.id())
                .neighbors
                .get_neighbor_state(&remote_ip())
                .unwrap()
                .link_address,
            LinkAddressResolutionValue::<Mac>::Known { address: TEST_REMOTE_MAC }
        );
        assert_eq!(
            EthernetNdpDevice::get_ndp_state::<_>(
                net.context("remote").state_mut(),
                device_id.id()
            )
            .neighbors
            .get_neighbor_state(&local_ip())
            .unwrap()
            .link_address,
            LinkAddressResolutionValue::<Mac>::Known { address: TEST_LOCAL_MAC }
        );
        // and the local timer should've been unscheduled:
        assert_eq!(net.context("local").dispatcher.timer_events().count(), 0);

        // upon link layer resolution, the original ping request should've been
        // sent out:
        assert_eq!(net.context("local").dispatcher.frames_sent().len(), 1);
        net.step();
        assert_eq!(
            *net.context("remote").state().test_counters.get("receive_icmp_packet::echo_request"),
            1
        );

        // TODO(brunodalbo): we should be able to verify that remote also sends
        //  back an echo reply, but we're having some trouble with IPv6 link
        //  local addresses.
    }

    #[test]
    fn test_dad_duplicate_address_detected_solicitation() {
        // Tests whether a duplicate address will get detected by solicitation
        // In this test, two nodes having the same MAC address will come up
        // at the same time. And both of them will use the EUI address. Each
        // of them should be able to detect each other is using the same address,
        // so they will both give up using that address.
        set_logger_for_test();
        let mac = Mac::new([1, 2, 3, 4, 5, 6]);
        let addr = AddrSubnet::new(mac.to_ipv6_link_local().get(), 128).unwrap();
        let multicast_addr = mac.to_ipv6_link_local().get().to_solicited_node_address();
        let mut local = DummyEventDispatcherBuilder::default();
        local.add_device(mac);
        let mut remote = DummyEventDispatcherBuilder::default();
        remote.add_device(mac);
        let device_id = DeviceId::new_ethernet(0);

        // We explicitly call `build_with` when building our contexts below because `build` will
        // set the default NDP parameter DUP_ADDR_DETECT_TRANSMITS to 0 (effectively disabling
        // DAD) so we use our own custom `StackStateBuilder` to set it to the default value
        // of `1` (see `DUP_ADDR_DETECT_TRANSMITS`).
        let mut net = DummyNetwork::new(
            vec![
                (
                    "local",
                    local.build_with(StackStateBuilder::default(), DummyEventDispatcher::default()),
                ),
                (
                    "remote",
                    remote
                        .build_with(StackStateBuilder::default(), DummyEventDispatcher::default()),
                ),
            ]
            .into_iter(),
            |ctx, dev| {
                if *ctx == "local" {
                    ("remote", device_id, None)
                } else {
                    ("local", device_id, None)
                }
            },
        );

        set_ip_addr_subnet(net.context("local"), device_id, addr);
        set_ip_addr_subnet(net.context("remote"), device_id, addr);
        assert_eq!(net.context("local").dispatcher.frames_sent().len(), 1);
        assert_eq!(net.context("remote").dispatcher.frames_sent().len(), 1);

        // Both devices should be in the solicited-node multicast group.
        assert!(is_in_ip_multicast(net.context("local"), device_id, multicast_addr));
        assert!(is_in_ip_multicast(net.context("remote"), device_id, multicast_addr));

        net.step();

        // they should now realize the address they intend to use has a duplicate
        // in the local network
        assert!(get_ip_addr_subnet::<_, Ipv6Addr>(net.context("local"), device_id).is_none());
        assert!(get_ip_addr_subnet::<_, Ipv6Addr>(net.context("remote"), device_id).is_none());

        // Both devices should not be in the multicast group
        assert!(!is_in_ip_multicast(net.context("local"), device_id, multicast_addr));
        assert!(!is_in_ip_multicast(net.context("remote"), device_id, multicast_addr));
    }

    #[test]
    fn test_dad_duplicate_address_detected_advertisement() {
        // Tests whether a duplicate address will get detected by advertisement
        // In this test, one of the node first assigned itself the local_ip(),
        // then the second node comes up and it should be able to find out that
        // it cannot use the address because someone else has already taken that
        // address.
        set_logger_for_test();
        let mut local = DummyEventDispatcherBuilder::default();
        local.add_device(TEST_LOCAL_MAC);
        let mut remote = DummyEventDispatcherBuilder::default();
        remote.add_device(TEST_REMOTE_MAC);
        let device_id = DeviceId::new_ethernet(0);

        // We explicitly call `build_with` when building our contexts below because `build` will
        // set the default NDP parameter DUP_ADDR_DETECT_TRANSMITS to 0 (effectively disabling
        // DAD) so we use our own custom `StackStateBuilder` to set it to the default value
        // of `1` (see `DUP_ADDR_DETECT_TRANSMITS`).
        let mut net = DummyNetwork::new(
            vec![
                (
                    "local",
                    local.build_with(StackStateBuilder::default(), DummyEventDispatcher::default()),
                ),
                (
                    "remote",
                    remote
                        .build_with(StackStateBuilder::default(), DummyEventDispatcher::default()),
                ),
            ]
            .into_iter(),
            |ctx, dev| {
                if *ctx == "local" {
                    ("remote", device_id, None)
                } else {
                    ("local", device_id, None)
                }
            },
        );

        println!("Setting new IP on local");

        let addr = AddrSubnet::new(local_ip(), 128).unwrap();
        let multicast_addr = local_ip().to_solicited_node_address();
        set_ip_addr_subnet(net.context("local"), device_id, addr);
        // Only local should be in the solicited node multicast group.
        assert!(is_in_ip_multicast(net.context("local"), device_id, multicast_addr));
        assert!(!is_in_ip_multicast(net.context("remote"), device_id, multicast_addr));
        assert!(testutil::trigger_next_timer(net.context("local")));

        let local_addr =
            EthernetNdpDevice::get_ipv6_addr(net.context("local").state_mut(), device_id.id())
                .unwrap();
        assert!(!local_addr.is_tentative());
        assert_eq!(local_ip(), local_addr.into_inner());

        println!("Set new IP on remote");

        set_ip_addr_subnet(net.context("remote"), device_id, addr);
        // local & remote should be in the multicast group.
        assert!(is_in_ip_multicast(net.context("local"), device_id, multicast_addr));
        assert!(is_in_ip_multicast(net.context("remote"), device_id, multicast_addr));

        net.step();

        let remote_addr = get_ip_addr_subnet::<_, Ipv6Addr>(net.context("remote"), device_id);
        assert!(remote_addr.is_none());
        // let's make sure that our local node still can use that address
        let local_addr =
            EthernetNdpDevice::get_ipv6_addr(net.context("local").state_mut(), device_id.id())
                .unwrap();
        assert!(!local_addr.is_tentative());
        assert_eq!(local_ip(), local_addr.into_inner());
        // Only local should be in the solicited node multicast group.
        assert!(is_in_ip_multicast(net.context("local"), device_id, multicast_addr));
        assert!(!is_in_ip_multicast(net.context("remote"), device_id, multicast_addr));
    }

    #[test]
    fn test_dad_set_ipv6_address_when_ongoing() {
        // Test that we can make our tentative address change when DAD is ongoing.

        // We explicitly call `build_with` when building our context below because `build` will
        // set the default NDP parameter DUP_ADDR_DETECT_TRANSMITS to 0 (effectively disabling
        // DAD) so we use our own custom `StackStateBuilder` to set it to the default value
        // of `1` (see `DUP_ADDR_DETECT_TRANSMITS`).
        let mut ctx = DummyEventDispatcherBuilder::default()
            .build_with(StackStateBuilder::default(), DummyEventDispatcher::default());
        let dev_id = ctx.state_mut().add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        set_ip_addr_subnet(&mut ctx, dev_id, AddrSubnet::new(local_ip(), 128).unwrap());
        assert_eq!(
            EthernetNdpDevice::get_ipv6_addr(ctx.state(), dev_id.id()).unwrap(),
            Tentative::new_tentative(local_ip())
        );
        set_ip_addr_subnet(&mut ctx, dev_id, AddrSubnet::new(remote_ip(), 128).unwrap());
        assert_eq!(
            EthernetNdpDevice::get_ipv6_addr(ctx.state(), dev_id.id()).unwrap(),
            Tentative::new_tentative(remote_ip())
        );
    }

    #[test]
    fn test_dad_three_transmits_no_conflicts() {
        let mut ctx = Context::with_default_state(DummyEventDispatcher::default());
        let dev_id = ctx.state_mut().add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        EthernetNdpDevice::get_ndp_state(&mut ctx.state_mut(), dev_id.id())
            .set_dad_transmits(NonZeroUsize::new(3));
        set_ip_addr_subnet(&mut ctx, dev_id, AddrSubnet::new(local_ip(), 128).unwrap());
        for i in 0..3 {
            testutil::trigger_next_timer(&mut ctx);
        }
        let addr = EthernetNdpDevice::get_ipv6_addr(ctx.state(), dev_id.id()).unwrap();
        assert_eq!(addr.try_into_permanent().unwrap(), local_ip());
    }

    #[test]
    fn test_dad_three_transmits_with_conflicts() {
        // test if the implementation is correct when we have more than 1
        // NS packets to send.
        set_logger_for_test();
        let mac = Mac::new([1, 2, 3, 4, 5, 6]);
        let mut local = DummyEventDispatcherBuilder::default();
        local.add_device(mac);
        let mut remote = DummyEventDispatcherBuilder::default();
        remote.add_device(mac);
        let device_id = DeviceId::new_ethernet(0);
        let mut net = DummyNetwork::new(
            vec![("local", local.build()), ("remote", remote.build())].into_iter(),
            |ctx, dev| {
                if *ctx == "local" {
                    ("remote", device_id, None)
                } else {
                    ("local", device_id, None)
                }
            },
        );

        EthernetNdpDevice::get_ndp_state(&mut net.context("local").state_mut(), device_id.id())
            .set_dad_transmits(NonZeroUsize::new(3));
        EthernetNdpDevice::get_ndp_state(&mut net.context("remote").state_mut(), device_id.id())
            .set_dad_transmits(NonZeroUsize::new(3));

        set_ip_addr_subnet(
            net.context("local"),
            device_id,
            AddrSubnet::new(mac.to_ipv6_link_local().get(), 128).unwrap(),
        );
        // during the first and second period, the remote host is still down.
        assert!(testutil::trigger_next_timer(net.context("local")));
        assert!(testutil::trigger_next_timer(net.context("local")));
        set_ip_addr_subnet(
            net.context("remote"),
            device_id,
            AddrSubnet::new(mac.to_ipv6_link_local().get(), 128).unwrap(),
        );
        // the local host should have sent out 3 packets while the remote one
        // should only have sent out 1.
        assert_eq!(net.context("local").dispatcher.frames_sent().len(), 3);
        assert_eq!(net.context("remote").dispatcher.frames_sent().len(), 1);

        net.step();

        // lets make sure that all timers are cancelled properly
        assert_eq!(net.context("local").dispatcher.timer_events().count(), 0);
        assert_eq!(net.context("remote").dispatcher.timer_events().count(), 0);

        // they should now realize the address they intend to use has a duplicate
        // in the local network
        assert!(get_ip_addr_subnet::<_, Ipv6Addr>(net.context("local"), device_id).is_none());
        assert!(get_ip_addr_subnet::<_, Ipv6Addr>(net.context("remote"), device_id).is_none());
    }

    #[test]
    fn test_receiving_router_solicitation_validity_check() {
        let config = get_dummy_config::<Ipv6Addr>();
        let src_ip = Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 10]);
        let src_mac = [10, 11, 12, 13, 14, 15];

        //
        // Test receiving NDP RS when not a router (should not receive)
        //

        let mut ctx = DummyEventDispatcherBuilder::from_config(config.clone())
            .build::<DummyEventDispatcher>();
        let device = Some(DeviceId::new_ethernet(0));
        let options = vec![NdpOption::SourceLinkLayerAddress(&src_mac[..])];
        let mut icmpv6_packet_buf = OptionsSerializer::new(options.iter())
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                config.local_ip,
                IcmpUnusedCode,
                RouterSolicitation::default(),
            ))
            .serialize_vec_outer()
            .unwrap();
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();

        receive_ndp_packet(&mut ctx, device, src_ip, config.local_ip, icmpv6_packet);
        assert_eq!(get_counter_val(&mut ctx, "ndp::rx_router_solicitation"), 0);

        //
        // Test receiving NDP RS as a router (should receive)
        //

        let mut state_builder = StackStateBuilder::default();
        state_builder.ip_builder().forward(true);
        let mut ctx = DummyEventDispatcherBuilder::from_config(config.clone())
            .build_with(state_builder, DummyEventDispatcher::default());
        icmpv6_packet_buf.reset();
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        receive_ndp_packet(&mut ctx, device, src_ip, config.local_ip, icmpv6_packet);
        assert_eq!(get_counter_val(&mut ctx, "ndp::rx_router_solicitation"), 1);

        //
        // Test receiving NDP RS as a router, but source is unspecified and the source
        // link layer option is included (should not receive)
        //

        let unspecified_source = Ipv6Addr::default();
        let mut icmpv6_packet_buf = OptionsSerializer::new(options.iter())
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                unspecified_source,
                config.local_ip,
                IcmpUnusedCode,
                RouterSolicitation::default(),
            ))
            .serialize_vec_outer()
            .unwrap();
        println!("{:?}", icmpv6_packet_buf);
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(
                unspecified_source,
                config.local_ip,
            ))
            .unwrap();
        receive_ndp_packet(&mut ctx, device, unspecified_source, config.local_ip, icmpv6_packet);
        assert_eq!(get_counter_val(&mut ctx, "ndp::rx_router_solicitation"), 1);
    }

    #[test]
    fn test_receiving_router_advertisement_validity_check() {
        let config = get_dummy_config::<Ipv6Addr>();
        let src_mac = [10, 11, 12, 13, 14, 15];
        let src_ip = Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 10]);
        let mut ctx = DummyEventDispatcherBuilder::from_config(config.clone())
            .build::<DummyEventDispatcher>();
        let device = Some(DeviceId::new_ethernet(0));

        //
        // Test receiving NDP RA where source ip is not a link local address (should not receive)
        //

        let mut icmpv6_packet_buf = Buf::new(Vec::new(), ..)
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                config.local_ip,
                IcmpUnusedCode,
                RouterAdvertisement::new(1, 2, 3, 4, 5),
            ))
            .serialize_vec_outer()
            .unwrap();
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        receive_ndp_packet(&mut ctx, device, src_ip, config.local_ip, icmpv6_packet);
        assert_eq!(get_counter_val(&mut ctx, "ndp::rx_router_advertisement"), 0);

        //
        // Test receiving NDP RA where source ip is a link local address (should receive)
        //

        let src_ip = Mac::new(src_mac).to_ipv6_link_local().get();
        let mut icmpv6_packet_buf = Buf::new(Vec::new(), ..)
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                config.local_ip,
                IcmpUnusedCode,
                RouterAdvertisement::new(1, 2, 3, 4, 5),
            ))
            .serialize_vec_outer()
            .unwrap();
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        receive_ndp_packet(&mut ctx, device, src_ip, config.local_ip, icmpv6_packet);
        assert_eq!(get_counter_val(&mut ctx, "ndp::rx_router_advertisement"), 1);
    }
}
