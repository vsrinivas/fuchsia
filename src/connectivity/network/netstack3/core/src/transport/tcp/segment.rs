// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The definition of a TCP segment.

use core::{convert::TryFrom as _, num::TryFromIntError, ops::Range};

use super::{
    seqnum::{SeqNum, WindowSize},
    Control,
};
use packet::{Fragment, FragmentedByteSlice};

/// A TCP segment.
#[derive(Debug, PartialEq, Eq)]
pub(super) struct Segment<P: Payload> {
    /// The sequence number of the segment.
    pub(super) seq: SeqNum,
    /// The acknowledge number of the segment. [`None`] if not present.
    pub(super) ack: Option<SeqNum>,
    /// The advertised window size.
    pub(super) wnd: WindowSize,
    /// The carried data and its control flag.
    pub(super) contents: Contents<P>,
}

/// The maximum length that the sequence number doesn't wrap around.
pub(super) const MAX_PAYLOAD_AND_CONTROL_LEN: usize = 1 << 31;
// The following `as` is sound because it is representable by `u32`.
const MAX_PAYLOAD_AND_CONTROL_LEN_U32: u32 = MAX_PAYLOAD_AND_CONTROL_LEN as u32;

/// The contents of a TCP segment that takes up some sequence number space.
#[derive(Debug, PartialEq, Eq)]
pub(super) struct Contents<P: Payload> {
    /// The control flag of the segment.
    control: Option<Control>,
    /// The data carried by the segment; it is guaranteed that
    /// `data.len() + control_len <= MAX_PAYLOAD_AND_CONTROL_LEN`.
    data: P,
}

impl<P: Payload> Contents<P> {
    /// Returns the length of the segment in sequence number space.
    ///
    /// Per RFC 793 (https://tools.ietf.org/html/rfc793#page-25):
    ///   SEG.LEN = the number of octets occupied by the data in the segment
    ///   (counting SYN and FIN)
    pub(super) fn len(&self) -> u32 {
        let Self { data, control } = self;
        // The following unwrap and addition are fine because:
        // - `u32::from(has_control_len)` is 0 or 1.
        // - `self.data.len() <= 2^31`.
        let has_control_len = control.map(Control::has_sequence_no).unwrap_or(false);
        u32::try_from(data.len()).unwrap() + u32::from(has_control_len)
    }

    pub(super) fn control(&self) -> Option<Control> {
        self.control
    }
}

impl<P: Payload> Segment<P> {
    /// Creates a new segment with data.
    ///
    /// Returns the segment along with how many bytes were removed to make sure
    /// sequence numbers don't wrap around, i.e., `seq.before(seq + seg.len())`.
    pub(super) fn with_data(
        seq: SeqNum,
        ack: Option<SeqNum>,
        control: Option<Control>,
        wnd: WindowSize,
        data: P,
    ) -> (Self, usize) {
        let has_control_len = control.map(Control::has_sequence_no).unwrap_or(false);

        let discarded_len =
            data.len().saturating_sub(MAX_PAYLOAD_AND_CONTROL_LEN - usize::from(has_control_len));

        let contents = if discarded_len > 0 {
            // If we have to truncate the segment, the FIN flag must be removed
            // because it is logically the last octet of the segment.
            let (control, control_len) = if control == Some(Control::FIN) {
                (None, 0)
            } else {
                (control, has_control_len.into())
            };
            // The following slice will not panic because `discarded_len > 0`,
            // thus `data.len() > MAX_PAYLOAD_AND_CONTROL_LEN - control_len`.
            Contents { control, data: data.slice(0..MAX_PAYLOAD_AND_CONTROL_LEN_U32 - control_len) }
        } else {
            Contents { control, data }
        };

        (Segment { seq, ack, wnd, contents }, discarded_len)
    }
}

impl Segment<()> {
    /// Creates a segment with no data.
    fn new(seq: SeqNum, ack: Option<SeqNum>, control: Option<Control>, wnd: WindowSize) -> Self {
        // All of the checks on lengths are optimized out:
        // https://godbolt.org/z/KPd537G6Y
        let (seg, truncated) = Segment::with_data(seq, ack, control, wnd, ());
        debug_assert_eq!(truncated, 0);
        seg
    }

    /// Creates an ACK segment.
    pub(super) fn ack(seq: SeqNum, ack: SeqNum, wnd: WindowSize) -> Self {
        Segment::new(seq, Some(ack), None, wnd)
    }

