// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, FromBytes, Unaligned};

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
pub(crate) enum TcpOption<'a> {
    /// A Maximum Segment Size (MSS) option.
    Mss(u16),
    /// A window scale option.
    WindowScale(u8),
    /// A selective ACK permitted option.
    SackPermitted,
    /// A selective ACK option.
    ///
    /// A variable-length number of selective ACK blocks. The length is in the
    /// range [0, 4].
    Sack(&'a [TcpSackBlock]),
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
#[derive(Copy, Clone, Default, Eq, PartialEq, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct TcpSackBlock {
    left_edge: [u8; 4],
    right_edge: [u8; 4],
}

impl TcpSackBlock {
    pub(crate) fn left_edge(self) -> u32 {
        NetworkEndian::read_u32(&self.left_edge)
    }

    pub(crate) fn right_edge(self) -> u32 {
        NetworkEndian::read_u32(&self.right_edge)
    }

    pub(crate) fn set_left_edge(&mut self, left_edge: u32) {
        NetworkEndian::write_u32(&mut self.left_edge, left_edge);
    }

    pub(crate) fn set_right_edge(&mut self, right_edge: u32) {
        NetworkEndian::write_u32(&mut self.right_edge, right_edge);
    }
}
