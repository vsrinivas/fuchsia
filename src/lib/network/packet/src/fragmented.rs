// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::ops::{Range, RangeBounds};

use zerocopy::{ByteSlice, ByteSliceMut};

use crate::{canonicalize_range, take_back, take_back_mut, take_front, take_front_mut};

/// A wrapper for a sequence of byte slices.
///
/// `FragmentedByteSlice` shares its underlying memory with the slice it was
/// constructed from and, as a result, operations on a `FragmentedByteSlice` may
/// mutate the backing slice.
#[derive(Debug)]
pub struct FragmentedByteSlice<'a, B: ByteSlice>(&'a mut [B]);

/// A single byte slice fragment in a [`FragmentedByteSlice`].
pub trait Fragment: ByteSlice {
    /// Takes `n` bytes from the front of this fragment.
    ///
    /// After a call to `take_front(n)`, the fragment is `n` bytes shorter.
    ///
    /// # Panics
    ///
    /// Panics if `n` is larger than the length of this `ByteSlice`.
    fn take_front(&mut self, n: usize) -> Self;

    /// Takes `n` bytes from the back of this fragment.
    ///
    /// After a call to `take_back(n)`, the fragment is `n` bytes shorter.
    ///
    /// # Panics
    ///
    /// Panics if `n` is larger than the length of this `ByteSlice`.
    fn take_back(&mut self, n: usize) -> Self;

    /// Constructs a new empty `Fragment`.
    fn empty() -> Self;
}

/// A type that can produce a `FragmentedByteSlice` view of itself.
pub trait AsFragmentedByteSlice<B: Fragment> {
    /// Generates a `FragmentedByteSlice` view of `self`.
    fn as_fragmented_byte_slice(&mut self) -> FragmentedByteSlice<'_, B>;
}

impl<O, B> AsFragmentedByteSlice<B> for O
where
    B: Fragment,
    O: AsMut<[B]>,
{
    fn as_fragmented_byte_slice(&mut self) -> FragmentedByteSlice<'_, B> {
        FragmentedByteSlice::new(self.as_mut())
    }
}

impl<'a> Fragment for &'a [u8] {
    fn take_front(&mut self, n: usize) -> Self {
        take_front(self, n)
    }

    fn take_back(&mut self, n: usize) -> Self {
        take_back(self, n)
    }

    fn empty() -> Self {
        &[]
    }
}

impl<'a> Fragment for &'a mut [u8] {
    fn take_front(&mut self, n: usize) -> Self {
        take_front_mut(self, n)
    }

    fn take_back(&mut self, n: usize) -> Self {
        take_back_mut(self, n)
    }

    fn empty() -> Self {
        &mut []
    }
}

