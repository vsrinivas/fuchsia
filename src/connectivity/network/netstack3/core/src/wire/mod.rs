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

use core::convert::TryInto;
use core::ops::Deref;

use byteorder::{ByteOrder, NetworkEndian};
use internet_checksum::Checksum;
use net_types::ip::IpAddress;
use packet::{BufferView, SerializeBuffer};
use specialize_ip_macro::specialize_ip_address;
use zerocopy::ByteSlice;

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

/// A frame or packet that can be created from a raw form.
///
/// `FromRaw` provides a common interface for packets or frames that have can be
/// created from an "unchecked" form, that is, that are parsed in raw without
/// any higher-order validation.
///
/// The type parameter `R` is the raw type that an implementer can be converted
/// from, given some arguments of type `A`.
pub(crate) trait FromRaw<R, A>: Sized {
    /// The type of error that may happen during validation.
    type Error;

    /// Attempts to create `Self` from the raw form in `raw` with `args`.
    fn try_from_raw_with(raw: R, args: A) -> Result<Self, Self::Error>;

    /// Attempts to create `Self` from the raw form in `raw`.
    fn try_from_raw(raw: R) -> Result<Self, <Self as FromRaw<R, A>>::Error>
    where
        Self: FromRaw<R, (), Error = <Self as FromRaw<R, A>>::Error>,
    {
        Self::try_from_raw_with(raw, ())
    }
}

/// A type that encapsulates a complete or incomplete parsing operation.
///
/// `MaybeParsed` is a common utility to provide partial/incomplete parsing
/// results. The type parameters `C` and `I` are the types for a "complete" and
/// "incomplete" parsing result, respectively.
pub(crate) enum MaybeParsed<C, I> {
    Complete(C),
    Incomplete(I),
}

impl<T> MaybeParsed<T, T> {
    /// Creates a `MaybeParsed` instance taking `n` bytes from the front of
    /// `buff`.
    ///
    /// Returns [`MaybeParsed::Complete`] with `n` bytes if `buff` contains at
    /// least `n` bytes. Otherwise returns [`MaybeParsed::Incomplete`] greedily
    /// taking all the remaining bytes from `buff`
    #[cfg(test)]
    pub(crate) fn take_from_buffer<BV: BufferView<T>>(buff: &mut BV, n: usize) -> Self
    where
        T: ByteSlice,
    {
        if let Some(v) = buff.take_front(n) {
            MaybeParsed::Complete(v)
        } else {
            MaybeParsed::Incomplete(buff.take_rest_front())
        }
    }

    /// Creates a `MaybeParsed` instance with `bytes` observing a minimum
    /// length `min_len`.
    ///
    /// Returns [`MaybeParsed::Complete`] if `bytes` is at least `min_len` long,
    /// otherwise returns [`MaybeParsed::Incomplete`]. In both cases, `bytes`
    /// is moved into one of the two `MaybeParsed` variants.
    pub(crate) fn new_with_min_len(bytes: T, min_len: usize) -> Self
    where
        T: ByteSlice,
    {
        if bytes.len() >= min_len {
            MaybeParsed::Complete(bytes)
        } else {
            MaybeParsed::Incomplete(bytes)
        }
    }

    /// Consumes this `MaybeParsed` and return its contained value if both the
    /// `Complete` and `Incomplete` variants contain the same type.
    pub(crate) fn into_inner(self) -> T {
        match self {
            MaybeParsed::Complete(c) => c,
            MaybeParsed::Incomplete(i) => i,
        }
    }
}

impl<C, I> MaybeParsed<C, I> {
    /// Creates a `MaybeParsed` instance taking `n` bytes from the front of
    /// `buff` and mapping with `map`.
    ///
    /// Returns [`MaybeParsed::Complete`] with the result of `map` of `n` bytes
    /// if `buff` contains at least `n` bytes. Otherwise returns
    /// [`MaybeParsed::Incomplete`] greedily
    /// taking all the remaining bytes from `buff`
    pub(crate) fn take_from_buffer_with<BV: BufferView<I>, F>(
        buff: &mut BV,
        n: usize,
        map: F,
    ) -> Self
    where
        F: FnOnce(I) -> C,
        I: ByteSlice,
    {
        if let Some(v) = buff.take_front(n) {
            MaybeParsed::Complete(map(v))
        } else {
            MaybeParsed::Incomplete(buff.take_rest_front())
        }
    }

    /// Maps a [`MaybeParsed::Complete`] variant to another type. Otherwise
    /// returns the containing [`MaybeParsed::Incomplete`] value.
    pub(crate) fn map<M, F>(self, f: F) -> MaybeParsed<M, I>
    where
        F: FnOnce(C) -> M,
    {
        match self {
            MaybeParsed::Incomplete(v) => MaybeParsed::Incomplete(v),
            MaybeParsed::Complete(v) => MaybeParsed::Complete(f(v)),
        }
    }

    /// Maps a [`MaybeParsed::Incomplete`] variant to another type. Otherwise
    /// returns the containing [`MaybeParsed::Complete`] value.
    pub(crate) fn map_incomplete<M, F>(self, f: F) -> MaybeParsed<C, M>
    where
        F: FnOnce(I) -> M,
    {
        match self {
            MaybeParsed::Incomplete(v) => MaybeParsed::Incomplete(f(v)),
            MaybeParsed::Complete(v) => MaybeParsed::Complete(v),
        }
    }

