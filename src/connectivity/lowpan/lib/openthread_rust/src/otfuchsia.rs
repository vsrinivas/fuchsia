// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia-specific type conversions
//!
//! This module contains various conversions and helpers that can only
//! be implemented properly in this crate.

use crate::prelude_internal::*;
pub use fuchsia_syslog::macros::*;
use fuchsia_zircon::Status as ZxStatus;

impl From<ot::Error> for ZxStatus {
    fn from(err: ot::Error) -> Self {
        match err {
            ot::Error::NotImplemented => ZxStatus::NOT_SUPPORTED,
            ot::Error::InvalidArgs => ZxStatus::INVALID_ARGS,
            ot::Error::InvalidState => ZxStatus::BAD_STATE,
            ot::Error::NoBufs => ZxStatus::NO_MEMORY,
            ot::Error::NotFound => ZxStatus::NOT_FOUND,
            other => {
                fx_log_warn!(
                    "Unable to convert {:?} to fuchsia_zircon::Status, will use INTERNAL",
                    other
                );
                ZxStatus::INTERNAL
            }
        }
    }
}

impl From<ot::ChannelOutOfRange> for ZxStatus {
    fn from(_: ot::ChannelOutOfRange) -> Self {
        ZxStatus::OUT_OF_RANGE
    }
}

impl From<Ip6NetworkPrefix> for fidl_fuchsia_net::Ipv6Address {
    /// Makes a [`fidl_fuchsia_net::Ipv6Address`] from a [`Ip6NetworkPrefix`],
    /// filling in the last 64 bits with zeros.
    fn from(prefix: Ip6NetworkPrefix) -> Self {
        let mut octets = [0u8; 16];
        octets[0..8].clone_from_slice(prefix.as_slice());
        fidl_fuchsia_net::Ipv6Address { addr: octets }
    }
}

impl From<fidl_fuchsia_net::Ipv6Address> for Ip6NetworkPrefix {
    /// Extracts the first 64 bits of a [`fidl_fuchsia_net::Ipv6Address`] to make
    /// a [`Ip6NetworkPrefix`].
    fn from(x: fidl_fuchsia_net::Ipv6Address) -> Self {
        let mut ret = Ip6NetworkPrefix::default();
        ret.0.m8.clone_from_slice(&x.addr[0..8]);
        ret
    }
}

impl From<fidl_fuchsia_net::Ipv6SocketAddress> for SockAddr {
    fn from(x: fidl_fuchsia_net::Ipv6SocketAddress) -> Self {
        SockAddr::new(Ip6Address::from(x.address.addr), x.port)
    }
}

impl From<SockAddr> for fidl_fuchsia_net::Ipv6SocketAddress {
    fn from(x: SockAddr) -> Self {
        fidl_fuchsia_net::Ipv6SocketAddress {
            address: fidl_fuchsia_net::Ipv6Address { addr: x.addr().octets() },
            port: x.port(),
            zone_index: 0,
        }
    }
}
