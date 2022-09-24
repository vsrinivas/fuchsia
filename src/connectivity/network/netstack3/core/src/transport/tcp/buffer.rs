// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the buffer traits needed by the TCP implementation. The traits
//! in this module provide a common interface for platform-specific buffers
//! used by TCP.

use alloc::{vec, vec::Vec};
use core::{cmp, convert::TryFrom, fmt::Debug, mem, num::TryFromIntError, ops::Range};
use either::Either;

use crate::transport::tcp::{
    segment::Payload,
    seqnum::{SeqNum, WindowSize},
    state::Takeable,
};

/// Common super trait for both sending and receiving buffer.
pub trait Buffer: Takeable + Debug + Sized {
    /// Returns the number of bytes in the buffer that can be read.
    fn len(&self) -> usize;

    /// Returns the maximum number of bytes that can reside in the buffer.
    fn cap(&self) -> usize;
}

/// A buffer supporting TCP receiving operations.
pub trait ReceiveBuffer: Buffer {
    /// Stores the remaining data that have not been read by the user when the
    /// connection is shutdown by the peer.
    type Residual: From<Self> + Takeable + Debug;

    /// Writes `data` into the buffer at `offset`.
    ///
    /// Returns the number of bytes written.
    fn write_at<P: Payload>(&mut self, offset: usize, data: &P) -> usize;

    /// Marks `count` bytes available for the application to read.
    ///
    /// # Panics
    ///
    /// Panics if the caller attempts to make more bytes readable than the
    /// buffer has capacity for. That is, this method panics if
    /// `self.len() + count > self.cap()`
    fn make_readable(&mut self, count: usize);
}

/// A buffer supporting TCP sending operations.
pub trait SendBuffer: Buffer {
    /// Removes `count` bytes from the beginning of the buffer as already read.
    ///
    /// # Panics
    ///
    /// Panics if more bytes are marked as read than are available, i.e.,
    /// `count > self.len`.
    fn mark_read(&mut self, count: usize);

    /// Calls `f` with contiguous sequences of readable bytes in the buffer
    /// without advancing the reading pointer.
    ///
    /// # Panics
    ///
    /// Panics if more bytes are peeked than are available, i.e.,
    /// `offset > self.len`
    // Note: This trait is tied closely to a ring buffer, that's why we use
    // the `SendPayload` rather than `&[&[u8]]` as in the Rx path. Currently
    // the language isn't flexible enough to allow its implementors to decide
    // the shape of readable region. It is theoretically possible and ideal
    // for this trait to have an associated type that describes the shape of
    // the borrowed readable region but is currently impossible because GATs
    // are not implemented yet.
    fn peek_with<'a, F, R>(&'a mut self, offset: usize, f: F) -> R
    where
        F: FnOnce(SendPayload<'a>) -> R;
}

/// A type for the payload being sent.
#[derive(Debug, PartialEq)]
#[cfg_attr(test, derive(Clone))]
pub enum SendPayload<'a> {
    /// The payload is contained in a single chunk of memory.
    Contiguous(&'a [u8]),
    /// The payload straddles across two chunks of memory.
    Straddle(&'a [u8], &'a [u8]),
}

impl Payload for SendPayload<'_> {
    fn len(&self) -> usize {
        match self {
            SendPayload::Contiguous(p) => p.len(),
            SendPayload::Straddle(p1, p2) => p1.len() + p2.len(),
        }
    }

    fn slice(self, range: Range<u32>) -> Self {
        match self {
            SendPayload::Contiguous(p) => SendPayload::Contiguous(p.slice(range)),
            SendPayload::Straddle(p1, p2) => {
                let Range { start, end } = range;
                let start = usize::try_from(start).unwrap_or_else(|TryFromIntError { .. }| {
                    panic!(
                        "range start index {} out of range for slice of length {}",
                        start,
                        self.len()
                    )
                });
                let end = usize::try_from(end).unwrap_or_else(|TryFromIntError { .. }| {
                    panic!(
                        "range end index {} out of range for slice of length {}",
                        end,
                        self.len()
                    )
                });
                assert!(start <= end);
                let first_len = p1.len();
                if start < first_len && end > first_len {
                    SendPayload::Straddle(&p1[start..first_len], &p2[0..end - first_len])
                } else if start >= first_len {
                    SendPayload::Contiguous(&p2[start - first_len..end - first_len])
                } else {
                    SendPayload::Contiguous(&p1[start..end])
                }
            }
        }
    }

    fn partial_copy(&self, offset: usize, dst: &mut [u8]) {
        match self {
            SendPayload::Contiguous(p) => p.partial_copy(offset, dst),
            SendPayload::Straddle(p1, p2) => {
                if offset < p1.len() {
                    let first_len = dst.len().min(p1.len() - offset);
                    p1.partial_copy(offset, &mut dst[..first_len]);
                    if dst.len() > first_len {
                        p2.partial_copy(0, &mut dst[first_len..]);
                    }
                } else {
                    p2.partial_copy(offset - p1.len(), dst);
                }
            }
        }
    }
}