impl<'a, B: 'a + Fragment> FragmentedByteSlice<'a, B> {
    /// Constructs a new `FragmentedByteSlice` from `bytes`.
    ///
    /// It is important to note that `FragmentedByteSlice` takes a mutable
    /// reference to a backing slice. Operations on the `FragmentedByteSlice`
    /// may mutate `bytes` as an optimization to avoid extra allocations.
    ///
    /// Users are encouraged to treat slices used to construct
    /// `FragmentedByteSlice`s as if they are not owned anymore and only serve
    /// as (usually temporary) backing for a `FragmentedByteSlice`.
    pub fn new(bytes: &'a mut [B]) -> Self {
        Self(bytes)
    }

    /// Constructs a new empty `FragmentedByteSlice`.
    pub fn new_empty() -> Self {
        Self(&mut [])
    }

    /// Gets the total length, in bytes, of this `FragmentedByteSlice`.
    pub fn len(&self) -> usize {
        // TODO(brunodalbo) explore if caching the total length in a
        // FragmentedByteSlice could be a worthy performance optimization.
        self.0.iter().map(|x| x.len()).sum()
    }

    /// Returns `true` if the `FragmentedByteSlice` is empty.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Slices this `FragmentedByteSlice`, reducing it to only the bytes within
    /// `range`.
    ///
    /// `slice` will mutate the backing slice by dropping or shrinking fragments
    /// as necessary so the overall composition matches the requested `range`.
    /// The returned `FragmentedByteSlice` uses the same (albeit possibly
    /// modified) backing mutable slice reference as `self`.
    ///
    /// # Panics
    ///
    /// Panics if the provided `range` is not within the bounds of this
    /// `FragmentedByteSlice`, or if the range is nonsensical (the end precedes
    /// the start).
    pub fn slice<R>(self, range: R) -> Self
    where
        R: RangeBounds<usize>,
    {
        let len = self.len();
        let range = canonicalize_range(len, &range);
        let mut bytes = self.0;
        // c is the amount of bytes we need to discard from the beginning of the
        // fragments.
        let mut c = range.start;
        while c != 0 {
            let first = &mut bytes[0];
            if first.len() > c {
                // if the first fragment contains more than c bytes, just take
                // c bytes out of its front and we're done.
                let _: B = first.take_front(c);
                break;
            } else {
                // otherwise, just account for the first fragment's entire
                // length and drop it.
                c -= first.len();
                bytes = &mut bytes[1..];
            }
        }
        // c is the amount of bytes we need to discard from the end of the
        // fragments.
        let mut c = len - range.end;
        while c != 0 {
            let idx = bytes.len() - 1;
            let last = &mut bytes[idx];
            if last.len() > c {
                // if the last fragment contains more than c bytes, just take
                // c bytes out of its back and we're done.
                let _: B = last.take_back(c);
                break;
            } else {
                // otherwise, just account for the last fragment's entire length
                // and drop it.
                c -= last.len();
                bytes = &mut bytes[..idx];
            }
        }
        Self(bytes)
    }

    /// Checks whether the contents of this `FragmentedByteSlice` are equal to
    /// the contents of `other`.
    pub fn eq_slice(&self, mut other: &[u8]) -> bool {
        for x in self.0.iter() {
            let x = x.as_ref();
            if other.len() < x.len() || !x.eq(&other[..x.len()]) {
                return false;
            }
            other = &other[x.len()..];
        }
        other.is_empty()
    }

    /// Iterates over all the bytes in this `FragmentedByteSlice`.
    pub fn iter(&self) -> impl '_ + Iterator<Item = u8> {
        self.0.iter().map(|x| x.iter()).flatten().copied()
    }

    /// Iterates over the fragments of this `FragmentedByteSlice`.
    pub fn iter_fragments(&'a self) -> impl 'a + Iterator<Item = &'a [u8]> + Clone {
        self.0.iter().map(|x| x.as_ref())
    }

    /// Copies all the bytes in `self` into the contiguous slice `dst`.
    ///
    /// # Panics
    ///
    /// Panics if `dst.len() != self.len()`.
    pub fn copy_into_slice(&self, mut dst: &mut [u8]) {
        for p in self.0.iter() {
            let (tgt, nxt) = dst.split_at_mut(p.len());
            tgt.copy_from_slice(p.as_ref());
            dst = nxt;
        }
        assert_eq!(dst.len(), 0);
    }

    /// Returns a flattened version of this buffer, copying its contents into a
    /// [`Vec`].
    pub fn to_flattened_vec(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(self.len());
        for x in self.0.iter() {
            out.extend_from_slice(x);
        }
        out
    }

    /// Creates an index tuple from a linear index `idx`.
    ///
    /// `get_index` creates a tuple index `(slice, byte)` where `slice` is the
    /// index in the backing slice of slices and `byte` is the byte index in the
    /// slice at `self.0[slice]` where `(slice, byte)` represents the `idx`th
    /// byte in this `FragmentedByteSlice`.
    ///
    /// # Panics
    ///
    /// Panics if `idx` is out of bounds.
    fn get_index(&self, mut idx: usize) -> (usize, usize) {
        let mut a = 0;
        while self.0[a].len() <= idx || self.0[a].len() == 0 {
            idx -= self.0[a].len();
            a += 1;
        }
        (a, idx)
    }

    /// Increments the index tuple `idx`.
    ///
    /// Increments the index tuple `idx` (see
    /// [`FragmentedByteSlice::get_index`]) so it references the next byte.
    /// `increment_index` will stop incrementing and just return a slice index
    /// equal to the length of the backing slice if `idx` can't be incremented
    /// anymore.
    fn increment_index(&self, idx: &mut (usize, usize)) {
        if self.0[idx.0].len() > (idx.1 + 1) {
            idx.1 += 1;
        } else {
            idx.0 += 1;
            // skip any empty slices:
            while idx.0 < self.0.len() && self.0[idx.0].len() == 0 {
                idx.0 += 1;
            }
            idx.1 = 0;
        }
    }

    /// Decrements the index tuple `idx`.
    ///
    /// Decrements the index tuple `idx` (see
    /// [`FragmentedByteSlice::get_index`]) so it references the previous byte.
    /// `decrement_index` will wrap around to an invalid out of bounds index
    /// (slice index is equal to the length of the backing slice) if `idx` is
    /// pointing to the `0`th byte.
    fn decrement_index(&self, idx: &mut (usize, usize)) {
        if idx.1 == 0 {
            if idx.0 == 0 {
                idx.0 = self.0.len();
                idx.1 = 0;
                return;
            }
            idx.0 -= 1;
            // skip any empty slices:
            while idx.0 != 0 && self.0[idx.0].len() == 0 {
                idx.0 -= 1;
            }
            if self.0[idx.0].len() != 0 {
                idx.1 = self.0[idx.0].len() - 1;
            } else {
                idx.0 = self.0.len();
                idx.1 = 0;
            }
        } else {
            idx.1 -= 1;
        }
    }

    /// Tries to convert this `FragmentedByteSlice` into a contiguous one.
    ///
    /// Returns `Ok` if `self`'s backing storage contains 0 or 1 byte slices,
    /// and `Err` otherwise.
    ///
    /// If `self`'s backing storage contains 1 byte slice, that byte slice will
    /// be replaced with an empty byte slice, and the original used to construct
    /// the return value.
    pub fn try_into_contiguous(self) -> Result<B, Self> {
        if self.0.is_empty() {
            Ok(B::empty())
        } else if self.0.len() == 1 {
            Ok(std::mem::replace(&mut self.0[0], B::empty()))
        } else {
            Err(self)
        }
    }

    /// Tries to get a contiguous reference to this `FragmentedByteSlice`.
    ///
    /// Returns `Some` if `self`'s backing storage contains 0 or 1 byte slices,
    /// and `None` otherwise.
    pub fn try_get_contiguous(&self) -> Option<&[u8]> {
        match &self.0 {
            [] => Some(&[]),
            [slc] => Some(slc),
            _ => None,
        }
    }

    /// Tries to split this `FragmentedByteSlice` into a contiguous prefix, a
    /// (possibly fragmented) body, and a contiguous suffix.
    ///
    /// Returns `None` if it isn't possible to form a contiguous prefix and
    /// suffix with the provided `range`.
    ///
    /// # Panics
    ///
    /// Panics if the range is out of bounds, or if the range is nonsensical
    /// (the end precedes the start).
    pub fn try_split_contiguous<R>(self, range: R) -> Option<(B, Self, B)>
    where
        R: RangeBounds<usize>,
    {
        let len = self.len();
        let range = canonicalize_range(len, &range);
        if len == 0 && range.start == 0 && range.end == 0 {
            // If own length is zero and the requested body range is an empty
            // body start at zero, avoid returning None in the call to
            // last_mut() below.
            return Some((B::empty(), FragmentedByteSlice(&mut []), B::empty()));
        }

        // take foot first, because if we have a single fragment, taking head
        // first will mess with the index calculations.

        let foot = self.0.last_mut()?;
        let take = len - range.end;
        if foot.len() < take {
            return None;
        }
        let foot = foot.take_back(take);

        let head = self.0.first_mut()?;
        if head.len() < range.start {
            return None;
        }
        let head = head.take_front(range.start);

        Some((head, self, foot))
    }
}

