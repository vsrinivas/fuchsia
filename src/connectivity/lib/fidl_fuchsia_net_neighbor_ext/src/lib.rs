// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for types in the `fidl_fuchsia_net_neighbor` crate.

#![deny(missing_docs)]

use fidl_fuchsia_net_ext::{IpAddress, MacAddress};
use fidl_fuchsia_net_neighbor as fidl;

/// Information on a neighboring device in the local network.
pub struct Entry(pub fidl::Entry);

impl From<fidl::Entry> for Entry {
    fn from(entry: fidl::Entry) -> Self {
        Entry(entry)
    }
}

macro_rules! write_field {
    ($f:expr, $field_name:literal, $field:expr, $sep:literal) => {
        let () = write!($f, "{} ", $field_name)?;
        match $field {
            None => {
                let () = write!($f, "?")?;
            }
            Some(val) => {
                let () = write!($f, "{}", val)?;
            }
        }
        let () = write!($f, " {} ", $sep)?;
    };
}

// TODO(https://fxbug.dev/90069): introduce a validated Entry struct and
// EntryState enum type in the same shape as UpdateResult in
// fidl_fuchsia_net_interfaces_ext.
/// Returns a &str suitable for display representing the EntryState parameter.
pub fn display_entry_state(state: &Option<fidl::EntryState>) -> &'static str {
    match state {
        None => "?",
        Some(fidl::EntryState::Incomplete) => "INCOMPLETE",
        Some(fidl::EntryState::Reachable) => "REACHABLE",
        Some(fidl::EntryState::Stale) => "STALE",
        Some(fidl::EntryState::Delay) => "DELAY",
        Some(fidl::EntryState::Probe) => "PROBE",
        Some(fidl::EntryState::Static) => "STATIC",
        Some(fidl::EntryState::Unreachable) => "UNREACHABLE",
    }
}

impl std::fmt::Display for Entry {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        let Self(fidl::Entry { interface, neighbor, mac, state, updated_at: _, .. }) = self;

        write_field!(f, "Interface", interface, "|");
        write_field!(f, "IP", neighbor.map(IpAddress::from), "|");
        write_field!(f, "MAC", mac.map(MacAddress::from), "|");
        write!(f, "{}", display_entry_state(state))
    }
}
