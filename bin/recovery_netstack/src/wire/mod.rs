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
#![cfg_attr(feature = "cargo-clippy", allow(block_in_if_condition_stmt))]

/// Emit a debug message and return an error.
///
/// Invoke the `debug!` macro on all but the first argument. A call to
/// `debug_err!(err, ...)` is an expression whose value is the expression `err`.
macro_rules! debug_err {
    ($err:expr, $($arg:tt)*) => (
        // TODO(joshlf): Uncomment once attributes are allowed on expressions
        // #[cfg_attr(feature = "cargo-clippy", allow(block_in_if_condition_stmt))]
        {
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
            debug!($($arg)*);
            $err
        }
    )
}

pub mod arp;
pub mod ethernet;
pub mod icmp;
pub mod ipv4;
pub mod ipv6;
pub mod tcp;
#[cfg(test)]
mod testdata;
pub mod udp;
mod util;

pub use self::ethernet::*;
pub use self::udp::*;
pub use self::util::{ensure_prefix_padding, BufferAndRange};

/// A callback used to serialize a packet.
///
/// When a layer of the stack wishes to serialize a packet for encapsulation by
/// a lower layer of the stack, it passes a `SerializationCallback` to the lower
/// layer. The lower layer calculates various information about minimum buffer
/// size, and invokes the callback with that information (see below for
/// details). The callback is expected to produce a `BufferAndRange` which
/// satisfies these minimum buffer size requirements, and whose range contains
/// the packet to be encapsulated. Once the callback returns, the lower layer
/// will serialize its own headers, and the packet will be sent further down the
/// stack.
///
/// The buffer size information mentioned in the previous paragraph is a) the
/// amount of space preceding the packet required to serialize any encapsulating
/// headers and, b) the minimum body size required by the lower layer protocol.
/// These are passed as the first and second arguments respectively. In the rest
/// of this documentation, we will refer to these as `min_prefix_size` and
/// `min_body_and_padding_size` respectively.
///
/// In order to satisfy (a), the callback must produce a buffer with at least
/// `min_prefix_size` bytes preceding the range. The lower layers will use these
/// bytes to serialize any encapsulating headers.
///
/// In order to satisfy (b), the callback must ensure that the sum of the range
/// size plus the number of bytes following the range is at least
/// `min_body_and_padding_size`. If the provided range is itself large enough,
/// then it will be used alone. However, if it is not large enough, then the
/// lower layer will use any bytes following the range as padding bytes in order
/// to meet the minimum body size requirement of that lower layer protocol.
///
/// # Examples
///
/// To see how this works in practice, consider the example of a TCP ACK segment
/// encapsulated in an IPv4 packet encapsulated in an Ethernet frame. We will
/// assume that neither the TCP segment nor the IPv4 packet supply any header
/// options. A TCP ACK segment has no body, and consumes 20 bytes. An IPv4
/// packet adds a 20-byte header, making for a total packet size of 40 bytes
/// when encapsulating a 20-byte TCP segment. Ethernet has a header size of at
/// most 18 bytes, and a minimum frame body size of at most 46 bytes.
///
/// When the TCP layer's `SerializationCallback` is invoked, the
/// `min_prefix_size` will be 38 - 20 bytes for the IPv4 header, and 18 bytes
/// for the Ethernet header. The `min_body_and_padding_size` will be 26. This is
/// because, while Ethernet has a minimum body size of 46 bytes, 20 of those
/// bytes are consumed by the IPv4 header, leaving 26 to be satisfied by the TCP
/// segment.
///
/// In order to meet these requirements, the TCP layer's `SerializationCallback`
/// must provide a buffer with at least 38 bytes preceding the range, 20 bytes
/// for the range itself (containing the TCP segment), and at least 6 bytes
/// following the range to be used as padding in order to reach the minimum
/// `min_body_and_padding_size` of 26 bytes.
///
/// When the IPv4 layer receives the TCP layer's buffer, it will treat the
/// 20-byte range as the IPv4 packet body, and will serialize its own header
/// just before that range. It will then expand the range to encompass the 40
/// bytes of its header and the TCP segment, and will return the buffer to the
/// Ethernet layer.
///
/// The Ethernet layer will receive a buffer with 18 bytes of prefix, a 40-byte
/// range (containing the IPV4 packet, in turn containing the TCP segment), and
/// 6 bytes following the range. In order to satisfy the minimum body size
/// requirement of 46 bytes, the Ethernet layer will extend the range to
/// encompass the 6 padding bytes at the end of the buffer. The range will now
/// be 46 bytes in size, and can be passed as the body to the Ethernet frame
/// serialization function, which will find that the body is sufficiently long.
/// It will serialize its header in the 18 bytes of prefix remaining before the
/// range.
///
/// NOTE: It's important that the TCP layer not include the 6 bytes of padding
/// in the range it passes to the IPv4 layer. If it did this, the IPv4 layer
/// would treat those 6 bytes as part of its IPv4 packet body. When the packet
/// was later parsed, the TCP stack would erroneously find a TCP segment with 6
/// bytes of body (and possibly an invalid checksum). Instead, the TCP layer
/// must leave the padding bytes after the range which it uses to tell the IPv4
/// layer what to encapsulate.
///
/// The following diagram illustrates the construction of the `BufferAndRange`
/// as it is passed through the layers of the stack. The dashed lines represent
/// the prefix and suffix preceding and following the range, while the pluses
/// represent the range itself. The bottom line indicates which byte ranges
/// constitute which parts of the Ethernet frame. Note that the padding bytes
/// are only interpreted as being part of the packet body by the Ethernet layer.
/// The TCP and IPv4 layers treat only the bytes of the TCP segment as being
/// part of the IPv4 packet body.
///
/// ```text
/// |-------------------------------------|++++++++++++++++++++|-----| TCP segment
/// |-----------------|++++++++++++++++++++++++++++++++++++++++|-----| IPv4 packet
/// |++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++| Ethernet frame
///
/// |-----------------|-------------------|--------------------|-----|
///   Ethernet header      IPv4 header         TCP segment      Padding
/// ```
pub trait SerializationCallback<B>: FnOnce(usize, usize) -> BufferAndRange<B> {}
impl<B, F: FnOnce(usize, usize) -> BufferAndRange<B>> SerializationCallback<B> for F {}

/// A `SerializationCallback` that also takes an address.
///
/// See the [`::wire::SerializationCallback`] documentation for more details.
pub trait AddrSerializationCallback<A, B>: FnOnce(A, usize, usize) -> BufferAndRange<B> {}
impl<A, B, F: FnOnce(A, usize, usize) -> BufferAndRange<B>> AddrSerializationCallback<A, B> for F {}