    /// Converts from `&MaybeParsed<C,I>` to `MaybeParsed<&C,&I>`.
    pub(crate) fn as_ref(&self) -> MaybeParsed<&C, &I> {
        match self {
            MaybeParsed::Incomplete(v) => MaybeParsed::Incomplete(v),
            MaybeParsed::Complete(v) => MaybeParsed::Complete(v),
        }
    }

    /// Returns `true` if `self` is [`MaybeParsed::Complete`].
    pub(crate) fn is_complete(&self) -> bool {
        match self {
            MaybeParsed::Incomplete { .. } => false,
            MaybeParsed::Complete(_) => true,
        }
    }

    /// Returns `true` if `self` is [`MaybeParsed::Incomplete`].
    #[cfg(test)]
    pub(crate) fn is_incomplete(&self) -> bool {
        match self {
            MaybeParsed::Incomplete { .. } => true,
            MaybeParsed::Complete(_) => false,
        }
    }

    /// Unwraps the complete value of `self`.
    ///
    /// # Panics
    ///
    /// Panics if `self` is not [`MaybeParsed::Complete`].
    #[cfg(test)]
    pub(crate) fn unwrap(self) -> C {
        match self {
            MaybeParsed::Incomplete { .. } => panic!("Called unwrap on incomplete MaybeParsed"),
            MaybeParsed::Complete(v) => v,
        }
    }

    /// Unwraps the incomplete value and error of `self`.
    ///
    /// # Panics
    ///
    /// Panics if `self` is not [`MaybeParsed::Incomplete`].
    #[cfg(test)]
    pub(crate) fn unwrap_incomplete(self) -> I {
        match self {
            MaybeParsed::Incomplete(v) => v,
            MaybeParsed::Complete(_) => panic!("Called unwrap_incomplete on complete MaybeParsed"),
        }
    }

    /// Transforms this `MaybeIncomplete` into a `Result` where the `Complete`
    /// variant becomes `Ok` and the `Incomplete` variant is passed through `f`
    /// and mapped to `Err`.
    pub(crate) fn ok_or_else<F, E>(self, f: F) -> Result<C, E>
    where
        F: FnOnce(I) -> E,
    {
        match self {
            MaybeParsed::Complete(v) => Ok(v),
            MaybeParsed::Incomplete(e) => Err(f(e)),
        }
    }
}

impl<C, I> MaybeParsed<C, I>
where
    C: Deref<Target = [u8]>,
    I: Deref<Target = [u8]>,
{
    /// Returns the length in bytes of the contained data.
    fn len(&self) -> usize {
        match self {
            MaybeParsed::Incomplete(v) => v.deref().len(),
            MaybeParsed::Complete(v) => v.deref().len(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_maybe_parsed_take_from_buffer() {
        let buff = [1_u8, 2, 3, 4];
        let mut bv = &mut &buff[..];
        let mp = MaybeParsed::take_from_buffer(&mut bv, 2);
        assert_eq!(mp.unwrap(), &buff[..2]);
        let mp = MaybeParsed::take_from_buffer(&mut bv, 3);
        assert_eq!(mp.unwrap_incomplete(), &buff[2..]);
    }

    #[test]
    fn test_maybe_parsed_min_len() {
        let buff = [1_u8, 2, 3, 4];
        let mp = MaybeParsed::new_with_min_len(&buff[..], 3);
        assert_eq!(mp.unwrap(), &buff[..]);
        let mp = MaybeParsed::new_with_min_len(&buff[..], 5);
        assert_eq!(mp.unwrap_incomplete(), &buff[..]);
    }

    #[test]
    fn test_maybe_parsed_take_from_buffer_with() {
        let buff = [1_u8, 2, 3, 4];
        let mut bv = &mut &buff[..];
        let mp = MaybeParsed::take_from_buffer_with(&mut bv, 2, |x| Some(usize::from(x[0] + x[1])));
        assert_eq!(mp.unwrap(), Some(3));
        let mp =
            MaybeParsed::take_from_buffer_with(&mut bv, 3, |_| panic!("map shouldn't be called"));
        assert_eq!(mp.unwrap_incomplete(), &buff[2..]);
    }

    #[test]
    fn test_maybe_parsed_map() {
        assert_eq!(
            MaybeParsed::<&str, ()>::Complete("hello").map(|x| format!("{} you", x)).unwrap(),
            "hello you".to_string()
        );
        assert_eq!(
            MaybeParsed::<(), &str>::Incomplete("hello")
                .map(|_| panic!("map shouldn't be called"))
                .unwrap_incomplete(),
            "hello"
        );
    }

    #[test]
    fn test_maybe_parsed_len() {
        let buff = [1_u8, 2, 3, 4];
        let mp1 = MaybeParsed::new_with_min_len(&buff[..], 2);
        let mp2 = MaybeParsed::new_with_min_len(&buff[..], 10);
        assert_eq!(mp1.len(), 4);
        assert_eq!(mp2.len(), 4);
    }

    #[test]
    fn test_maybe_parsed_complete_incomplete() {
        let complete = MaybeParsed::<(), ()>::Complete(());
        let incomplete = MaybeParsed::<(), ()>::Incomplete(());
        assert!(complete.is_complete());
        assert!(!complete.is_incomplete());
        assert!(!incomplete.is_complete());
        assert!(incomplete.is_incomplete());
    }
}
