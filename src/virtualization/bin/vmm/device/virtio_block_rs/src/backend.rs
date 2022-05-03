// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire, anyhow::Error, async_trait::async_trait, std::borrow::Cow,
    virtio_device::mem::DeviceRange,
};

/// Represents a 512 byte sector.
#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct Sector(u64);

impl Sector {
    /// The largest sector value that can be represented in bytes using `u64`.
    #[cfg(test)]
    pub const MAX: Self = Self(
        (u64::MAX - (u64::MAX % wire::VIRTIO_BLOCK_SECTOR_SIZE)) / wire::VIRTIO_BLOCK_SECTOR_SIZE,
    );

    /// Constructs a sector from a raw numeric value.
    pub fn from_raw_sector(sector: u64) -> Self {
        Self(sector)
    }

    /// Constructs a sector from a raw byte value. If the value is not sector aligned it will
    /// be rounded down to the nearest sector.
    pub fn from_bytes_round_down(bytes: u64) -> Self {
        Self(bytes / wire::VIRTIO_BLOCK_SECTOR_SIZE)
    }

    /// Convert the sector address to a byte address.
    ///
    /// Returns `None` if the conversion would result in overflow.
    pub fn to_bytes(&self) -> Option<u64> {
        self.0.checked_mul(wire::VIRTIO_BLOCK_SECTOR_SIZE)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeviceAttrs {
    /// The capacity of this device.
    pub capacity: Sector,

    /// If Some, the preferred block_size of this device in bytes. None indicates the backend has
    /// no specific preference for block_size.
    pub block_size: Option<u32>,
}

/// `RangesBounded` is a wrapper around an iterator of `DeviceRange`s that can enforce an upper bound
/// on the length of any single `DeviceRange` by splitting it into multiple sub-ranges.
///
/// For example, if you have a single `DeviceRange` that is 4096 bytes, `RangesBounded` with a bound of
/// 1024 will produce 4 ranges:
///   > [0, 1024)
///   > [1024, 2048)
///   > [2048, 3072)
///   > [3072, 4096)
///
/// The iterator produces a tuple of a `u64` that indicates the device offset for the request, and
/// a `DeviceRange` that specifies the guest memory region for the request.
pub struct RangesBounded<'a, I: Iterator<Item = DeviceRange<'a>>> {
    iter: I,
    current: Option<DeviceRange<'a>>,
    offset: u64,
    bound: usize,
}

impl<'a, I: Iterator<Item = DeviceRange<'a>>> RangesBounded<'a, I> {
    pub fn next_range(&mut self, bound: usize) -> Option<DeviceRange<'a>> {
        if self.current.is_none() {
            self.current = self.iter.next();
        }
        if let Some(range) = self.current.take() {
            if range.len() > bound {
                let (new_range, remaining) = range.split_at(bound).unwrap();
                self.current = Some(remaining);
                Some(new_range)
            } else {
                Some(range)
            }
        } else {
            None
        }
    }

    pub fn next_with_bound(&mut self, bound: usize) -> Option<(u64, DeviceRange<'a>)> {
        let range = self.next_range(bound);
        if let Some(inner) = range {
            let offset = self.offset;
            self.offset = self.offset.checked_add(inner.len() as u64).unwrap();
            Some((offset, inner))
        } else {
            None
        }
    }
}

impl<'a, I: Iterator<Item = DeviceRange<'a>>> Iterator for RangesBounded<'a, I> {
    type Item = (u64, DeviceRange<'a>);

    fn next(&mut self) -> Option<Self::Item> {
        self.next_with_bound(self.bound)
    }
}

#[derive(Debug, Clone)]
pub struct Request<'a, 'b> {
    /// The offset, in bytes, from the start of the device to access.
    pub sector: Sector,

    /// A set of device memory regions that will be read from or written to based on the
    /// operation.
    pub ranges: Cow<'b, [DeviceRange<'a>]>,
}

impl<'a, 'b> Request<'a, 'b> {
    pub fn from_ref(ranges: &'b [DeviceRange<'a>], sector: Sector) -> Self {
        Self { ranges: Cow::Borrowed(ranges), sector }
    }

    /// Returns an unbounded iterator of the ranges in a Request that includes an accumulated
    /// disk offset.
    ///
    /// The primary use case of this is to read from the set of ranges with a variable bound. For
    /// example, the following are equivalent:
    ///
    /// ```
    ///     const BOUND: usize = 10;
    ///
    ///     // No ranges returned from `next()` will be greater than `BOUND`.
    ///     let ranges = request.ranges_bounded(BOUND);
    ///     while let Some(range) = ranges.next() { }
    ///
    ///     // Ranges returned from `next()` will be whatever is in the underlying virtio chain
    ///     // unmodified, but `next_with_bound` can be used to pull a bounded range out of the
    ///     // chain.
    ///     let ranges = request.ranges_unbounded();
    ///     while let Some(range) = ranges.next_with_bound(BOUND) { }
    /// ```
    #[allow(dead_code)]
    pub fn ranges_unbounded(
        &'b self,
    ) -> RangesBounded<'a, std::iter::Cloned<std::slice::Iter<'b, DeviceRange<'a>>>> {
        self.ranges_bounded(usize::MAX)
    }

    /// Returns an iterator over the `DeviceRange`s in this request, splitting any ranges that
    /// have a length that exceeds `bound`.
    pub fn ranges_bounded(
        &'b self,
        bound: usize,
    ) -> RangesBounded<'a, std::iter::Cloned<std::slice::Iter<'b, DeviceRange<'a>>>> {
        RangesBounded {
            iter: self.ranges.as_ref().iter().cloned(),
            current: None,
            offset: self.sector.to_bytes().unwrap(),
            bound,
        }
    }
}

