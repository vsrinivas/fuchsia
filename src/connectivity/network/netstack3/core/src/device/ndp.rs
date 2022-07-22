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

use alloc::collections::HashMap;
use core::{fmt::Debug, marker::PhantomData, num::NonZeroU8, time::Duration};

use assert_matches::assert_matches;
use log::{debug, trace};
use net_types::{
    ip::{Ip, Ipv6, Ipv6Addr, Ipv6Scope, Ipv6SourceAddr},
    LinkLocalAddress, LinkLocalUnicastAddr, MulticastAddr, MulticastAddress, ScopeableAddress,
    SpecifiedAddr, UnicastAddr, Witness,
};
use nonzero_ext::nonzero;
use packet::{EmptyBuf, InnerPacketBuilder, Serializer};
use packet_formats::{
    icmp::{
        ndp::{
            self,
            options::{NdpOption, NdpOptionBuilder},
            NdpPacket, NeighborAdvertisement, NeighborSolicitation, Options, RouterSolicitation,
        },
        IcmpMessage, IcmpPacket, IcmpPacketBuilder, IcmpUnusedCode,
    },
    ip::Ipv6Proto,
    ipv6::Ipv6PacketBuilder,
    utils::NonZeroDuration,
};
use rand::{thread_rng, Rng};
use zerocopy::ByteSlice;

use crate::{
    context::{CounterContext, StateContext, TimerContext},
    device::{
        link::{LinkAddress, LinkDevice},
        DeviceIdContext,
    },
    ip::device::state::{AddressState, IpDeviceState, SlaacConfig},
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
const REQUIRED_NDP_IP_PACKET_HOP_LIMIT: u8 = 255;

// Node Constants

/// The default value for the default hop limit to be used when sending IP
/// packets.
pub(crate) const HOP_LIMIT_DEFAULT: NonZeroU8 = nonzero!(64u8);

/// The default value for *BaseReachableTime* as defined in [RFC 4861 section
/// 10].
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
const REACHABLE_TIME_DEFAULT: Duration = Duration::from_secs(30);

/// The maximum number of multicast solicitations as defined in [RFC 4861
/// section 10].
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
const MAX_MULTICAST_SOLICIT: u8 = 3;

// NOTE(joshlf): The `LinkDevice` parameter may seem unnecessary. We only ever
// use the associated `Address` type, so why not just take that directly? By the
// same token, why have it as a parameter on `NdpState` and `NdpTimerId`? The
// answer is that, if we did, there would be no way to distinguish between
// different link device protocols that all happened to use the same hardware
// addressing scheme.
//
// Consider that the way that we implement context traits is via blanket impls.
// Even though each module's code _feels_ isolated from the rest of the system,
// in reality, all context impls end up on the same context type. In particular,
// all impls are of the form `impl<C: SomeContextTrait> SomeOtherContextTrait
// for C`. The `C` is the same throughout the whole stack.
//
// Thus, for two different link device protocols with the same hardware address
// type, if we used an `LinkAddress` parameter rather than a `LinkDevice`
// parameter, the `NdpContext` impls would conflict (in fact, the `StateContext`
// and `TimerContext` impls would conflict for similar reasons).

/// An NDP handler for NDP Events.
///
/// `NdpHandler<D>` is implemented for any type which implements
/// [`NdpContext<D>`], and it can also be mocked for use in testing.
pub(crate) trait NdpHandler<D: LinkDevice, C>: DeviceIdContext<D> {
    /// Cleans up state associated with the device.
    ///
    /// The contract is that after `deinitialize` is called, nothing else should
    /// be done with the state.
    fn deinitialize(&mut self, ctx: &mut C, device_id: Self::DeviceId);

    /// Look up the link layer address.
    ///
    /// Begins the address resolution process if the link layer address for
    /// `lookup_addr` is not already known.
    fn lookup(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        lookup_addr: UnicastAddr<Ipv6Addr>,
    ) -> Option<D::Address>;

    /// Handles a timer firing.
    fn handle_timer(&mut self, ctx: &mut C, id: NdpTimerId<D, Self::DeviceId>);

    /// Insert a neighbor to the known neighbors table.
    ///
    /// This method only gets called when testing to force a neighbor so link
    /// address lookups completes immediately without doing address resolution.
    #[cfg(test)]
    fn insert_static_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        net: UnicastAddr<Ipv6Addr>,
        hw: D::Address,
    );
}

impl<D: LinkDevice, C: NdpNonSyncContext<D, SC::DeviceId>, SC: NdpContext<D, C>> NdpHandler<D, C>
    for SC
where
    D::Address: for<'a> From<&'a MulticastAddr<Ipv6Addr>>,
{
    fn deinitialize(&mut self, ctx: &mut C, device_id: Self::DeviceId) {
        deinitialize(self, ctx, device_id)
    }

    fn lookup(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        lookup_addr: UnicastAddr<Ipv6Addr>,
    ) -> Option<D::Address> {
        lookup(self, ctx, device_id, lookup_addr)
    }

    fn handle_timer(&mut self, ctx: &mut C, id: NdpTimerId<D, Self::DeviceId>) {
        handle_timer(self, ctx, id)
    }

    #[cfg(test)]
    fn insert_static_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        net: UnicastAddr<Ipv6Addr>,
        hw: D::Address,
    ) {
        insert_neighbor(self, ctx, device_id, net, hw)
    }
}

/// The non-synchronized execution context for NDP.
pub(crate) trait NdpNonSyncContext<D: LinkDevice, DeviceId>:
    TimerContext<NdpTimerId<D, DeviceId>> + CounterContext
{
}
impl<DeviceId, D: LinkDevice, C: TimerContext<NdpTimerId<D, DeviceId>> + CounterContext>
    NdpNonSyncContext<D, DeviceId> for C
{
}

/// The execution context for an NDP device.
pub(crate) trait NdpContext<D: LinkDevice, C: NdpNonSyncContext<D, Self::DeviceId>>:
    Sized + DeviceIdContext<D> + StateContext<C, NdpState<D>, <Self as DeviceIdContext<D>>::DeviceId>
{
    /// Returns the NDP retransmission timer configured on the device.
    // TODO(https://fxbug.dev/72378): Remove this method once NUD operates in
    // L3.
    fn get_retrans_timer(&self, device_id: Self::DeviceId) -> NonZeroDuration;

    /// Get the link layer address for a device.
    fn get_link_layer_addr(&self, device_id: Self::DeviceId) -> UnicastAddr<D::Address>;

    /// Gets the IP state for this device.
    fn get_ip_device_state(&self, device_id: Self::DeviceId) -> &IpDeviceState<C::Instant, Ipv6>;

    /// Gets the IP state for this device mutably.
    fn get_ip_device_state_mut(
        &mut self,
        device_id: Self::DeviceId,
    ) -> &mut IpDeviceState<C::Instant, Ipv6>;

    /// Gets a non-tentative global or link-local address.
    ///
    /// Returns a non-tentative global address, if it is available. Otherwise,
    /// returns a link-local address, if it is available. Otherwise, returns
    /// `None`.
    fn get_non_tentative_global_or_link_local_addr(
        &self,
        device_id: Self::DeviceId,
    ) -> Option<UnicastAddr<Ipv6Addr>> {
        let mut non_tentative_addrs = self
            .get_ip_device_state(device_id)
            .iter_addrs()
            .filter(|entry| match entry.state {
                AddressState::Assigned => true,
                AddressState::Tentative { dad_transmits_remaining: _ } => false,
            })
            .map(|entry| entry.addr_sub().addr());

        non_tentative_addrs
            .clone()
            .find(|addr| addr.scope() == Ipv6Scope::Global)
            .or_else(|| non_tentative_addrs.find(|addr| addr.is_link_local()))
    }

    // TODO(joshlf): Use `FrameContext` instead.

    /// Send a packet in a device layer frame.
    ///
    /// `send_ipv6_frame` accepts a device ID, a next hop IP address, and a
    /// `Serializer`. Implementers must resolve the destination link-layer
    /// address from the provided `next_hop` IPv6 address.
    ///
    /// # Panics
    ///
    /// May panic if `device_id` is not initialized. See
    /// [`crate::device::testutil::enable_device`] for more information.
    fn send_ipv6_frame<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        next_hop: SpecifiedAddr<Ipv6Addr>,
        body: S,
    ) -> Result<(), S>;

    /// Notifies device layer that the link-layer address for the neighbor in
    /// `address` has been resolved to `link_address`.
    ///
    /// Implementers may use this signal to dispatch any packets that were
    /// queued waiting for address resolution.
    fn address_resolved(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        address: &UnicastAddr<Ipv6Addr>,
        link_address: D::Address,
    );

    /// Notifies the device layer that the link-layer address resolution for the
    /// neighbor in `address` failed.
    fn address_resolution_failed(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        address: &UnicastAddr<Ipv6Addr>,
    );

    /// Set Link MTU.
    ///
    /// `set_mtu` is used when a host receives a Router Advertisement with the
    /// MTU option.
    ///
    /// `set_mtu` MAY set the device's new MTU to a value less than `mtu` if the
    /// device does not support using `mtu` as its new MTU. `set_mtu` MUST NOT
    /// use a new MTU value that is greater than `mtu`.
    ///
    /// See [RFC 4861 section 6.3.4] for more information.
    ///
    /// # Panics
    ///
    /// `set_mtu` is allowed to panic if `mtu` is less than the IPv6 minimum
    /// link MTU, [`Ipv6::MINIMUM_LINK_MTU`].
    ///
    /// [RFC 4861 section 6.3.4]: https://tools.ietf.org/html/rfc4861#section-6.3.4
    fn set_mtu(&mut self, ctx: &mut C, device_id: Self::DeviceId, mtu: u32);

    /// Can `device_id` route IP packets not destined for it?
    ///
    /// If `is_router` returns `true`, we know that both the `device_id` and the
    /// netstack (`ctx`) have routing enabled; if `is_router` returns false,
    /// either `device_id` or the netstack (`ctx`) has routing disabled.
    fn is_router_device(&self, device_id: Self::DeviceId) -> bool {
        self.get_ip_device_state(device_id).routing_enabled
    }
}

fn deinitialize<D: LinkDevice, C: NdpNonSyncContext<D, SC::DeviceId>, SC: NdpContext<D, C>>(
    _sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
) {
    // Remove all timers associated with the device
    ctx.cancel_timers_with(|timer_id| timer_id.get_device_id() == device_id);
    // TODO(rheacock): Send any immediate packets, and potentially flag the
    // state as uninitialized?
}

/// The state associated with an instance of the Neighbor Discovery Protocol
/// (NDP).
///
/// Each device will contain an `NdpState` object to keep track of discovery
/// operations.
pub(crate) struct NdpState<D: LinkDevice> {
    //
    // NDP operation data structures.
    //
    /// List of neighbors.
    neighbors: NeighborTable<D::Address>,

    //
    // Interface parameters learned from Router Advertisements.
    //
    // See RFC 4861 section 6.3.2.
    //
    /// A base value used for computing the random `reachable_time` value.
    ///
    /// Default: `REACHABLE_TIME_DEFAULT`.
    ///
    /// See BaseReachableTime in [RFC 4861 section 6.3.2] for more details.
    ///
    /// [RFC 4861 section 6.3.2]: https://tools.ietf.org/html/rfc4861#section-6.3.2
    base_reachable_time: Duration,

    /// The time a neighbor is considered reachable after receiving a
    /// reachability confirmation.
    ///
    /// This value should be uniformly distributed between MIN_RANDOM_FACTOR
    /// (0.5) and MAX_RANDOM_FACTOR (1.5) times `base_reachable_time`
    /// milliseconds. A new random should be calculated when
    /// `base_reachable_time` changes (due to Router Advertisements) or at least
    /// every few hours even if no Router Advertisements are received.
    ///
    /// See ReachableTime in [RFC 4861 section 6.3.2] for more details.
    ///
    /// [RFC 4861 section 6.3.2]: https://tools.ietf.org/html/rfc4861#section-6.3.2
    // TODO(fxbug.dev/69490): Remove this or explain why it's here.
    #[allow(dead_code)]
    reachable_time: Duration,
}

impl<D: LinkDevice> NdpState<D> {
    pub(crate) fn new() -> Self {
        let mut ret = Self {
            neighbors: NeighborTable::default(),

            base_reachable_time: REACHABLE_TIME_DEFAULT,
            reachable_time: REACHABLE_TIME_DEFAULT,
        };

        // Calculate an actually random `reachable_time` value instead of using
        // a constant.
        ret.recalculate_reachable_time();

        ret
    }

    // Interface parameters learned from Router Advertisements.

    /// Set the base value used for computing the random `reachable_time` value.
    ///
    /// This method will also recalculate the `reachable_time` if the new base
    /// value is different from the current value. If the new base value is the
    /// same as the current value, `set_base_reachable_time` does nothing.
    pub(crate) fn set_base_reachable_time(&mut self, v: Duration) {
        assert_ne!(Duration::new(0, 0), v);

        if self.base_reachable_time == v {
            return;
        }

        self.base_reachable_time = v;

        self.recalculate_reachable_time();
    }

    /// Recalculate `reachable_time`.
    ///
    /// The new `reachable_time` will be a random value between a factor of
    /// MIN_RANDOM_FACTOR and MAX_RANDOM_FACTOR, as per [RFC 4861 section
    /// 6.3.2].
    ///
    /// [RFC 4861 section 6.3.2]: https://tools.ietf.org/html/rfc4861#section-6.3.2
    pub(crate) fn recalculate_reachable_time(&mut self) {
        let base = self.base_reachable_time;
        let half = base / 2;
        let reachable_time = half + thread_rng().gen_range(Duration::new(0, 0)..base);

        // Random value must between a factor of MIN_RANDOM_FACTOR (0.5) and
        // MAX_RANDOM_FACTOR (1.5), as per RFC 4861 section 6.3.2.
        assert!((reachable_time >= half) && (reachable_time <= (base + half)));

        self.reachable_time = reachable_time;
    }
}

/// The identifier for timer events in NDP operations.
#[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
pub(crate) struct NdpTimerId<D: LinkDevice, DeviceId> {
    device_id: DeviceId,
    inner: InnerNdpTimerId,
    _marker: PhantomData<D>,
}

/// The types of NDP timers.
#[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
pub(crate) enum InnerNdpTimerId {
    /// This is used to retry sending Neighbor Discovery Protocol requests.
    LinkAddressResolution { neighbor_addr: UnicastAddr<Ipv6Addr> },
}

impl<D: LinkDevice, DeviceId: Copy> NdpTimerId<D, DeviceId> {
    fn new(device_id: DeviceId, inner: InnerNdpTimerId) -> NdpTimerId<D, DeviceId> {
        NdpTimerId { device_id, inner, _marker: PhantomData }
    }

    /// Creates a new `NdpTimerId` wrapped inside a `TimerId` with the provided
    /// `device_id` and `neighbor_addr`.
    pub(crate) fn new_link_address_resolution(
        device_id: DeviceId,
        neighbor_addr: UnicastAddr<Ipv6Addr>,
    ) -> NdpTimerId<D, DeviceId> {
        NdpTimerId::new(device_id, InnerNdpTimerId::LinkAddressResolution { neighbor_addr })
    }

    pub(crate) fn get_device_id(&self) -> DeviceId {
        self.device_id
    }
}

fn handle_timer<D: LinkDevice, C: NdpNonSyncContext<D, SC::DeviceId>, SC: NdpContext<D, C>>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: NdpTimerId<D, SC::DeviceId>,
) {
    match id.inner {
        InnerNdpTimerId::LinkAddressResolution { neighbor_addr } => {
            let ndp_state = sync_ctx.get_state_mut_with(id.device_id);
            if let Some(NeighborState {
                state: NeighborEntryState::Incomplete { transmit_counter },
                ..
            }) = ndp_state.neighbors.get_neighbor_state_mut(&neighbor_addr)
            {
                if *transmit_counter < MAX_MULTICAST_SOLICIT {
                    // Increase the transmit counter and send the solicitation
                    // again
                    *transmit_counter += 1;
                    send_neighbor_solicitation(sync_ctx, ctx, id.device_id, neighbor_addr);

                    let retrans_timer = sync_ctx.get_retrans_timer(id.device_id);
                    let _: Option<C::Instant> = ctx.schedule_timer(
                        retrans_timer.get(),
                        NdpTimerId::new_link_address_resolution(id.device_id, neighbor_addr).into(),
                    );
                } else {
                    // To make sure we don't get stuck in this neighbor
                    // unreachable state forever, remove the neighbor from the
                    // database:
                    ndp_state.neighbors.delete_neighbor_state(&neighbor_addr);
                    ctx.increment_counter("ndp::neighbor_solicitation_timer");

                    sync_ctx.address_resolution_failed(ctx, id.device_id, &neighbor_addr);
                }
            } else {
                unreachable!("handle_timer: timer for neighbor {:?} address resolution should not exist if no entry exists", neighbor_addr);
            }
        }
    }
}

fn lookup<D: LinkDevice, C: NdpNonSyncContext<D, SC::DeviceId>, SC: NdpContext<D, C>>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    lookup_addr: UnicastAddr<Ipv6Addr>,
) -> Option<D::Address>
where
    D::Address: for<'a> From<&'a MulticastAddr<Ipv6Addr>>,
{
    trace!("ndp::lookup: {:?}", lookup_addr);

    // TODO(brunodalbo): Figure out what to do if a frame can't be sent
    let ndpstate = sync_ctx.get_state_mut_with(device_id);
    let result = ndpstate.neighbors.get_neighbor_state(&lookup_addr);

    match result {
        // TODO(ghanan): As long as have ever received a link layer address for
        //               `lookup_addr` from any NDP packet with the source link
        //               layer option, we would have stored that address. Here
        //               we simply return that address without checking the
        //               actual state of the neighbor entry. We should make sure
        //               that the entry is not Stale before returning the
        //               address. If it is stale, we should make sure it is
        //               reachable first. See RFC 4861 section 7.3.2 for more
        //               information.
        Some(NeighborState { link_address: Some(address), .. }) => Some(*address),

        // We do not know about the neighbor and need to start address
        // resolution.
        None => {
            trace!("ndp::lookup: starting address resolution process for {:?}", lookup_addr);

            // If we're not already waiting for a neighbor solicitation
            // response, mark it as Incomplete and send a neighbor solicitation,
            // also setting the transmission count to 1.
            ndpstate.neighbors.add_incomplete_neighbor_state(lookup_addr);

            send_neighbor_solicitation(sync_ctx, ctx, device_id, lookup_addr);

            // Also schedule a timer to retransmit in case we don't get neighbor
            // advertisements back.
            let retrans_timer = sync_ctx.get_retrans_timer(device_id);
            let _: Option<C::Instant> = ctx.schedule_timer(
                retrans_timer.get(),
                NdpTimerId::new_link_address_resolution(device_id, lookup_addr).into(),
            );

            // Returning `None` as we do not have a link-layer address to give
            // yet.
            None
        }

        // Address resolution is currently in progress.
        Some(NeighborState { state: NeighborEntryState::Incomplete { .. }, .. }) => {
            trace!(
                "ndp::lookup: still waiting for address resolution to complete for {:?}",
                lookup_addr
            );
            None
        }

        // TODO(ghanan): Handle case where a neighbor entry exists for a
        //               `link_addr` but no link address as been discovered.
        _ => unimplemented!("A neighbor entry exists but no link address is discovered"),
    }
}

