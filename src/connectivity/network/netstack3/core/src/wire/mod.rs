// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Serialization and deserialization of wire formats.
//!
//! This module provides efficient serialization and deserialization of the
//! various wire formats used by this program. Where possible, it uses lifetimes
//! and immutability to allow for safe zero-copy parsing.
//!
//! # Endianness
//!
//! All values exposed or consumed by this crate are in host byte order, so the
//! caller does not need to worry about it. Any necessary conversions are
//! performed under the hood.

// TODO(joshlf): Move into debug_err! and debug_err_fn! definitions once
// attributes are allowed on expressions
// (https://github.com/rust-lang/rust/issues/15701).
#![allow(clippy::block_in_if_condition_stmt)]

/// Emit a debug message and return an error.
///
/// Invoke the `debug!` macro on all but the first argument. A call to
/// `debug_err!(err, ...)` is an expression whose value is the expression `err`.
macro_rules! debug_err {
    ($err:expr, $($arg:tt)*) => (
        // TODO(joshlf): Uncomment once attributes are allowed on expressions
        // #[cfg_attr(feature = "cargo-clippy", allow(block_in_if_condition_stmt))]
        {
            use ::log::debug;
            debug!($($arg)*);
            $err
        }
    )
}

/// Create a closure which emits a debug message and returns an error.
///
/// Create a closure which, when called, invokes the `debug!` macro on all but
/// the first argument, and returns the first argument.
macro_rules! debug_err_fn {
    ($err:expr, $($arg:tt)*) => (
        // TODO(joshlf): Uncomment once attributes are allowed on expressions
        // #[cfg_attr(feature = "cargo-clippy", allow(block_in_if_condition_stmt))]
        || {
            use ::log::debug;
            debug!($($arg)*);
            $err
        }
    )
}

#[macro_use]
mod macros;
pub(crate) mod arp;
pub(crate) mod ethernet;
pub(crate) mod icmp;
pub(crate) mod igmp;
pub(crate) mod ipv4;
pub(crate) mod ipv6;
pub(crate) mod records;
pub(crate) mod tcp;

#[cfg(test)]
pub(crate) mod testdata;
pub(crate) mod udp;

use std::convert::TryInto;

use byteorder::{ByteOrder, NetworkEndian};
use internet_checksum::Checksum;
use specialize_ip_macro::specialize_ip_address;

use crate::ip::{IpAddress, IpProto};

pub(crate) type U16 = zerocopy::U16<NetworkEndian>;
pub(crate) type U32 = zerocopy::U32<NetworkEndian>;

/// Compute the checksum used by TCP and UDP.
///
/// `compute_transport_checksum` computes the checksum used by TCP and UDP. For
/// IPv4, the total packet length must fit in a `u16`, and for IPv6, a `u32`. If
/// the provided packet is too big, `compute_transport_checksum` returns `None`.
#[specialize_ip_address]
pub(crate) fn compute_transport_checksum<A: IpAddress>(
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    packet: &[u8],
) -> Option<u16> {
    // See for details:
    // https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Checksum_computation
    #[ipv4addr]
    let pseudo_header = {
        // 4 bytes for src_ip + 4 bytes for dst_ip + 1 byte of zeros + 1 byte
        // for protocol + 2 bytes for total_len
        let mut pseudo_header = [0u8; 12];
        (&mut pseudo_header[..4]).copy_from_slice(src_ip.bytes());
        (&mut pseudo_header[4..8]).copy_from_slice(dst_ip.bytes());
        pseudo_header[9] = proto.into();
        NetworkEndian::write_u16(&mut pseudo_header[10..12], packet.len().try_into().ok()?);
        pseudo_header
    };
    #[ipv6addr]
    let pseudo_header = {
        // 16 bytes for src_ip + 16 bytes for dst_ip + 4 bytes for total_len + 3
        // bytes of zeroes + 1 byte for next header
        let mut pseudo_header = [0u8; 40];
        (&mut pseudo_header[..16]).copy_from_slice(src_ip.bytes());
        (&mut pseudo_header[16..32]).copy_from_slice(dst_ip.bytes());
        NetworkEndian::write_u32(&mut pseudo_header[32..36], packet.len().try_into().ok()?);
        pseudo_header[39] = proto.into();
        pseudo_header
    };
    let mut checksum = Checksum::new();
    // add_bytes contains some branching logic at the beginning which is a bit
    // more expensive than the main loop of the algorithm. In order to make sure
    // we go through that logic as few times as possible, we construct the
    // entire pseudo-header first, and then add it to the checksum all at once.
    checksum.add_bytes(&pseudo_header[..]);
    checksum.add_bytes(packet);
    Some(checksum.checksum())
}
