// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A TCP header option.
///
/// A TCP header option comprises an option kind byte, a length, and the option
///  data itself. While kind-byte-only options are supported, all such kinds are
///  handled by the utilities in `wire::util::options`, so this type only
///  supports options with variable-length data.
///
/// See [Wikipedia] or [RFC 793] for more details.
///
/// [Wikipedia]: https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure
/// [RFC 793]: https://tools.ietf.org/html/rfc793#page-17
#[allow(missing_docs)]
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum TcpOption {
    /// A Maximum Segment Size (MSS) option.
    Mss(u16),
    /// A window scale option.
    WindowScale(u8),
    /// A selective ACK permitted option.
    SackPermitted,
    /// A selective ACK option.
    ///
    /// A variable-length number of selective ACK blocks indicated by
    /// `num_blocks`. All blocks beyond `num_blocks` are meaningless and should
    /// be ignored.
    Sack {
        blocks: [TcpSackBlock; 4],
        num_blocks: u8,
    },
    /// A timestamp option.
    Timestamp { ts_val: u32, ts_echo_reply: u32 },
}

/// A TCP selective ACK block.
///
/// A selective ACK block indicates that the range of bytes `[left_edge,
/// right_edge)` have been received.
///
/// See [RFC 2018] for more details.
///
/// [RFC 2018]: https://tools.ietf.org/html/rfc2018
#[derive(Copy, Clone, Default, Eq, PartialEq, Debug)]
pub struct TcpSackBlock {
    /// The sequence number of the first byte in this block.
    pub left_edge: u32,
    /// The sequence number of the first byte following the end of this block.
    pub right_edge: u32,
}
