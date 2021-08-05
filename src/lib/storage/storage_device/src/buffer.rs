// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::buffer_allocator::BufferAllocator,
    std::ops::{Bound, Range, RangeBounds},
    std::slice::SliceIndex,
};

pub(super) fn round_down<T>(value: T, granularity: T) -> T
where
    T: num::Num + Copy,
{
    value - value % granularity
}

pub(super) fn round_up<T>(value: T, granularity: T) -> T
where
    T: num::Num + Copy,
{
    round_down(value + granularity - T::one(), granularity)
}

// Returns a range within a range.
// For example, subrange(100..200, 20..30) = 120..130.
fn subrange<R: RangeBounds<usize>>(source: &Range<usize>, bounds: &R) -> Range<usize> {
    let subrange = (match bounds.start_bound() {
        Bound::Included(&s) => source.start + s,
        Bound::Excluded(&s) => source.start + s + 1,
        Bound::Unbounded => source.start,
    })..(match bounds.end_bound() {
        Bound::Included(&e) => source.start + e + 1,
        Bound::Excluded(&e) => source.start + e,
        Bound::Unbounded => source.end,
    });
    assert!(subrange.end <= source.end);
    subrange
}

fn split_range(range: &Range<usize>, mid: usize) -> (Range<usize>, Range<usize>) {
    let l = range.end - range.start;
    let base = range.start;
    (base..base + mid, base + mid..base + l)
}

// TODO(jfsulliv): Eventually we will want zero-copy buffers which are provided by filesystem
// clients (e.g. via zx::stream) and which we either splice pages into or out of from a transfer
// buffer, or which we directly connect to the block device, or which we read and write to in some
// different way (involving changes to the block interface).

// TODO(jfsulliv): Eventually we will want unmapped buffers, which is necessary in some cases
// (e.g. the source VMO given to zx_pager_supply_pages cannot be mapped). This would most likely
// be a new type.

/// Buffer is a read-write buffer that can be used for I/O with the block device. They are created
/// by a BufferAllocator, and automatically deallocate themselves when they go out of scope.
///
/// Most usage will be on the unowned BufferRef and MutableBufferRef types, since these types are
/// used for Device::read and Device::write.
///
/// Buffers are always block-aligned (both in offset and length), but unaligned slices can be made
/// with the reference types. That said, the Device trait requires aligned BufferRef and
/// MutableBufferRef objects, so alignment must be restored by the time a device read/write is
/// requested.
///
/// For example, when writing an unaligned amount of data to the device, generally two Buffers
/// would need to be involved; the input Buffer could be used to write everything up to the last
/// block, and a second single-block alignment Buffer would be used to read-modify-update the last
/// block.
#[derive(Debug)]
pub struct Buffer<'a>(MutableBufferRef<'a>);

// Alias for the traits which need to be satisfied for |subslice| and friends.
// This trait is automatically satisfied for most typical uses (a..b, a.., ..b, ..).
pub trait SliceRange: Clone + RangeBounds<usize> + SliceIndex<[u8], Output = [u8]> {}
impl<T> SliceRange for T where T: Clone + RangeBounds<usize> + SliceIndex<[u8], Output = [u8]> {}

impl<'a> Buffer<'a> {
    pub(super) fn new(
        slice: &'a mut [u8],
        range: Range<usize>,
        allocator: &'a BufferAllocator,
    ) -> Self {
        Self(MutableBufferRef { slice, range, allocator })
    }

    /// Takes a read-only reference to this buffer.
    pub fn as_ref(&self) -> BufferRef<'_> {
        self.subslice(..)
    }

    /// Takes a read-only reference to this buffer over |range| (which must be within the size of
    /// the buffer).
    pub fn subslice<R: SliceRange>(&self, range: R) -> BufferRef<'_> {
        self.0.subslice(range)
    }

    /// Takes a read-write reference to this buffer.
    pub fn as_mut(&mut self) -> MutableBufferRef<'_> {
        self.subslice_mut(..)
    }

    /// Takes a read-write reference to this buffer over |range| (which must be within the size of
    /// the buffer).
    pub fn subslice_mut<R: SliceRange>(&mut self, range: R) -> MutableBufferRef<'_> {
        self.0.reborrow().subslice_mut(range)
    }

    /// Returns the buffer's capacity.
    pub fn len(&self) -> usize {
        self.0.len()
    }

    /// Returns a slice of the buffer's contents.
    pub fn as_slice(&self) -> &[u8] {
        self.0.as_slice()
    }

    /// Returns a mutable slice of the buffer's contents.
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        self.0.as_mut_slice()
    }

    /// Returns the range in the underlying BufferSource that this buffer covers.
    pub fn range(&self) -> Range<usize> {
        self.0.range()
    }
}

impl<'a> Drop for Buffer<'a> {
    fn drop(&mut self) {
        self.0.allocator.free_buffer(self.range());
    }
}

/// BufferRef is an unowned, read-only view over a Buffer.
#[derive(Clone, Copy, Debug)]
pub struct BufferRef<'a> {
    slice: &'a [u8],
    start: usize, // Not range so that we get Copy.
    end: usize,
    allocator: &'a BufferAllocator,
}