#[async_trait(?Send)]
pub trait BlockBackend {
    /// Query basic attributes about the device.
    async fn get_attrs(&self) -> Result<DeviceAttrs, Error>;

    /// Read bytes from a starting sector into a set of DeviceRanges.
    async fn read<'a, 'b>(&self, request: Request<'a, 'b>) -> Result<(), Error>;

    /// Writes bytes from a starting sector into a set of DeviceRanges.
    ///
    /// Writes will be considered volatile after this operation completes, up until a subsequent
    /// flush command.
    async fn write<'a, 'b>(&self, requests: Request<'a, 'b>) -> Result<(), Error>;

    /// Commit any pending writes to non-volatile storage. The driver may consider a write to be
    /// durable after this operation completes.
    async fn flush(&self) -> Result<(), Error>;
}

#[cfg(test)]
mod tests {
    use {super::*, crate::wire, virtio_device::fake_queue::IdentityDriverMem};

    #[test]
    fn test_ranges_bounded_no_change() {
        // Create a Request with 3 ranges.
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
        ];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Create an iterator with a sector size bound. Since we only created sector-sized ranges
        // this should produce the same ranges as our input.
        let mut iter = request.ranges_bounded(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize);
        assert_eq!(Some((0, ranges[0].clone())), iter.next());
        assert_eq!(Some((1 * wire::VIRTIO_BLOCK_SECTOR_SIZE, ranges[1].clone())), iter.next());
        assert_eq!(Some((2 * wire::VIRTIO_BLOCK_SECTOR_SIZE, ranges[2].clone())), iter.next());
        assert_eq!(None, iter.next());
        assert_eq!(None, iter.next());
        assert_eq!(None, iter.next());
    }

    #[test]
    fn test_ranges_bounded_split_range() {
        // Create a Request with 3 ranges.
        let mem = IdentityDriverMem::new();
        let ranges = vec![mem.new_range(4 * wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap()];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Create an iterator with a sector size bound. We have one large range as an input so
        // we expect to have that split up into 4 sector-sized ranges.
        let mut iter = request.ranges_bounded(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize);
        let (range0, remain) = ranges[0].split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        let (range1, remain) = remain.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        let (range2, range3) = remain.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        assert_eq!(Some((0, range0)), iter.next());
        assert_eq!(Some((1 * wire::VIRTIO_BLOCK_SECTOR_SIZE, range1)), iter.next());
        assert_eq!(Some((2 * wire::VIRTIO_BLOCK_SECTOR_SIZE, range2)), iter.next());
        assert_eq!(Some((3 * wire::VIRTIO_BLOCK_SECTOR_SIZE, range3)), iter.next());
        assert_eq!(None, iter.next());
        assert_eq!(None, iter.next());
        assert_eq!(None, iter.next());
    }

    #[test]
    fn test_ranges_unbounded() {
        // Create a Request with 3 ranges.
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
        ];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Check that the unbounded iterator is the same as the slice iterator but with the
        // accumulated offset.
        ranges.iter().cloned().zip(request.ranges_unbounded()).fold(
            0,
            |expected_offset, (r1, (offset, r2))| {
                assert_eq!(r1, r2);
                assert_eq!(expected_offset, offset);
                expected_offset + r2.len() as u64
            },
        );
    }

    #[test]
    fn test_ranges_unbounded_split() {
        // Create a Request with 3 ranges.
        let mem = IdentityDriverMem::new();
        let ranges = vec![mem.new_range(4 * wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap()];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Create an iterator with a sector size bound. Since we only created sector-sized ranges
        // this should produce the same ranges as our input.
        let mut iter = request.ranges_unbounded();
        let (range0, remain) = ranges[0].split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        let (range1, remain) = remain.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        let (range2, range3) = remain.split_at(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap();
        assert_eq!(
            Some((0, range0)),
            iter.next_with_bound(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize)
        );
        assert_eq!(
            Some((1 * wire::VIRTIO_BLOCK_SECTOR_SIZE, range1)),
            iter.next_with_bound(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize)
        );
        assert_eq!(
            Some((2 * wire::VIRTIO_BLOCK_SECTOR_SIZE, range2)),
            iter.next_with_bound(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize)
        );
        assert_eq!(
            Some((3 * wire::VIRTIO_BLOCK_SECTOR_SIZE, range3)),
            iter.next_with_bound(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize)
        );
        assert_eq!(None, iter.next_with_bound(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize));
        assert_eq!(None, iter.next_with_bound(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize));
        assert_eq!(None, iter.next_with_bound(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize));
    }
}