/// A circular buffer implementation.
#[derive(Debug, Clone)]
#[cfg_attr(test, derive(PartialEq, Eq))]
pub struct RingBuffer {
    storage: Vec<u8>,
    /// The index where the reader starts to read.
    ///
    /// Maintains the invariant that `head < storage.len()` by wrapping
    /// around to 0 as needed.
    head: usize,
    /// The amount of readable data in `storage`.
    ///
    /// Anything between [head, head+len) is readable. This will never exceed
    /// `storage.len()`.
    len: usize,
}

impl RingBuffer {
    /// Creates a new `RingBuffer`.
    pub fn new(capacity: usize) -> Self {
        Self { storage: vec![0; capacity], head: 0, len: 0 }
    }
}

impl Default for RingBuffer {
    fn default() -> Self {
        Self::new(WindowSize::DEFAULT.into())
    }
}

impl RingBuffer {
    /// Calls `f` on the contiguous sequences from `start` up to `len` bytes.
    fn with_readable<'a, F, R>(storage: &'a Vec<u8>, start: usize, len: usize, f: F) -> R
    where
        F: for<'b> FnOnce(&'b [&'a [u8]]) -> R,
    {
        // Don't read past the end of storage.
        let end = start + len;
        if end > storage.len() {
            let first_part = &storage[start..storage.len()];
            let second_part = &storage[0..len - first_part.len()];
            f(&[first_part, second_part][..])
        } else {
            let all_bytes = &storage[start..end];
            f(&[all_bytes][..])
        }
    }

    /// Calls `f` with contiguous sequences of readable bytes in the buffer and
    /// discards the amount of bytes returned by `f`.
    ///
    /// # Panics
    ///
    /// Panics if the closure wants to discard more bytes than possible, i.e.,
    /// the value returned by `f` is greater than `self.len()`.
    pub fn read_with<'a, F>(&'a mut self, f: F) -> usize
    where
        F: for<'b> FnOnce(&'b [&'a [u8]]) -> usize,
    {
        let Self { storage, head, len } = self;
        if storage.len() == 0 {
            return f(&[&[]]);
        }
        let nread = RingBuffer::with_readable(storage, *head, *len, f);
        assert!(nread <= *len);
        *len -= nread;
        *head = (*head + nread) % storage.len();
        nread
    }

    /// Enqueues as much of `data` as possible to the end of the buffer.
    ///
    /// Returns the number of bytes actually queued.
    pub(crate) fn enqueue_data(&mut self, data: &[u8]) -> usize {
        let nwritten = self.write_at(0, &data);
        self.make_readable(nwritten);
        nwritten
    }

    /// Returns the writable regions of the [`RingBuffer`].
    ///
    /// The [`RingBuffer`] itself holds the memory regions that are readable, so
    /// anything else that is not readable, i.e., outside the readable regions
    /// are deemed as available to write.
    pub fn writable_regions(&mut self) -> impl IntoIterator<Item = &mut [u8]> {
        if self.head + self.len > self.storage.len() {
            let available = self.storage.len() - self.len;
            Either::Left([&mut self.storage[self.head - available..self.head]].into_iter())
        } else {
            let (b1, b2) = self.storage[..].split_at_mut(self.head + self.len);
            Either::Right([b2, &mut b1[..self.head]].into_iter())
        }
    }
}

impl Buffer for RingBuffer {
    fn len(&self) -> usize {
        self.len
    }

    fn cap(&self) -> usize {
        self.storage.len()
    }
}