#[cfg(test)]
fn insert_neighbor<D: LinkDevice, C: NdpNonSyncContext<D, SC::DeviceId>, SC: NdpContext<D, C>>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    device_id: SC::DeviceId,
    net: UnicastAddr<Ipv6Addr>,
    hw: D::Address,
) {
    // Neighbor `net` should be marked as reachable.
    sync_ctx.get_state_mut_with(device_id).neighbors.set_link_address(net, hw, true)
}

/// `NeighborState` keeps all state that NDP may want to keep about neighbors,
/// like link address resolution and reachability information, for example.
#[cfg_attr(test, derive(Debug, Eq, PartialEq))]
struct NeighborState<H> {
    state: NeighborEntryState,
    link_address: Option<H>,
}

impl<H> NeighborState<H> {
    fn new() -> Self {
        Self { state: NeighborEntryState::Incomplete { transmit_counter: 0 }, link_address: None }
    }

    /// Is the neighbor incomplete (waiting for address resolution)?
    fn is_incomplete(&self) -> bool {
        if let NeighborEntryState::Incomplete { .. } = self.state {
            true
        } else {
            false
        }
    }

    /// Is the neighbor reachable?
    fn is_reachable(&self) -> bool {
        self.state == NeighborEntryState::Reachable
    }
}

/// The various states a Neighbor cache entry can be in.
///
/// See [RFC 4861 section 7.3.2].
///
/// [RFC 4861 section 7.3.2]: https://tools.ietf.org/html/rfc4861#section-7.3.2
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
enum NeighborEntryState {
    /// Address resolution is being performed on the entry. Specifically, a
    /// Neighbor Solicitation has been sent to the solicited-node multicast
    /// address of the target, but the corresponding Neighbor Advertisement has
    /// not yet been received.
    ///
    /// `transmit_counter` is the count of Neighbor Solicitation messages sent
    /// as part of the Address resolution process.
    Incomplete { transmit_counter: u8 },

    /// Positive confirmation was received within the last ReachableTime
    /// milliseconds that the forward path to the neighbor was functioning
    /// properly.  While `Reachable`, no special action takes place as packets
    /// are sent.
    Reachable,

    /// More than ReachableTime milliseconds have elapsed since the last
    /// positive confirmation was received that the forward path was functioning
    /// properly.  While stale, no action takes place until a packet is sent.
    ///
    /// The `Stale` state is entered upon receiving an unsolicited Neighbor
    /// Discovery message that updates the cached link-layer address.  Receipt
    /// of such a message does not confirm reachability, and entering the
    /// `Stale` state ensures reachability is verified quickly if the entry is
    /// actually being used.  However, reachability is not actually verified
    /// until the entry is actually used.
    Stale,

    /// More than ReachableTime milliseconds have elapsed since the last
    /// positive confirmation was received that the forward path was functioning
    /// properly, and a packet was sent within the last DELAY_FIRST_PROBE_TIME
    /// seconds.  If no reachability confirmation is received within
    /// DELAY_FIRST_PROBE_TIME seconds of entering the DELAY state, send a
    /// Neighbor Solicitation and change the state to PROBE.
    ///
    /// The DELAY state is an optimization that gives upper- layer protocols
    /// additional time to provide reachability confirmation in those cases
    /// where ReachableTime milliseconds have passed since the last confirmation
    /// due to lack of recent traffic.  Without this optimization, the opening
    /// of a TCP connection after a traffic lull would initiate probes even
    /// though the subsequent three-way handshake would provide a reachability
    /// confirmation almost immediately.
    _Delay,

    /// A reachability confirmation is actively sought by retransmitting
    /// Neighbor Solicitations every RetransTimer milliseconds until a
    /// reachability confirmation is received.
    _Probe,
}

struct NeighborTable<H> {
    table: HashMap<UnicastAddr<Ipv6Addr>, NeighborState<H>>,
}

impl<H: PartialEq + Debug> NeighborTable<H> {
    /// Sets the link address for a neighbor.
    ///
    /// If `is_reachable` is `true`, the state of the neighbor will be set to
    /// `NeighborEntryState::Reachable`. Otherwise, it will be set to
    /// `NeighborEntryState::Stale` if the address was updated. A `false` value
    /// for `is_reachable` does not mean that the neighbor is unreachable, it
    /// just means that we do not know if it is reachable.
    fn set_link_address(
        &mut self,
        neighbor: UnicastAddr<Ipv6Addr>,
        address: H,
        is_reachable: bool,
    ) {
        let address = Some(address);
        let neighbor_state = self.table.entry(neighbor).or_insert_with(NeighborState::new);

        trace!("set_link_address: setting link address for neighbor {:?} to address", address);

        if is_reachable {
            trace!("set_link_address: reachability is known, so setting state for neighbor {:?} to Reachable", neighbor);

            neighbor_state.state = NeighborEntryState::Reachable;
        } else if neighbor_state.link_address != address {
            trace!("set_link_address: new link addr different from old and reachability is unknown, so setting state for neighbor {:?} to Stale", neighbor);

            neighbor_state.state = NeighborEntryState::Stale;
        }

        neighbor_state.link_address = address;
    }
}

impl<H> NeighborTable<H> {
    /// Create a new incomplete state of a neighbor, setting the transmit
    /// counter to 1.
    fn add_incomplete_neighbor_state(&mut self, neighbor: UnicastAddr<Ipv6Addr>) {
        let mut state = NeighborState::new();
        state.state = NeighborEntryState::Incomplete { transmit_counter: 1 };

        let _: Option<_> = self.table.insert(neighbor, state);
    }

    /// Get the neighbor's state, if it exists.
    fn get_neighbor_state(&self, neighbor: &UnicastAddr<Ipv6Addr>) -> Option<&NeighborState<H>> {
        self.table.get(neighbor)
    }

    /// Get a  the neighbor's mutable state, if it exists.
    fn get_neighbor_state_mut(
        &mut self,
        neighbor: &UnicastAddr<Ipv6Addr>,
    ) -> Option<&mut NeighborState<H>> {
        self.table.get_mut(neighbor)
    }

    /// Delete the neighbor's state, if it exists.
    fn delete_neighbor_state(&mut self, neighbor: &UnicastAddr<Ipv6Addr>) {
        let _: Option<_> = self.table.remove(neighbor);
    }
}

impl<H> Default for NeighborTable<H> {
    fn default() -> Self {
        NeighborTable { table: HashMap::default() }
    }
}

fn send_neighbor_solicitation<
    D: LinkDevice,
    C: NdpNonSyncContext<D, SC::DeviceId>,
    SC: NdpContext<D, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    lookup_addr: UnicastAddr<Ipv6Addr>,
) {
    trace!("send_neighbor_solicitation: lookup_addr {:?}", lookup_addr);

    // TODO(brunodalbo) when we send neighbor solicitations, we SHOULD set the
    //  source IP to the same IP as the packet that triggered the solicitation,
    //  so that when we hit the neighbor they'll have us in their cache,
    //  reducing overall burden on the network.
    if let Some(src_ip) = sync_ctx.get_non_tentative_global_or_link_local_addr(device_id) {
        assert!(src_ip.is_valid_unicast());
        let src_ll = sync_ctx.get_link_layer_addr(device_id);
        let dst_ip = lookup_addr.to_solicited_node_address();
        // TODO(https://fxbug.dev/85055): Either panic or guarantee that this
        // error can't happen statically?
        let _ = send_ndp_packet::<_, _, _, &[u8], _>(
            sync_ctx,
            ctx,
            device_id,
            src_ip.get(),
            dst_ip.into_specified(),
            NeighborSolicitation::new(lookup_addr.get()),
            &[NdpOptionBuilder::SourceLinkLayerAddress(src_ll.bytes())],
        );
    } else {
        // Nothing can be done if we don't have any ipv6 addresses to send
        // packets out to.
        debug!("Not sending NDP request, since we don't know our IPv6 address");
    }
}

fn send_neighbor_advertisement<
    D: LinkDevice,
    C: NdpNonSyncContext<D, SC::DeviceId>,
    SC: NdpContext<D, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    solicited: bool,
    device_addr: SpecifiedAddr<Ipv6Addr>,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
) {
    debug!("send_neighbor_advertisement from {:?} to {:?}", device_addr, dst_ip);
    debug_assert!(device_addr.is_valid_unicast());
    // We currently only allow the destination address to be:
    // 1) a unicast address.
    // 2) a multicast destination but the message should be a unsolicited
    //    neighbor advertisement.
    // NOTE: this assertion may need change if more messages are to be allowed in the future.
    debug_assert!(dst_ip.is_valid_unicast() || (!solicited && dst_ip.is_multicast()));

    // We must call into the higher level send_ndp_packet function because it is
    // not guaranteed that we have actually saved the link layer address of the
    // destination IP. Typically, the solicitation request will carry that
    // information, but it is not necessary. So it is perfectly valid that
    // trying to send this advertisement will end up triggering a neighbor
    // solicitation to be sent.
    let src_ll = sync_ctx.get_link_layer_addr(device_id);
    // TODO(https://fxbug.dev/85055): Either panic or guarantee that this error
    // can't happen statically?
    let device_addr = device_addr.get();
    let is_router_device = sync_ctx.is_router_device(device_id);
    let _ = send_ndp_packet::<_, _, _, &[u8], _>(
        sync_ctx,
        ctx,
        device_id,
        device_addr,
        dst_ip,
        NeighborAdvertisement::new(is_router_device, solicited, false, device_addr),
        &[NdpOptionBuilder::TargetLinkLayerAddress(src_ll.bytes())],
    );
}

/// Helper function to send MTU packet over an NdpDevice to `dst_ip`.
// TODO(https://fxbug.dev/85055): Is it possible to guarantee that some types of
// errors don't happen?
pub(super) fn send_ndp_packet<
    D: LinkDevice,
    C: NdpNonSyncContext<D, SC::DeviceId>,
    SC: NdpContext<D, C>,
    B: ByteSlice,
    M,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    src_ip: Ipv6Addr,
    dst_ip: SpecifiedAddr<Ipv6Addr>,
    message: M,
    options: &[NdpOptionBuilder<'_>],
) -> Result<(), ()>
where
    M: IcmpMessage<Ipv6, B, Code = IcmpUnusedCode>,
{
    trace!("send_ndp_packet: src_ip={:?} dst_ip={:?}", src_ip, dst_ip);

    sync_ctx
        .send_ipv6_frame(
            ctx,
            device_id,
            dst_ip,
            ndp::OptionSequenceBuilder::new(options.iter())
                .into_serializer()
                .encapsulate(IcmpPacketBuilder::<Ipv6, B, M>::new(
                    src_ip,
                    dst_ip,
                    IcmpUnusedCode,
                    message,
                ))
                .encapsulate(Ipv6PacketBuilder::new(
                    src_ip,
                    dst_ip,
                    REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                    Ipv6Proto::Icmpv6,
                )),
        )
        .map_err(|_| ())
}

/// A handler for incoming NDP packets.
///
/// An implementation of `NdpPacketHandler` is provided by the device layer (see
/// the `crate::device` module) to the IP layer so that it can pass incoming NDP
/// packets. It can also be mocked for use in testing.
pub(crate) trait NdpPacketHandler<C, DeviceId> {
    /// Receive an NDP packet.
    fn receive_ndp_packet<B: ByteSlice>(
        &mut self,
        ctx: &mut C,
        device: DeviceId,
        src_ip: Ipv6SourceAddr,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: NdpPacket<B>,
    );
}

