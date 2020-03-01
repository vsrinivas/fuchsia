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
pub(crate) mod tcp;

#[cfg(test)]
pub(crate) mod testdata;
pub(crate) mod udp;

use core::convert::TryInto;

use byteorder::{ByteOrder, NetworkEndian};
use internet_checksum::Checksum;
use net_types::ip::IpAddress;
use packet::SerializeBuffer;
use specialize_ip_macro::specialize_ip_address;

use crate::ip::IpProto;

pub(crate) type U16 = zerocopy::U16<NetworkEndian>;
pub(crate) type U32 = zerocopy::U32<NetworkEndian>;

#[specialize_ip_address]
fn update_transport_checksum_pseudo_header<A: IpAddress>(
    checksum: &mut Checksum,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    transport_len: usize,
) -> Result<(), core::num::TryFromIntError> {
    #[ipv4addr]
    let pseudo_header = {
        // 4 bytes for src_ip + 4 bytes for dst_ip + 1 byte of zeros + 1 byte
        // for protocol + 2 bytes for total_len
        let mut pseudo_header = [0u8; 12];
        (&mut pseudo_header[..4]).copy_from_slice(src_ip.bytes());
        (&mut pseudo_header[4..8]).copy_from_slice(dst_ip.bytes());
        pseudo_header[9] = proto.into();
        NetworkEndian::write_u16(&mut pseudo_header[10..12], transport_len.try_into()?);
        pseudo_header
    };
    #[ipv6addr]
    let pseudo_header = {
        // 16 bytes for src_ip + 16 bytes for dst_ip + 4 bytes for total_len + 3
        // bytes of zeroes + 1 byte for next header
        let mut pseudo_header = [0u8; 40];
        (&mut pseudo_header[..16]).copy_from_slice(src_ip.bytes());
        (&mut pseudo_header[16..32]).copy_from_slice(dst_ip.bytes());
        NetworkEndian::write_u32(&mut pseudo_header[32..36], transport_len.try_into()?);
        pseudo_header[39] = proto.into();
        pseudo_header
    };
    // add_bytes contains some branching logic at the beginning which is a bit
    // more expensive than the main loop of the algorithm. In order to make sure
    // we go through that logic as few times as possible, we construct the
    // entire pseudo-header first, and then add it to the checksum all at once.
    checksum.add_bytes(&pseudo_header[..]);
    Ok(())
}

/// Compute the checksum used by TCP and UDP.
///
/// `compute_transport_checksum` computes the checksum used by TCP and UDP. For
/// IPv4, the total packet length `transport_len` must fit in a `u16`, and for
/// IPv6, a `u32`. If the provided packet is too big,
/// `compute_transport_checksum` returns `None`.
pub(crate) fn compute_transport_checksum_parts<'a, A: IpAddress, I>(
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    parts: I,
) -> Option<[u8; 2]>
where
    I: Iterator<Item = &'a &'a [u8]> + Clone,
{
    // See for details:
    // https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Checksum_computation
    let mut checksum = Checksum::new();
    let transport_len = parts.clone().map(|b| b.len()).sum();
    update_transport_checksum_pseudo_header(&mut checksum, src_ip, dst_ip, proto, transport_len)
        .ok()?;
    for p in parts {
        checksum.add_bytes(p);
    }
    Some(checksum.checksum())
}

/// Compute the checksum used by TCP and UDP.
///
/// Same as [`compute_transport_checksum_parts`] but gets the parts from a
/// `SerializeBuffer`.
pub(crate) fn compute_transport_checksum_serialize<A: IpAddress>(
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    buffer: &mut SerializeBuffer,
) -> Option<[u8; 2]> {
    // See for details:
    // https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Checksum_computation
    let mut checksum = Checksum::new();
    let transport_len = buffer.len();
    update_transport_checksum_pseudo_header(&mut checksum, src_ip, dst_ip, proto, transport_len)
        .ok()?;

    checksum.add_bytes(buffer.header());
    for p in buffer.body().iter_fragments() {
        checksum.add_bytes(p);
    }
    checksum.add_bytes(buffer.footer());
    Some(checksum.checksum())
}

/// Compute the checksum used by TCP and UDP.
///
/// Same as [`compute_transport_checksum_parts`] but with a single part.
#[cfg(test)]
pub(crate) fn compute_transport_checksum<A: IpAddress>(
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    packet: &[u8],
) -> Option<[u8; 2]> {
    let mut checksum = Checksum::new();
    update_transport_checksum_pseudo_header(&mut checksum, src_ip, dst_ip, proto, packet.len())
        .ok()?;
    checksum.add_bytes(packet);
    Some(checksum.checksum())
}
