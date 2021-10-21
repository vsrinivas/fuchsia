// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for types in the `fidl_fuchsia_net_neighbor` crate.

#![deny(missing_docs)]

use fidl_fuchsia_net_ext::{IpAddress, MacAddress};
use fidl_fuchsia_net_neighbor as fidl;

/// Information on a neighboring device in the local network.
pub struct Entry(fidl::Entry);

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

impl std::fmt::Display for Entry {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        let Self(fidl::Entry { interface, neighbor, mac, state, updated_at: _, .. }) = self;

        write_field!(f, "Interface", interface, "|");
        write_field!(f, "IP", neighbor.map(IpAddress::from), "|");
        write_field!(f, "MAC", mac.map(MacAddress::from), "|");
        match state {
            None => write!(f, "?"),
            Some(fidl::EntryState::Incomplete) => write!(f, "INCOMPLETE"),
            Some(fidl::EntryState::Reachable) => write!(f, "REACHABLE"),
            Some(fidl::EntryState::Stale) => write!(f, "STALE"),
            Some(fidl::EntryState::Delay) => write!(f, "DELAY"),
            Some(fidl::EntryState::Probe) => write!(f, "PROBE"),
            Some(fidl::EntryState::Static) => write!(f, "STATIC"),
            Some(fidl::EntryState::Unreachable) => write!(f, "UNREACHABLE"),
        }
    }
}
