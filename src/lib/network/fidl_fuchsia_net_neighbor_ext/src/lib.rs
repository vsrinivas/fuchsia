// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for types in the `fidl_fuchsia_net_neighbor` crate.

#![deny(missing_docs)]

use std::convert::TryInto as _;

use fidl_fuchsia_net_ext::{IpAddress, MacAddress};
use fidl_fuchsia_net_neighbor as fidl;

use chrono::{Local, SecondsFormat, TimeZone};

const NANOS_PER_SEC: i64 = 1_000_000_000;

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
        let Self(fidl::Entry { interface, neighbor, mac, state, updated_at: _ }) = self;

        write_field!(f, "Interface", interface, "|");
        write_field!(f, "IP", neighbor.map(IpAddress::from), "|");
        write_field!(f, "MAC", mac.map(MacAddress::from), "|");
        match state {
            None => write!(f, "?"),
            Some(fidl::EntryState::Incomplete(fidl::IncompleteState {})) => write!(f, "INCOMPETE"),
            Some(fidl::EntryState::Reachable(fidl::ReachableState { expires_at })) => {
                write!(f, "REACHABLE | ")?;
                match expires_at {
                    None => write!(f, "Expiration unknown"),
                    Some(expires_at) => {
                        let sec = expires_at / NANOS_PER_SEC;
                        let ns = expires_at % NANOS_PER_SEC;
                        match ns.try_into() {
                            Err(std::num::TryFromIntError { .. }) => {
                                write!(f, "Invalid nanosecond expiration of {}", ns)
                            }
                            Ok(ns) => match Local.timestamp_opt(sec, ns).single() {
                                None => write!(f, "Invalid expiration"),
                                Some(exp) => write!(
                                    f,
                                    "Expires at {}",
                                    exp.to_rfc3339_opts(SecondsFormat::Secs, true)
                                ),
                            },
                        }
                    }
                }
            }
            Some(fidl::EntryState::Stale(fidl::StaleState {})) => write!(f, "STALE"),
            Some(fidl::EntryState::Delay(fidl::DelayState {})) => write!(f, "DELAY"),
            Some(fidl::EntryState::Probe(fidl::ProbeState {})) => write!(f, "PROBE"),
            Some(fidl::EntryState::Static_(fidl::StaticState {})) => write!(f, "STATIC"),
        }
    }
}
