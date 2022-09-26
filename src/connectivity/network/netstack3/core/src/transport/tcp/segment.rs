// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The definition of a TCP segment.

use core::{convert::TryFrom as _, num::TryFromIntError, ops::Range};

use crate::transport::tcp::{
    buffer::SendPayload,
    seqnum::{SeqNum, WindowSize},
    Control,
};

/// A TCP segment.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub(crate) struct Segment<P: Payload> {
    /// The sequence number of the segment.
    pub(crate) seq: SeqNum,
    /// The acknowledge number of the segment. [`None`] if not present.
    pub(crate) ack: Option<SeqNum>,
    /// The advertised window size.
    pub(crate) wnd: WindowSize,
    /// The carried data and its control flag.
    pub(crate) contents: Contents<P>,
}

/// The maximum length that the sequence number doesn't wrap around.
pub(super) const MAX_PAYLOAD_AND_CONTROL_LEN: usize = 1 << 31;
// The following `as` is sound because it is representable by `u32`.
const MAX_PAYLOAD_AND_CONTROL_LEN_U32: u32 = MAX_PAYLOAD_AND_CONTROL_LEN as u32;

/// The contents of a TCP segment that takes up some sequence number space.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub(crate) struct Contents<P: Payload> {
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

    pub(super) fn data(&self) -> &P {
        &self.data
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

impl<P: Payload> Segment<P> {
    /// Returns the part of the incoming segment within the receive window.
    pub(super) fn overlap(self, rnxt: SeqNum, rwnd: WindowSize) -> Option<Segment<P>> {
        let Segment { seq, ack, wnd, contents } = self;
        let len = contents.len();
        let Contents { control, data } = contents;
        // RFC 793 (https://tools.ietf.org/html/rfc793#page-69):
        //   There are four cases for the acceptability test for an incoming
        //   segment:
        //       Segment Receive  Test
        //       Length  Window
        //       ------- -------  -------------------------------------------
        //          0       0     SEG.SEQ = RCV.NXT
        //          0      >0     RCV.NXT =< SEG.SEQ < RCV.NXT+RCV.WND
        //         >0       0     not acceptable
        //         >0      >0     RCV.NXT =< SEG.SEQ < RCV.NXT+RCV.WND
        //                     or RCV.NXT =< SEG.SEQ+SEG.LEN-1 < RCV.NXT+RCV.WND
        let overlap = match (len, rwnd) {
            (0, WindowSize::ZERO) => seq == rnxt,
            (0, rwnd) => !rnxt.after(seq) && seq.before(rnxt + rwnd),
            (_len, WindowSize::ZERO) => false,
            (len, rwnd) => {
                (!rnxt.after(seq) && seq.before(rnxt + rwnd))
                    // Note: here we use RCV.NXT <= SEG.SEQ+SEG.LEN instead of
                    // the condition as quoted above because of the following
                    // text immediately after the above table:
                    //   One could tailor actual segments to fit this assumption by
                    //   trimming off any portions that lie outside the window
                    //   (including SYN and FIN), and only processing further if
                    //   the segment then begins at RCV.NXT.
                    // This is essential for TCP simultaneous open to work,
                    // otherwise, the state machine would reject the SYN-ACK
                    // sent by the peer.
                    || (!(seq + len).before(rnxt) && !(seq + len).after(rnxt + rwnd))
            }
        };
        overlap.then(move || {
            // We deliberately don't define `PartialOrd` for `SeqNum`, so we use
            // `cmp` below to utilize `cmp::{max,min}_by`.
            let cmp = |lhs: &SeqNum, rhs: &SeqNum| (*lhs - *rhs).cmp(&0);
            let new_seq = core::cmp::max_by(seq, rnxt, cmp);
            let new_len = core::cmp::min_by(seq + len, rnxt + rwnd, cmp) - new_seq;
            // The following unwrap won't panic because:
            // 1. if `seq` is after `rnxt`, then `start` would be 0.
            // 2. the interesting case is when `rnxt` is after `seq`, in that
            // case, we have `rnxt - seq > 0`, thus `new_seq - seq > 0`.
            let start = u32::try_from(new_seq - seq).unwrap();
            // The following unwrap won't panic because:
            // 1. The witness on `Segment` and `WindowSize` guarantees that
            // `len <= 2^31` and `rwnd <= 2^30-1` thus
            // `seq <= seq + len` and `rnxt <= rnxt + rwnd`.
            // 2. We are in the closure because `overlap` is true which means
            // `seq <= rnxt + rwnd` and `rnxt <= seq + len`.
            // With these two conditions combined, `new_len` can't be negative
            // so the unwrap can't panic.
            let new_len = u32::try_from(new_len).unwrap();
            let (new_control, new_data) = {
                match control {
                    Some(Control::SYN) => {
                        if start == 0 {
                            (Some(Control::SYN), data.slice(start..start + new_len - 1))
                        } else {
                            (None, data.slice(start - 1..start + new_len - 1))
                        }
                    }
                    Some(Control::FIN) => {
                        if len == start + new_len {
                            if new_len > 0 {
                                (Some(Control::FIN), data.slice(start..start + new_len - 1))
                            } else {
                                (None, data.slice(start - 1..start - 1))
                            }
                        } else {
                            (None, data.slice(start..start + new_len))
                        }
                    }
                    Some(Control::RST) | None => (control, data.slice(start..start + new_len)),
                }
            };
            Segment {
                seq: new_seq,
                ack,
                wnd,
                contents: Contents { control: new_control, data: new_data },
            }
        })
    }
}

impl Segment<()> {
    /// Creates a segment with no data.
    pub(super) fn new(
        seq: SeqNum,
        ack: Option<SeqNum>,
        control: Option<Control>,
        wnd: WindowSize,
    ) -> Self {
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

    /// Creates a FIN segment.
    pub(super) fn fin(seq: SeqNum, ack: SeqNum, wnd: WindowSize) -> Self {
        Segment::new(seq, Some(ack), Some(Control::FIN), wnd)
    }
}

/// A TCP payload that operates around `u32` instead of `usize`.
pub trait Payload: Sized {
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

    /// Copies part of the payload beginning at `offset` into `dst`.
    ///
    /// # Panics
    ///
    /// Panics if offset is too large or we couldn't fill the `dst` slice.
    fn partial_copy(&self, offset: usize, dst: &mut [u8]);
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

    fn partial_copy(&self, offset: usize, dst: &mut [u8]) {
        dst.copy_from_slice(&self[offset..offset + dst.len()])
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

    fn partial_copy(&self, offset: usize, dst: &mut [u8]) {
        if dst.len() != 0 || offset != 0 {
            panic!(
                "source slice length (0) does not match destination slice length ({})",
                dst.len()
            );
        }
    }
}

impl From<Segment<()>> for Segment<&'static [u8]> {
    fn from(
        Segment { seq, ack, wnd, contents: Contents { control, data: () } }: Segment<()>,
    ) -> Self {
        Segment { seq, ack, wnd, contents: Contents { control, data: &[] } }
    }
}

impl From<Segment<()>> for Segment<SendPayload<'static>> {
    fn from(
        Segment { seq, ack, wnd, contents: Contents { control, data: () } }: Segment<()>,
    ) -> Self {
        Segment {
            seq,
            ack,
            wnd,
            contents: Contents { control, data: SendPayload::Contiguous(&[]) },
        }
    }
}

#[cfg(test)]
mod test {
    use test_case::test_case;

    use super::*;

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
    fn payload_slice_copy(data: impl Payload, range: Range<u32>) -> [u8; 4] {
        let sliced = data.slice(range);
        let mut buffer = [0; 4];
        sliced.partial_copy(0, &mut buffer[..sliced.len()]);
        buffer
    }

    #[derive(Debug, PartialEq, Eq)]
    struct TestPayload(Range<u32>);

    impl TestPayload {
        fn new(len: usize) -> Self {
            Self(0..u32::try_from(len).unwrap())
        }
    }

    impl Payload for TestPayload {
        fn len(&self) -> usize {
            self.0.len()
        }

        fn slice(self, range: Range<u32>) -> Self {
            let Self(this) = self;
            assert!(range.start >= this.start && range.end <= this.end);
            TestPayload(range)
        }

        fn partial_copy(&self, _offset: usize, _dst: &mut [u8]) {
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
        let (seg, truncated) = Segment::with_data(
            SeqNum::new(0),
            None,
            control,
            WindowSize::ZERO,
            TestPayload::new(len),
        );
        (seg.contents.data.len(), seg.contents.control, truncated)
    }

    struct OverlapTestArgs {
        seg_seq: u32,
        control: Option<Control>,
        data_len: u32,
        rcv_nxt: u32,
        rcv_wnd: usize,
    }
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: None,
        data_len: 0,
        rcv_nxt: 0,
        rcv_wnd: 0,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: None,
        data_len: 0,
        rcv_nxt: 1,
        rcv_wnd: 0,
    } => Some((SeqNum::new(1), None, 0..0)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: None,
        data_len: 0,
        rcv_nxt: 2,
        rcv_wnd: 0,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: Some(Control::SYN),
        data_len: 0,
        rcv_nxt: 2,
        rcv_wnd: 0,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: Some(Control::SYN),
        data_len: 0,
        rcv_nxt: 1,
        rcv_wnd: 0,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: Some(Control::SYN),
        data_len: 0,
        rcv_nxt: 0,
        rcv_wnd: 0,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: Some(Control::FIN),
        data_len: 0,
        rcv_nxt: 2,
        rcv_wnd: 0,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: Some(Control::FIN),
        data_len: 0,
        rcv_nxt: 1,
        rcv_wnd: 0,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: Some(Control::FIN),
        data_len: 0,
        rcv_nxt: 0,
        rcv_wnd: 0,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 0,
        control: None,
        data_len: 0,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: None,
        data_len: 0,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => Some((SeqNum::new(1), None, 0..0)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 2,
        control: None,
        data_len: 0,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 0,
        control: None,
        data_len: 1,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => Some((SeqNum::new(1), None, 1..1)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 0,
        control: Some(Control::SYN),
        data_len: 0,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => Some((SeqNum::new(1), None, 0..0)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 2,
        control: None,
        data_len: 1,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => None)]
    #[test_case(OverlapTestArgs{
        seg_seq: 0,
        control: None,
        data_len: 2,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => Some((SeqNum::new(1), None, 1..2)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: None,
        data_len: 2,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => Some((SeqNum::new(1), None, 0..1)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 0,
        control: Some(Control::SYN),
        data_len: 1,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => Some((SeqNum::new(1), None, 0..1)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: Some(Control::SYN),
        data_len: 1,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => Some((SeqNum::new(1), Some(Control::SYN), 0..0)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 0,
        control: Some(Control::FIN),
        data_len: 1,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => Some((SeqNum::new(1), Some(Control::FIN), 1..1)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: Some(Control::FIN),
        data_len: 1,
        rcv_nxt: 1,
        rcv_wnd: 1,
    } => Some((SeqNum::new(1), None, 0..1)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: None,
        data_len: MAX_PAYLOAD_AND_CONTROL_LEN_U32,
        rcv_nxt: 1,
        rcv_wnd: 10,
    } => Some((SeqNum::new(1), None, 0..10)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 10,
        control: None,
        data_len: MAX_PAYLOAD_AND_CONTROL_LEN_U32,
        rcv_nxt: 1,
        rcv_wnd: 10,
    } => Some((SeqNum::new(10), None, 0..1)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: None,
        data_len: 10,
        rcv_nxt: 1,
        rcv_wnd: 1 << 30 - 1,
    } => Some((SeqNum::new(1), None, 0..10)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 10,
        control: None,
        data_len: 10,
        rcv_nxt: 1,
        rcv_wnd: 1 << 30 - 1,
    } => Some((SeqNum::new(10), None, 0..10)))]
    #[test_case(OverlapTestArgs{
        seg_seq: 1,
        control: Some(Control::FIN),
        data_len: 1,
        rcv_nxt: 3,
        rcv_wnd: 10,
    } => Some((SeqNum::new(3), None, 1..1)); "regression test for https://fxbug.dev/110391")]
    fn segment_overlap(
        OverlapTestArgs { seg_seq, control, data_len, rcv_nxt, rcv_wnd }: OverlapTestArgs,
    ) -> Option<(SeqNum, Option<Control>, Range<u32>)> {
        let (seg, discarded) = Segment::with_data(
            SeqNum::new(seg_seq),
            None,
            control,
            WindowSize::ZERO,
            TestPayload(0..data_len),
        );
        assert_eq!(discarded, 0);
        seg.overlap(SeqNum::new(rcv_nxt), WindowSize::new(rcv_wnd).unwrap()).map(
            |Segment {
                 seq,
                 ack: _,
                 wnd: _,
                 contents: Contents { control, data: TestPayload(range) },
             }| { (seq, control, range) },
        )
    }
}