impl ReceiveBuffer for RingBuffer {
    type Residual = Self;

    fn write_at<P: Payload>(&mut self, offset: usize, data: &P) -> usize {
        let Self { storage, head, len } = self;
        if storage.len() == 0 {
            return 0;
        }
        let available = storage.len() - *len;
        if offset > available {
            return 0;
        }
        let start_at = (*head + *len + offset) % storage.len();
        let to_write = cmp::min(data.len(), available);
        // Write the first part of the payload.
        let first_len = cmp::min(to_write, storage.len() - start_at);
        data.partial_copy(0, &mut storage[start_at..start_at + first_len]);
        // If we have more to write, wrap around and start from the beginning
        // of the storage.
        if to_write > first_len {
            data.partial_copy(first_len, &mut storage[0..to_write - first_len]);
        }
        to_write
    }

    fn make_readable(&mut self, count: usize) {
        assert!(count <= self.cap() - self.len());
        self.len += count;
    }
}

impl SendBuffer for RingBuffer {
    fn mark_read(&mut self, count: usize) {
        let Self { storage, head, len } = self;
        assert!(count <= *len);
        *len -= count;
        *head = (*head + count) % storage.len();
    }

    fn peek_with<'a, F, R>(&'a mut self, offset: usize, f: F) -> R
    where
        F: FnOnce(SendPayload<'a>) -> R,
    {
        let Self { storage, head, len } = self;
        if storage.len() == 0 {
            return f(SendPayload::Contiguous(&[]));
        }
        assert!(offset <= *len);
        RingBuffer::with_readable(
            storage,
            (*head + offset) % storage.len(),
            *len - offset,
            |readable| match readable.len() {
                1 => f(SendPayload::Contiguous(readable[0])),
                2 => f(SendPayload::Straddle(readable[0], readable[1])),
                x => unreachable!(
                    "the ring buffer cannot have more than 2 fragments, got {} fragments ({:?})",
                    x, readable
                ),
            },
        )
    }
}

/// Assembler for out-of-order segment data.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
pub(super) struct Assembler {
    // `nxt` is the next sequence number to be expected. It should be before
    // any sequnce number of the out-of-order sequence numbers we keep track
    // of below.
    nxt: SeqNum,
    // Holds all the sequence number ranges which we have already received.
    // These ranges are sorted and should have a gap of at least 1 byte
    // between any consecutive two. These ranges should only be after `nxt`.
    outstanding: Vec<Range<SeqNum>>,
}

impl Assembler {
    /// Creates a new assembler.
    pub(super) fn new(nxt: SeqNum) -> Self {
        Self { outstanding: Vec::new(), nxt }
    }

    /// Returns the next sequence number expected to be received.
    pub(super) fn nxt(&self) -> SeqNum {
        self.nxt
    }

    /// Inserts a received segment.
    ///
    /// The newly added segment will be merged with as many existing ones as
    /// possible and `nxt` will be advanced to the highest ACK number possible.
    ///
    /// Returns number of bytes that should be available for the application
    /// to consume.
    ///
    /// # Panics
    ///
    /// Panics if `start` is after `end` or if `start` is before `self.nxt`.
    pub(super) fn insert(&mut self, Range { start, end }: Range<SeqNum>) -> usize {
        assert!(!start.after(end));
        assert!(!start.before(self.nxt));
        self.insert_inner(start..end);

        let Self { outstanding, nxt } = self;
        if outstanding[0].start == *nxt {
            let advanced = outstanding.remove(0);
            *nxt = advanced.end;
            // The following unwrap is safe because it is invalid to have
            // have a range where `end` is before `start`.
            usize::try_from(advanced.end - advanced.start).unwrap()
        } else {
            0
        }
    }