impl<'a> BufferRef<'a> {
    /// Returns the buffer's capacity.
    pub fn len(&self) -> usize {
        self.end - self.start
    }

    pub fn is_empty(&self) -> bool {
        self.end == self.start
    }

    /// Returns a slice of the buffer's contents.
    pub fn as_slice(&self) -> &[u8] {
        self.slice
    }

    /// Slices and consumes this reference. See Buffer::subslice.
    pub fn subslice<R: SliceRange>(&self, range: R) -> BufferRef<'_> {
        let slice = &self.slice[range.clone()];
        let range = subrange(&self.range(), &range);
        BufferRef { slice, start: range.start, end: range.end, allocator: self.allocator }
    }

    /// Splits at |mid| (included in the right child), yielding two BufferRefs.
    pub fn split_at(&self, mid: usize) -> (BufferRef<'_>, BufferRef<'_>) {
        let slices = self.slice.split_at(mid);
        let ranges = split_range(&self.range(), mid);
        (
            BufferRef {
                slice: slices.0,
                start: ranges.0.start,
                end: ranges.0.end,
                allocator: self.allocator,
            },
            BufferRef {
                slice: slices.1,
                start: ranges.1.start,
                end: ranges.1.end,
                allocator: self.allocator,
            },
        )
    }

    /// Returns the range in the underlying BufferSource that this BufferRef covers.
    /// TODO(jfsulliv): Currently unused in host code. Remove when there is a real Device impl.
    #[allow(dead_code)]
    pub fn range(&self) -> Range<usize> {
        self.start..self.end
    }
}

/// MutableBufferRef is an unowned, read-write view of a Buffer.
#[derive(Debug)]
pub struct MutableBufferRef<'a> {
    slice: &'a mut [u8],
    range: Range<usize>,
    allocator: &'a BufferAllocator,
}

impl<'a> MutableBufferRef<'a> {
    /// Returns the buffer's capacity.
    pub fn len(&self) -> usize {
        self.range.end - self.range.start
    }

    pub fn is_empty(&self) -> bool {
        self.range.end == self.range.start
    }

    pub fn as_ref(&self) -> BufferRef<'_> {
        self.subslice(..)
    }

    /// Returns a slice of the buffer's contents.
    pub fn as_slice(&self) -> &[u8] {
        self.slice
    }

    /// Returns a mutable slice of the buffer's contents.
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        self.slice
    }

    /// Reborrows this reference with a lesser lifetime. This mirrors the usual borrowing semantics
    /// (i.e. the borrow ends when the new reference goes out of scope), and exists so that a
    /// MutableBufferRef can be subsliced without consuming it.
    ///
    /// For example:
    ///    let mut buf: MutableBufferRef<'_> = ...;
    ///    {
    ///        let sub = buf.reborrow().subslice_mut(a..b);
    ///    }
    pub fn reborrow(&mut self) -> MutableBufferRef<'_> {
        MutableBufferRef { slice: self.slice, range: self.range.clone(), allocator: self.allocator }
    }

    /// Slices this reference. See Buffer::subslice.
    pub fn subslice<R: SliceRange>(&self, range: R) -> BufferRef<'_> {
        let slice = &self.slice[range.clone()];
        let range = subrange(&self.range, &range);
        BufferRef { slice, start: range.start, end: range.end, allocator: self.allocator }
    }

    /// Slices and consumes this reference. See Buffer::subslice_mut.
    pub fn subslice_mut<R: SliceRange>(mut self, range: R) -> MutableBufferRef<'a> {
        self.slice = &mut self.slice[range.clone()];
        self.range = subrange(&self.range, &range);
        self
    }

    /// Splits at |mid| (included in the right child), yielding two BufferRefs.
    pub fn split_at(&self, mid: usize) -> (BufferRef<'_>, BufferRef<'_>) {
        let slices = self.slice.split_at(mid);
        let ranges = split_range(&self.range, mid);
        (
            BufferRef {
                slice: slices.0,
                start: ranges.0.start,
                end: ranges.0.end,
                allocator: self.allocator,
            },
            BufferRef {
                slice: slices.1,
                start: ranges.1.start,
                end: ranges.1.end,
                allocator: self.allocator,
            },
        )
    }

    /// Consumes the reference and splits it at |mid| (included in the right child), yielding two
    /// MutableBufferRefs.
    pub fn split_at_mut(self, mid: usize) -> (MutableBufferRef<'a>, MutableBufferRef<'a>) {
        let slices = self.slice.split_at_mut(mid);
        let ranges = split_range(&self.range, mid);
        (
            MutableBufferRef { slice: slices.0, range: ranges.0, allocator: self.allocator },
            MutableBufferRef { slice: slices.1, range: ranges.1, allocator: self.allocator },
        )
    }

    /// Returns the range in the underlying BufferSource that this MutableBufferRef covers.
    /// TODO(jfsulliv): Currently unused in host code. Remove when there is a real Device impl.
    #[allow(dead_code)]
    pub fn range(&self) -> Range<usize> {
        self.range.clone()
    }
}
