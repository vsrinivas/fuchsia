// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use self::checksum::*;
pub use self::range::*;

/// Whether `size` fits in a `u16`.
pub fn fits_in_u16(size: usize) -> bool {
    size < 1 << 16
}

/// Whether `size` fits in a `u32`.
pub fn fits_in_u32(size: usize) -> bool {
    // trivially true when usize is 32 bits wide
    cfg!(target_pointer_width = "32") || size < 1 << 32
}

mod checksum {
    use byteorder::{BigEndian, ByteOrder};

    /// A checksum used by IPv4, TCP, and UDP.
    ///
    /// This checksum operates by computing the 1s complement of the 1s
    /// complement sum of successive 16-bit words of the input.
    pub struct Checksum(u32);

    impl Checksum {
        /// Initialize a new checksum.
        pub fn new() -> Self {
            Checksum(0)
        }

        /// Add bytes to the checksum.
        ///
        /// If `bytes` does not contain an even number of bytes, a single zero byte
        /// will be added to the end before updating the checksum.
        pub fn add_bytes(&mut self, mut bytes: &[u8]) {
            while bytes.len() > 1 {
                self.0 += u32::from(BigEndian::read_u16(bytes));
                bytes = &bytes[2..];
            }
            if bytes.len() == 1 {
                self.0 += u32::from(BigEndian::read_u16(&[bytes[0], 0]));
            }
        }

        /// Compute the checksum.
        ///
        /// `sum` returns the checksum of all data added using `add_bytes` so far.
        /// Calling `sum` does *not* reset the checksum. More bytes may be added
        /// after calling `sum`, and they will be added to the checksum as expected.
        pub fn sum(&self) -> u16 {
            let mut sum = self.0;
            while (sum >> 16) != 0 {
                sum = (sum >> 16) + (sum & 0xFF);
            }
            !sum as u16
        }
    }

    /// Checksum bytes.
    ///
    /// `checksum` is a shorthand for
    ///
    /// ```rust
    /// let mut c = Checksum::new();
    /// c.add_bytes(bytes);
    /// c.sum()
    /// ```
    pub fn checksum(bytes: &[u8]) -> u16 {
        let mut c = Checksum::new();
        c.add_bytes(bytes);
        c.sum()
    }
}

mod range {
    use std::ops::{Bound, RangeBounds};

    use zerocopy::ByteSlice;

    /// Extract a range from a slice of bytes.
    ///
    /// `extract_slice_range` extracts the given range from the given slice of
    /// bytes. It also returns the byte slices before and after the range.
    ///
    /// If the provided range is out of bounds of the slice, or if the range
    /// itself is nonsensical (if the upper bound precedes the lower bound),
    /// `extract_slice_range` returns `None`.
    pub fn extract_slice_range<B: ByteSlice, R: RangeBounds<usize>>(
        bytes: B, range: R,
    ) -> Option<(B, B, B)> {
        let lower = resolve_lower_bound(range.start_bound());
        let upper = resolve_upper_bound(bytes.len(), range.end_bound())?;
        if lower > upper {
            return None;
        }
        let (a, rest) = bytes.split_at(lower);
        let (b, c) = rest.split_at(upper - lower);
        Some((a, b, c))
    }

    // return the inclusive equivalent of the bound
    fn resolve_lower_bound(bound: Bound<&usize>) -> usize {
        match bound {
            Bound::Included(x) => *x,
            Bound::Excluded(x) => *x + 1,
            Bound::Unbounded => 0,
        }
    }

    // return the exclusive equivalent of the bound, verifying that it is in
    // range of len
    fn resolve_upper_bound(len: usize, bound: Bound<&usize>) -> Option<usize> {
        let bound = match bound {
            Bound::Included(x) => *x + 1,
            Bound::Excluded(x) => *x,
            Bound::Unbounded => len,
        };
        if bound > len {
            return None;
        }
        Some(bound)
    }
}