    fn insert_inner(&mut self, Range { mut start, mut end }: Range<SeqNum>) {
        let Self { outstanding, nxt: _ } = self;

        if start == end {
            return;
        }

        if outstanding.is_empty() {
            outstanding.push(Range { start, end });
            return;
        }

        // Search for the first segment whose `start` is greater.
        let first_after = {
            let mut cur = 0;
            while cur < outstanding.len() {
                if start.before(outstanding[cur].start) {
                    break;
                }
                cur += 1;
            }
            cur
        };

        let mut merge_right = 0;
        for range in &outstanding[first_after..outstanding.len()] {
            if end.before(range.start) {
                break;
            }
            merge_right += 1;
            if end.before(range.end) {
                end = range.end;
                break;
            }
        }

        let mut merge_left = 0;
        for range in (&outstanding[0..first_after]).iter().rev() {
            if start.after(range.end) {
                break;
            }
            // There is no guarantee that `end.after(range.end)`, not doing
            // the following may shrink existing coverage. For example:
            // range.start = 0, range.end = 10, start = 0, end = 1, will result
            // in only 0..1 being tracked in the resulting assembler. We didn't
            // do the symmetrical thing above when merging to the right because
            // the search guarantees that `start.before(range.start)`, thus the
            // problem doesn't exist there. The asymmetry rose from the fact
            // that we used `start` to perform the search.
            if end.before(range.end) {
                end = range.end;
            }
            merge_left += 1;
            if start.after(range.start) {
                start = range.start;
                break;
            }
        }

        if merge_left == 0 && merge_right == 0 {
            // If the new segment cannot merge with any of its neighbors, we
            // add a new entry for it.
            outstanding.insert(first_after, Range { start, end });
        } else {
            // Otherwise, we put the new segment at the left edge of the merge
            // window and remove all other existing segments.
            let left_edge = first_after - merge_left;
            let right_edge = first_after + merge_right;
            outstanding[left_edge] = Range { start, end };
            for i in right_edge..outstanding.len() {
                outstanding[i - merge_left - merge_right + 1] = outstanding[i].clone();
            }
            outstanding.truncate(outstanding.len() - merge_left - merge_right + 1);
        }
    }
}

/// A conversion trait that converts the object that Bindings give us into a
/// pair of receive and send buffers.
pub trait IntoBuffers<R: ReceiveBuffer, S: SendBuffer> {
    /// Converts to receive and send buffers.
    fn into_buffers(self) -> (R, S);
}

impl<R: Default + ReceiveBuffer, S: Default + SendBuffer> IntoBuffers<R, S> for () {
    fn into_buffers(self) -> (R, S) {
        Default::default()
    }
}

#[cfg(test)]
mod test {
    use proptest::{
        proptest,
        strategy::{Just, Strategy},
        test_runner::Config,
    };
    use proptest_support::failed_seeds;
    use test_case::test_case;

    use super::*;

    use crate::transport::tcp::seqnum::WindowSize;

    const TEST_BYTES: &'static [u8] = "Hello World!".as_bytes();