impl<'a, B: 'a + ByteSliceMut + Fragment> FragmentedByteSlice<'a, B> {
    /// Iterates over mutable references to all the bytes in this
    /// `FragmentedByteSlice`.
    pub fn iter_mut(&mut self) -> impl '_ + Iterator<Item = &'_ mut u8> {
        self.0.iter_mut().map(|x| x.iter_mut()).flatten()
    }

    /// Copies all the bytes in `src` to `self`.
    ///
    /// # Panics
    ///
    /// Panics if `self.len() != src.len()`.
    pub fn copy_from_slice(&mut self, mut src: &[u8]) {
        for p in self.0.iter_mut() {
            let (cur, nxt) = src.split_at(p.len());
            p.as_mut().copy_from_slice(cur);
            src = nxt;
        }
        assert_eq!(src.len(), 0);
    }

    /// Copies all the bytes from another `FragmentedByteSlice` `other` into
    /// `self`.
    ///
    /// # Panics
    ///
    /// Panics if `self.len() != other.len()`.
    pub fn copy_from<BB>(&mut self, other: &FragmentedByteSlice<'_, BB>)
    where
        BB: ByteSlice,
    {
        // keep an iterator over the fragments in other.
        let mut oth = other.0.iter().map(|z| z.as_ref());
        // op is the current fragment in other we're copying from.
        let mut op = oth.next();
        for part in self.0.iter_mut() {
            // p is the current fragment in self we're feeding bytes into.
            let mut p = part.as_mut();
            // iterate until this fragment is all consumed.
            while !p.is_empty() {
                // skip any empty slices in other.
                while op.unwrap().is_empty() {
                    op = oth.next();
                }
                // get the current fragment in other.
                let k = op.unwrap();
                if k.len() <= p.len() {
                    // if k does not have enough bytes to fill p, copy what we
                    // can, change p to the region that hasn't been updated, and
                    // then fetch the next fragment from other.
                    let (dst, nxt) = p.split_at_mut(k.len());
                    dst.copy_from_slice(k.as_ref());
                    p = nxt;
                    op = oth.next();
                } else {
                    // Otherwise, copy the p.len() first bytes from k, and
                    // modify op to keep the rest of the bytes in k.
                    let (src, nxt) = k.split_at(p.len());
                    p.copy_from_slice(src.as_ref());
                    op = Some(nxt);
                    // break from loop, p had all its bytes copied.
                    break;
                }
            }
        }
        // If anything is left in our iterator, panic if it isn't an empty slice
        // since the lengths must match.
        while let Some(v) = op {
            assert_eq!(v.len(), 0);
            op = oth.next();
        }
    }

    /// Copies elements from one part of the `FragmentedByteSlice` to another
    /// part of itself.
    ///
    /// `src` is the range within `self` to copy from. `dst` is the starting
    /// index of the range within `self` to copy to, which will have the same
    /// length as `src`. The two ranges may overlap. The ends of the two ranges
    /// must be less than or equal to `self.len()`.
    ///
    /// # Panics
    ///
    /// Panics if either the source or destination range is out of bounds, or if
    /// `src` is nonsensical (its end precedes its start).
    pub fn copy_within<R: RangeBounds<usize>>(&mut self, src: R, dst: usize) {
        let Range { start, end } = canonicalize_range(self.len(), &src);
        assert!(end >= start);
        let len = end - start;
        if start == dst || len == 0 {
            // no work to do
        } else if start > dst {
            // copy front to back
            let mut start = self.get_index(start);
            let mut dst = self.get_index(dst);
            for _ in 0..len {
                self.0[dst.0][dst.1] = self.0[start.0][start.1];
                self.increment_index(&mut start);
                self.increment_index(&mut dst);
            }
        } else {
            // copy back to front
            let mut start = self.get_index(end - 1);
            let mut dst = self.get_index(dst + len - 1);
            for _ in 0..len {
                self.0[dst.0][dst.1] = self.0[start.0][start.1];
                self.decrement_index(&mut start);
                self.decrement_index(&mut dst);
            }
        }
    }

    /// Attempts to get a contiguous mutable reference to this
    /// `FragmentedByteSlice`.
    ///
    /// Returns `Some` if this `FragmentedByteSlice` is a single contiguous part
    /// (or is empty). Returns `None` otherwise.
    pub fn try_get_contiguous_mut(&mut self) -> Option<&mut [u8]> {
        match &mut self.0 {
            [] => Some(&mut []),
            [slc] => Some(slc),
            _ => None,
        }
    }
}