    /// Creates a SYN segment.
    pub(super) fn syn(seq: SeqNum, wnd: WindowSize) -> Self {
        Segment::new(seq, None, Some(Control::SYN), wnd)
    }

    /// Creates a SYN-ACK segment.
    pub(super) fn syn_ack(seq: SeqNum, ack: SeqNum, wnd: WindowSize) -> Self {
        Segment::new(seq, Some(ack), Some(Control::SYN), wnd)
    }

    /// Creates a RST segment.
    pub(super) fn rst(seq: SeqNum) -> Self {
        Segment::new(seq, None, Some(Control::RST), WindowSize::ZERO)
    }

    /// Creates a RST-ACK segment.
    pub(super) fn rst_ack(seq: SeqNum, ack: SeqNum) -> Self {
        Segment::new(seq, Some(ack), Some(Control::RST), WindowSize::ZERO)
    }

    #[cfg_attr(not(test), allow(dead_code))]
    /// Creates a FIN segment.
    pub(super) fn fin(seq: SeqNum, ack: SeqNum, wnd: WindowSize) -> Self {
        Segment::new(seq, Some(ack), Some(Control::FIN), wnd)
    }
}

/// A TCP payload that operates around `u32` instead of `usize`.
pub(super) trait Payload: Sized {
    /// Returns the length of the payload.
    fn len(&self) -> usize;

    /// Creates a slice of the payload, reducing it to only the bytes within
    /// `range`.
    ///
    /// # Panics
    ///
    /// Panics if the provided `range` is not within the bounds of this
    /// `Payload`, or if the range is nonsensical (the end precedes
    /// the start).
    fn slice(self, range: Range<u32>) -> Self;

    /// Copies the payload into `dst`.
    ///
    /// # Panics
    ///
    /// This function will panic if the target and the payload have different
    /// lengths.
    fn copy_to(&self, dst: &mut [u8]);
}

impl Payload for &[u8] {
    fn len(&self) -> usize {
        <[u8]>::len(self)
    }

    fn slice(self, Range { start, end }: Range<u32>) -> Self {
        // The following `unwrap`s are ok because:
        // `usize::try_from(x)` fails when `x > usize::MAX`; given that
        // `self.len() <= usize::MAX`, panic would be expected because `range`
        // exceeds the bound of `self`.
        let start = usize::try_from(start).unwrap_or_else(|TryFromIntError { .. }| {
            panic!("range start index {} out of range for slice of length {}", start, self.len())
        });
        let end = usize::try_from(end).unwrap_or_else(|TryFromIntError { .. }| {
            panic!("range end index {} out of range for slice of length {}", end, self.len())
        });
        &self[start..end]
    }

    fn copy_to(&self, dst: &mut [u8]) {
        dst.copy_from_slice(self)
    }
}

impl Payload for () {
    fn len(&self) -> usize {
        0
    }

    fn slice(self, Range { start, end }: Range<u32>) -> Self {
        if start != 0 {
            panic!("range start index {} out of range for slice of length 0", start);
        }
        if end != 0 {
            panic!("range end index {} out of range for slice of length 0", end);
        }
        ()
    }

    fn copy_to(&self, dst: &mut [u8]) {
        if dst.len() != 0 {
            panic!(
                "source slice length (0) does not match destination slice length ({})",
                dst.len()
            );
        }
    }
}

impl<B: Fragment> Payload for FragmentedByteSlice<'_, B> {
    fn len(&self) -> usize {
        FragmentedByteSlice::len(self)
    }

    fn slice(self, Range { start, end }: Range<u32>) -> Self {
        // The following `unwrap`s are ok because:
        // `usize::try_from(x)` fails when `x > usize::MAX`; given that
        // `self.len() <= usize::MAX`, panic would be expected because `range`
        // exceeds the bound of the `self`.
        let start = usize::try_from(start).unwrap();
        let end = usize::try_from(end).unwrap();
        FragmentedByteSlice::slice(self, start..end)
    }

    fn copy_to(&self, dst: &mut [u8]) {
        self.copy_into_slice(dst)
    }
}