    proptest! {
        #![proptest_config(Config {
            // Add all failed seeds here.
            failure_persistence: failed_seeds!(
                "cc f621ca7d3a2b108e0dc41f7169ad028f4329b79e90e73d5f68042519a9f63999"
            ),
            ..Config::default()
        })]

        #[test]
        fn assembler_insertion(insertions in proptest::collection::vec(assembler::insertions(), 200)) {
            let mut assembler = Assembler::new(SeqNum::new(0));
            let mut num_insertions_performed = 0;
            let mut min_seq = SeqNum::new(WindowSize::MAX.into());
            let mut max_seq = SeqNum::new(0);
            for Range { start, end } in insertions {
                if min_seq.after(start) {
                    min_seq = start;
                }
                if max_seq.before(end) {
                    max_seq = end;
                }
                // assert that it's impossible to have more entries than the
                // number of insertions performed.
                assert!(assembler.outstanding.len() <= num_insertions_performed);
                assembler.insert_inner(start..end);
                num_insertions_performed += 1;

                // assert that the ranges are sorted and don't overlap with
                // each other.
                for i in 1..assembler.outstanding.len() {
                    assert!(assembler.outstanding[i-1].end.before(assembler.outstanding[i].start));
                }
            }
            assert_eq!(assembler.outstanding.first().unwrap().start, min_seq);
            assert_eq!(assembler.outstanding.last().unwrap().end, max_seq);
        }

        #[test]
        fn ring_buffer_make_readable((mut rb, avail) in ring_buffer::with_available()) {
            let old_storage = rb.storage.clone();
            let old_head = rb.head;
            let old_len = rb.len();
            rb.make_readable(avail);
            // Assert that length is updated but everything else is unchanged.
            let RingBuffer { storage, head, len } = rb;
            assert_eq!(len, old_len + avail);
            assert_eq!(head, old_head);
            assert_eq!(storage, old_storage);
        }

        #[test]
        fn ring_buffer_write_at((mut rb, offset, data) in ring_buffer::with_offset_data()) {
            let old_head = rb.head;
            let old_len = rb.len();
            assert_eq!(rb.write_at(offset, &&data[..]), data.len());
            assert_eq!(rb.head, old_head);
            assert_eq!(rb.len(), old_len);
            for i in 0..data.len() {
                let masked = (rb.head + rb.len + offset + i) % rb.storage.len();
                // Make sure that data are written.
                assert_eq!(rb.storage[masked], data[i]);
                rb.storage[masked] = 0;
            }
            // And the other parts of the storage are untouched.
            assert_eq!(rb.storage, vec![0; rb.storage.len()])
        }

        #[test]
        fn ring_buffer_read_with((mut rb, expected, consume) in ring_buffer::with_read_data()) {
            assert_eq!(rb.len(), expected.len());
            let nread = rb.read_with(|readable| {
                assert!(readable.len() == 1 || readable.len() == 2);
                let got = readable.concat();
                assert_eq!(got, expected);
                consume
            });
            assert_eq!(nread, consume);
            assert_eq!(rb.len(), expected.len() - consume);
        }

        #[test]
        fn ring_buffer_mark_read((mut rb, readable) in ring_buffer::with_readable()) {
            let old_storage = rb.storage.clone();
            let old_head = rb.head;
            let old_len = rb.len();
            rb.mark_read(readable);
            // Assert that length and head are updated but everything else is
            // unchanged.
            let RingBuffer { storage, head, len } = rb;
            assert_eq!(len, old_len - readable);
            assert_eq!(head, (old_head + readable) % old_storage.len());
            assert_eq!(storage, old_storage);
        }

        #[test]
        fn ring_buffer_peek_with((mut rb, expected, offset) in ring_buffer::with_read_data()) {
            assert_eq!(rb.len(), expected.len());
            let () = rb.peek_with(offset, |readable| {
                assert_eq!(readable.to_vec(), &expected[offset..]);
            });
            assert_eq!(rb.len(), expected.len());
        }

        #[test]
        fn ring_buffer_writable_regions(mut rb in ring_buffer::arb_ring_buffer()) {
            const BYTE_TO_WRITE: u8 = 0x42;
            let writable_len = rb.writable_regions().into_iter().fold(0, |acc, slice| {
                slice.fill(BYTE_TO_WRITE);
                acc + slice.len()
            });
            assert_eq!(writable_len + rb.len(), rb.cap());
            for i in 0..rb.cap() {
                let expected = if i < rb.len() {
                    0
                } else {
                    BYTE_TO_WRITE
                };
                let idx = (rb.head + i) % rb.storage.len();
                assert_eq!(rb.storage[idx], expected);
            }
        }

        #[test]
        fn send_payload_len((payload, _idx) in send_payload::with_index()) {
            assert_eq!(payload.len(), TEST_BYTES.len())
        }

        #[test]
        fn send_payload_slice((payload, idx) in send_payload::with_index()) {
            let idx_u32 = u32::try_from(idx).unwrap();
            let end = u32::try_from(TEST_BYTES.len()).unwrap();
            assert_eq!(payload.clone().slice(0..idx_u32).to_vec(), &TEST_BYTES[..idx]);
            assert_eq!(payload.clone().slice(idx_u32..end).to_vec(), &TEST_BYTES[idx..]);
        }

        #[test]
        fn send_payload_partial_copy((payload, offset, len) in send_payload::with_offset_and_length()) {
            let mut buffer = [0; TEST_BYTES.len()];
            payload.partial_copy(offset, &mut buffer[0..len]);
            assert_eq!(&buffer[0..len], &TEST_BYTES[offset..offset + len]);
        }
    }

    #[test_case([Range { start: 0, end: 10 }]
        => Assembler { outstanding: vec![], nxt: SeqNum::new(10) })]
    #[test_case([Range{ start: 10, end: 15 }, Range { start: 5, end: 10 }]
        => Assembler { outstanding: vec![Range { start: SeqNum::new(5), end: SeqNum::new(15) }], nxt: SeqNum::new(0)})]
    #[test_case([Range{ start: 10, end: 15 }, Range { start: 0, end: 5 }, Range { start: 5, end: 10 }]
        => Assembler { outstanding: vec![], nxt: SeqNum::new(15) })]
    #[test_case([Range{ start: 10, end: 15 }, Range { start: 5, end: 10 }, Range { start: 0, end: 5 }]
        => Assembler { outstanding: vec![], nxt: SeqNum::new(15) })]
    fn assembler_examples(ops: impl IntoIterator<Item = Range<u32>>) -> Assembler {
        let mut assembler = Assembler::new(SeqNum::new(0));
        for Range { start, end } in ops.into_iter() {
            let _advanced = assembler.insert(SeqNum::new(start)..SeqNum::new(end));
        }
        assembler
    }

    #[test]
    // Regression test for https://fxbug.dev/109960.
    fn ring_buffer_wrap_around() {
        const CAPACITY: usize = 16;
        let mut rb = RingBuffer::new(CAPACITY);

        // Write more than half the buffer.
        const BUF_SIZE: usize = 10;
        assert_eq!(rb.enqueue_data(&[0xAA; BUF_SIZE]), BUF_SIZE);
        rb.peek_with(0, |payload| assert_eq!(payload, SendPayload::Contiguous(&[0xAA; BUF_SIZE])));
        rb.mark_read(BUF_SIZE);

        // Write around the end of the buffer.
        assert_eq!(rb.enqueue_data(&[0xBB; BUF_SIZE]), BUF_SIZE);
        rb.peek_with(0, |payload| {
            assert_eq!(
                payload,
                SendPayload::Straddle(
                    &[0xBB; (CAPACITY - BUF_SIZE)],
                    &[0xBB; (BUF_SIZE * 2 - CAPACITY)]
                )
            )
        });
        // Mark everything read, which should advance `head` around to the
        // beginning of the buffer.
        rb.mark_read(BUF_SIZE);

        // Now make a contiguous sequence of bytes readable.
        assert_eq!(rb.enqueue_data(&[0xCC; BUF_SIZE]), BUF_SIZE);
        rb.peek_with(0, |payload| assert_eq!(payload, SendPayload::Contiguous(&[0xCC; BUF_SIZE])));

        // Check that the unwritten bytes are left untouched. If `head` was
        // advanced improperly, this will crash.
        let read = rb.read_with(|segments| {
            assert_eq!(segments, [[0xCC; BUF_SIZE]]);
            BUF_SIZE
        });
        assert_eq!(read, BUF_SIZE);
    }

    #[test]
    fn ring_buffer_example() {
        let mut rb = RingBuffer::new(16);
        assert_eq!(rb.write_at(5, &"World".as_bytes()), 5);
        assert_eq!(rb.write_at(0, &"Hello".as_bytes()), 5);
        rb.make_readable(10);
        assert_eq!(
            rb.read_with(|readable| {
                assert_eq!(readable, &["HelloWorld".as_bytes()]);
                5
            }),
            5
        );
        assert_eq!(
            rb.read_with(|readable| {
                assert_eq!(readable, &["World".as_bytes()]);
                readable[0].len()
            }),
            5
        );
        assert_eq!(rb.write_at(0, &"HelloWorld".as_bytes()), 10);
        rb.make_readable(10);
        assert_eq!(
            rb.read_with(|readable| {
                assert_eq!(readable, &["HelloW".as_bytes(), "orld".as_bytes()]);
                6
            }),
            6
        );
        assert_eq!(rb.len(), 4);
        assert_eq!(
            rb.read_with(|readable| {
                assert_eq!(readable, &["orld".as_bytes()]);
                4
            }),
            4
        );
        assert_eq!(rb.len(), 0);

        assert_eq!(rb.enqueue_data("Hello".as_bytes()), 5);
        assert_eq!(rb.len(), 5);

        let () = rb.peek_with(3, |readable| {
            assert_eq!(readable.to_vec(), "lo".as_bytes());
        });

        rb.mark_read(2);

        let () = rb.peek_with(0, |readable| {
            assert_eq!(readable.to_vec(), "llo".as_bytes());
        });
    }

    mod assembler {
        use super::*;
        pub(super) fn insertions() -> impl Strategy<Value = Range<SeqNum>> {
            (0..u32::from(WindowSize::MAX)).prop_flat_map(|start| {
                (start + 1..=u32::from(WindowSize::MAX)).prop_flat_map(move |end| {
                    Just(Range { start: SeqNum::new(start), end: SeqNum::new(end) })
                })
            })
        }
    }

    mod ring_buffer {
        use super::*;

        fn arb_ring_buffer_args() -> impl Strategy<Value = (usize, usize, usize)> {
            // Use a small capacity so that we have a higher chance to exercise
            // wrapping around logic.
            (1..=32usize).prop_flat_map(|cap| {
                //  cap      head     len
                (Just(cap), 0..cap, 0..=cap)
            })
        }

        pub(super) fn arb_ring_buffer() -> impl Strategy<Value = RingBuffer> {
            arb_ring_buffer_args().prop_map(|(cap, head, len)| RingBuffer {
                storage: vec![0; cap],
                head,
                len,
            })
        }

        /// A strategy for a [`RingBuffer`] and a valid length to mark read.
        pub(super) fn with_readable() -> impl Strategy<Value = (RingBuffer, usize)> {
            arb_ring_buffer_args().prop_flat_map(|(cap, head, len)| {
                (Just(RingBuffer { storage: vec![0; cap], head, len }), 0..=len)
            })
        }

        /// A strategy for a [`RingBuffer`] and a valid offset to write.
        pub(super) fn with_available() -> impl Strategy<Value = (RingBuffer, usize)> {
            arb_ring_buffer_args().prop_flat_map(|(cap, head, len)| {
                (Just(RingBuffer { storage: vec![0; cap], head, len }), 0..=cap - len)
            })
        }

        /// A strategy for a [`RingBuffer`], a valid offset and data to write.
        pub(super) fn with_offset_data() -> impl Strategy<Value = (RingBuffer, usize, Vec<u8>)> {
            arb_ring_buffer_args().prop_flat_map(|(cap, head, len)| {
                (0..=cap - len).prop_flat_map(move |offset| {
                    (0..=cap - len - offset).prop_flat_map(move |data_len| {
                        (
                            Just(RingBuffer { storage: vec![0; cap], head, len }),
                            Just(offset),
                            proptest::collection::vec(1..=u8::MAX, data_len),
                        )
                    })
                })
            })
        }

        /// A strategy for a [`RingBuffer`], its readable data, and how many
        /// bytes to consume.
        pub(super) fn with_read_data() -> impl Strategy<Value = (RingBuffer, Vec<u8>, usize)> {
            arb_ring_buffer_args().prop_flat_map(|(cap, head, len)| {
                proptest::collection::vec(1..=u8::MAX, len).prop_flat_map(move |data| {
                    // Fill the RingBuffer with the data.
                    let mut rb = RingBuffer { storage: vec![0; cap], head, len: 0 };
                    assert_eq!(rb.write_at(0, &&data[..]), len);
                    rb.make_readable(len);
                    (Just(rb), Just(data), 0..=len)
                })
            })
        }
    }
    mod send_payload {
        use super::*;
        use alloc::{borrow::ToOwned as _, vec::Vec};

        pub(super) fn with_index() -> impl Strategy<Value = (SendPayload<'static>, usize)> {
            proptest::prop_oneof![
                (Just(SendPayload::Contiguous(TEST_BYTES)), 0..TEST_BYTES.len()),
                (0..TEST_BYTES.len()).prop_flat_map(|split_at| {
                    (
                        Just(SendPayload::Straddle(
                            &TEST_BYTES[..split_at],
                            &TEST_BYTES[split_at..],
                        )),
                        0..TEST_BYTES.len(),
                    )
                })
            ]
        }

        pub(super) fn with_offset_and_length(
        ) -> impl Strategy<Value = (SendPayload<'static>, usize, usize)> {
            with_index().prop_flat_map(|(payload, index)| {
                (Just(payload), Just(index), 0..=TEST_BYTES.len() - index)
            })
        }

        impl SendPayload<'_> {
            pub(super) fn to_vec(self) -> Vec<u8> {
                match self {
                    SendPayload::Contiguous(p) => p.to_owned(),
                    SendPayload::Straddle(p1, p2) => [p1, p2].concat(),
                }
            }
        }
    }
}