/// A [`FragmentedByteSlice`] backed by immutable byte slices.
pub type FragmentedBytes<'a, 'b> = FragmentedByteSlice<'a, &'b [u8]>;
/// A [`FragmentedByteSlice`] backed by mutable byte slices.
pub type FragmentedBytesMut<'a, 'b> = FragmentedByteSlice<'a, &'b mut [u8]>;

#[cfg(test)]
mod tests {
    use super::*;

    /// Calls `f` with all the possible three way slicings of a non-mutable
    /// buffer containing `[1,2,3,4,5]` (including cases with empty slices).
    fn with_fragments<F: for<'a, 'b> FnMut(FragmentedBytes<'a, 'b>)>(mut f: F) {
        let buff = [1_u8, 2, 3, 4, 5];
        for i in 0..buff.len() {
            for j in i..buff.len() {
                let (a, x) = buff.split_at(i);
                let (b, c) = x.split_at(j - i);
                let mut frags = [a, b, c];
                f(frags.as_fragmented_byte_slice());
            }
        }
    }

    /// Calls `f` with all the possible three way slicings of a non-mutable
    /// buffer containing `[1,2,3,4,5]` (including cases with empty slices).
    fn with_fragments_mut<F: for<'a, 'b> FnMut(FragmentedBytesMut<'a, 'b>)>(mut f: F) {
        let buff = [1_u8, 2, 3, 4, 5];
        for i in 0..buff.len() {
            for j in i..buff.len() {
                let mut buff = [1_u8, 2, 3, 4, 5];
                let (a, x) = buff.split_at_mut(i);
                let (b, c) = x.split_at_mut(j - i);
                let mut frags = [a, b, c];
                f(frags.as_fragmented_byte_slice());
            }
        }
    }