impl From<Segment<()>> for Segment<&'static [u8]> {
    fn from(
        Segment { seq, ack, wnd, contents: Contents { control, data: () } }: Segment<()>,
    ) -> Self {
        Segment { seq, ack, wnd, contents: Contents { control, data: &[] } }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use packet::AsFragmentedByteSlice as _;
    use test_case::test_case;

    #[test_case(None, &[][..] => (0, &[][..]); "empty")]
    #[test_case(None, &[1][..] => (1, &[1][..]); "no control")]
    #[test_case(Some(Control::SYN), &[][..] => (1, &[][..]); "empty slice with syn")]
    #[test_case(Some(Control::SYN), &[1][..] => (2, &[1][..]); "non-empty slice with syn")]
    #[test_case(Some(Control::FIN), &[][..] => (1, &[][..]); "empty slice with fin")]
    #[test_case(Some(Control::FIN), &[1][..] => (2, &[1][..]); "non-empty slice with fin")]
    #[test_case(Some(Control::RST), &[][..] => (0, &[][..]); "empty slice with rst")]
    #[test_case(Some(Control::RST), &[1][..] => (1, &[1][..]); "non-empty slice with rst")]
    fn segment_len(control: Option<Control>, data: &[u8]) -> (u32, &[u8]) {
        let (seg, truncated) = Segment::with_data(
            SeqNum::new(1),
            Some(SeqNum::new(1)),
            control,
            WindowSize::ZERO,
            data,
        );
        assert_eq!(truncated, 0);
        (seg.contents.len(), seg.contents.data)
    }

    #[test_case(&[1, 2, 3, 4, 5][..], 0..4 => [1, 2, 3, 4])]
    #[test_case((), 0..0 => [0, 0, 0, 0])]
    #[test_case([&[1, 2][..], &[3, 4][..], &[5, 6][..]].as_fragmented_byte_slice(), 1..5 => [2, 3, 4, 5])]
    fn payload_slice_copy(data: impl Payload, range: Range<u32>) -> [u8; 4] {
        let sliced = data.slice(range);
        let mut buffer = [0; 4];
        sliced.copy_to(&mut buffer[..sliced.len()]);
        buffer
    }

    struct PayloadLen {
        len: usize,
    }

    impl Payload for PayloadLen {
        fn len(&self) -> usize {
            self.len
        }

        fn slice(self, range: Range<u32>) -> Self {
            PayloadLen { len: range.len() }
        }

        fn copy_to(&self, _dst: &mut [u8]) {
            unimplemented!("TestPayload doesn't carry any data");
        }
    }

    #[test_case(100, Some(Control::SYN) => (100, Some(Control::SYN), 0))]
    #[test_case(100, Some(Control::FIN) => (100, Some(Control::FIN), 0))]
    #[test_case(100, Some(Control::RST) => (100, Some(Control::RST), 0))]
    #[test_case(100, None => (100, None, 0))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN - 1, Some(Control::SYN)
    => (MAX_PAYLOAD_AND_CONTROL_LEN - 1, Some(Control::SYN), 0))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN - 1, Some(Control::FIN)
    => (MAX_PAYLOAD_AND_CONTROL_LEN - 1, Some(Control::FIN), 0))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN - 1, Some(Control::RST)
    => (MAX_PAYLOAD_AND_CONTROL_LEN - 1, Some(Control::RST), 0))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN - 1, None
    => (MAX_PAYLOAD_AND_CONTROL_LEN - 1, None, 0))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN, Some(Control::SYN)
    => (MAX_PAYLOAD_AND_CONTROL_LEN - 1, Some(Control::SYN), 1))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN, Some(Control::FIN)
    => (MAX_PAYLOAD_AND_CONTROL_LEN, None, 1))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN, Some(Control::RST)
    => (MAX_PAYLOAD_AND_CONTROL_LEN, Some(Control::RST), 0))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN, None
    => (MAX_PAYLOAD_AND_CONTROL_LEN, None, 0))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN + 1, Some(Control::SYN)
    => (MAX_PAYLOAD_AND_CONTROL_LEN - 1, Some(Control::SYN), 2))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN + 1, Some(Control::FIN)
    => (MAX_PAYLOAD_AND_CONTROL_LEN, None, 2))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN + 1, Some(Control::RST)
    => (MAX_PAYLOAD_AND_CONTROL_LEN, Some(Control::RST), 1))]
    #[test_case(MAX_PAYLOAD_AND_CONTROL_LEN + 1, None
    => (MAX_PAYLOAD_AND_CONTROL_LEN, None, 1))]
    #[test_case(u32::MAX as usize, Some(Control::SYN)
    => (MAX_PAYLOAD_AND_CONTROL_LEN - 1, Some(Control::SYN), 1 << 31))]
    fn segment_truncate(len: usize, control: Option<Control>) -> (usize, Option<Control>, usize) {
        let (seg, truncated) =
            Segment::with_data(SeqNum::new(0), None, control, WindowSize::ZERO, PayloadLen { len });
        (seg.contents.data.len(), seg.contents.control, truncated)
    }
}