pub(crate) fn receive_ndp_packet<
    D: LinkDevice,
    C: NdpNonSyncContext<D, SC::DeviceId>,
    SC: NdpContext<D, C>,
    B,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    src_ip: Ipv6SourceAddr,
    _dst_ip: SpecifiedAddr<Ipv6Addr>,
    packet: NdpPacket<B>,
) where
    B: ByteSlice,
{
    // TODO(ghanan): Make sure the IP packet's hop limit was set to 255 as per
    //               RFC 4861 sections 4.1, 4.2, 4.3, 4.4, and 4.5 (each type of
    //               NDP packet).

    match packet {
        NdpPacket::RouterSolicitation(p) => {
            let _: IcmpPacket<Ipv6, B, RouterSolicitation> = p;

            trace!("receive_ndp_packet: Received NDP RS");

            if !sync_ctx.is_router_device(device_id) {
                // Hosts MUST silently discard Router Solicitation messages as
                // per RFC 4861 section 6.1.1.
                trace!(
                    "receive_ndp_packet: device {:?} is not a router, discarding NDP RS",
                    device_id
                );
                return;
            }
        }
        NdpPacket::RouterAdvertisement(p) => {
            // Note that some parts of RFC 4861 w.r.t RAs are handled elsewhere.
            //
            // TODO(https://fxbug.dev/93817): Pull SLAAC handling into IP
            // so this module doesn't handle RAs at all.

            // Nodes MUST silently discard any received Router Advertisement
            // message where the IP source address is not a link-local
            // address as routers must use their link-local address as the
            // source for Router Advertisements so hosts can uniquely
            // identify routers, as per RFC 4861 section 6.1.2.
            let src_ip = match match src_ip {
                Ipv6SourceAddr::Unicast(ip) => LinkLocalUnicastAddr::new(ip),
                Ipv6SourceAddr::Unspecified => None,
            } {
                Some(ip) => {
                    trace!("receive_ndp_packet: NDP RA source={:?}", ip);
                    ip
                }
                None => {
                    trace!(
                        "receive_ndp_packet: NDP RA source={:?} is not link-local; discarding",
                        src_ip
                    );
                    return;
                }
            };

            // TODO(ghanan): Make sure IP's hop limit is set to 255 as per RFC
            // 4861 section 6.1.2.

            ctx.increment_counter("ndp::rx_router_advertisement");

            if sync_ctx.is_router_device(device_id) {
                trace!("receive_ndp_packet: received NDP RA as a router, discarding NDP RA");
                return;
            }

            let ra = p.message();

            // Borrow again so that a) we shadow the original `ndp_state` and
            // thus, b) the original is dropped before `ctx` is used mutably in
            // various code above (namely, to schedule timers). Now that all of
            // that mutation has happened, we can borrow `ctx` mutably again and
            // not run afoul of the borrow checker.
            let ndp_state = sync_ctx.get_state_mut_with(device_id);

            // As per RFC 4861 section 6.3.4:
            // If the received Reachable Time value is specified, the host
            // SHOULD set its BaseReachableTime variable to the received value.
            // If the new value differs from the previous value, the host SHOULD
            // re-compute a new random ReachableTime value.
            //
            // TODO(ghanan): Make the updating of this field from the RA message
            //               configurable since the RFC does not say we MUST
            //               update the field.
            //
            // TODO(ghanan): In most cases, the advertised Reachable Time value
            //               will be the same in consecutive Router
            //               Advertisements, and a host's BaseReachableTime
            //               rarely changes.  In such cases, an implementation
            //               SHOULD ensure that a new random value gets
            //               re-computed at least once every few hours.
            if let Some(base_reachable_time) = ra.reachable_time() {
                trace!("receive_ndp_packet: NDP RA: updating base_reachable_time to {:?} for router: {:?}", base_reachable_time, src_ip);
                ndp_state.set_base_reachable_time(base_reachable_time.get());
            }

            // As per RFC 4861 section 6.3.4:
            // If the received Cur Hop Limit value is specified, the host SHOULD
            // set its CurHopLimit variable to the received value.
            //
            // TODO(ghanan): Make the updating of this field from the RA message
            //               configurable since the RFC does not say we MUST
            //               update the field.
            if let Some(hop_limit) = ra.current_hop_limit() {
                trace!("receive_ndp_packet: NDP RA: updating device's hop limit to {:?} for router: {:?}", ra.current_hop_limit(), src_ip);

                sync_ctx.get_ip_device_state_mut(device_id).default_hop_limit = hop_limit;
            }

            for option in p.body().iter() {
                match option {
                    // As per RFC 4861 section 6.3.4, if a Neighbor Cache entry
                    // is created for the router, its reachability state MUST be
                    // set to STALE as specified in Section 7.3.3.  If a cache
                    // entry already exists and is updated with a different
                    // link-layer address, the reachability state MUST also be
                    // set to STALE.
                    //
                    // TODO(ghanan): Mark NDP state as STALE as per the RFC once
                    //               we implement the RFC compliant states.
                    NdpOption::SourceLinkLayerAddress(a) => {
                        let ndp_state = sync_ctx.get_state_mut_with(device_id);
                        let link_addr = D::Address::from_bytes(&a[..D::Address::BYTES_LENGTH]);

                        trace!("receive_ndp_packet: NDP RA: setting link address for router {:?} to {:?}", src_ip, link_addr);

                        // Set the link address and mark it as stale if we
                        // either created the neighbor entry, or updated an
                        // existing one.
                        ndp_state.neighbors.set_link_address(src_ip.get(), link_addr, false);
                    }
                    NdpOption::Mtu(mtu) => {
                        trace!("receive_ndp_packet: mtu option with mtu = {:?}", mtu);

                        // TODO(ghanan): Make updating the MTU from an RA
                        // message configurable.
                        if mtu >= Ipv6::MINIMUM_LINK_MTU.into() {
                            // `set_mtu` may panic if `mtu` is less than
                            // `MINIMUM_LINK_MTU` but we just checked to make
                            // sure that `mtu` is at least `MINIMUM_LINK_MTU` so
                            // we know `set_mtu` will not panic.
                            sync_ctx.set_mtu(ctx, device_id, mtu);
                        } else {
                            trace!("receive_ndp_packet: NDP RA: not setting link MTU (from {:?}) to {:?} as it is less than Ipv6::MINIMUM_LINK_MTU", src_ip, mtu);
                        }
                    }
                    // These are handled elsewhere.
                    //
                    // TODO(https://fxbub.dev/99830): Move all of NDP handling
                    // to IP.
                    NdpOption::TargetLinkLayerAddress(_)
                    | NdpOption::RedirectedHeader { .. }
                    | NdpOption::RecursiveDnsServer(_)
                    | NdpOption::RouteInformation(_)
                    | NdpOption::PrefixInformation(_) => {}
                }
            }
        }
        NdpPacket::NeighborSolicitation(p) => {
            let target_address = p.message().target_address();
            let target_address = match UnicastAddr::new(*target_address) {
                Some(addr) => {
                    trace!("receive_ndp_packet: NDP NS target={:?}", addr);
                    addr
                }
                None => {
                    trace!(
                        "receive_ndp_packet: NDP NS target={:?} is not unicast; discarding",
                        target_address
                    );
                    return;
                }
            };

            // At this point, we guarantee the following is true because of the
            // earlier checks (with 2 & 3 being done in IP):
            //
            //   1) The target address is a valid unicast address.
            //   2) The target address is an address that is on our device,
            //      `device_id`.
            //   3) The target address is not tentative.
            //
            // TODO(https://fxbub.dev/99830): Move all of NDP handling
            // to IP.
            ctx.increment_counter("ndp::rx_neighbor_solicitation");

            // If we have a source link layer address option, we take it and
            // save to our cache.
            if let Ipv6SourceAddr::Unicast(src_ip) = src_ip {
                // We only update the cache if it is not from an unspecified
                // address, i.e., it is not a DAD message. (RFC 4861)
                if let Some(ll) = get_source_link_layer_option(p.body()) {
                    trace!("receive_ndp_packet: Received NDP NS from {:?} has source link layer option w/ link address {:?}", src_ip, ll);

                    // Set the link address and mark it as stale if we either
                    // create the neighbor entry, or updated an existing one, as
                    // per RFC 4861 section 7.2.3.
                    sync_ctx
                        .get_state_mut_with(device_id)
                        .neighbors
                        .set_link_address(src_ip, ll, false);
                }

                trace!(
                    "receive_ndp_packet: Received NDP NS: sending NA to source of NS {:?}",
                    src_ip
                );

                // Finally we ought to reply to the Neighbor Solicitation with a
                // Neighbor Advertisement.
                //
                // TODO(https://fxbug.dev/99830): Move NUD to IP.
                send_neighbor_advertisement(
                    sync_ctx,
                    ctx,
                    device_id,
                    true,
                    target_address.into_specified(),
                    src_ip.into_specified(),
                );
            } else {
                // TODO(https://fxbub.dev/99830): Move all of NDP handling
                // to IP.
                unreachable!("Handled by caller")
            }
        }
        NdpPacket::NeighborAdvertisement(p) => {
            let message = p.message();
            let target_address = p.message().target_address();

            let src_ip = match src_ip {
                Ipv6SourceAddr::Unicast(src_ip) => {
                    trace!(
                        "receive_ndp_packet: NDP NA source={:?} target={:?}",
                        src_ip,
                        target_address
                    );
                    src_ip
                }
                Ipv6SourceAddr::Unspecified => {
                    trace!("receive_ndp_packet: NDP NA source={:?} target={:?}; source is not specified; discarding", src_ip, target_address);
                    return;
                }
            };

            ctx.increment_counter("ndp::rx_neighbor_advertisement");

            let ndp_state = sync_ctx.get_state_mut_with(device_id);

            // TODO(https://fxbug.dev/99830): Move NUD to IP.
            let neighbor_state = if let Some(state) =
                ndp_state.neighbors.get_neighbor_state_mut(&src_ip)
            {
                state
            } else {
                // If the neighbor is not in the cache, we just ignore the
                // advertisement, as we're not yet interested in communicating
                // with it, as per RFC 4861 section 7.2.5.
                trace!("receive_ndp_packet: Ignoring NDP NA from {:?} does not already exist in our list of neighbors, so discarding", src_ip);
                return;
            };

            let target_ll = get_target_link_layer_option(p.body());

            if neighbor_state.is_incomplete() {
                // If we are in the Incomplete state, we should not have ever
                // learned about a link-layer address.
                assert_eq!(neighbor_state.link_address, None);

                if let Some(address) = target_ll {
                    // Record the link-layer address.
                    //
                    // If the advertisement's Solicited flag is set, the state
                    // of the entry is set to REACHABLE; otherwise, it is set to
                    // STALE, as per RFC 4861 section 7.2.5.
                    //
                    // Note, since the neighbor's link address was `None`
                    // before, we will definitely update the address, so the
                    // state will be set to STALE if the solicited flag is
                    // unset.
                    trace!(
                        "receive_ndp_packet: Resolving link address of {:?} to {:?}",
                        src_ip,
                        address
                    );
                    ndp_state.neighbors.set_link_address(src_ip, address, message.solicited_flag());

                    // Cancel the resolution timeout.
                    let _: Option<C::Instant> = ctx.cancel_timer(
                        NdpTimerId::new_link_address_resolution(device_id, src_ip).into(),
                    );

                    // Send any packets queued for the neighbor awaiting address
                    // resolution.
                    sync_ctx.address_resolved(ctx, device_id, &src_ip, address);
                } else {
                    trace!("receive_ndp_packet: Performing address resolution but the NDP NA from {:?} does not have a target link layer address option, so discarding", src_ip);
                    return;
                }

                return;
            }

            // If we are not in the Incomplete state, we should have (at some
            // point) learned about a link-layer address.
            assert_matches!(neighbor_state.link_address, Some(_));

            if !message.override_flag() {
                // As per RFC 4861 section 7.2.5:
                //
                // If the Override flag is clear and the supplied link-layer
                // address differs from that in the cache, then one of two
                // actions takes places:
                //
                // a) If the state of the entry is REACHABLE, set it to STALE,
                //    but do not update the entry in any other way.
                //
                // b) Otherwise, the received advertisement should be ignored
                //    and MUST NOT update cache.
                if target_ll.map_or(false, |x| neighbor_state.link_address != Some(x)) {
                    if neighbor_state.is_reachable() {
                        trace!("receive_ndp_packet: NDP RS from known reachable neighbor {:?} does not have override set, but supplied link addr is different, setting state to stale", src_ip);
                        neighbor_state.state = NeighborEntryState::Stale;
                    } else {
                        trace!("receive_ndp_packet: NDP RS from known neighbor {:?} (with reachability unknown) does not have override set, but supplied link addr is different, ignoring", src_ip);
                    }
                }
            }

            // Ignore this unless `target_ll` is `Some`.
            let mut is_same = false;

            // If override is set, the link-layer address MUST be inserted into
            // the cache (if one is supplied and differs from the already
            // recoded address).
            if let Some(address) = target_ll {
                let address = Some(address);

                is_same = neighbor_state.link_address == address;

                if !is_same && message.override_flag() {
                    neighbor_state.link_address = address;
                }
            }

            // If the override flag is set, or the supplied link-layer address
            // is the same as that in the cache, or no Target Link-Layer Address
            // option was supplied:
            if message.override_flag() || target_ll.is_none() || is_same {
                // - If the solicited flag is set, the state of the entry MUST
                //   be set to REACHABLE.
                // - Else, if it was unset, and the link address was updated,
                //   the state MUST be set to STALE.
                // - Otherwise, the state remains the same.
                if message.solicited_flag() {
                    trace!("receive_ndp_packet: NDP RS from {:?} is solicited and either has override set, link address isn't provided, or the provided address is not different, updating state to Reachable", src_ip);
                    neighbor_state.state = NeighborEntryState::Reachable;
                } else if message.override_flag() && target_ll.is_some() && !is_same {
                    trace!("receive_ndp_packet: NDP RS from {:?} is unsolicited and the link address was updated, updating state to Stale", src_ip);

                    neighbor_state.state = NeighborEntryState::Stale;
                } else {
                    trace!("receive_ndp_packet: NDP RS from {:?} is unsolicited and the link address was not updated, doing nothing", src_ip);
                }
            }
        }
        NdpPacket::Redirect(_) => log_unimplemented!((), "NDP Redirect not implemented"),
    }
}
#[derive(PartialEq, Eq)]
enum SlaacType {
    Static,
    Temporary,
}

impl core::fmt::Debug for SlaacType {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            SlaacType::Static => f.write_str("static"),
            SlaacType::Temporary => f.write_str("temporary"),
        }
    }
}

impl<'a, Instant> From<&'a SlaacConfig<Instant>> for SlaacType {
    fn from(slaac_config: &'a SlaacConfig<Instant>) -> Self {
        match slaac_config {
            SlaacConfig::Static { .. } => SlaacType::Static,
            SlaacConfig::Temporary { .. } => SlaacType::Temporary,
        }
    }
}

fn get_source_link_layer_option<L: LinkAddress, B>(options: &Options<B>) -> Option<L>
where
    B: ByteSlice,
{
    options.iter().find_map(|o| match o {
        NdpOption::SourceLinkLayerAddress(a) => {
            if a.len() >= L::BYTES_LENGTH {
                Some(L::from_bytes(&a[..L::BYTES_LENGTH]))
            } else {
                None
            }
        }
        _ => None,
    })
}