    #[test]
    fn test_iter() {
        // check iterator over different fragment permutations.
        with_fragments(|bytes| {
            let mut iter = bytes.iter();
            for i in 1_u8..6 {
                assert_eq!(iter.next().unwrap(), i);
            }
            assert!(iter.next().is_none());
            assert!(iter.next().is_none());
        });
    }

    #[test]
    fn test_eq() {
        // check equality over different fragment permutations.
        with_fragments(|bytes| {
            assert!(bytes.eq_slice([1_u8, 2, 3, 4, 5].as_ref()));
            assert!(!bytes.eq_slice([1_u8, 2, 3, 4].as_ref()));
            assert!(!bytes.eq_slice([1_u8, 2, 3, 4, 5, 6].as_ref()));
            assert!(!bytes.eq_slice(&[]));
        });

        // check equality for the empty slice case.
        let bytes = FragmentedBytes::new_empty();
        assert!(!bytes.eq_slice([1_u8, 2, 3, 4, 5].as_ref()));
        assert!(bytes.eq_slice(&[]));
    }

    #[test]
    fn test_slice() {
        // test all valid ranges with all possible permutations of a three way
        // slice.
        for i in 0..6 {
            for j in i..6 {
                with_fragments(|bytes| {
                    let range = bytes.slice(i..j);
                    let x = [1_u8, 2, 3, 4, 5];
                    assert_eq!(&range.to_flattened_vec()[..], &x[i..j], "{}..{}", i, j);
                });
            }
        }
    }

    #[test]
    #[should_panic]
    fn test_slice_out_of_range() {
        // check that slicing out of range will panic
        with_fragments(|bytes| {
            let _ = bytes.slice(0..15);
        });
    }

    #[test]
    #[should_panic]
    fn test_copy_into_slice_too_big() {
        // check that copy_into_slice panics for different lengths.
        with_fragments(|bytes| {
            let mut slice = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
            bytes.copy_into_slice(&mut slice[..]);
        });
    }

    #[test]
    #[should_panic]
    fn test_copy_into_slice_too_small() {
        // check that copy_into_slice panics for different lengths.
        with_fragments(|bytes| {
            let mut slice = [1, 2];
            bytes.copy_into_slice(&mut slice[..]);
        });
    }

