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

use crate::ip::Ipv6Addr;
use crate::{Context, EventDispatcher};
use std::collections::HashMap;

/// A link layer address that can be discovered using NDP.
pub(crate) trait LinkLayerAddress: Copy + Clone {}

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

/// Look up the link layer address
pub(crate) fn lookup<D: EventDispatcher, ND: NdpDevice>(
    ctx: &mut Context<D>,
    device_id: u64,
    lookup_addr: Ipv6Addr,
) -> Option<ND::LinkAddress> {
    // TODO(brunodalbo): Figure out what to do if a frame can't be sent
    let result = ND::get_ndp_state(ctx, device_id).neighbors.lookup_link_addr(lookup_addr).cloned();

    // Send an Neighbor Solicitation Request if the address is not in our cache
    if result.is_none() {
        log_unimplemented!((), "Neighbor Solicitation queries not implemented")
    }

    result
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
        Self { link_address: LinkAddressResolutionValue::Waiting }
    }
}

#[derive(Debug, Eq, PartialEq)] // for testing
enum LinkAddressResolutionValue<H> {
    Known(H),
    Waiting,
}

struct NeighborTable<H> {
    table: HashMap<Ipv6Addr, NeighborState<H>>,
}

impl<H> NeighborTable<H> {
    fn set_link_address(&mut self, neighbor: Ipv6Addr, link: H) {
        self.table.entry(neighbor).or_insert_with(|| NeighborState::new()).link_address =
            LinkAddressResolutionValue::Known(link);
    }

    fn set_waiting_link_address(&mut self, neighbor: Ipv6Addr) {
        self.table.entry(neighbor).or_insert_with(|| NeighborState::new()).link_address =
            LinkAddressResolutionValue::Waiting;
    }

    fn lookup_link_addr(&self, neighbor: Ipv6Addr) -> Option<&H> {
        match self.table.get(&neighbor) {
            Some(NeighborState { link_address: LinkAddressResolutionValue::Known(x) }) => Some(x),
            _ => None,
        }
    }
}

impl<H> Default for NeighborTable<H> {
    fn default() -> Self {
        NeighborTable { table: HashMap::default() }
    }
}
