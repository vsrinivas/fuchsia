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

    pub fn raw(&self) -> u64 {
        self.0
    }
}

impl std::ops::Add for Sector {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        Sector(self.0 + other.0)
    }
}

impl std::ops::AddAssign for Sector {
    fn add_assign(&mut self, other: Self) {
        self.0 += other.0
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

    pub fn split_at<'c>(&self, offset: Sector) -> Option<(Request<'a, 'c>, Request<'a, 'c>)> {
        let offset_bytes = offset.to_bytes().unwrap();
        let mut range_bytes = 0;
        for (i, range) in self.ranges.iter().enumerate() {
            let new_range_bytes = range_bytes + range.len() as u64;

            if new_range_bytes == offset_bytes {
                // If we have a split at a range boundary, we can just split the request slice.
                //
                // A further improvement here would involve just using a direct reference to our
                // slice in this situation instead of making a copy into a vector. This requires
                // some improvements to our bucketing algorithm in the CopyOnWriteBackend, however.
                let bound = i + 1;
                let v1 = self.ranges[0..bound].to_vec();
                let v2 = self.ranges[bound..].to_vec();
                let r1 = Request { sector: self.sector, ranges: Cow::Owned(v1) };
                let r2 = Request { sector: self.sector + offset, ranges: Cow::Owned(v2) };
                return Some((r1, r2));
            } else if new_range_bytes >= offset_bytes {
                // If this range spans the range boundary then we need to split this range.
                //
                // First we split the range that spans the request boundary.
                let (fragment1, fragment2) =
                    self.ranges[i].split_at((offset_bytes - range_bytes) as usize).unwrap();

                // Now create the two vectors. Note that we include the split index in both of the
                // split vectors since we need to update that with a partial range in both vectors.
                let bound = i + 1;
                let mut v1 = self.ranges[0..bound].to_vec();
                let mut v2 = self.ranges[i..].to_vec();

                // Now overwrite the range that spans the split point.
                let v1_last = v1.len() - 1;
                v1[v1_last] = fragment1;
                v2[0] = fragment2;

                let r1 = Request { sector: self.sector, ranges: Cow::Owned(v1) };
                let r2 = Request { sector: self.sector + offset, ranges: Cow::Owned(v2) };
                return Some((r1, r2));
            }

            range_bytes = new_range_bytes;
        }

        // If we haven't split then the offset is not valid.
        None
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

    #[test]
    fn test_request_split_at_large_range() {
        // Create a Request with a single 4-sector range.
        let mem = IdentityDriverMem::new();
        let ranges = vec![mem.new_range(4 * wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap()];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Split this at the first sector. This should yield a 1-sector request followed by a
        // 3-sector request.
        let (r1, r2) = request.split_at(Sector::from_raw_sector(1)).unwrap();
        assert_eq!(r1.sector, Sector::from_raw_sector(0));
        assert_eq!(r1.ranges.len(), 1);
        assert_eq!(r1.ranges[0].get().start, ranges[0].get().start);
        assert_eq!(r1.ranges[0].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize);

        assert_eq!(r2.sector, Sector::from_raw_sector(1));
        assert_eq!(r2.ranges.len(), 1);
        assert_eq!(
            r2.ranges[0].get().start,
            ranges[0].get().start + wire::VIRTIO_BLOCK_SECTOR_SIZE as usize
        );
        assert_eq!(r2.ranges[0].len(), 3 * wire::VIRTIO_BLOCK_SECTOR_SIZE as usize);
    }

    #[test]
    fn test_request_split_at_range_boundary() {
        // Create a Request with 3 ranges.
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
        ];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Split this at the first sector. This should yield a 1-sector request followed by a
        // 2-sector request.
        let (r1, r2) = request.split_at(Sector::from_raw_sector(1)).unwrap();
        assert_eq!(r1.sector, Sector::from_raw_sector(0));
        assert_eq!(r1.ranges.len(), 1);
        assert_eq!(r1.ranges[0].get().start, ranges[0].get().start);
        assert_eq!(r1.ranges[0].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize);

        assert_eq!(r2.sector, Sector::from_raw_sector(1));
        assert_eq!(r2.ranges.len(), 2);
        assert_eq!(r2.ranges[0].get().start, ranges[1].get().start);
        assert_eq!(r2.ranges[0].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize);

        let (r3, r4) = r2.split_at(Sector::from_raw_sector(1)).unwrap();
        assert_eq!(r3.sector, Sector::from_raw_sector(1));
        assert_eq!(r3.ranges.len(), 1);
        assert_eq!(r3.ranges[0].get().start, ranges[1].get().start);
        assert_eq!(r3.ranges[0].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize);

        assert_eq!(r4.sector, Sector::from_raw_sector(2));
        assert_eq!(r4.ranges.len(), 1);
        assert_eq!(r4.ranges[0].get().start, ranges[2].get().start);
        assert_eq!(r4.ranges[0].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize);
    }

    #[test]
    fn test_request_split_at_mixed_ranges() {
        // We have a request that spans 2 full sectors (half sector, full sector, half sector).
        let mem = IdentityDriverMem::new();
        let ranges = vec![
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 2).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap(),
            mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 2).unwrap(),
        ];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Split it in the middle to produce two 1-sector requests.
        let (r1, r2) = request.split_at(Sector::from_raw_sector(1)).unwrap();
        assert_eq!(r1.sector, Sector::from_raw_sector(0));
        assert_eq!(r1.ranges.len(), 2);
        assert_eq!(r1.ranges[0].get().start, ranges[0].get().start);
        assert_eq!(r1.ranges[0].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 2);
        assert_eq!(r1.ranges[1].get().start, ranges[1].get().start);
        assert_eq!(r1.ranges[1].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 2);

        assert_eq!(r2.sector, Sector::from_raw_sector(1));
        assert_eq!(r2.ranges.len(), 2);
        assert_eq!(r2.ranges[0].get().end, ranges[1].get().end);
        assert_eq!(r2.ranges[0].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 2);
        assert_eq!(r2.ranges[1].get().start, ranges[2].get().start);
        assert_eq!(r2.ranges[1].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 2);
    }

    #[test]
    fn test_request_split_at_end() {
        let mem = IdentityDriverMem::new();
        let ranges = vec![mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap()];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Split the at the end of the request.
        let (r1, r2) = request.split_at(Sector::from_raw_sector(1)).unwrap();

        // The first request should equal the input request.
        assert_eq!(r1.sector, Sector::from_raw_sector(0));
        assert_eq!(r1.ranges.len(), 1);
        assert_eq!(r1.ranges[0].get().start, ranges[0].get().start);
        assert_eq!(r1.ranges[0].len(), wire::VIRTIO_BLOCK_SECTOR_SIZE as usize);

        // The second request should be empty.
        assert_eq!(r2.sector, Sector::from_raw_sector(1));
        assert_eq!(r2.ranges.len(), 0);
    }

    #[test]
    fn test_request_split_at_out_of_range() {
        let mem = IdentityDriverMem::new();
        let ranges = vec![mem.new_range(wire::VIRTIO_BLOCK_SECTOR_SIZE as usize).unwrap()];
        let request = Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(0));

        // Split the at the end of the request.
        let result = request.split_at(Sector::from_raw_sector(2));

        // The first request should equal the input request.
        assert!(result.is_none());
    }
}