    #[test]
    fn test_copy_into_slice() {
        // try copy_into_slice with all different fragment permutations.
        with_fragments(|bytes| {
            let mut slice = [0; 5];
            bytes.copy_into_slice(&mut slice[..]);
            assert_eq!(slice, &[1, 2, 3, 4, 5][..]);
        });
    }

    #[test]
    #[should_panic]
    fn test_copy_from_slice_too_big() {
        // check that copy_from_slice panics for different lengths.
        with_fragments_mut(|mut bytes| {
            let slice = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
            bytes.copy_from_slice(&slice[..]);
        });
    }

    #[test]
    #[should_panic]
    fn test_copy_from_slice_too_small() {
        // check that copy_from_slice panics for different lengths.
        with_fragments_mut(|mut bytes| {
            let slice = [1, 2, 3];
            bytes.copy_from_slice(&slice[..]);
        });
    }

    #[test]
    fn test_copy_from_slice() {
        // test copy_from_slice with all fragment permutations.
        with_fragments_mut(|mut bytes| {
            let slice = [10, 20, 30, 40, 50];
            bytes.copy_from_slice(&slice[..]);
            assert_eq!(&bytes.to_flattened_vec()[..], &slice[..]);
        });
    }

    #[test]
    fn test_copy_from() {
        // test copying from another FragmentedByteSlice, going over all
        // fragment permutations for both src and dst.
        with_fragments(|src| {
            with_fragments_mut(|mut dst| {
                // zer-out dst
                dst.copy_from_slice(&[0; 5][..]);
                dst.copy_from(&src);
                assert_eq!(&dst.to_flattened_vec()[..], &[1_u8, 2, 3, 4, 5][..]);
            })
        });
    }

    #[test]
    #[should_panic]
    fn test_copy_from_too_long() {
        // copying from another FragmentedByteSlice should panic if the lengths
        // differ.
        let mut a = [0; 2];
        let mut b = [0; 2];
        let mut frags = [a.as_mut(), b.as_mut()];
        with_fragments(|src| {
            frags.as_fragmented_byte_slice().copy_from(&src);
        });
    }

    #[test]
    #[should_panic]
    fn test_copy_from_too_short() {
        // copying from another FragmentedByteSlice should panic if the lengths
        // differ.
        let mut a = [0; 5];
        let mut b = [0; 2];
        let mut frags = [a.as_mut(), b.as_mut()];
        with_fragments(|src| {
            frags.as_fragmented_byte_slice().copy_from(&src);
        });
    }

    #[test]
    fn test_indexing() {
        // Test the internal indexing functions over all fragment permutations.
        with_fragments(|bytes| {
            for i in 0..5 {
                // check that get_index addresses the expected byte.
                let mut idx = bytes.get_index(i);
                assert_eq!(bytes.0[idx.0][idx.1], (i + 1) as u8);

                // check that we can increase it correctly until the end of the
                // buffer.
                for j in 1..(6 - i - 1) {
                    bytes.increment_index(&mut idx);
                    assert_eq!(bytes.0[idx.0][idx.1], (i + j + 1) as u8);
                }

                // fetch the same index again.
                let mut idx = bytes.get_index(i);
                assert_eq!(bytes.0[idx.0][idx.1], (i + 1) as u8);

                // check that we can decrease it correctly until the beginning
                // of the buffer.
                for j in 1..=i {
                    bytes.decrement_index(&mut idx);
                    assert_eq!(bytes.0[idx.0][idx.1], (i - j + 1) as u8);
                }
            }
        });
    }

    #[test]
    fn test_copy_within() {
        with_fragments_mut(|mut bytes| {
            // copy last half to beginning:
            bytes.copy_within(3..5, 0);
            assert_eq!(&bytes.to_flattened_vec()[..], &[4, 5, 3, 4, 5]);
        });
        with_fragments_mut(|mut bytes| {
            // copy first half to end:
            bytes.copy_within(0..2, 3);
            assert_eq!(&bytes.to_flattened_vec()[..], &[1, 2, 3, 1, 2]);
        });
    }

    #[test]
    #[should_panic]
    fn test_copy_within_src_out_of_bounds() {
        with_fragments_mut(|mut bytes| {
            // try to copy out of bounds
            bytes.copy_within(3..15, 0);
        });
    }

