// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for types in the `fidl_fuchsia_net_neighbor` crate.

#![deny(missing_docs)]

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_neighbor as fnet_neighbor;
use fidl_table_validation::*;
use fuchsia_zircon_types as zx;

/// Information on a neighboring device in the local network.
#[derive(Clone, Debug, Eq, PartialEq, ValidFidlTable)]
#[fidl_table_src(fnet_neighbor::Entry)]
pub struct Entry {
    /// Identifier for the interface used for communicating with the neighbor.
    pub interface: u64,
    /// IP address of the neighbor.
    pub neighbor: fnet::IpAddress,
    /// State of the entry within the Neighbor Unreachability Detection (NUD)
    /// state machine.
    pub state: fnet_neighbor::EntryState,
    /// MAC address of the neighboring device's network interface controller.
    #[fidl_field_type(optional)]
    pub mac: Option<fnet::MacAddress>,
    /// Timestamp when this entry has changed `state`.
    // TODO(https://fxbug.dev/75531): Replace with zx::Time once there is
    // support for custom conversion functions.
    pub updated_at: zx::zx_time_t,
}

/// Returns a &str suitable for display representing the EntryState parameter.
pub fn display_entry_state(state: &fnet_neighbor::EntryState) -> &'static str {
    match state {
        fnet_neighbor::EntryState::Incomplete => "INCOMPLETE",
        fnet_neighbor::EntryState::Reachable => "REACHABLE",
        fnet_neighbor::EntryState::Stale => "STALE",
        fnet_neighbor::EntryState::Delay => "DELAY",
        fnet_neighbor::EntryState::Probe => "PROBE",
        fnet_neighbor::EntryState::Static => "STATIC",
        fnet_neighbor::EntryState::Unreachable => "UNREACHABLE",
    }
}

impl std::fmt::Display for Entry {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        let Self { interface, neighbor, mac, state, updated_at: _ } = self;
        write!(f, "Interface {} | IP {} | MAC ", interface, fnet_ext::IpAddress::from(*neighbor))?;
        if let Some(mac) = mac {
            write!(f, "{}", fnet_ext::MacAddress::from(*mac))?;
        } else {
            write!(f, "?")?;
        }
        write!(f, " | {}", display_entry_state(state))
    }
}
