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

#![feature(specialization)]
#![cfg_attr(not(test), no_std)]
// TODO(joshlf): Move into debug_err! and debug_err_fn! definitions once
// attributes are allowed on expressions
// (https://github.com/rust-lang/rust/issues/15701).
#![allow(clippy::blocks_in_if_conditions)]
#![deny(missing_docs, unreachable_patterns)]

extern crate alloc;

// TODO(https://github.com/dtolnay/thiserror/pull/64): remove this module.
#[cfg(not(test))]
extern crate fakestd as std;

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
pub mod arp;
pub mod error;
pub mod ethernet;
pub mod icmp;
pub mod igmp;
pub mod ip;
pub mod ipv4;
pub mod ipv6;
pub mod tcp;
pub mod testdata;
pub mod testutil;
pub mod udp;
pub mod utils;

use core::convert::TryInto;

use internet_checksum::Checksum;
use net_types::ip::{IpAddress, Ipv4Addr, Ipv6Addr};
use packet::SerializeBuffer;
use zerocopy::byteorder::{ByteOrder, NetworkEndian};

use crate::ip::{IpExt, Ipv4Proto, Ipv6Proto};

// The "sealed trait" pattern.
//
// https://rust-lang.github.io/api-guidelines/future-proofing.html
mod private {
    pub trait Sealed {}
}

type U16 = zerocopy::U16<NetworkEndian>;
type U32 = zerocopy::U32<NetworkEndian>;

trait IpAddressExt: IpAddress
where
    <Self as IpAddress>::Version: IpExt,
{
    fn update_transport_checksum_pseudo_header(
        checksum: &mut Checksum,
        src_ip: Self,
        dst_ip: Self,
        proto: <Self::Version as IpExt>::Proto,
        transport_len: usize,
    ) -> Result<(), core::num::TryFromIntError>;
}

impl<A: IpAddress> IpAddressExt for A {
    default fn update_transport_checksum_pseudo_header(
        _checksum: &mut Checksum,
        _src_ip: A,
        _dst_ip: A,
        _proto: <Self::Version as IpExt>::Proto,
        _transport_len: usize,
    ) -> Result<(), core::num::TryFromIntError> {
        unimplemented!()
    }
}

impl IpAddressExt for Ipv4Addr {
    fn update_transport_checksum_pseudo_header(
        checksum: &mut Checksum,
        src_ip: Ipv4Addr,
        dst_ip: Ipv4Addr,
        proto: Ipv4Proto,
        transport_len: usize,
    ) -> Result<(), core::num::TryFromIntError> {
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
        // add_bytes contains some branching logic at the beginning which is a bit
        // more expensive than the main loop of the algorithm. In order to make sure
        // we go through that logic as few times as possible, we construct the
        // entire pseudo-header first, and then add it to the checksum all at once.
        checksum.add_bytes(&pseudo_header[..]);
        Ok(())
    }
}

impl IpAddressExt for Ipv6Addr {
    fn update_transport_checksum_pseudo_header(
        checksum: &mut Checksum,
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        proto: Ipv6Proto,
        transport_len: usize,
    ) -> Result<(), core::num::TryFromIntError> {
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
}

/// Compute the checksum used by TCP and UDP.
///
/// `compute_transport_checksum` computes the checksum used by TCP and UDP. For
/// IPv4, the total packet length `transport_len` must fit in a `u16`, and for
/// IPv6, a `u32`. If the provided packet is too big,
/// `compute_transport_checksum` returns `None`.
fn compute_transport_checksum_parts<'a, A: IpAddressExt, I>(
    src_ip: A,
    dst_ip: A,
    proto: <A::Version as IpExt>::Proto,
    parts: I,
) -> Option<[u8; 2]>
where
    I: Iterator<Item = &'a &'a [u8]> + Clone,
{
    // See for details:
    // https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Checksum_computation
    let mut checksum = Checksum::new();
    let transport_len = parts.clone().map(|b| b.len()).sum();
    A::update_transport_checksum_pseudo_header(&mut checksum, src_ip, dst_ip, proto, transport_len)
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
fn compute_transport_checksum_serialize<A: IpAddressExt>(
    src_ip: A,
    dst_ip: A,
    proto: <A::Version as IpExt>::Proto,
    buffer: &mut SerializeBuffer<'_, '_>,
) -> Option<[u8; 2]> {
    // See for details:
    // https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Checksum_computation
    let mut checksum = Checksum::new();
    let transport_len = buffer.len();
    A::update_transport_checksum_pseudo_header(&mut checksum, src_ip, dst_ip, proto, transport_len)
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
fn compute_transport_checksum<A: IpAddressExt>(
    src_ip: A,
    dst_ip: A,
    proto: <A::Version as IpExt>::Proto,
    packet: &[u8],
) -> Option<[u8; 2]> {
    let mut checksum = Checksum::new();
    A::update_transport_checksum_pseudo_header(&mut checksum, src_ip, dst_ip, proto, packet.len())
        .ok()?;
    checksum.add_bytes(packet);
    Some(checksum.checksum())
}