    #[test]
    #[should_panic]
    fn test_copy_within_dst_out_of_bounds() {
        with_fragments_mut(|mut bytes| {
            // try to copy out of bounds
            bytes.copy_within(3..5, 15);
        });
    }

    #[test]
    #[should_panic]
    fn test_copy_within_bad_range() {
        with_fragments_mut(|mut bytes| {
            // pass a bad range (end before start)
            #[allow(clippy::reversed_empty_ranges)]
            bytes.copy_within(5..3, 0);
        });
    }

    #[test]
    fn test_get_contiguous() {
        // If we have fragments, get_contiguous should fail:
        with_fragments_mut(|mut bytes| {
            assert!(bytes.try_get_contiguous().is_none());
            assert!(bytes.try_get_contiguous_mut().is_none());
            assert!(bytes.try_into_contiguous().is_err());
        });

        // otherwise we should be able to get the contiguous bytes:
        let mut single = [1_u8, 2, 3, 4, 5];
        let mut single = [&mut single[..]];
        let mut single = single.as_fragmented_byte_slice();
        assert_eq!(single.try_get_contiguous().unwrap(), &[1, 2, 3, 4, 5][..]);
        assert_eq!(single.try_get_contiguous_mut().unwrap(), &[1, 2, 3, 4, 5][..]);
        assert_eq!(single.try_into_contiguous().unwrap(), &[1, 2, 3, 4, 5][..]);
    }

    #[test]
    fn test_split_contiguous() {
        let data = [1_u8, 2, 3, 4, 5, 6];

        // try with a single continuous slice
        let mut refs = [&data[..]];
        let frag = refs.as_fragmented_byte_slice();
        let (head, body, foot) = frag.try_split_contiguous(2..4).unwrap();
        assert_eq!(head, &data[..2]);
        assert_eq!(&body.to_flattened_vec()[..], &data[2..4]);
        assert_eq!(foot, &data[4..]);

        // try splitting just part of the header
        let mut refs = [&data[0..3], &data[3..]];
        let frag = refs.as_fragmented_byte_slice();
        let (head, body, foot) = frag.try_split_contiguous(2..6).unwrap();
        assert_eq!(head, &data[..2]);
        assert_eq!(&body.to_flattened_vec()[..], &data[2..]);
        assert!(foot.is_empty());

        // try splitting just part of the footer
        let mut refs = [&data[0..3], &data[3..]];
        let frag = refs.as_fragmented_byte_slice();
        let (head, body, foot) = frag.try_split_contiguous(..4).unwrap();
        assert!(head.is_empty());
        assert_eq!(&body.to_flattened_vec()[..], &data[..4]);
        assert_eq!(foot, &data[4..]);

        // try completely extracting both:
        let mut refs = [&data[0..3], &data[3..]];
        let frag = refs.as_fragmented_byte_slice();
        let (head, body, foot) = frag.try_split_contiguous(3..3).unwrap();
        assert_eq!(head, &data[0..3]);
        assert_eq!(body.len(), 0);
        assert_eq!(foot, &data[3..]);

        // try getting contiguous bytes from an empty FragmentedByteSlice:
        let frag = FragmentedBytes::new_empty();
        let (head, body, foot) = frag.try_split_contiguous(..).unwrap();
        assert!(head.is_empty());
        assert!(body.is_empty());
        assert!(foot.is_empty());
    }

    #[test]
    #[should_panic]
    fn test_split_contiguous_out_of_bounds() {
        let data = [1_u8, 2, 3, 4, 5, 6];
        let mut refs = [&data[..]];
        let frag = refs.as_fragmented_byte_slice();
        let _ = frag.try_split_contiguous(2..8);
    }

    #[test]
    fn test_empty() {
        // Can create empty FragmentedByteSlices with no fragments or with one
        // empty fragment.
        // is_empty should return true for both cases.
        let empty = FragmentedByteSlice::<&'static [u8]>::new_empty();
        assert!(empty.is_empty());
        let empty = [0_u8; 0];
        let mut empty = [&empty[..]];
        let empty = empty.as_fragmented_byte_slice();
        assert!(empty.is_empty());
    }
}