fn get_target_link_layer_option<L: LinkAddress, B>(options: &Options<B>) -> Option<L>
where
    B: ByteSlice,
{
    options.iter().find_map(|o| match o {
        NdpOption::TargetLinkLayerAddress(a) => {
            if a.len() >= L::BYTES_LENGTH {
                Some(L::from_bytes(&a[..L::BYTES_LENGTH]))
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

    use alloc::{collections::HashSet, vec, vec::Vec};
    use core::convert::{TryFrom, TryInto as _};

    use net_declare::net::{mac, subnet_v6};
    use net_types::{
        ethernet::Mac,
        ip::{AddrSubnet, Subnet},
    };
    use packet::{Buf, ParseBuffer};
    use packet_formats::{
        icmp::{
            ndp::{
                options::PrefixInformation, OptionSequenceBuilder, RouterAdvertisement,
                RouterSolicitation,
            },
            IcmpEchoRequest, Icmpv6Packet,
        },
        ip::IpProto,
        testutil::{parse_ethernet_frame, parse_icmp_packet_in_ip_packet_in_ethernet_frame},
        utils::NonZeroDuration,
    };
    use rand::RngCore;

    use crate::{
        algorithm::{
            generate_opaque_interface_identifier, OpaqueIidNonce, STABLE_IID_SECRET_KEY_BYTES,
        },
        context::{
            testutil::{DummyInstant, DummyTimerCtxExt as _, StepResult},
            InstantContext as _, RngContext as _,
        },
        device::{
            add_ip_addr_subnet, del_ip_addr,
            ethernet::{EthernetLinkDevice, EthernetTimerId},
            testutil::receive_frame_or_panic,
            DeviceId, DeviceIdInner, DeviceLayerTimerId, DeviceLayerTimerIdInner, EthernetDeviceId,
            FrameDestination,
        },
        ip::{
            device::{
                get_ipv6_hop_limit, is_ip_routing_enabled,
                router_solicitation::{MAX_RTR_SOLICITATION_DELAY, RTR_SOLICITATION_INTERVAL},
                set_ipv6_routing_enabled,
                slaac::{SlaacConfiguration, SlaacTimerId, TemporarySlaacAddressConfiguration},
                state::{
                    AddrConfig, Ipv6AddressEntry, Ipv6DeviceConfiguration, Lifetime,
                    TemporarySlaacConfig,
                },
                with_assigned_ipv6_addr_subnets, Ipv6DeviceHandler, Ipv6DeviceTimerId,
            },
            receive_ipv6_packet,
            testutil::is_in_ip_multicast,
            SendIpPacketMeta,
        },
        testutil::{
            assert_empty, get_counter_val, handle_timer, set_logger_for_test,
            DummyEventDispatcherBuilder, TestIpExt, DUMMY_CONFIG_V6,
        },
        Ctx, Instant, StackStateBuilder, TimerId, TimerIdInner,
    };

    type IcmpParseArgs = packet_formats::icmp::IcmpParseArgs<Ipv6Addr>;

    impl From<NdpTimerId<EthernetLinkDevice, EthernetDeviceId>> for TimerId {
        fn from(id: NdpTimerId<EthernetLinkDevice, EthernetDeviceId>) -> Self {
            TimerId(TimerIdInner::DeviceLayer(DeviceLayerTimerId(
                DeviceLayerTimerIdInner::Ethernet(EthernetTimerId::Ndp(id)),
            )))
        }
    }

    // TODO(https://github.com/rust-lang/rust/issues/67441): Make these constants once const
    // Option::unwrap is stablized
    fn local_mac() -> UnicastAddr<Mac> {
        UnicastAddr::new(Mac::new([0, 1, 2, 3, 4, 5])).unwrap()
    }

    fn remote_mac() -> UnicastAddr<Mac> {
        UnicastAddr::new(Mac::new([6, 7, 8, 9, 10, 11])).unwrap()
    }

    fn local_ip() -> UnicastAddr<Ipv6Addr> {
        UnicastAddr::from_witness(DUMMY_CONFIG_V6.local_ip).unwrap()
    }

    fn remote_ip() -> UnicastAddr<Ipv6Addr> {
        UnicastAddr::from_witness(DUMMY_CONFIG_V6.remote_ip).unwrap()
    }

    fn router_advertisement_message(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        current_hop_limit: u8,
        managed_flag: bool,
        other_config_flag: bool,
        router_lifetime: u16,
        reachable_time: u32,
        retransmit_timer: u32,
    ) -> Buf<Vec<u8>> {
        Buf::new(Vec::new(), ..)
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                RouterAdvertisement::new(
                    current_hop_limit,
                    managed_flag,
                    other_config_flag,
                    router_lifetime,
                    reachable_time,
                    retransmit_timer,
                ),
            ))
            .serialize_vec_outer()
            .unwrap()
            .into_inner()
    }

    fn neighbor_advertisement_message(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        router_flag: bool,
        solicited_flag: bool,
        override_flag: bool,
        mac: Option<Mac>,
    ) -> Buf<Vec<u8>> {
        let mac = mac.map(|x| x.bytes());

        let mut options = Vec::new();

        if let Some(ref mac) = mac {
            options.push(NdpOptionBuilder::TargetLinkLayerAddress(mac));
        }

        OptionSequenceBuilder::new(options.iter())
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                NeighborAdvertisement::new(router_flag, solicited_flag, override_flag, src_ip),
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_b()
    }

    impl TryFrom<DeviceId> for EthernetDeviceId {
        type Error = DeviceId;
        fn try_from(id: DeviceId) -> Result<EthernetDeviceId, DeviceId> {
            match id.inner() {
                DeviceIdInner::Ethernet(id) => Ok(id),
                DeviceIdInner::Loopback => Err(id),
            }
        }
    }

    #[test]
    fn test_send_neighbor_solicitation_on_cache_miss() {
        set_logger_for_test();
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let dev_id = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, dev_id);
        // Now we have to manually assign the IP addresses, see
        // `EthernetLinkDevice::get_ipv6_addr`
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            AddrSubnet::new(local_ip().get(), 128).unwrap(),
        )
        .unwrap();

        assert_eq!(
            lookup::<EthernetLinkDevice, _, _>(
                &mut sync_ctx,
                &mut non_sync_ctx,
                dev_id.try_into().expect("expected ethernet ID"),
                remote_ip()
            ),
            None
        );

        // Check that we send the original neighbor solicitation, then resend a
        // few times if we don't receive a response.
        for packet_num in 0..usize::from(MAX_MULTICAST_SOLICIT) {
            assert_eq!(non_sync_ctx.frames_sent().len(), packet_num + 1);

            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
                NdpTimerId::new_link_address_resolution(
                    dev_id.try_into().expect("expected ethernet ID"),
                    remote_ip()
                )
                .into()
            );
        }
        // Check that we hit the timeout after MAX_MULTICAST_SOLICIT.
        assert_eq!(
            get_counter_val(&non_sync_ctx, "ndp::neighbor_solicitation_timer"),
            1,
            "timeout counter at zero"
        );
    }

    #[test]
    fn test_address_resolution() {
        set_logger_for_test();
        let mut local = DummyEventDispatcherBuilder::default();
        assert_eq!(local.add_device(local_mac()), 0);
        let mut remote = DummyEventDispatcherBuilder::default();
        assert_eq!(remote.add_device(remote_mac()), 0);
        let device_id = DeviceId::new_ethernet(0);

        let mut net = crate::context::testutil::new_legacy_simple_dummy_network(
            "local",
            local.build(),
            "remote",
            remote.build(),
        );

        // Let's try to ping the remote device from the local device:
        let req = IcmpEchoRequest::new(0, 0);
        let req_body = &[1, 2, 3, 4];
        let body = Buf::new(req_body.to_vec(), ..).encapsulate(
            IcmpPacketBuilder::<Ipv6, &[u8], _>::new(local_ip(), remote_ip(), IcmpUnusedCode, req),
        );
        // Manually assigning the addresses.
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            add_ip_addr_subnet(
                sync_ctx,
                non_sync_ctx,
                device_id,
                AddrSubnet::new(remote_ip().get(), 128).unwrap(),
            )
            .unwrap();

            assert_empty(non_sync_ctx.frames_sent());
        });
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            add_ip_addr_subnet(
                sync_ctx,
                non_sync_ctx,
                device_id,
                AddrSubnet::new(local_ip().get(), 128).unwrap(),
            )
            .unwrap();

            assert_empty(non_sync_ctx.frames_sent());

            crate::ip::send_ipv6_packet_from_device(
                sync_ctx,
                non_sync_ctx,
                SendIpPacketMeta {
                    device: device_id,
                    src_ip: Some(local_ip().into_specified()),
                    dst_ip: remote_ip().into_specified(),
                    next_hop: remote_ip().into_specified(),
                    proto: Ipv6Proto::Icmpv6,
                    ttl: None,
                    mtu: None,
                },
                body,
            )
            .unwrap();
            // This should've triggered a neighbor solicitation to come out of
            // local.
            assert_eq!(non_sync_ctx.frames_sent().len(), 1);
            // A timer should've been started.
            assert_eq!(non_sync_ctx.timer_ctx().timers().len(), 1);
        });

        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);
        // Neighbor entry for remote should be marked as Incomplete.
        assert_eq!(
            StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
                net.sync_ctx("local"),
                device_id.try_into().expect("expected ethernet ID")
            )
            .neighbors
            .get_neighbor_state(&remote_ip())
            .unwrap()
            .state,
            NeighborEntryState::Incomplete { transmit_counter: 1 }
        );

        assert_eq!(
            get_counter_val(net.non_sync_ctx("remote"), "ndp::rx_neighbor_solicitation"),
            1,
            "remote received solicitation"
        );
        assert_eq!(net.non_sync_ctx("remote").frames_sent().len(), 1);

        // Forward advertisement response back to local.
        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);

        assert_eq!(
            get_counter_val(net.non_sync_ctx("local"), "ndp::rx_neighbor_advertisement"),
            1,
            "local received advertisement"
        );

        // At the end of the exchange, both sides should have each other in
        // their NDP tables.
        let local_neighbor =
            StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
                net.sync_ctx("local"),
                device_id.try_into().expect("expected ethernet ID"),
            )
            .neighbors
            .get_neighbor_state(&remote_ip())
            .unwrap();
        assert_eq!(local_neighbor.link_address.unwrap(), remote_mac().get(),);
        // Remote must be reachable from local since it responded with an NA
        // message with the solicited flag set.
        assert_eq!(local_neighbor.state, NeighborEntryState::Reachable,);

        let remote_neighbor =
            StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
                net.sync_ctx("remote"),
                device_id.try_into().expect("expected ethernet ID"),
            )
            .neighbors
            .get_neighbor_state(&local_ip())
            .unwrap();
        assert_eq!(remote_neighbor.link_address.unwrap(), local_mac().get(),);
        // Local must be marked as stale because remote got an NS from it but
        // has not itself sent any packets to it and confirmed that local
        // actually received it.
        assert_eq!(remote_neighbor.state, NeighborEntryState::Stale);

        // The local timer should've been unscheduled.
        net.with_context("local", |Ctx { sync_ctx: _, non_sync_ctx }| {
            assert_empty(non_sync_ctx.timer_ctx().timers());

            // Upon link layer resolution, the original ping request should've been
            // sent out.
            assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        });
        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);
        assert_eq!(
            get_counter_val(net.non_sync_ctx("remote"), "<IcmpIpTransportContext as BufferIpTransportContext<Ipv6>>::receive_ip_packet::echo_request"),
            1
        );

        // TODO(brunodalbo): We should be able to verify that remote also sends
        //  back an echo reply, but we're having some trouble with IPv6 link
        //  local addresses.
    }

    #[test]
    fn test_deinitialize_cancels_timers() {
        // Test that associated timers are cancelled when the NDP device
        // is deinitialized.

        set_logger_for_test();
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let dev_id = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, dev_id);
        // Now we have to manually assign the IP addresses, see
        // `EthernetLinkDevice::get_ipv6_addr`
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            AddrSubnet::new(local_ip().get(), 128).unwrap(),
        )
        .unwrap();

        let device_id = dev_id.try_into().unwrap();
        assert_eq!(
            lookup::<EthernetLinkDevice, _, _>(
                &mut sync_ctx,
                &mut non_sync_ctx,
                device_id,
                remote_ip()
            ),
            None
        );

        // This should have scheduled a timer
        let timer_id = NdpTimerId::new_link_address_resolution(device_id, remote_ip()).into();
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id, ..)]);

        // Deinitializing a different ID should not impact the current timer
        let other_id = {
            let EthernetDeviceId(id) = device_id;
            EthernetDeviceId(id + 1).into()
        };
        deinitialize(&mut sync_ctx, &mut non_sync_ctx, other_id);
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id, ..)]);

        // Deinitializing the correct ID should cancel the timer.
        deinitialize(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id.try_into().expect("expected ethernet ID"),
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }

    #[test]
    fn test_dad_duplicate_address_detected_solicitation() {
        // Tests whether a duplicate address will get detected by solicitation
        // In this test, two nodes having the same MAC address will come up at
        // the same time. And both of them will use the EUI address. Each of
        // them should be able to detect each other is using the same address,
        // so they will both give up using that address.
        set_logger_for_test();
        let mac = UnicastAddr::new(Mac::new([6, 5, 4, 3, 2, 1])).unwrap();
        let ll_addr: Ipv6Addr = mac.to_ipv6_link_local().addr().get();
        let multicast_addr = ll_addr.to_solicited_node_address();
        let local = DummyEventDispatcherBuilder::default();
        let remote = DummyEventDispatcherBuilder::default();
        let device_id = DeviceId::new_ethernet(0);

        let stack_builder = StackStateBuilder::default();
        let mut net = crate::context::testutil::new_legacy_simple_dummy_network(
            "local",
            local.build_with(stack_builder.clone()),
            "remote",
            remote.build_with(stack_builder),
        );

        // Create the devices (will start DAD at the same time).
        let update = |ipv6_config: &mut Ipv6DeviceConfiguration| {
            ipv6_config.ip_config.ip_enabled = true;

            // Doesn't matter as long as we perform DAD.
            ipv6_config.dad_transmits = NonZeroU8::new(1);
        };
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            assert_eq!(
                crate::add_ethernet_device(
                    sync_ctx,
                    non_sync_ctx,
                    mac,
                    Ipv6::MINIMUM_LINK_MTU.into(),
                ),
                device_id
            );
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, device_id, update);
            assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        });
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            assert_eq!(
                crate::add_ethernet_device(
                    sync_ctx,
                    non_sync_ctx,
                    mac,
                    Ipv6::MINIMUM_LINK_MTU.into(),
                ),
                device_id
            );
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, device_id, update);
            assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        });

        // Both devices should be in the solicited-node multicast group.
        assert!(is_in_ip_multicast(net.sync_ctx("local"), device_id, multicast_addr));
        assert!(is_in_ip_multicast(net.sync_ctx("remote"), device_id, multicast_addr));

        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);

        // They should now realize the address they intend to use has a
        // duplicate in the local network.
        with_assigned_ipv6_addr_subnets(net.sync_ctx("local"), device_id, |addrs| {
            assert_empty(addrs)
        });
        with_assigned_ipv6_addr_subnets(net.sync_ctx("remote"), device_id, |addrs| {
            assert_empty(addrs)
        });

        // Both devices should not be in the multicast group
        assert!(!is_in_ip_multicast(net.sync_ctx("local"), device_id, multicast_addr));
        assert!(!is_in_ip_multicast(net.sync_ctx("remote"), device_id, multicast_addr));
    }

    fn dad_timer_id(id: EthernetDeviceId, addr: UnicastAddr<Ipv6Addr>) -> TimerId {
        TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Dad(
            crate::ip::device::dad::DadTimerId { device_id: id.into(), addr },
        )))
    }

    fn rs_timer_id(id: EthernetDeviceId) -> TimerId {
        TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Rs(
            crate::ip::device::router_solicitation::RsTimerId { device_id: id.into() },
        )))
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
        assert_eq!(local.add_device(local_mac()), 0);
        let mut remote = DummyEventDispatcherBuilder::default();
        assert_eq!(remote.add_device(remote_mac()), 0);
        let device_id = DeviceId::new_ethernet(0);

        let mut net = crate::context::testutil::new_legacy_simple_dummy_network(
            "local",
            local.build(),
            "remote",
            remote.build(),
        );

        // Enable DAD.
        let update = |ipv6_config: &mut Ipv6DeviceConfiguration| {
            ipv6_config.ip_config.ip_enabled = true;

            // Doesn't matter as long as we perform DAD.
            ipv6_config.dad_transmits = NonZeroU8::new(1);
        };
        let addr = AddrSubnet::new(local_ip().get(), 128).unwrap();
        let multicast_addr = local_ip().to_solicited_node_address();
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, device_id, update);
            add_ip_addr_subnet(sync_ctx, non_sync_ctx, device_id, addr).unwrap();
        });
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, device_id, update);
        });

        // Only local should be in the solicited node multicast group.
        assert!(is_in_ip_multicast(net.sync_ctx("local"), device_id, multicast_addr));
        assert!(!is_in_ip_multicast(net.sync_ctx("remote"), device_id, multicast_addr));

        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            assert_eq!(
                non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
                dad_timer_id(device_id.try_into().expect("expected ethernet ID"), local_ip())
            );
        });

        assert!(NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(
            net.sync_ctx("local"),
            device_id.try_into().expect("expected ethernet ID")
        )
        .find_addr(&local_ip())
        .unwrap()
        .state
        .is_assigned());

        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            add_ip_addr_subnet(sync_ctx, non_sync_ctx, device_id, addr).unwrap();
        });
        // Local & remote should be in the multicast group.
        assert!(is_in_ip_multicast(net.sync_ctx("local"), device_id, multicast_addr));
        assert!(is_in_ip_multicast(net.sync_ctx("remote"), device_id, multicast_addr));

        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);

        assert_eq!(
            with_assigned_ipv6_addr_subnets(net.sync_ctx("remote"), device_id, |addrs| addrs
                .count()),
            1
        );

        // Let's make sure that our local node still can use that address.
        assert!(NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(
            net.sync_ctx("local"),
            device_id.try_into().expect("expected ethernet ID")
        )
        .find_addr(&local_ip())
        .unwrap()
        .state
        .is_assigned());

        // Only local should be in the solicited node multicast group.
        assert!(is_in_ip_multicast(net.sync_ctx("local"), device_id, multicast_addr));
        assert!(!is_in_ip_multicast(net.sync_ctx("remote"), device_id, multicast_addr));
    }

    #[test]
    fn test_dad_set_ipv6_address_when_ongoing() {
        // Test that we can make our tentative address change when DAD is
        // ongoing.

        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let dev_id = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            |config| {
                config.ip_config.ip_enabled = true;
                config.dad_transmits = NonZeroU8::new(1);
            },
        );
        let addr = local_ip();
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            AddrSubnet::new(addr.get(), 128).unwrap(),
        )
        .unwrap();
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(
                &sync_ctx,
                dev_id.try_into().expect("expected ethernet ID")
            )
            .find_addr(&addr)
            .unwrap()
            .state,
            AddressState::Tentative { dad_transmits_remaining: None },
        );
        let addr = remote_ip();
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(
                &sync_ctx,
                dev_id.try_into().expect("expected ethernet ID")
            )
            .find_addr(&addr),
            None
        );
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            AddrSubnet::new(addr.get(), 128).unwrap(),
        )
        .unwrap();
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(
                &sync_ctx,
                dev_id.try_into().expect("expected ethernet ID")
            )
            .find_addr(&addr)
            .unwrap()
            .state,
            AddressState::Tentative { dad_transmits_remaining: None },
        );
    }

    #[test]
    fn test_dad_three_transmits_no_conflicts() {
        let Ctx { mut sync_ctx, mut non_sync_ctx } = crate::testutil::DummyCtx::default();
        let dev_id = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, dev_id);

        // Enable DAD.
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            |config| {
                config.ip_config.ip_enabled = true;
                config.dad_transmits = NonZeroU8::new(3);
            },
        );
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            AddrSubnet::new(local_ip().get(), 128).unwrap(),
        )
        .unwrap();
        for _ in 0..3 {
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
                dad_timer_id(dev_id.try_into().expect("expected ethernet ID"), local_ip())
            );
        }
        assert!(NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(
            &sync_ctx,
            dev_id.try_into().expect("expected ethernet ID")
        )
        .find_addr(&local_ip())
        .unwrap()
        .state
        .is_assigned());
    }

    #[test]
    fn test_dad_three_transmits_with_conflicts() {
        // Test if the implementation is correct when we have more than 1 NS
        // packets to send.
        set_logger_for_test();
        let mac = UnicastAddr::new(Mac::new([6, 5, 4, 3, 2, 1])).unwrap();
        let mut local = DummyEventDispatcherBuilder::default();
        assert_eq!(local.add_device(mac), 0);
        let mut remote = DummyEventDispatcherBuilder::default();
        assert_eq!(remote.add_device(mac), 0);
        let device_id = DeviceId::new_ethernet(0);
        let mut net = crate::context::testutil::new_legacy_simple_dummy_network(
            "local",
            local.build(),
            "remote",
            remote.build(),
        );

        let update = |ipv6_config: &mut Ipv6DeviceConfiguration| {
            ipv6_config.ip_config.ip_enabled = true;
            ipv6_config.dad_transmits = NonZeroU8::new(3);
        };
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, device_id, update);

            add_ip_addr_subnet(
                sync_ctx,
                non_sync_ctx,
                device_id,
                AddrSubnet::new(local_ip().get(), 128).unwrap(),
            )
            .unwrap();
        });
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, device_id, update);
        });

        let expected_timer_id =
            dad_timer_id(device_id.try_into().expect("expected ethernet ID"), local_ip());
        // During the first and second period, the remote host is still down.
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            assert_eq!(
                non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
                expected_timer_id
            );
            assert_eq!(
                non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
                expected_timer_id
            );
        });
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            add_ip_addr_subnet(
                sync_ctx,
                non_sync_ctx,
                device_id,
                AddrSubnet::new(local_ip().get(), 128).unwrap(),
            )
            .unwrap();
        });
        // The local host should have sent out 3 packets while the remote one
        // should only have sent out 1.
        assert_eq!(net.non_sync_ctx("local").frames_sent().len(), 3);
        assert_eq!(net.non_sync_ctx("remote").frames_sent().len(), 1);

        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);

        // Let's make sure that all timers are cancelled properly.
        net.with_context("local", |Ctx { sync_ctx: _, non_sync_ctx }| {
            assert_empty(non_sync_ctx.timer_ctx().timers());
        });
        net.with_context("remote", |Ctx { sync_ctx: _, non_sync_ctx }| {
            assert_empty(non_sync_ctx.timer_ctx().timers());
        });

        // They should now realize the address they intend to use has a
        // duplicate in the local network.
        assert_eq!(
            with_assigned_ipv6_addr_subnets(net.sync_ctx("local"), device_id, |a| a.count()),
            1
        );
        assert_eq!(
            with_assigned_ipv6_addr_subnets(net.sync_ctx("remote"), device_id, |a| a.count()),
            1
        );
    }

    fn get_address_state(
        sync_ctx: &crate::testutil::DummySyncCtx,
        device: DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    ) -> Option<AddressState> {
        crate::ip::device::IpDeviceContext::<Ipv6, _>::with_ip_device_state(
            sync_ctx,
            device,
            |state| state.ip_state.find_addr(&addr).map(|a| a.state),
        )
    }

    #[test]
    fn test_dad_multiple_ips_simultaneously() {
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let dev_id = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, dev_id);

        assert_empty(non_sync_ctx.frames_sent());

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            |ipv6_config| {
                ipv6_config.ip_config.ip_enabled = true;
                ipv6_config.dad_transmits = NonZeroU8::new(3);
                ipv6_config.max_router_solicitations = None;
            },
        );

        // Add an IP.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            AddrSubnet::new(local_ip().get(), 128).unwrap(),
        )
        .unwrap();
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);

        // Send another NS.
        let local_timer_id =
            dad_timer_id(dev_id.try_into().expect("expected ethernet ID"), local_ip());
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                &mut sync_ctx,
                Duration::from_secs(1),
                crate::handle_timer
            ),
            [local_timer_id]
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);

        // Add another IP
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            AddrSubnet::new(remote_ip().get(), 128).unwrap(),
        )
        .unwrap();
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);

        // Run to the end for DAD for local ip
        let remote_timer_id =
            dad_timer_id(dev_id.try_into().expect("expected ethernet ID"), remote_ip());
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                &mut sync_ctx,
                Duration::from_secs(2),
                crate::handle_timer
            ),
            [local_timer_id, remote_timer_id, local_timer_id, remote_timer_id]
        );
        assert_eq!(get_address_state(&sync_ctx, dev_id, local_ip()), Some(AddressState::Assigned));
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 6);

        // Run to the end for DAD for local ip
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                &mut sync_ctx,
                Duration::from_secs(1),
                crate::handle_timer
            ),
            [remote_timer_id]
        );
        assert_eq!(get_address_state(&sync_ctx, dev_id, local_ip()), Some(AddressState::Assigned));
        assert_eq!(get_address_state(&sync_ctx, dev_id, remote_ip()), Some(AddressState::Assigned));
        assert_eq!(non_sync_ctx.frames_sent().len(), 6);

        // No more timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer), None);
    }

    #[test]
    fn test_dad_cancel_when_ip_removed() {
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let dev_id = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, dev_id);

        // Enable DAD.
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            |ipv6_config| {
                ipv6_config.ip_config.ip_enabled = true;
                ipv6_config.dad_transmits = NonZeroU8::new(3);
                ipv6_config.max_router_solicitations = None;
            },
        );

        assert_empty(non_sync_ctx.frames_sent());

        // Add an IP.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            AddrSubnet::new(local_ip().get(), 128).unwrap(),
        )
        .unwrap();
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);

        // Send another NS.
        let local_timer_id =
            dad_timer_id(dev_id.try_into().expect("expected ethernet ID"), local_ip());
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                &mut sync_ctx,
                Duration::from_secs(1),
                crate::handle_timer
            ),
            [local_timer_id]
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);

        // Add another IP
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dev_id,
            AddrSubnet::new(remote_ip().get(), 128).unwrap(),
        )
        .unwrap();
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);

        // Run 1s
        let remote_timer_id =
            dad_timer_id(dev_id.try_into().expect("expected ethernet ID"), remote_ip());
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                &mut sync_ctx,
                Duration::from_secs(1),
                crate::handle_timer
            ),
            [local_timer_id, remote_timer_id]
        );
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 5);

        // Remove local ip
        del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, dev_id, &local_ip().into_specified())
            .unwrap();
        assert_eq!(get_address_state(&sync_ctx, dev_id, local_ip()), None);
        assert_matches!(
            get_address_state(&sync_ctx, dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 5);

        // Run to the end for DAD for local ip
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                &mut sync_ctx,
                Duration::from_secs(2),
                crate::handle_timer
            ),
            [remote_timer_id, remote_timer_id]
        );
        assert_eq!(get_address_state(&sync_ctx, dev_id, local_ip()), None);
        assert_eq!(get_address_state(&sync_ctx, dev_id, remote_ip()), Some(AddressState::Assigned));
        assert_eq!(non_sync_ctx.frames_sent().len(), 6);

        // No more timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer), None);
    }

    trait UnwrapNdp<B: ByteSlice> {
        fn unwrap_ndp(self) -> NdpPacket<B>;
    }

    impl<B: ByteSlice> UnwrapNdp<B> for Icmpv6Packet<B> {
        fn unwrap_ndp(self) -> NdpPacket<B> {
            match self {
                Icmpv6Packet::Ndp(ndp) => ndp,
                _ => unreachable!(),
            }
        }
    }

    #[test]
    fn test_receiving_router_solicitation_validity_check() {
        let config = Ipv6::DUMMY_CONFIG;
        let src_ip = Ipv6Addr::from([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 10]);
        let src_mac = [10, 11, 12, 13, 14, 15];
        let options = vec![NdpOptionBuilder::SourceLinkLayerAddress(&src_mac[..])];

        // Test receiving NDP RS when not a router (should not receive)

        let Ctx { mut sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(config.clone()).build();
        let device_id = DeviceId::new_ethernet(0);

        let mut icmpv6_packet_buf = OptionSequenceBuilder::new(options.iter())
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

        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device_id,
            src_ip.try_into().unwrap(),
            config.local_ip,
            icmpv6_packet.unwrap_ndp(),
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_solicitation"), 0);
    }

    #[test]
    fn test_receiving_router_advertisement_validity_check() {
        let config = Ipv6::DUMMY_CONFIG;
        let src_mac = [10, 11, 12, 13, 14, 15];
        let src_ip = Ipv6Addr::from([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 10]);
        let Ctx { mut sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(config.clone()).build();
        let device_id = DeviceId::new_ethernet(0);

        // Test receiving NDP RA where source IP is not a link local address
        // (should not receive).

        let mut icmpv6_packet_buf = router_advertisement_message(
            src_ip.into(),
            config.local_ip.get(),
            1,
            false,
            false,
            3,
            4,
            5,
        );
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device_id,
            src_ip.try_into().unwrap(),
            config.local_ip,
            icmpv6_packet.unwrap_ndp(),
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 0);

        // Test receiving NDP RA where source IP is a link local address (should
        // receive).

        let src_ip = Mac::new(src_mac).to_ipv6_link_local().addr().get();
        let mut icmpv6_packet_buf =
            router_advertisement_message(src_ip, config.local_ip.get(), 1, false, false, 3, 4, 5);
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device_id,
            src_ip.try_into().unwrap(),
            config.local_ip,
            icmpv6_packet.unwrap_ndp(),
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 1);
    }

    #[test]
    fn test_sending_ipv6_packet_after_hop_limit_change() {
        // Sets the hop limit with a router advertisement and sends a packet to
        // make sure the packet uses the new hop limit.
        fn inner_test(
            sync_ctx: &mut crate::testutil::DummySyncCtx,
            ctx: &mut crate::testutil::DummyNonSyncCtx,
            hop_limit: u8,
            frame_offset: usize,
        ) {
            let config = Ipv6::DUMMY_CONFIG;
            let device_id = DeviceId::new_ethernet(0);
            let src_ip = config.remote_mac.to_ipv6_link_local().addr();

            let mut icmpv6_packet_buf = router_advertisement_message(
                src_ip.get(),
                config.local_ip.get(),
                hop_limit,
                false,
                false,
                0,
                0,
                0,
            );
            let icmpv6_packet = icmpv6_packet_buf
                .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
                .unwrap();
            sync_ctx.receive_ndp_packet(
                ctx,
                device_id,
                Ipv6SourceAddr::from_witness(src_ip).unwrap(),
                config.local_ip,
                icmpv6_packet.unwrap_ndp(),
            );
            assert_eq!(get_ipv6_hop_limit(sync_ctx, device_id).get(), hop_limit);
            crate::ip::send_ipv6_packet_from_device(
                sync_ctx,
                ctx,
                SendIpPacketMeta {
                    device: device_id,
                    src_ip: Some(config.local_ip),
                    dst_ip: config.remote_ip,
                    next_hop: config.remote_ip,
                    proto: IpProto::Tcp.into(),
                    ttl: None,
                    mtu: None,
                },
                Buf::new(vec![0; 10], ..),
            )
            .unwrap();
            let (buf, _, _, _) =
                parse_ethernet_frame(&ctx.frames_sent()[frame_offset].1[..]).unwrap();
            // Packet's hop limit should be 100.
            assert_eq!(buf[7], hop_limit);
        }

        let Ctx { mut sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(Ipv6::DUMMY_CONFIG).build();

        // Set hop limit to 100.
        inner_test(&mut sync_ctx, &mut non_sync_ctx, 100, 0);

        // Set hop limit to 30.
        inner_test(&mut sync_ctx, &mut non_sync_ctx, 30, 1);
    }

    #[test]
    fn test_receiving_router_advertisement_source_link_layer_option() {
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(config.clone()).build();
        let device_id = DeviceId::new_ethernet(0);
        let src_mac = Mac::new([10, 11, 12, 13, 14, 15]);
        let src_ip = src_mac.to_ipv6_link_local().addr();
        let src_mac_bytes = src_mac.bytes();
        let options = vec![NdpOptionBuilder::SourceLinkLayerAddress(&src_mac_bytes[..])];

        // First receive a Router Advertisement without the source link layer
        // and make sure no new neighbor gets added.

        let mut icmpv6_packet_buf = router_advertisement_message(
            src_ip.get(),
            config.local_ip.get(),
            1,
            false,
            false,
            3,
            4,
            5,
        );
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        let ndp_state = StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
            &mut sync_ctx,
            device_id.try_into().expect("expected ethernet ID"),
        );
        assert_eq!(ndp_state.neighbors.get_neighbor_state(&src_ip), None);
        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device_id,
            Ipv6SourceAddr::from_witness(src_ip).unwrap(),
            config.local_ip,
            icmpv6_packet.unwrap_ndp(),
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 1);
        let ndp_state = StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
            &mut sync_ctx,
            device_id.try_into().expect("expected ethernet ID"),
        );
        // Should still not have a neighbor added.
        assert_eq!(ndp_state.neighbors.get_neighbor_state(&src_ip), None);

        // Receive a new RA but with the source link layer option

        let mut icmpv6_packet_buf = OptionSequenceBuilder::new(options.iter())
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                config.local_ip,
                IcmpUnusedCode,
                RouterAdvertisement::new(1, false, false, 3, 4, 5),
            ))
            .serialize_vec_outer()
            .unwrap();
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device_id,
            Ipv6SourceAddr::from_witness(src_ip).unwrap(),
            config.local_ip,
            icmpv6_packet.unwrap_ndp(),
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 2);
        let ndp_state = StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
            &mut sync_ctx,
            device_id.try_into().expect("expected ethernet ID"),
        );
        let neighbor = ndp_state.neighbors.get_neighbor_state(&src_ip).unwrap();
        assert_eq!(neighbor.link_address.unwrap(), src_mac);
        // Router should be marked stale as a neighbor.
        assert_eq!(neighbor.state, NeighborEntryState::Stale);
    }

    #[test]
    fn test_receiving_router_advertisement_mtu_option() {
        fn packet_buf(src_ip: Ipv6Addr, dst_ip: Ipv6Addr, mtu: u32) -> Buf<Vec<u8>> {
            let options = &[NdpOptionBuilder::Mtu(mtu)];
            OptionSequenceBuilder::new(options.iter())
                .into_serializer()
                .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                    src_ip,
                    dst_ip,
                    IcmpUnusedCode,
                    RouterAdvertisement::new(1, false, false, 3, 4, 5),
                ))
                .serialize_vec_outer()
                .unwrap()
                .unwrap_b()
        }

        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let hw_mtu = 5000;
        let device =
            crate::add_ethernet_device(&mut sync_ctx, &mut non_sync_ctx, local_mac(), hw_mtu);
        let src_mac = Mac::new([10, 11, 12, 13, 14, 15]);
        let src_ip = src_mac.to_ipv6_link_local().addr();

        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        // Receive a new RA with a valid MTU option (but the new MTU should only
        // be 5000 as that is the max MTU of the device).

        let mut icmpv6_packet_buf = packet_buf(src_ip.get(), config.local_ip.get(), 5781);
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device,
            Ipv6SourceAddr::from_witness(src_ip).unwrap(),
            config.local_ip,
            icmpv6_packet.unwrap_ndp(),
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 1);
        assert_eq!(crate::ip::IpDeviceContext::<Ipv6, _>::get_mtu(&sync_ctx, device), hw_mtu);

        // Receive a new RA with an invalid MTU option (value is lower than IPv6
        // min MTU).

        let mut icmpv6_packet_buf =
            packet_buf(src_ip.get(), config.local_ip.get(), u32::from(Ipv6::MINIMUM_LINK_MTU) - 1);
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device,
            Ipv6SourceAddr::from_witness(src_ip).unwrap(),
            config.local_ip,
            icmpv6_packet.unwrap_ndp(),
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 2);
        assert_eq!(crate::ip::IpDeviceContext::<Ipv6, _>::get_mtu(&sync_ctx, device), hw_mtu);

        // Receive a new RA with a valid MTU option (value is exactly IPv6 min
        // MTU).

        let mut icmpv6_packet_buf =
            packet_buf(src_ip.get(), config.local_ip.get(), Ipv6::MINIMUM_LINK_MTU.into());
        let icmpv6_packet = icmpv6_packet_buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, config.local_ip))
            .unwrap();
        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device,
            Ipv6SourceAddr::from_witness(src_ip).unwrap(),
            config.local_ip,
            icmpv6_packet.unwrap_ndp(),
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 3);
        assert_eq!(
            crate::ip::IpDeviceContext::<Ipv6, _>::get_mtu(&sync_ctx, device),
            Ipv6::MINIMUM_LINK_MTU.into()
        );
    }

    #[test]
    fn test_host_send_router_solicitations() {
        fn validate_params(
            src_mac: Mac,
            src_ip: Ipv6Addr,
            message: RouterSolicitation,
            code: IcmpUnusedCode,
        ) {
            let dummy_config = Ipv6::DUMMY_CONFIG;
            assert_eq!(src_mac, dummy_config.local_mac.get());
            assert_eq!(src_ip, dummy_config.local_mac.to_ipv6_link_local().addr().get());
            assert_eq!(message, RouterSolicitation::default());
            assert_eq!(code, IcmpUnusedCode);
        }

        let dummy_config = Ipv6::DUMMY_CONFIG;

        let Ctx { mut sync_ctx, mut non_sync_ctx } = crate::testutil::DummyCtx::default();

        assert_empty(non_sync_ctx.frames_sent());
        let device_id = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            |config| {
                config.ip_config.ip_enabled = true;

                // Test expects to send 3 RSs.
                config.max_router_solicitations = NonZeroU8::new(3);
            },
        );
        assert_empty(non_sync_ctx.frames_sent());

        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(device_id.try_into().expect("expected ethernet ID")).into()
        );
        // Initial router solicitation should be a random delay between 0 and
        // `MAX_RTR_SOLICITATION_DELAY`.
        assert!(non_sync_ctx.now().duration_since(time) < MAX_RTR_SOLICITATION_DELAY);
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        let (src_mac, _, src_ip, _, _, message, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[0].1,
                |_| {},
            )
            .unwrap();
        validate_params(src_mac, src_ip, message, code);

        // Should get 2 more router solicitation messages
        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(device_id.try_into().expect("expected ethernet ID")).into()
        );
        assert_eq!(non_sync_ctx.now().duration_since(time), RTR_SOLICITATION_INTERVAL);
        let (src_mac, _, src_ip, _, _, message, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[1].1,
                |_| {},
            )
            .unwrap();
        validate_params(src_mac, src_ip, message, code);

        // Before the next one, lets assign an IP address (DAD won't be
        // performed so it will be assigned immediately). The router solicitation
        // message should continue to use the link-local address.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            AddrSubnet::new(dummy_config.local_ip.get(), 128).unwrap(),
        )
        .unwrap();
        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(device_id.try_into().expect("expected ethernet ID")).into()
        );
        assert_eq!(non_sync_ctx.now().duration_since(time), RTR_SOLICITATION_INTERVAL);
        let (src_mac, _, src_ip, _, _, message, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[2].1,
                |p| {
                    // We should have a source link layer option now because we
                    // have a source IP address set.
                    assert_eq!(p.body().iter().count(), 1);
                    if let Some(ll) = get_source_link_layer_option::<Mac, _>(p.body()) {
                        assert_eq!(ll, dummy_config.local_mac.get());
                    } else {
                        panic!("Should have a source link layer option");
                    }
                },
            )
            .unwrap();
        validate_params(src_mac, src_ip, message, code);

        // No more timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer), None);
        // Should have only sent 3 packets (Router solicitations).
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);

        let Ctx { mut sync_ctx, mut non_sync_ctx } = crate::testutil::DummyCtx::default();
        assert_empty(non_sync_ctx.frames_sent());
        let device_id = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            |config| {
                config.ip_config.ip_enabled = true;
                config.max_router_solicitations = NonZeroU8::new(2);
            },
        );
        assert_empty(non_sync_ctx.frames_sent());

        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(device_id.try_into().expect("expected ethernet ID")).into()
        );
        // Initial router solicitation should be a random delay between 0 and
        // `MAX_RTR_SOLICITATION_DELAY`.
        assert!(non_sync_ctx.now().duration_since(time) < MAX_RTR_SOLICITATION_DELAY);
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);

        // Should trigger 1 more router solicitations
        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(device_id.try_into().expect("expected ethernet ID")).into()
        );
        assert_eq!(non_sync_ctx.now().duration_since(time), RTR_SOLICITATION_INTERVAL);
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);

        // Each packet would be the same.
        for f in non_sync_ctx.frames_sent() {
            let (src_mac, _, src_ip, _, _, message, code) =
                parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                    &f.1,
                    |_| {},
                )
                .unwrap();
            validate_params(src_mac, src_ip, message, code);
        }

        // No more timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer), None);
    }

    #[test]
    fn test_router_solicitation_on_routing_enabled_changes() {
        // Make sure that when an interface goes from host -> router, it stops
        // sending Router Solicitations, and starts sending them when it goes
        // form router -> host as routers should not send Router Solicitation
        // messages, but hosts should.

        let dummy_config = Ipv6::DUMMY_CONFIG;

        // If netstack is not set to forward packets, make sure router
        // solicitations do not get cancelled when we enable forwarding on the
        // device.

        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();

        assert_empty(non_sync_ctx.frames_sent());
        assert_empty(non_sync_ctx.timer_ctx().timers());

        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |config| {
                config.ip_config.ip_enabled = true;

                // Doesn't matter as long as we are configured to send at least 2
                // solicitations.
                config.max_router_solicitations = NonZeroU8::new(2);
            },
        );
        let timer_id = rs_timer_id(device.try_into().expect("expected ethernet ID")).into();

        // Send the first router solicitation.
        assert_empty(non_sync_ctx.frames_sent());
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id, ..)]);

        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            timer_id
        );

        // Should have sent a router solicitation and still have the timer
        // setup.
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        let (_, _dst_mac, _, _, _, _, _) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[0].1,
                |_| {},
            )
            .unwrap();
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id, ..)]);

        // Enable routing on device.
        set_ipv6_routing_enabled(&mut sync_ctx, &mut non_sync_ctx, device, true)
            .expect("error setting routing enabled");
        assert!(is_ip_routing_enabled::<Ipv6, _, _>(&sync_ctx, device));

        // Should have not sent any new packets, but unset the router
        // solicitation timer.
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        assert_empty(non_sync_ctx.timer_ctx().timers().iter().filter(|x| x.1 == timer_id));

        // Unsetting routing should succeed.
        set_ipv6_routing_enabled(&mut sync_ctx, &mut non_sync_ctx, device, false)
            .expect("error setting routing enabled");
        assert!(!is_ip_routing_enabled::<Ipv6, _, _>(&sync_ctx, device));
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id, ..)]);

        // Send the first router solicitation after being turned into a host.
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            timer_id
        );

        // Should have sent a router solicitation.
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);
        assert_matches::assert_matches!(
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[1].1,
                |_| {},
            ),
            Ok((_, _, _, _, _, _, _))
        );
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id, ..)]);
    }

    #[test]
    fn test_set_ndp_config_dup_addr_detect_transmits() {
        // Test that updating the DupAddrDetectTransmits parameter on an
        // interface updates the number of DAD messages (NDP Neighbor
        // Solicitations) sent before concluding that an address is not a
        // duplicate.

        let dummy_config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);
        assert_empty(non_sync_ctx.frames_sent());
        assert_empty(non_sync_ctx.timer_ctx().timers());

        // Updating the IP should resolve immediately since DAD is turned off by
        // `DummyEventDispatcherBuilder::build`.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            AddrSubnet::new(dummy_config.local_ip.get(), 128).unwrap(),
        )
        .unwrap();
        let device_id = device.try_into().unwrap();
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(&sync_ctx, device_id)
                .find_addr(&dummy_config.local_ip.try_into().unwrap())
                .unwrap()
                .state,
            AddressState::Assigned
        );
        assert_empty(non_sync_ctx.frames_sent());
        assert_empty(non_sync_ctx.timer_ctx().timers());

        // Enable DAD for the device.
        const DUP_ADDR_DETECT_TRANSMITS: u8 = 3;
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |ipv6_config| {
                ipv6_config.ip_config.ip_enabled = true;
                ipv6_config.dad_transmits = NonZeroU8::new(DUP_ADDR_DETECT_TRANSMITS);
            },
        );

        // Updating the IP should start the DAD process.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            AddrSubnet::new(dummy_config.remote_ip.get(), 128).unwrap(),
        )
        .unwrap();
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(&sync_ctx, device_id)
                .find_addr(&dummy_config.local_ip.try_into().unwrap())
                .unwrap()
                .state,
            AddressState::Assigned
        );
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(&sync_ctx, device_id)
                .find_addr(&dummy_config.remote_ip.try_into().unwrap())
                .unwrap()
                .state,
            AddressState::Tentative {
                dad_transmits_remaining: NonZeroU8::new(DUP_ADDR_DETECT_TRANSMITS - 1)
            }
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        assert_eq!(non_sync_ctx.timer_ctx().timers().len(), 1);

        // Disable DAD during DAD.
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |ipv6_config| {
                ipv6_config.dad_transmits = None;
            },
        );
        let expected_timer_id = dad_timer_id(device_id, dummy_config.remote_ip.try_into().unwrap());
        // Allow already started DAD to complete (2 more more NS, 3 more timers).
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            expected_timer_id
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            expected_timer_id
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            expected_timer_id
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(&sync_ctx, device_id)
                .find_addr(&dummy_config.remote_ip.try_into().unwrap())
                .unwrap()
                .state,
            AddressState::Assigned
        );

        // Updating the IP should resolve immediately since DAD has just been
        // turned off.
        let new_ip = Ipv6::get_other_ip_address(3);
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            AddrSubnet::new(new_ip.get(), 128).unwrap(),
        )
        .unwrap();
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(&sync_ctx, device_id)
                .find_addr(&dummy_config.local_ip.try_into().unwrap())
                .unwrap()
                .state,
            AddressState::Assigned
        );
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(&sync_ctx, device_id)
                .find_addr(&dummy_config.remote_ip.try_into().unwrap())
                .unwrap()
                .state,
            AddressState::Assigned
        );
        assert_eq!(
            NdpContext::<EthernetLinkDevice, _>::get_ip_device_state(&sync_ctx, device_id)
                .find_addr(&new_ip.try_into().unwrap())
                .unwrap()
                .state,
            AddressState::Assigned
        );
    }

    #[test]
    fn test_receiving_neighbor_advertisements() {
        fn test_receiving_na_from_known_neighbor(
            sync_ctx: &mut crate::testutil::DummySyncCtx,
            ctx: &mut crate::testutil::DummyNonSyncCtx,
            src_ip: Ipv6Addr,
            dst_ip: SpecifiedAddr<Ipv6Addr>,
            device: DeviceId,
            router_flag: bool,
            solicited_flag: bool,
            override_flag: bool,
            mac: Option<Mac>,
            expected_state: NeighborEntryState,
            expected_link_addr: Option<Mac>,
        ) {
            let mut buf = neighbor_advertisement_message(
                src_ip,
                dst_ip.get(),
                router_flag,
                solicited_flag,
                override_flag,
                mac,
            );
            let packet =
                buf.parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, dst_ip)).unwrap();
            sync_ctx.receive_ndp_packet(
                ctx,
                device,
                src_ip.try_into().unwrap(),
                dst_ip,
                packet.unwrap_ndp(),
            );

            let neighbor_state =
                StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
                    sync_ctx,
                    device.try_into().unwrap(),
                )
                .neighbors
                .get_neighbor_state(&src_ip.try_into().unwrap())
                .unwrap();
            assert_eq!(neighbor_state.state, expected_state);
            assert_eq!(neighbor_state.link_address, expected_link_addr);
        }

        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        let neighbor_mac = config.remote_mac.get();
        let neighbor_ip = neighbor_mac.to_ipv6_link_local().addr();
        let all_nodes_addr = Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.into_specified();

        // Should not know about the neighbor yet.
        let device_id = device.try_into().unwrap();
        assert_eq!(
            StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
                &mut sync_ctx,
                device_id
            )
            .neighbors
            .get_neighbor_state(&neighbor_ip.get()),
            None
        );

        // Receiving unsolicited NA from a neighbor we don't care about yet
        // should do nothing.

        // Receive the NA.
        let mut buf = neighbor_advertisement_message(
            neighbor_ip.get(),
            all_nodes_addr.get(),
            false,
            false,
            false,
            None,
        );
        let packet = buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(neighbor_ip, all_nodes_addr))
            .unwrap();
        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device,
            Ipv6SourceAddr::from_witness(neighbor_ip).unwrap(),
            all_nodes_addr,
            packet.unwrap_ndp(),
        );

        // We still do not know about the neighbor since the NA was unsolicited
        // and we never were interested in the neighbor yet.
        assert_eq!(
            StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
                &mut sync_ctx,
                device_id
            )
            .neighbors
            .get_neighbor_state(&neighbor_ip),
            None
        );

        // Receiving solicited NA from a neighbor we don't care about yet should
        // do nothing (should never happen).

        // Receive the NA.
        let mut buf = neighbor_advertisement_message(
            neighbor_ip.get(),
            all_nodes_addr.get(),
            false,
            true,
            false,
            None,
        );
        let packet = buf
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(neighbor_ip, all_nodes_addr))
            .unwrap();
        sync_ctx.receive_ndp_packet(
            &mut non_sync_ctx,
            device,
            Ipv6SourceAddr::from_witness(neighbor_ip).unwrap(),
            all_nodes_addr,
            packet.unwrap_ndp(),
        );

        // We still do not know about the neighbor since the NA was unsolicited
        // and we never were interested in the neighbor yet.
        assert_eq!(
            StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
                &mut sync_ctx,
                device_id
            )
            .neighbors
            .get_neighbor_state(&neighbor_ip),
            None
        );

        // Receiving solicited NA from a neighbor we are trying to resolve, but
        // no target link addr.
        //
        // Should do nothing (still INCOMPLETE).

        // Create incomplete neighbor entry.
        let neighbors =
            &mut StateContext::<_, NdpState<EthernetLinkDevice>, _>::get_state_mut_with(
                &mut sync_ctx,
                device_id,
            )
            .neighbors;
        neighbors.add_incomplete_neighbor_state(neighbor_ip.get());

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            false,
            true,
            false,
            None,
            NeighborEntryState::Incomplete { transmit_counter: 1 },
            None,
        );

        // Receiving solicited NA from a neighbor we are resolving, but with
        // target link addr.
        //
        // Should update link layer address and set state to REACHABLE.

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            false,
            true,
            false,
            Some(neighbor_mac),
            NeighborEntryState::Reachable,
            Some(neighbor_mac),
        );

        // Receive unsolicited NA from a neighbor with router flag updated (no
        // target link addr).
        //
        // Should update is_router to true.

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            true,
            false,
            false,
            None,
            NeighborEntryState::Reachable,
            Some(neighbor_mac),
        );

        // Receive unsolicited NA from a neighbor without router flag set and
        // same target link addr.
        //
        // Should update is_router, state should be unchanged.

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            false,
            false,
            false,
            Some(neighbor_mac),
            NeighborEntryState::Reachable,
            Some(neighbor_mac),
        );

        // Receive unsolicited NA from a neighbor with new target link addr.
        //
        // Should NOT update link layer addr, but set state to STALE.

        let new_mac = Mac::new([99, 98, 97, 96, 95, 94]);

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            false,
            false,
            false,
            Some(new_mac),
            NeighborEntryState::Stale,
            Some(neighbor_mac),
        );

        // Receive unsolicited NA from a neighbor with new target link addr and
        // override set.
        //
        // Should update link layer addr and set state to STALE.

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            false,
            false,
            true,
            Some(new_mac),
            NeighborEntryState::Stale,
            Some(new_mac),
        );

        // Receive solicited NA from a neighbor with the same link layer addr.
        //
        // Should not update link layer addr, but set state to REACHABLE.

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            false,
            true,
            false,
            Some(new_mac),
            NeighborEntryState::Reachable,
            Some(new_mac),
        );

        // Receive unsolicited NA from a neighbor with new target link addr and
        // override set.
        //
        // Should update link layer addr, and set state to Stale.

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            false,
            false,
            true,
            Some(neighbor_mac),
            NeighborEntryState::Stale,
            Some(neighbor_mac),
        );

        // Receive solicited NA from a neighbor with new target link addr and
        // override set.
        //
        // Should set state to Reachable.

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            false,
            true,
            true,
            Some(neighbor_mac),
            NeighborEntryState::Reachable,
            Some(neighbor_mac),
        );

        // Receive unsolicited NA from a neighbor with no target link addr and
        // override set.
        //
        // Should do nothing.

        test_receiving_na_from_known_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            neighbor_ip.get(),
            config.local_ip,
            device,
            false,
            false,
            true,
            None,
            NeighborEntryState::Reachable,
            Some(neighbor_mac),
        );
    }

    fn slaac_packet_buf(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        prefix: Ipv6Addr,
        prefix_length: u8,
        on_link_flag: bool,
        autonomous_address_configuration_flag: bool,
        valid_lifetime_secs: u32,
        preferred_lifetime_secs: u32,
    ) -> Buf<Vec<u8>> {
        let p = PrefixInformation::new(
            prefix_length,
            on_link_flag,
            autonomous_address_configuration_flag,
            valid_lifetime_secs,
            preferred_lifetime_secs,
            prefix,
        );
        let options = &[NdpOptionBuilder::PrefixInformation(p)];
        OptionSequenceBuilder::new(options.iter())
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                RouterAdvertisement::new(0, false, false, 0, 0, 0),
            ))
            .encapsulate(Ipv6PacketBuilder::new(
                src_ip,
                dst_ip,
                REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                Ipv6Proto::Icmpv6,
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_b()
    }

    fn iter_global_ipv6_addrs<
        'a,
        D: LinkDevice,
        C: NdpNonSyncContext<D, SC::DeviceId>,
        SC: NdpContext<D, C>,
    >(
        sync_ctx: &'a SC,
        device_id: SC::DeviceId,
    ) -> impl Iterator<Item = &'a Ipv6AddressEntry<C::Instant>> {
        sync_ctx.get_ip_device_state(device_id).iter_addrs().filter(|entry| {
            match entry.addr_sub.addr().scope() {
                Ipv6Scope::Global => true,
                Ipv6Scope::InterfaceLocal
                | Ipv6Scope::LinkLocal
                | Ipv6Scope::AdminLocal
                | Ipv6Scope::SiteLocal
                | Ipv6Scope::OrganizationLocal
                | Ipv6Scope::Reserved(_)
                | Ipv6Scope::Unassigned(_) => false,
            }
        })
    }

    #[test]
    fn test_router_stateless_address_autoconfiguration() {
        // Routers should not perform SLAAC for global addresses.

        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);
        set_ipv6_routing_enabled(&mut sync_ctx, &mut non_sync_ctx, device, true)
            .expect("error setting routing enabled");

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&src_mac.to_eui64()[..]);

        // Receive a new RA with new prefix (autonomous).
        //
        // Should not get a new IP.

        let icmpv6_packet_buf = slaac_packet_buf(
            src_ip,
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            prefix,
            prefix_length,
            false,
            false,
            100,
            0,
        );
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );

        assert_empty(iter_global_ipv6_addrs(&sync_ctx, device.try_into().unwrap()));

        // No timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer), None);
    }

    impl From<SlaacTimerId<DeviceId>> for TimerId {
        fn from(id: SlaacTimerId<DeviceId>) -> TimerId {
            TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Slaac(id)))
        }
    }

    #[derive(Copy, Clone, Debug)]
    struct TestSlaacPrefix {
        prefix: Subnet<Ipv6Addr>,
        valid_for: u32,
        preferred_for: u32,
    }
    impl TestSlaacPrefix {
        fn send_prefix_update(
            &self,
            sync_ctx: &mut crate::testutil::DummySyncCtx,
            ctx: &mut crate::testutil::DummyNonSyncCtx,
            device: DeviceId,
            src_ip: Ipv6Addr,
        ) {
            let Self { prefix, valid_for, preferred_for } = *self;

            receive_prefix_update(sync_ctx, ctx, device, src_ip, prefix, preferred_for, valid_for);
        }

        fn valid_until<I: Instant>(&self, now: I) -> I {
            now.checked_add(Duration::from_secs(self.valid_for.into())).unwrap()
        }
    }

    fn slaac_address<I: Instant>(
        entry: &Ipv6AddressEntry<I>,
    ) -> Option<(UnicastAddr<Ipv6Addr>, SlaacConfig<I>)> {
        match entry.config {
            AddrConfig::Manual => None,
            AddrConfig::Slaac(s) => Some((entry.addr_sub().addr(), s)),
        }
    }

    /// Extracts the single static and temporary address config from the provided iterator and
    /// returns them as (static, temporary).
    ///
    /// Panics
    ///
    /// Panics if the iterator doesn't contain exactly one static and one temporary SLAAC entry.
    fn single_static_and_temporary<
        I: Copy + Debug,
        A: Copy + Debug,
        It: Iterator<Item = (A, SlaacConfig<I>)>,
    >(
        slaac_configs: It,
    ) -> ((A, SlaacConfig<I>), (A, SlaacConfig<I>)) {
        {
            let (static_addresses, temporary_addresses): (Vec<_>, Vec<_>) = slaac_configs
                .partition(|(_, s)| if let SlaacConfig::Static { .. } = s { true } else { false });

            let static_addresses: [_; 1] =
                static_addresses.try_into().expect("expected a single static address");
            let temporary_addresses: [_; 1] =
                temporary_addresses.try_into().expect("expected a single temporary address");
            (static_addresses[0], temporary_addresses[0])
        }
    }

    /// Enables temporary addressing with the provided parameters.
    ///
    /// `rng` is used to initialize the key that is used to generate new addresses.
    fn enable_temporary_addresses<R: RngCore>(
        config: &mut SlaacConfiguration,
        rng: &mut R,
        max_valid_lifetime: NonZeroDuration,
        max_preferred_lifetime: NonZeroDuration,
        max_generation_retries: u8,
    ) {
        let mut secret_key = [0; STABLE_IID_SECRET_KEY_BYTES];
        rng.fill_bytes(&mut secret_key);
        config.temporary_address_configuration = Some(TemporarySlaacAddressConfiguration {
            temp_valid_lifetime: max_valid_lifetime,
            temp_preferred_lifetime: max_preferred_lifetime,
            temp_idgen_retries: max_generation_retries,
            secret_key,
        })
    }

    fn initialize_with_temporary_addresses_enabled(
    ) -> (crate::testutil::DummyCtx, DeviceId, SlaacConfiguration) {
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let Ctx { sync_ctx, non_sync_ctx } = &mut ctx;
        let device = crate::add_ethernet_device(
            sync_ctx,
            non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(sync_ctx, non_sync_ctx, device);

        let max_valid_lifetime = Duration::from_secs(60 * 60);
        let max_preferred_lifetime = Duration::from_secs(30 * 60);
        let idgen_retries = 3;
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(max_valid_lifetime).unwrap(),
            NonZeroDuration::new(max_preferred_lifetime).unwrap(),
            idgen_retries,
        );

        crate::ip::device::update_ipv6_configuration(
            sync_ctx,
            non_sync_ctx,
            device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
            },
        );
        (ctx, device, slaac_config)
    }

    #[test]
    fn test_host_stateless_address_autoconfiguration_multiple_prefixes() {
        let (Ctx { mut sync_ctx, mut non_sync_ctx }, device, _): (_, _, SlaacConfiguration) =
            initialize_with_temporary_addresses_enabled();
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |config| {
                config.slaac_config.enable_stable_addresses = true;
            },
        );

        let prefix1 = TestSlaacPrefix {
            prefix: subnet_v6!("1:2:3:4::/64"),
            valid_for: 1500,
            preferred_for: 900,
        };
        let prefix2 = TestSlaacPrefix {
            prefix: subnet_v6!("5:6:7:8::/64"),
            valid_for: 1200,
            preferred_for: 600,
        };

        let config = Ipv6::DUMMY_CONFIG;
        let src_mac = config.remote_mac;
        let src_ip: Ipv6Addr = src_mac.to_ipv6_link_local().addr().get();

        // After the RA for the first prefix, we should have two addresses, one
        // static and one temporary.
        prefix1.send_prefix_update(&mut sync_ctx, &mut non_sync_ctx, device, src_ip);

        let (prefix_1_static, prefix_1_temporary) = {
            let slaac_configs = iter_global_ipv6_addrs(&sync_ctx, device.try_into().unwrap())
                .filter_map(slaac_address)
                .filter(|(a, _)| prefix1.prefix.contains(a));

            let (static_address, temporary_address) = single_static_and_temporary(slaac_configs);

            let now = non_sync_ctx.now();
            let prefix1_valid_until = prefix1.valid_until(now);
            assert_matches!(static_address, (_addr,
            SlaacConfig::Static { valid_until }) => {
                assert_eq!(valid_until, Lifetime::Finite(prefix1_valid_until))
            });
            assert_matches!(temporary_address, (_addr,
                SlaacConfig::Temporary(TemporarySlaacConfig {
                    valid_until,
                    creation_time,
                    desync_factor: _,
                    dad_counter: _ })) => {
                    assert_eq!(creation_time, now);
                    assert_eq!(valid_until, prefix1_valid_until);
            });
            (static_address.0, temporary_address.0)
        };

        // When the RA for the second prefix comes in, we should leave the entries for the addresses
        // in the first prefix alone.
        prefix2.send_prefix_update(&mut sync_ctx, &mut non_sync_ctx, device, src_ip);

        {
            // Check prefix 1 addresses again.
            let slaac_configs = iter_global_ipv6_addrs(&sync_ctx, device.try_into().unwrap())
                .filter_map(slaac_address)
                .filter(|(a, _)| prefix1.prefix.contains(a));
            let (static_address, temporary_address) = single_static_and_temporary(slaac_configs);

            let now = non_sync_ctx.now();
            let prefix1_valid_until = prefix1.valid_until(now);
            assert_matches!(static_address, (addr, SlaacConfig::Static { valid_until }) => {
                assert_eq!(addr, prefix_1_static);
                assert_eq!(valid_until, Lifetime::Finite(prefix1_valid_until));
            });
            assert_matches!(temporary_address,
            (addr, SlaacConfig::Temporary(TemporarySlaacConfig { valid_until, creation_time, desync_factor: _, dad_counter: 0 })) => {
                assert_eq!(addr, prefix_1_temporary);
                assert_eq!(creation_time, now);
                assert_eq!(valid_until, prefix1_valid_until);
            });
        }
        {
            // Check prefix 2 addresses.
            let slaac_configs = iter_global_ipv6_addrs(&sync_ctx, device.try_into().unwrap())
                .filter_map(slaac_address)
                .filter(|(a, _)| prefix2.prefix.contains(a));
            let (static_address, temporary_address) = single_static_and_temporary(slaac_configs);

            let now = non_sync_ctx.now();
            let prefix2_valid_until = prefix2.valid_until(now);
            assert_matches!(static_address, (_, SlaacConfig::Static { valid_until }) => {
                assert_eq!(valid_until, Lifetime::Finite(prefix2_valid_until))
            });
            assert_matches!(temporary_address,
            (_, SlaacConfig::Temporary(TemporarySlaacConfig {
                valid_until, creation_time, desync_factor: _, dad_counter: 0 })) => {
                assert_eq!(creation_time, now);
                assert_eq!(valid_until, prefix2_valid_until);
            });
        }
    }

    fn test_host_generate_temporary_slaac_address(
        valid_lifetime_in_ra: u32,
        preferred_lifetime_in_ra: u32,
    ) -> (crate::testutil::DummyCtx, DeviceId, UnicastAddr<Ipv6Addr>) {
        set_logger_for_test();
        let (mut ctx, device, slaac_config) = initialize_with_temporary_addresses_enabled();
        let Ctx { sync_ctx, non_sync_ctx } = &mut ctx;

        let max_valid_lifetime =
            slaac_config.temporary_address_configuration.unwrap().temp_valid_lifetime.get();
        let config = Ipv6::DUMMY_CONFIG;

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let subnet = subnet_v6!("0102:0304:0506:0708::/64");
        let interface_identifier = generate_opaque_interface_identifier(
            subnet,
            &config.local_mac.to_eui64()[..],
            [],
            // Clone the RNG so we can see what the next value (which will be
            // used to generate the temporary address) will be.
            OpaqueIidNonce::Random(non_sync_ctx.rng().clone().next_u64()),
            &slaac_config.temporary_address_configuration.unwrap().secret_key,
        );
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&interface_identifier.to_be_bytes()[..8]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();
        let expected_addr_sub = AddrSubnet::from_witness(expected_addr, subnet.prefix()).unwrap();
        assert_eq!(expected_addr_sub.subnet(), subnet);

        // Receive a new RA with new prefix (autonomous).
        //
        // Should get a new temporary IP.

        receive_prefix_update(
            sync_ctx,
            non_sync_ctx,
            device,
            src_ip,
            subnet,
            preferred_lifetime_in_ra,
            valid_lifetime_in_ra,
        );

        // Should have gotten a new temporary IP.
        let temporary_slaac_addresses =
            iter_global_ipv6_addrs(sync_ctx, device.try_into().unwrap())
                .filter_map(|entry| match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Static { .. }) => None,
                    AddrConfig::Slaac(SlaacConfig::Temporary(TemporarySlaacConfig {
                        creation_time: _,
                        desync_factor: _,
                        valid_until,
                        dad_counter: _,
                    })) => Some((entry.addr_sub(), entry.state, valid_until)),
                    AddrConfig::Manual => None,
                })
                .collect::<Vec<_>>();
        assert_eq!(temporary_slaac_addresses.len(), 1);
        let (addr_sub, state, valid_until) = temporary_slaac_addresses.into_iter().next().unwrap();
        assert_eq!(addr_sub.subnet(), subnet);
        assert_eq!(state, AddressState::Assigned);
        assert!(valid_until <= non_sync_ctx.now().checked_add(max_valid_lifetime).unwrap());

        (ctx, device, expected_addr)
    }

    const INFINITE_LIFETIME: u32 = u32::MAX;

    #[test]
    fn test_host_temporary_slaac_and_manual_addresses_conflict() {
        // Verify that if the temporary SLAAC address generation picks an
        // address that is already assigned, it tries again. The difficulty here
        // is that the test uses an RNG to pick an address. To make sure we
        // assign the address that the code _would_ pick, we run the code twice
        // with the same RNG seed and parameters. The first time is lets us
        // figure out the temporary address that is generated. Then, we run the
        // same code with the address already assigned to verify the behavior.
        const RNG_SEED: [u8; 16] = [1; 16];
        let config = Ipv6::DUMMY_CONFIG;
        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let subnet = subnet_v6!("0102:0304:0506:0708::/64");

        // Receive an RA to figure out the temporary address that is assigned.
        let conflicted_addr = {
            let (Ctx { mut sync_ctx, mut non_sync_ctx }, device, _config) =
                initialize_with_temporary_addresses_enabled();

            *non_sync_ctx.rng_mut() = rand::SeedableRng::from_seed(RNG_SEED);

            // Receive an RA and determine what temporary address was assigned, then return it.
            receive_prefix_update(
                &mut sync_ctx,
                &mut non_sync_ctx,
                device,
                src_ip,
                subnet,
                9000,
                10000,
            );
            *get_matching_slaac_address_entry(&sync_ctx, device, |entry| match entry.config {
                AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                AddrConfig::Manual => false,
            })
            .unwrap()
            .addr_sub()
        };
        assert!(subnet.contains(&conflicted_addr.addr().get()));

        // Now that we know what address will be assigned, create a new instance
        // of the stack and assign that same address manually.
        let (Ctx { mut sync_ctx, mut non_sync_ctx }, device, _config) =
            initialize_with_temporary_addresses_enabled();
        let device_id = device.try_into().unwrap();
        crate::device::add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            conflicted_addr.to_witness(),
        )
        .expect("adding address failed");

        // Sanity check: `conflicted_addr` is already assigned on the device.
        assert_matches!(
            iter_global_ipv6_addrs(&sync_ctx, device_id)
                .find(|entry| entry.addr_sub() == &conflicted_addr),
            Some(_)
        );

        // Seed the RNG right before the RA is received, just like in our
        // earlier run above.
        *non_sync_ctx.rng_mut() = rand::SeedableRng::from_seed(RNG_SEED);

        // Receive a new RA with new prefix (autonomous). The system will assign
        // a temporary and static SLAAC address. The first temporary address
        // tried will conflict with `conflicted_addr` assigned above, so a
        // different one will be generated.
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            9000,
            10000,
        );

        // Verify that `conflicted_addr` was generated and rejected.
        assert_eq!(get_counter_val(&non_sync_ctx, "generated_slaac_addr_exists"), 1);

        // Should have gotten a new temporary IP.
        let temporary_slaac_addresses =
            get_matching_slaac_address_entries(&sync_ctx, device, |entry| match entry.config {
                AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                AddrConfig::Manual => false,
            })
            .map(|entry| entry.addr_sub())
            .collect::<Vec<_>>();
        assert_matches!(&temporary_slaac_addresses[..], [&temporary_addr] => {
            assert_eq!(temporary_addr.subnet(), conflicted_addr.subnet());
            assert_ne!(temporary_addr, conflicted_addr);
        });
    }

    #[test]
    fn test_host_slaac_invalid_prefix_information() {
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;

        let device_id = device.try_into().unwrap();
        assert_empty(iter_global_ipv6_addrs(&sync_ctx, device_id));

        // Receive a new RA with new prefix (autonomous), but preferred lifetime
        // is greater than valid.
        //
        // Should not get a new IP.

        let icmpv6_packet_buf = slaac_packet_buf(
            src_ip,
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            prefix,
            prefix_length,
            false,
            true,
            9000,
            10000,
        );
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );
        assert_empty(iter_global_ipv6_addrs(&sync_ctx, device_id));

        // Address invalidation timers were added.
        assert_empty(non_sync_ctx.timer_ctx().timers());
    }

    #[test]
    fn test_host_slaac_address_deprecate_while_tentative() {
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = subnet_v6!("0102:0304:0506:0708::/64");
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&config.local_mac.to_eui64()[..]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();
        let expected_addr_sub = AddrSubnet::from_witness(expected_addr, prefix.prefix()).unwrap();

        // Have no addresses yet.
        let device_id = device.try_into().unwrap();
        assert_empty(iter_global_ipv6_addrs(&sync_ctx, device_id));

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |config| {
                config.ip_config.ip_enabled = true;
                config.slaac_config.enable_stable_addresses = true;

                // Doesn't matter as long as we perform DAD.
                config.dad_transmits = NonZeroU8::new(1);
            },
        );

        // Set the retransmit timer between neighbor solicitations to be greater
        // than the preferred lifetime of the prefix.
        Ipv6DeviceHandler::set_discovered_retrans_timer(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            NonZeroDuration::from_nonzero_secs(nonzero!(10u64)),
        );

        // Receive a new RA with new prefix (autonomous).
        //
        // Should get a new IP and set preferred lifetime to 1s.

        let valid_lifetime = 2;
        let preferred_lifetime = 1;

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            prefix,
            preferred_lifetime,
            valid_lifetime,
        );

        // Should have gotten a new IP.
        let now = non_sync_ctx.now();
        let valid_until = now + Duration::from_secs(valid_lifetime.into());
        let expected_address_entry = Ipv6AddressEntry {
            addr_sub: expected_addr_sub,
            state: AddressState::Tentative { dad_transmits_remaining: None },
            config: AddrConfig::Slaac(SlaacConfig::Static {
                valid_until: Lifetime::Finite(DummyInstant::from(valid_until)),
            }),
            deprecated: false,
        };
        assert_eq!(
            iter_global_ipv6_addrs(&sync_ctx, device_id).collect::<Vec<_>>(),
            [&expected_address_entry]
        );

        // Make sure deprecate and invalidation timers are set.
        non_sync_ctx.timer_ctx().assert_some_timers_installed([
            (
                SlaacTimerId::new_deprecate_slaac_address(device, expected_addr).into(),
                now + Duration::from_secs(preferred_lifetime.into()),
            ),
            (SlaacTimerId::new_invalidate_slaac_address(device, expected_addr).into(), valid_until),
        ]);

        // Trigger the deprecation timer.
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer).unwrap(),
            SlaacTimerId::new_deprecate_slaac_address(device, expected_addr).into()
        );
        assert_eq!(
            iter_global_ipv6_addrs(&sync_ctx, device_id).collect::<Vec<_>>(),
            [&Ipv6AddressEntry { deprecated: true, ..expected_address_entry }]
        );
    }

    fn receive_prefix_update(
        sync_ctx: &mut crate::testutil::DummySyncCtx,
        ctx: &mut crate::testutil::DummyNonSyncCtx,
        device: DeviceId,
        src_ip: Ipv6Addr,
        subnet: Subnet<Ipv6Addr>,
        preferred_lifetime: u32,
        valid_lifetime: u32,
    ) {
        let prefix = subnet.network();
        let prefix_length = subnet.prefix();

        let icmpv6_packet_buf = slaac_packet_buf(
            src_ip,
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            prefix,
            prefix_length,
            false,
            true,
            valid_lifetime,
            preferred_lifetime,
        );
        receive_ipv6_packet(sync_ctx, ctx, device, FrameDestination::Multicast, icmpv6_packet_buf);
    }

    fn get_matching_slaac_address_entries<F: FnMut(&&Ipv6AddressEntry<DummyInstant>) -> bool>(
        sync_ctx: &crate::testutil::DummySyncCtx,
        device: DeviceId,
        filter: F,
    ) -> impl Iterator<Item = &Ipv6AddressEntry<DummyInstant>> {
        iter_global_ipv6_addrs(sync_ctx, device.try_into().unwrap()).filter(filter)
    }

    fn get_matching_slaac_address_entry<F: FnMut(&&Ipv6AddressEntry<DummyInstant>) -> bool>(
        sync_ctx: &crate::testutil::DummySyncCtx,
        device: DeviceId,
        filter: F,
    ) -> Option<&Ipv6AddressEntry<DummyInstant>> {
        let mut matching_addrs = get_matching_slaac_address_entries(sync_ctx, device, filter);
        let entry = matching_addrs.next();
        assert_eq!(matching_addrs.next(), None);
        entry
    }

    fn get_slaac_address_entry(
        sync_ctx: &crate::testutil::DummySyncCtx,
        device: DeviceId,
        addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
    ) -> Option<&Ipv6AddressEntry<DummyInstant>> {
        let mut matching_addrs = iter_global_ipv6_addrs(sync_ctx, device.try_into().unwrap())
            .filter(|entry| *entry.addr_sub() == addr_sub);
        let entry = matching_addrs.next();
        assert_eq!(matching_addrs.next(), None);
        entry
    }

    fn assert_slaac_lifetimes_enforced(
        non_sync_ctx: &crate::testutil::DummyNonSyncCtx,
        device: DeviceId,
        entry: &Ipv6AddressEntry<DummyInstant>,
        valid_until: DummyInstant,
        preferred_until: DummyInstant,
    ) {
        assert_eq!(entry.state, AddressState::Assigned);
        assert_matches!(entry.config, AddrConfig::Slaac(_));
        let entry_valid_until = match entry.config {
            AddrConfig::Slaac(SlaacConfig::Static { valid_until }) => valid_until,
            AddrConfig::Slaac(SlaacConfig::Temporary(TemporarySlaacConfig {
                valid_until,
                desync_factor: _,
                creation_time: _,
                dad_counter: _,
            })) => Lifetime::Finite(valid_until),
            AddrConfig::Manual => unreachable!(),
        };
        assert_eq!(entry_valid_until, Lifetime::Finite(valid_until));
        non_sync_ctx.timer_ctx().assert_some_timers_installed([
            (
                SlaacTimerId::new_deprecate_slaac_address(device, entry.addr_sub().addr()).into(),
                preferred_until,
            ),
            (
                SlaacTimerId::new_invalidate_slaac_address(device, entry.addr_sub().addr()).into(),
                valid_until,
            ),
        ]);
    }

    #[test]
    fn test_host_static_slaac_valid_lifetime_updates() {
        // Make sure we update the valid lifetime only in certain scenarios
        // to prevent denial-of-service attacks as outlined in RFC 4862 section
        // 5.5.3.e. Note, the preferred lifetime should always be updated.

        set_logger_for_test();
        fn inner_test(
            sync_ctx: &mut crate::testutil::DummySyncCtx,
            ctx: &mut crate::testutil::DummyNonSyncCtx,
            device: DeviceId,
            src_ip: Ipv6Addr,
            subnet: Subnet<Ipv6Addr>,
            addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
            preferred_lifetime: u32,
            valid_lifetime: u32,
            expected_valid_lifetime: u32,
        ) {
            receive_prefix_update(
                sync_ctx,
                ctx,
                device,
                src_ip,
                subnet,
                preferred_lifetime,
                valid_lifetime,
            );
            let entry =
                get_slaac_address_entry(sync_ctx, device.try_into().unwrap(), addr_sub).unwrap();
            let now = ctx.now();
            let valid_until =
                now.checked_add(Duration::from_secs(expected_valid_lifetime.into())).unwrap();
            let preferred_until =
                now.checked_add(Duration::from_secs(preferred_lifetime.into())).unwrap();

            assert_slaac_lifetimes_enforced(ctx, device, entry, valid_until, preferred_until);
        }

        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |config| {
                config.ip_config.ip_enabled = true;
                config.slaac_config.enable_stable_addresses = true;
            },
        );

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&config.local_mac.to_eui64()[..]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();
        let expected_addr_sub = AddrSubnet::from_witness(expected_addr, prefix_length).unwrap();

        // Have no addresses yet.
        assert_empty(iter_global_ipv6_addrs(&sync_ctx, device.try_into().unwrap()));

        // Receive a new RA with new prefix (autonomous).
        //
        // Should get a new IP and set preferred lifetime to 1s.

        // Make sure deprecate and invalidation timers are set.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            30,
            60,
            60,
        );

        // If the valid lifetime is greater than the remaining lifetime, update
        // the valid lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            70,
            70,
            70,
        );

        // If the valid lifetime is greater than 2 hrs, update the valid
        // lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            7201,
            7201,
        );

        // Make remaining lifetime < 2 hrs.
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                &mut sync_ctx,
                Duration::from_secs(1000),
                crate::handle_timer
            ),
            []
        );

        // If the remaining lifetime is <= 2 hrs & valid lifetime is less than
        // that, don't update valid lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1000,
            2000,
            6201,
        );

        // Make the remaining lifetime > 2 hours.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1000,
            10800,
            10800,
        );

        // If the remaining lifetime is > 2 hours, and new valid lifetime is < 2
        // hours, set the valid lifetime to 2 hours.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1000,
            1000,
            7200,
        );

        // If the remaining lifetime is <= 2 hrs & valid lifetime is less than
        // that, don't update valid lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1000,
            2000,
            7200,
        );

        // Increase valid lifetime twice while it is greater than 2 hours.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            7201,
            7201,
        );
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            7202,
            7202,
        );

        // Make remaining lifetime < 2 hrs.
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                &mut sync_ctx,
                Duration::from_secs(1000),
                crate::handle_timer
            ),
            []
        );

        // If the remaining lifetime is <= 2 hrs & valid lifetime is less than
        // that, don't update valid lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            6202,
            6202,
        );

        // Increase valid lifetime twice while it is less than 2 hours.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            6203,
            6203,
        );
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            6204,
            6204,
        );
    }

    #[test]
    fn test_host_temporary_slaac_regenerates_address_on_dad_failure() {
        // Check that when a tentative temporary address is detected as a
        // duplicate, a new address gets created.
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );

        let router_mac = config.remote_mac;
        let router_ip = router_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();

        const MAX_VALID_LIFETIME: Duration = Duration::from_secs(15000);
        const MAX_PREFERRED_LIFETIME: Duration = Duration::from_secs(5000);

        let idgen_retries = 3;

        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(MAX_PREFERRED_LIFETIME).unwrap(),
            idgen_retries,
        );

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
                ipv6_config.ip_config.ip_enabled = true;

                // Doesn't matter as long as we perform DAD.
                ipv6_config.dad_transmits = NonZeroU8::new(1);
            },
        );

        // Send an update with lifetimes that are smaller than the ones specified in the preferences.
        let valid_lifetime = 10000;
        let preferred_lifetime = 4000;
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            router_ip,
            subnet,
            preferred_lifetime,
            valid_lifetime,
        );

        let first_addr_entry = *get_matching_slaac_address_entry(&sync_ctx, device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        assert_eq!(
            first_addr_entry.state,
            AddressState::Tentative { dad_transmits_remaining: None }
        );

        receive_neighbor_advertisement_for_duplicate_address(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            first_addr_entry.addr_sub().addr(),
        );

        // In response to the advertisement with the duplicate address, a
        // different address should be selected.
        let second_addr_entry = *get_matching_slaac_address_entry(&sync_ctx, device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        let first_addr_entry_valid = assert_matches!(first_addr_entry.config,
            AddrConfig::Slaac(SlaacConfig::Temporary(TemporarySlaacConfig {
                valid_until, creation_time: _, desync_factor: _, dad_counter: 0})) => {valid_until});
        let first_addr_sub = first_addr_entry.addr_sub();
        let second_addr_sub = second_addr_entry.addr_sub();
        assert_eq!(first_addr_sub.subnet(), second_addr_sub.subnet());
        assert_ne!(first_addr_sub.addr(), second_addr_sub.addr());

        assert_matches!(second_addr_entry.config, AddrConfig::Slaac(SlaacConfig::Temporary(
            TemporarySlaacConfig {
            valid_until,
            creation_time,
            desync_factor: _,
            dad_counter: 1,
        })) => {
            assert_eq!(creation_time, non_sync_ctx.now());
            assert_eq!(valid_until, first_addr_entry_valid);
        });
    }

    fn receive_neighbor_advertisement_for_duplicate_address(
        sync_ctx: &mut crate::testutil::DummySyncCtx,
        ctx: &mut crate::testutil::DummyNonSyncCtx,
        device: DeviceId,
        source_ip: UnicastAddr<Ipv6Addr>,
    ) {
        let peer_mac = mac!("00:11:22:33:44:55");
        let dest_ip = Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get();
        let router_flag = false;
        let solicited_flag = false;
        let override_flag = true;

        let src_ip = source_ip.get();
        receive_ipv6_packet(
            sync_ctx,
            ctx,
            device,
            FrameDestination::Multicast,
            Buf::new(
                neighbor_advertisement_message(
                    src_ip,
                    dest_ip,
                    router_flag,
                    solicited_flag,
                    override_flag,
                    Some(peer_mac),
                ),
                ..,
            )
            .encapsulate(Ipv6PacketBuilder::new(
                src_ip,
                dest_ip,
                REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                Ipv6Proto::Icmpv6,
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_b(),
        )
    }

    #[test]
    fn test_host_temporary_slaac_gives_up_after_dad_failures() {
        // Check that when the chosen tentative temporary addresses are detected
        // as duplicates enough times, the system gives up.
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );

        let router_mac = config.remote_mac;
        let router_ip = router_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();

        const MAX_VALID_LIFETIME: Duration = Duration::from_secs(15000);
        const MAX_PREFERRED_LIFETIME: Duration = Duration::from_secs(5000);

        let idgen_retries = 3;
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(MAX_PREFERRED_LIFETIME).unwrap(),
            idgen_retries,
        );

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
                ipv6_config.ip_config.ip_enabled = true;

                // Doesn't matter as long as we perform DAD.
                ipv6_config.dad_transmits = NonZeroU8::new(1);
            },
        );

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            router_ip,
            subnet,
            MAX_PREFERRED_LIFETIME.as_secs() as u32,
            MAX_VALID_LIFETIME.as_secs() as u32,
        );

        let match_temporary_address = |entry: &&Ipv6AddressEntry<DummyInstant>| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        };

        // The system should try several (1 initial + # retries) times to
        // generate an address. In the loop below, each generated address is
        // detected as a duplicate.
        let attempted_addresses: Vec<_> = (0..=idgen_retries)
            .into_iter()
            .map(|_| {
                // An address should be selected. This must be checked using DAD
                // against other hosts on the network.
                let addr_entry =
                    *get_matching_slaac_address_entry(&sync_ctx, device, match_temporary_address)
                        .unwrap();
                assert_eq!(
                    addr_entry.state,
                    AddressState::Tentative { dad_transmits_remaining: None }
                );

                // A response is received to the DAD request indicating that it
                // is a duplicate.
                receive_neighbor_advertisement_for_duplicate_address(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    device,
                    addr_entry.addr_sub().addr(),
                );

                // The address should be unassigned from the device.
                assert_eq!(
                    get_slaac_address_entry(&sync_ctx, device, *addr_entry.addr_sub()),
                    None
                );
                *addr_entry.addr_sub()
            })
            .collect();

        // After the last failed try, the system should have given up, and there
        // should be no temporary address for the subnet.
        assert_eq!(
            get_matching_slaac_address_entry(&sync_ctx, device, match_temporary_address),
            None
        );

        // All the attempted addresses should be unique.
        let unique_addresses = attempted_addresses.iter().collect::<HashSet<_>>();
        assert_eq!(
            unique_addresses.len(),
            (1 + idgen_retries).into(),
            "not all addresses are unique: {attempted_addresses:?}"
        );
    }

    #[test]
    fn test_host_temporary_slaac_deprecate_before_regen() {
        // Check that if there are multiple non-deprecated addresses in a subnet
        // and the regen timer goes off, no new address is generated. This tests
        // the following scenario:
        //
        // 1. At time T, an address A is created for a subnet whose preferred
        //    lifetime is PA. This results in a regen timer set at T + PA - R.
        // 2. At time T + PA - R, a new address B is created for the same
        //    subnet when the regen timer for A expires, with a preferred
        //    lifetime of PB (PA != PB because of the desync values).
        // 3. Before T + PA, an advertisement is received for the prefix with
        //    preferred lifetime X. Address A is now preferred until T + PA + X
        //    and regenerated at T + PA + X - R and address B is preferred until
        //    (T + PA - R) + PB + X.
        //
        // Since both addresses are preferred, we expect that when the regen
        // timer for address A goes off, it is ignored since there is already
        // another preferred address (namely B) for the subnet.
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );

        let router_mac = config.remote_mac;
        let router_ip = router_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();

        const MAX_VALID_LIFETIME: Duration = Duration::from_secs(15000);
        const MAX_PREFERRED_LIFETIME: Duration = Duration::from_secs(5000);
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(MAX_PREFERRED_LIFETIME).unwrap(),
            0,
        );

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
                ipv6_config.ip_config.ip_enabled = true;
            },
        );

        // The prefix updates contains a shorter preferred lifetime than
        // the preferences allow.
        let prefix_preferred_for: Duration = MAX_PREFERRED_LIFETIME * 2 / 3;
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            router_ip,
            subnet,
            prefix_preferred_for.as_secs().try_into().unwrap(),
            MAX_VALID_LIFETIME.as_secs().try_into().unwrap(),
        );

        let first_addr_entry = *get_matching_slaac_address_entry(&sync_ctx, device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        let regen_timer_id = SlaacTimerId::new_regenerate_temporary_slaac_address(
            device,
            *first_addr_entry.addr_sub(),
        );
        trace!("advancing to regen for first address");
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer),
            Some(regen_timer_id.into())
        );

        // The regeneration timer should cause a new address to be created in
        // the same subnet.
        assert_matches!(
            get_matching_slaac_address_entry(&sync_ctx, device, |entry| {
                entry.addr_sub().subnet() == subnet
                    && entry.addr_sub() != first_addr_entry.addr_sub()
                    && match entry.config {
                        AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                        AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                        AddrConfig::Manual => false,
                    }
            }),
            Some(_)
        );

        // Now the router sends a new update that extends the preferred lifetime
        // of addresses.
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            router_ip,
            subnet,
            prefix_preferred_for.as_secs().try_into().unwrap(),
            MAX_VALID_LIFETIME.as_secs().try_into().unwrap(),
        );
        let addresses = get_matching_slaac_address_entries(&sync_ctx, device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .map(|entry| entry.addr_sub().addr())
        .collect::<Vec<_>>();

        for address in &addresses {
            assert_matches!(
                non_sync_ctx.scheduled_instant(SlaacTimerId::new_deprecate_slaac_address(
                    device,
                    *address,
                )),
                Some(deprecate_at) => {
                    let preferred_for = deprecate_at - non_sync_ctx.now();
                    assert!(preferred_for <= prefix_preferred_for, "{:?} <= {:?}", preferred_for, prefix_preferred_for);
                }
            );
        }

        trace!("advancing to new regen for first address");
        // Running the context forward until the first address is again eligible
        // for regeneration doesn't result in a new address being created.
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer),
            Some(regen_timer_id.into())
        );
        assert_eq!(
            get_matching_slaac_address_entries(&sync_ctx, device, |entry| entry
                .addr_sub()
                .subnet()
                == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                })
            .map(|entry| entry.addr_sub().addr())
            .collect::<HashSet<_>>(),
            addresses.iter().cloned().collect()
        );

        trace!("advancing to deprecation for first address");
        // If we continue on until the first address is deprecated, we still
        // shouldn't regenerate since the second address is active.
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, crate::handle_timer),
            Some(
                SlaacTimerId::new_deprecate_slaac_address(
                    device,
                    first_addr_entry.addr_sub().addr()
                )
                .into()
            )
        );

        let remaining_addresses = addresses
            .into_iter()
            .filter(|addr| addr != &first_addr_entry.addr_sub().addr())
            .collect::<HashSet<_>>();
        assert_eq!(
            get_matching_slaac_address_entries(&sync_ctx, device, |entry| entry
                .addr_sub()
                .subnet()
                == subnet
                && !entry.deprecated
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                })
            .map(|entry| entry.addr_sub().addr())
            .collect::<HashSet<_>>(),
            remaining_addresses
        );
    }

    #[test]
    fn test_host_temporary_slaac_config_update_skips_regen() {
        // If the NDP configuration gets updated such that the target regen time
        // for an address is moved earlier than the current time, the address
        // should be regenerated immediately.
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        let device_id = device.try_into().unwrap();
        // No DAD for the auto-generated link-local address.
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |ipv6_config| {
                ipv6_config.dad_transmits = None;
                ipv6_config.ip_config.ip_enabled = true;
            },
        );

        let router_mac = config.remote_mac;
        let router_ip = router_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();

        const MAX_VALID_LIFETIME: Duration = Duration::from_secs(15000);
        let max_preferred_lifetime = Duration::from_secs(5000);
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(max_preferred_lifetime).unwrap(),
            1,
        );

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |ipv6_config| {
                // Perform DAD for later addresses.
                ipv6_config.dad_transmits = NonZeroU8::new(1);
                ipv6_config.slaac_config = slaac_config;
            },
        );

        // Set a large value for the retransmit period. This forces
        // REGEN_ADVANCE to be large, which increases the window between when an
        // address is regenerated and when it becomes deprecated.
        Ipv6DeviceHandler::set_discovered_retrans_timer(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            NonZeroDuration::new(max_preferred_lifetime / 4).unwrap(),
        );

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            router_ip,
            subnet,
            max_preferred_lifetime.as_secs().try_into().unwrap(),
            MAX_VALID_LIFETIME.as_secs().try_into().unwrap(),
        );

        let first_addr_entry = *get_matching_slaac_address_entry(&sync_ctx, device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        let regen_at = non_sync_ctx
            .scheduled_instant(SlaacTimerId::new_regenerate_temporary_slaac_address(
                device,
                *first_addr_entry.addr_sub(),
            ))
            .unwrap();

        let before_regen = regen_at - Duration::from_secs(10);
        // The only events that run before regen should be the DAD timers for
        // the static and temporary address that were created earlier.
        let dad_timer_ids = get_matching_slaac_address_entries(&sync_ctx, device, |entry| {
            entry.addr_sub().subnet() == subnet
        })
        .map(|entry| dad_timer_id(device_id, entry.addr_sub().addr()))
        .collect::<Vec<_>>();
        non_sync_ctx.trigger_timers_until_and_expect_unordered(
            &mut sync_ctx,
            before_regen,
            dad_timer_ids,
            crate::handle_timer,
        );

        let preferred_until = non_sync_ctx
            .scheduled_instant(SlaacTimerId::new_deprecate_slaac_address(
                device,
                first_addr_entry.addr_sub().addr(),
            ))
            .unwrap();

        let max_preferred_lifetime = max_preferred_lifetime * 4 / 5;
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(max_preferred_lifetime).unwrap(),
            1,
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
            },
        );

        // Receiving this update should result in requiring a regen time that is
        // before the current time. The address should be regenerated
        // immediately.
        let prefix_preferred_for = preferred_until - non_sync_ctx.now();

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            router_ip,
            subnet,
            prefix_preferred_for.as_secs().try_into().unwrap(),
            MAX_VALID_LIFETIME.as_secs().try_into().unwrap(),
        );

        // The regeneration is still handled by timer, so handle any pending
        // events.
        assert_eq!(
            non_sync_ctx.trigger_timers_for(&mut sync_ctx, Duration::ZERO, crate::handle_timer),
            vec![SlaacTimerId::new_regenerate_temporary_slaac_address(
                device,
                *first_addr_entry.addr_sub()
            )
            .into()]
        );

        let addresses = get_matching_slaac_address_entries(&sync_ctx, device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .map(|entry| entry.addr_sub().addr())
        .collect::<HashSet<_>>();
        assert!(addresses.contains(&first_addr_entry.addr_sub().addr()));
        assert_eq!(addresses.len(), 2);
    }

    #[test]
    fn test_host_temporary_slaac_lifetime_updates_respect_max() {
        // Make sure that the preferred and valid lifetimes of the NDP
        // configuration are respected.

        let src_mac = Ipv6::DUMMY_CONFIG.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let subnet = subnet_v6!("0102:0304:0506:0708::/64");
        let (Ctx { mut sync_ctx, mut non_sync_ctx }, device, config) =
            initialize_with_temporary_addresses_enabled();
        let now = non_sync_ctx.now();
        let start = now;
        let temporary_address_config = config.temporary_address_configuration.unwrap();

        let max_valid_lifetime = temporary_address_config.temp_valid_lifetime;
        let max_valid_until = now.checked_add(max_valid_lifetime.get()).unwrap();
        let max_preferred_lifetime = temporary_address_config.temp_preferred_lifetime;
        let max_preferred_until = now.checked_add(max_preferred_lifetime.get()).unwrap();
        let secret_key = temporary_address_config.secret_key;

        let interface_identifier = generate_opaque_interface_identifier(
            subnet,
            &Ipv6::DUMMY_CONFIG.local_mac.to_eui64()[..],
            [],
            // Clone the RNG so we can see what the next value (which will be
            // used to generate the temporary address) will be.
            OpaqueIidNonce::Random(non_sync_ctx.rng().clone().next_u64()),
            &secret_key,
        );
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&interface_identifier.to_be_bytes()[..8]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();
        let expected_addr_sub = AddrSubnet::from_witness(expected_addr, subnet.prefix()).unwrap();

        // Send an update with lifetimes that are smaller than the ones specified in the preferences.
        let valid_lifetime = 2000;
        let preferred_lifetime = 1500;
        assert!(u64::from(valid_lifetime) < max_valid_lifetime.get().as_secs());
        assert!(u64::from(preferred_lifetime) < max_preferred_lifetime.get().as_secs());
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            preferred_lifetime,
            valid_lifetime,
        );

        let entry = get_slaac_address_entry(&sync_ctx, device, expected_addr_sub).unwrap();
        let expected_valid_until =
            now.checked_add(Duration::from_secs(valid_lifetime.into())).unwrap();
        let expected_preferred_until =
            now.checked_add(Duration::from_secs(preferred_lifetime.into())).unwrap();
        assert!(
            expected_valid_until < max_valid_until,
            "expected {:?} < {:?}",
            expected_valid_until,
            max_valid_until
        );
        assert!(expected_preferred_until < max_preferred_until);

        assert_slaac_lifetimes_enforced(
            &non_sync_ctx,
            device,
            entry,
            expected_valid_until,
            expected_preferred_until,
        );

        // After some time passes, another update is received with the same lifetimes for the
        // prefix. Per RFC 8981 Section 3.4.1, the lifetimes for the address should obey the
        // overall constraints expressed in the preferences.

        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                &mut sync_ctx,
                Duration::from_secs(1000),
                crate::handle_timer
            ),
            []
        );
        let now = non_sync_ctx.now();
        let expected_valid_until =
            now.checked_add(Duration::from_secs(valid_lifetime.into())).unwrap();
        let expected_preferred_until =
            now.checked_add(Duration::from_secs(preferred_lifetime.into())).unwrap();

        // The preferred lifetime advertised by the router is now past the max allowed by
        // the NDP configuration.
        assert!(expected_preferred_until > max_preferred_until);

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            src_ip,
            subnet,
            preferred_lifetime,
            valid_lifetime,
        );

        let entry = get_matching_slaac_address_entry(&sync_ctx, device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        let desync_factor = match entry.config {
            AddrConfig::Slaac(SlaacConfig::Temporary(TemporarySlaacConfig {
                desync_factor,
                creation_time: _,
                valid_until: _,
                dad_counter: _,
            })) => desync_factor,
            AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => {
                unreachable!("temporary address")
            }
            AddrConfig::Manual => unreachable!("temporary slaac address"),
        };
        assert_slaac_lifetimes_enforced(
            &non_sync_ctx,
            device,
            entry,
            expected_valid_until,
            max_preferred_until - desync_factor,
        );

        // Update the max allowed lifetime in the NDP configuration. This won't take effect until
        // the next router advertisement is reeived.
        let max_valid_lifetime = max_preferred_lifetime;
        let idgen_retries = 3;
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            max_valid_lifetime,
            max_preferred_lifetime,
            idgen_retries,
        );

        crate::ip::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |config| {
                config.slaac_config = slaac_config;
            },
        );
        // The new valid time is measured from the time at which the address was created (`start`),
        // not the current time (`now`). That means the max valid lifetime takes precedence over
        // the router's advertised valid lifetime.
        let max_valid_until = start.checked_add(max_valid_lifetime.get()).unwrap();
        assert!(expected_valid_until > max_valid_until);

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device.try_into().unwrap(),
            src_ip,
            subnet,
            preferred_lifetime,
            valid_lifetime,
        );

        let entry = get_matching_slaac_address_entry(&sync_ctx, device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        assert_slaac_lifetimes_enforced(
            &non_sync_ctx,
            device,
            entry,
            max_valid_until,
            max_preferred_until - desync_factor,
        );
    }

    #[test]
    fn test_remove_stable_slaac_address() {
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { mut sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let device = crate::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            |config| {
                config.ip_config.ip_enabled = true;
                config.slaac_config.enable_stable_addresses = true;
            },
        );

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&config.local_mac.to_eui64()[..]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();

        // Receive a new RA with new prefix (autonomous).
        //
        // Should get a new IP.

        const VALID_LIFETIME_SECS: u32 = 10000;
        const PREFERRED_LIFETIME_SECS: u32 = 9000;

        let icmpv6_packet_buf = slaac_packet_buf(
            src_ip,
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            prefix,
            prefix_length,
            false,
            true,
            VALID_LIFETIME_SECS,
            PREFERRED_LIFETIME_SECS,
        );
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );

        // Should have gotten a new IP.
        let now = non_sync_ctx.now();
        let valid_until = now + Duration::from_secs(VALID_LIFETIME_SECS.into());
        let expected_address_entry = Ipv6AddressEntry {
            addr_sub: AddrSubnet::new(expected_addr.get(), prefix_length).unwrap(),
            state: AddressState::Assigned,
            config: AddrConfig::Slaac(SlaacConfig::Static {
                valid_until: Lifetime::Finite(DummyInstant::from(valid_until)),
            }),
            deprecated: false,
        };
        let device_id = device.try_into().unwrap();
        assert_eq!(
            iter_global_ipv6_addrs(&sync_ctx, device_id).collect::<Vec<_>>(),
            [&expected_address_entry]
        );
        // Make sure deprecate and invalidation timers are set.
        non_sync_ctx.timer_ctx().assert_some_timers_installed([
            (
                SlaacTimerId::new_deprecate_slaac_address(device, expected_addr).into(),
                now + Duration::from_secs(PREFERRED_LIFETIME_SECS.into()),
            ),
            (SlaacTimerId::new_invalidate_slaac_address(device, expected_addr).into(), valid_until),
        ]);

        // Deleting the address should cancel its SLAAC timers.
        del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, device, &expected_addr.into_specified())
            .unwrap();
        assert_empty(iter_global_ipv6_addrs(&sync_ctx, device_id));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }

    #[test]
    fn test_remove_temporary_slaac_address() {
        // We use the infinite lifetime so that the stable address does not have
        // any timers as it is valid and preferred forever. As a result, we will
        // only observe timers for temporary addresses.
        let (Ctx { mut sync_ctx, mut non_sync_ctx }, device, expected_addr) =
            test_host_generate_temporary_slaac_address(INFINITE_LIFETIME, INFINITE_LIFETIME);

        // Deleting the address should cancel its SLAAC timers.
        del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, device, &expected_addr.into_specified())
            .unwrap();
        assert_empty(iter_global_ipv6_addrs(&sync_ctx, device.try_into().unwrap()).filter(|e| {
            match e.config {
                AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                AddrConfig::Manual => false,
            }
        }));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }
}
