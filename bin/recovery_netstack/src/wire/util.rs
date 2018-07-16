// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use self::buffer::*;
pub use self::checksum::*;
pub use self::options::*;

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
    use byteorder::{ByteOrder, NetworkEndian};

    // TODO(joshlf):
    // - Speed this up by only doing the endianness swap at the end as described
    //   in RFC 1071 Section 2(B).
    // - Explore SIMD optimizations

    /// A checksum used by IPv4, TCP, and UDP.
    ///
    /// This checksum operates by computing the 1s complement of the 1s
    /// complement sum of successive 16-bit words of the input. It is specified
    /// in [RFC 1071].
    ///
    /// [RFC 1071]: https://tools.ietf.org/html/rfc1071
    pub struct Checksum {
        sum: u32,
        // since odd-length inputs are treated specially, we store the trailing
        // byte for use in future calls to add_bytes(), and only treat it as a
        // true trailing byte in checksum()
        trailing_byte: Option<u8>,
    }

    impl Checksum {
        /// Initialize a new checksum.
        pub fn new() -> Self {
            Checksum {
                sum: 0,
                trailing_byte: None,
            }
        }

        /// Add bytes to the checksum.
        ///
        /// If `bytes` does not contain an even number of bytes, a single zero byte
        /// will be added to the end before updating the checksum.
        pub fn add_bytes(&mut self, mut bytes: &[u8]) {
            // if there's a trailing byte, consume it first
            if let Some(byte) = self.trailing_byte {
                if !bytes.is_empty() {
                    Self::add_u16(&mut self.sum, NetworkEndian::read_u16(&[byte, bytes[0]]));
                    bytes = &bytes[1..];
                    self.trailing_byte = None;
                }
            }
            // continue with the normal algorithm
            while bytes.len() > 1 {
                Self::add_u16(&mut self.sum, NetworkEndian::read_u16(bytes));
                bytes = &bytes[2..];
            }
            if bytes.len() == 1 {
                self.trailing_byte = Some(bytes[0]);
            }
        }

        /// Update bytes in an existing checksum.
        ///
        /// `update` updates a checksum to reflect that the already-checksummed
        /// bytes `old` have been updated to contain the values in `new`. It
        /// implements the algorithm described in Equation 3 in [RFC 1624]. The
        /// first byte must be at an even number offset in the original input.
        /// If an odd number offset byte needs to be updated, the caller should
        /// simply include the preceding byte as well. If an odd number of bytes
        /// is given, it is assumed that these are the last bytes of the input.
        /// If an odd number of bytes in the middle of the input needs to be
        /// updated, the next byte of the input should be added on the end to
        /// make an even number of bytes.
        ///
        /// # Panics
        ///
        /// `update` panics if `old.len() != new.len()`.
        ///
        /// [RFC 1624]: https://tools.ietf.org/html/rfc1624
        pub fn update(checksum: u16, mut old: &[u8], mut new: &[u8]) -> u16 {
            assert_eq!(old.len(), new.len());

            // We compute on the sum, not the one's complement of the sum.
            // checksum is the one's complement of the sum, so we need to get
            // back to the sum. Thus, we negate checksum.
            let mut sum = u32::from(!checksum);
            while old.len() > 1 {
                let old_u16 = NetworkEndian::read_u16(old);
                let new_u16 = NetworkEndian::read_u16(new);
                // RFC 1624 Eqn. 3
                Self::add_u16(&mut sum, !old_u16);
                Self::add_u16(&mut sum, new_u16);
                old = &old[2..];
                new = &new[2..];
            }
            if old.len() == 1 {
                let old_u16 = NetworkEndian::read_u16(&[old[0], 0]);
                let new_u16 = NetworkEndian::read_u16(&[new[0], 0]);
                // RFC 1624 Eqn. 3
                Self::add_u16(&mut sum, !old_u16);
                Self::add_u16(&mut sum, new_u16);
            }
            !Self::normalize(sum)
        }

        /// Compute the checksum.
        ///
        /// `checksum` returns the checksum of all data added using `add_bytes`
        /// so far. Calling `checksum` does *not* reset the checksum. More bytes
        /// may be added after calling `checksum`, and they will be added to the
        /// checksum as expected.
        ///
        /// If an odd number of bytes have been added so far, the checksum will
        /// be computed as though a single 0 byte had been added at the end in
        /// order to even out the length of the input.
        pub fn checksum(&self) -> u16 {
            let mut sum = self.sum;
            if let Some(byte) = self.trailing_byte {
                Self::add_u16(&mut sum, NetworkEndian::read_u16(&[byte, 0]));
            }
            !Self::normalize(sum)
        }

        // Normalize a 32-bit accumulator by mopping up the overflow until it
        // fits in a u16.
        fn normalize(mut sum: u32) -> u16 {
            while (sum >> 16) != 0 {
                sum = (sum >> 16) + (sum & 0xFFFF);
            }
            sum as u16
        }

        // Add a new u16 to a running sum, checking for overflow. If overflow
        // is detected, normalize back to a 16-bit representation and perform
        // the addition again.
        fn add_u16(sum: &mut u32, u: u16) {
            let new = if let Some(new) = sum.checked_add(u32::from(u)) {
                new
            } else {
                let tmp = *sum;
                *sum = u32::from(Self::normalize(tmp));
                // sum is now in the range [0, 2^16), so this can't overflow
                *sum + u32::from(u)
            };
            *sum = new;
        }
    }

    #[cfg(test)]
    mod tests {
        use rand::Rng;

        use super::*;
        use testutil::new_rng;
        use wire::testdata::IPV4_HEADERS;

        #[test]
        fn test_checksum() {
            for buf in IPV4_HEADERS {
                // compute the checksum as normal
                let mut c = Checksum::new();
                c.add_bytes(&buf);
                assert_eq!(c.checksum(), 0);
                // compute the checksum one byte at a time to make sure our
                // trailing_byte logic works
                let mut c = Checksum::new();
                for byte in *buf {
                    c.add_bytes(&[*byte]);
                }
                assert_eq!(c.checksum(), 0);

                // Make sure that it works even if we overflow u32. Performing this
                // loop 2 * 2^16 times is guaranteed to cause such an overflow
                // because 0xFFFF + 0xFFFF > 2^16, and we're effectively adding
                // (0xFFFF + 0xFFFF) 2^16 times. We verify the overflow as well by
                // making sure that, at least once, the sum gets smaller from one
                // loop iteration to the next.
                let mut c = Checksum::new();
                c.add_bytes(&[0xFF, 0xFF]);
                let mut prev_sum = c.sum;
                let mut overflowed = false;
                for _ in 0..((2 * (1 << 16)) - 1) {
                    c.add_bytes(&[0xFF, 0xFF]);
                    if c.sum < prev_sum {
                        overflowed = true;
                    }
                    prev_sum = c.sum;
                }
                assert!(overflowed);
                assert_eq!(c.checksum(), 0);
            }
        }

        #[test]
        fn test_update() {
            for b in IPV4_HEADERS {
                let mut buf = Vec::new();
                buf.extend_from_slice(b);

                let mut c = Checksum::new();
                c.add_bytes(&buf);
                assert_eq!(c.checksum(), 0);

                // replace the destination IP with the loopback address
                let old = [buf[16], buf[17], buf[18], buf[19]];
                (&mut buf[16..20]).copy_from_slice(&[127, 0, 0, 1]);
                let updated = Checksum::update(c.checksum(), &old, &[127, 0, 0, 1]);
                let from_scratch = {
                    let mut c = Checksum::new();
                    c.add_bytes(&buf);
                    c.checksum()
                };
                assert_eq!(updated, from_scratch);
            }
        }

        #[test]
        fn test_smoke_update() {
            let mut rng = new_rng(70812476915813);

            for _ in 0..2048 {
                // use an odd length so we test the odd length logic
                const BUF_LEN: usize = 31;
                let buf: [u8; BUF_LEN] = rng.gen();
                let mut c = Checksum::new();
                c.add_bytes(&buf);

                let (begin, end) = loop {
                    let begin = rng.gen::<usize>() % BUF_LEN;
                    let end = begin + (rng.gen::<usize>() % (BUF_LEN + 1 - begin));
                    // update requires that begin is even and end is either even
                    // or the end of the input
                    if begin % 2 == 0 && (end % 2 == 0 || end == BUF_LEN) {
                        break (begin, end);
                    }
                };

                let mut new_buf = buf;
                for i in begin..end {
                    new_buf[i] = rng.gen();
                }
                let updated =
                    Checksum::update(c.checksum(), &buf[begin..end], &new_buf[begin..end]);
                let from_scratch = {
                    let mut c = Checksum::new();
                    c.add_bytes(&new_buf);
                    c.checksum()
                };
                assert_eq!(updated, from_scratch);
            }
        }
    }
}

mod options {
    use std::marker::PhantomData;
    use std::ops::Deref;

    use zerocopy::ByteSlice;

    /// A parsed set of header options.
    ///
    /// `Options` represents a parsed set of options from a TCP or IPv4 header.
    #[derive(Debug)]
    pub struct Options<B, O> {
        bytes: B,
        _marker: PhantomData<O>,
    }

    /// An iterator over header options.
    ///
    /// `OptionIter` is an iterator over packet header options stored in the
    /// format used by IPv4 and TCP, where each option is either a single kind
    /// byte or a kind byte, a length byte, and length - 2 data bytes.
    ///
    /// In both IPv4 and TCP, the only single-byte options are End of Options
    /// List (EOL) and No Operation (NOP), both of which can be handled
    /// internally by OptionIter. Thus, the caller only needs to be able to
    /// parse multi-byte options.
    pub struct OptionIter<'a, O> {
        bytes: &'a [u8],
        idx: usize,
        _marker: PhantomData<O>,
    }

    /// Errors returned from parsing options.
    ///
    /// `OptionParseErr` is either `Internal`, which indicates that this module
    /// encountered a malformed sequence of options (likely with a length field
    /// larger than the remaining bytes in the options buffer), or `External`,
    /// which indicates that the `OptionImpl::parse` callback returned an error.
    #[derive(Debug, Eq, PartialEq)]
    pub enum OptionParseErr<E> {
        Internal,
        External(E),
    }

    /// An implementation of an options parser.
    ///
    /// `OptionImpl` provides functions to parse fixed- and variable-length
    /// options. It is required in order to construct an `Options` or
    /// `OptionIter`.
    pub trait OptionImpl {
        type Output;
        type Error;

        /// Parse an option.
        ///
        /// `parse` takes a kind byte and variable-length data associated and
        /// returns `Ok(Some(o))` if the option successfully parsed as `o`,
        /// `Ok(None)` if the kind byte was unrecognized, and `Err(err)` if the
        /// kind byte was recognized but `data` was malformed for that option
        /// kind. `parse` is allowed to not recognize certain option kinds, as
        /// the length field can still be used to safely skip over them.
        ///
        /// `parse` must be deterministic, or else `Options::parse` cannot
        /// guarantee that future iterations will not produce errors (and
        /// panic).
        fn parse(kind: u8, data: &[u8]) -> Result<Option<Self::Output>, Self::Error>;
    }

    impl<B, O> Options<B, O>
    where
        B: ByteSlice,
        O: OptionImpl,
    {
        /// Parse a set of options.
        ///
        /// `parse` parses `bytes` as a sequence of options. `parse` performs a
        /// single pass over all of the options to verify that they are
        /// well-formed. Once `parse` returns successfully, the resulting
        /// `Options` can be used to construct infallible iterators.
        pub fn parse(bytes: B) -> Result<Options<B, O>, OptionParseErr<O::Error>> {
            // First, do a single pass over the bytes to detect any errors up
            // front. Once this is done, since we have a reference to `bytes`,
            // these bytes can't change out from under us, and so we can treat
            // any iterator over these bytes as infallible. This makes a few
            // assumptions, but none of them are that big of a deal. In all
            // cases, breaking these assumptions would just result in a runtime
            // panic.
            // - B could return different bytes each time
            // - O::parse could be non-deterministic
            let mut idx = 0;
            while next::<B, O>(&bytes, &mut idx)?.is_some() {}
            Ok(Options {
                bytes,
                _marker: PhantomData,
            })
        }
    }

    impl<B: Deref<Target = [u8]>, O> Options<B, O> {
        /// Get the underlying bytes.
        ///
        /// `bytes` returns a reference to the byte slice backing this
        /// `Options`.
        pub fn bytes(&self) -> &[u8] {
            &self.bytes
        }
    }

    impl<'a, B, O> Options<B, O>
    where
        B: 'a + ByteSlice,
        O: OptionImpl,
    {
        /// Create an iterator over options.
        ///
        /// `iter` constructs an iterator over the options. Since the options
        /// were validated in `parse`, then so long as `from_kind` and
        /// `from_data` are deterministic, the iterator is infallible.
        pub fn iter(&'a self) -> OptionIter<'a, O> {
            OptionIter {
                bytes: &self.bytes,
                idx: 0,
                _marker: PhantomData,
            }
        }
    }

    impl<'a, O> Iterator for OptionIter<'a, O>
    where
        O: OptionImpl,
    {
        type Item = O::Output;

        fn next(&mut self) -> Option<O::Output> {
            // use match rather than expect because expect requires that Err: Debug
            #[cfg_attr(feature = "cargo-clippy", allow(match_wild_err_arm))]
            match next::<&'a [u8], O>(&self.bytes, &mut self.idx) {
                Ok(o) => o,
                Err(_) => panic!("already-validated options should not fail to parse"),
            }
        }
    }

    // End of Options List in both IPv4 and TCP
    const END_OF_OPTIONS: u8 = 0;
    // NOP in both IPv4 and TCP
    const NOP: u8 = 1;

    fn next<B, O>(bytes: &B, idx: &mut usize) -> Result<Option<O::Output>, OptionParseErr<O::Error>>
    where
        B: ByteSlice,
        O: OptionImpl,
    {
        // For an explanation of this format, see the "Options" section of
        // https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure
        loop {
            let bytes = &bytes[*idx..];
            if bytes.is_empty() {
                return Ok(None);
            }
            if bytes[0] == END_OF_OPTIONS {
                return Ok(None);
            }
            if bytes[0] == NOP {
                *idx += 1;
                continue;
            }
            let len = bytes[1] as usize;
            if len < 2 || len > bytes.len() {
                return Err(OptionParseErr::Internal);
            }
            *idx += len;
            match O::parse(bytes[0], &bytes[2..len]) {
                Ok(Some(o)) => return Ok(Some(o)),
                Ok(None) => {}
                Err(err) => return Err(OptionParseErr::External(err)),
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[derive(Debug)]
        struct DummyOptionImpl;

        impl OptionImpl for DummyOptionImpl {
            type Output = (u8, Vec<u8>);
            type Error = ();

            fn parse(kind: u8, data: &[u8]) -> Result<Option<Self::Output>, Self::Error> {
                let mut v = Vec::new();
                v.extend_from_slice(data);
                Ok(Some((kind, v)))
            }
        }

        #[derive(Debug)]
        struct AlwaysErrOptionImpl;

        impl OptionImpl for AlwaysErrOptionImpl {
            type Output = ();
            type Error = ();

            fn parse(_kind: u8, _data: &[u8]) -> Result<Option<()>, ()> {
                Err(())
            }
        }

        #[test]
        fn test_empty_options() {
            // all END_OF_OPTIONS
            let bytes = [END_OF_OPTIONS; 64];
            let options = Options::<_, DummyOptionImpl>::parse(&bytes[..]).unwrap();
            assert_eq!(options.iter().count(), 0);

            // all NOP
            let bytes = [NOP; 64];
            let options = Options::<_, DummyOptionImpl>::parse(&bytes[..]).unwrap();
            assert_eq!(options.iter().count(), 0);
        }

        #[test]
        fn test_parse() {
            // Construct byte sequences in the pattern [3, 2], [4, 3, 2], [5, 4,
            // 3, 2], etc. The second byte is the length byte, so these are all
            // valid options (with data [], [2], [3, 2], etc).
            let mut bytes = Vec::new();
            for i in 4..16 {
                // from the user's perspective, these NOPs should be transparent
                bytes.push(NOP);
                for j in (2..i).rev() {
                    bytes.push(j);
                }
                // from the user's perspective, these NOPs should be transparent
                bytes.push(NOP);
            }

            let options = Options::<_, DummyOptionImpl>::parse(bytes.as_slice()).unwrap();
            for (idx, (kind, data)) in options.iter().enumerate() {
                assert_eq!(kind as usize, idx + 3);
                assert_eq!(data.len(), idx);
                let mut bytes = Vec::new();
                for i in (2..(idx + 2)).rev() {
                    println!("{}", i);
                    bytes.push(i as u8);
                }
                assert_eq!(data, bytes);
            }

            // Test that we get no parse errors so long as
            // AlwaysErrOptionImpl::parse is never called.
            let bytes = [NOP; 64];
            let options = Options::<_, AlwaysErrOptionImpl>::parse(&bytes[..]).unwrap();
            assert_eq!(options.iter().count(), 0);
        }

        #[test]
        fn test_parse_err() {
            // the length byte is too short
            let bytes = [2, 1];
            assert_eq!(
                Options::<_, DummyOptionImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr::Internal
            );

            // the length byte is too long
            let bytes = [2, 3];
            assert_eq!(
                Options::<_, DummyOptionImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr::Internal
            );

            // the buffer is fine, but the implementation returns a parse error
            let bytes = [2, 2];
            assert_eq!(
                Options::<_, AlwaysErrOptionImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr::External(())
            );
        }
    }
}

mod buffer {
    use std::cmp;
    use std::convert::TryFrom;
    use std::ops::{Bound, Range, RangeBounds};

    // return the inclusive equivalent of the bound
    fn canonicalize_lower_bound(bound: Bound<&usize>) -> usize {
        match bound {
            Bound::Included(x) => *x,
            Bound::Excluded(x) => *x + 1,
            Bound::Unbounded => 0,
        }
    }

    // return the exclusive equivalent of the bound, verifying that it is in
    // range of len
    fn canonicalize_upper_bound(len: usize, bound: Bound<&usize>) -> Option<usize> {
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

    // return the inclusive-exclusive equivalent of the bound, verifying that it
    // is in range of len, and panicking if it is not or if the range is
    // nonsensical
    fn canonicalize_range_infallible<R: RangeBounds<usize>>(len: usize, range: &R) -> Range<usize> {
        let lower = canonicalize_lower_bound(range.start_bound());
        let upper = canonicalize_upper_bound(len, range.end_bound()).expect("range out of bounds");
        assert!(lower <= upper, "invalid range");
        lower..upper
    }

    /// A buffer and a range into that buffer.
    ///
    /// A `BufferAndRange` stores a pair of a buffer and a range which
    /// represents a subset of the buffer. It implements `AsRef<[u8]>` and
    /// `AsMut<[u8]>` for the range of the buffer.
    ///
    /// `BufferAndRange` is useful for passing nested payloads up the stack
    /// while still maintaining access to the entire buffer in case it is needed
    /// again in the future, such as to serialize new packets.
    pub struct BufferAndRange<B> {
        buffer: B,
        range: Range<usize>,
    }

    impl<B> BufferAndRange<B>
    where
        B: AsRef<[u8]>,
    {
        /// Construct a new `BufferAndRange`.
        ///
        /// # Panics
        ///
        /// `new` panics if `range` is out of bounds of `buffer` or is
        /// nonsensical (i.e., the upper bound precedes the lower bound).
        pub fn new<R: RangeBounds<usize>>(buffer: B, range: R) -> BufferAndRange<B> {
            let len = buffer.as_ref().len();
            BufferAndRange {
                buffer,
                range: canonicalize_range_infallible(len, &range),
            }
        }

        /// Extend the end of the range forwards towards the end of the buffer.
        ///
        /// `extend_forwards` adds `bytes` to the end index of the buffer's
        /// range, resulting in the range being `bytes` bytes closer to the end
        /// of the buffer than it was before.
        ///
        /// # Panics
        ///
        /// `extend_forwards` panics if there are fewer than `bytes` bytes
        /// following the existing range.
        pub fn extend_forwards(&mut self, bytes: usize) {
            assert!(
                bytes <= self.buffer.as_ref().len() - self.range.end,
                "cannot extend range with {} following bytes forwards by {} bytes",
                self.buffer.as_ref().len() - self.range.end,
                bytes
            );
            self.range.end += bytes;
        }
    }

    impl<B> BufferAndRange<B> {
        /// Shrink the buffer range.
        ///
        /// `slice` shrinks the buffer's range to be equal to the provided
        /// range. It interprets `range` as relative to the current range. For
        /// example, if, in a 10-byte buffer, the current range is `[2, 8)`, and
        /// the `range` argument is `[2, 6)`, then after `slice` returns, the
        /// beginning of the buffer's range will be `2 + 2 = 4`. Since `range`
        /// has a length of 4, the end of the buffer's range will be `4 + 4 =
        /// 8`.
        ///
        /// # Examples
        ///
        /// ```rust
        /// let buf = [0; 10];
        /// let mut buf = BufferAndRange::new(&buf, 2..8);
        /// assert_eq!(buf.as_ref().len(), 6);
        /// buf.slice(2..6);
        /// assert_eq!(buf.as_ref().len(), 4);
        /// ```
        ///
        /// # Panics
        ///
        /// `slice` panics if `range` is out of bounds for the existing buffer
        /// range, or if it nonsensical (i.e., the upper bound precedes the
        /// lower bound).
        pub fn slice<R: RangeBounds<usize>>(&mut self, range: R) {
            let cur_range_len = self.range.end - self.range.start;
            let range = canonicalize_range_infallible(cur_range_len, &range);
            self.range = translate_range(range, isize::try_from(self.range.start).unwrap());
        }

        /// Extend the beginning of the range backwards towards the beginning of
        /// the buffer.
        ///
        /// `extend_backwards` subtracts `bytes` from the beginning index of the
        /// buffer's range, resulting in the range being `bytes` bytes closer to
        /// the beginning of the buffer than it was before.
        ///
        /// # Panics
        ///
        /// `extend_backwards` panics if there are fewer than `bytes` bytes
        /// preceding the existing range.
        pub fn extend_backwards(&mut self, bytes: usize) {
            assert!(
                bytes <= self.range.start,
                "cannot extend range starting at {} backwards by {} bytes",
                self.range.start,
                bytes
            );
            self.range.start -= bytes;
        }

        /// Get the range.
        pub fn range(&self) -> Range<usize> {
            self.range.clone()
        }

        /// Consume the `BufferAndRange` and return the contained buffer and range.
        pub fn into_buffer_and_range(self) -> (B, Range<usize>) {
            let BufferAndRange { buffer, range } = self;
            (buffer, range)
        }
    }

    impl<B> BufferAndRange<B>
    where
        B: AsMut<[u8]>,
    {
        /// Extract the prefix, range, and suffix from the buffer.
        ///
        /// `parts_mut` returns the region of the buffer preceding the range,
        /// the range itself, and the region of the buffer following the range.
        pub fn parts_mut(&mut self) -> (&mut [u8], &mut [u8], &mut [u8]) {
            let (prefix, rest) = (&mut self.buffer.as_mut()[..]).split_at_mut(self.range.start);
            let (mid, suffix) = rest.split_at_mut(self.range.end - self.range.start);
            (prefix, mid, suffix)
        }
    }

    impl<B> BufferAndRange<B>
    where
        B: AsRef<[u8]> + AsMut<[u8]>,
    {
        /// Extend the end of the range forwards towards the end of the buffer,
        /// zeroing the newly-included bytes.
        ///
        /// `extend_forwards_zero` adds `bytes` to the end index of the buffer's
        /// range, resulting in the range being `bytes` bytes closer to the end
        /// of the buffer than it was before. These new bytes are set to zero,
        /// which can be useful when extending a body to include padding which
        /// has not yet been zeroed.
        ///
        /// # Panics
        ///
        /// `extend_forwards_zero` panics if there are fewer than `bytes` bytes
        /// following the existing range.
        pub fn extend_forwards_zero(&mut self, bytes: usize) {
            self.extend_forwards(bytes);
            let slice = self.as_mut();
            let len = slice.len();
            zero(&mut slice[len - bytes..]);
        }
    }

    impl<B> AsRef<[u8]> for BufferAndRange<B>
    where
        B: AsRef<[u8]>,
    {
        fn as_ref(&self) -> &[u8] {
            &self.buffer.as_ref()[self.range.clone()]
        }
    }

    impl<B> AsMut<[u8]> for BufferAndRange<B>
    where
        B: AsMut<[u8]>,
    {
        fn as_mut(&mut self) -> &mut [u8] {
            &mut self.buffer.as_mut()[self.range.clone()]
        }
    }

    /// Create a `BufferAndRange` with a guranteed prefix and padding size.
    ///
    /// Given a buffer and a range, `ensure_prefix_padding` returns a
    /// `BufferAndRange` with  at least `prefix` bytes preceding the range and
    /// at least `range_plus_padding` bytes in the range plus any padding bytes
    /// following the range. If `buf` already has enough prefix and
    /// range/padding bytes, then the returned `BufferAndRange` simply wraps a
    /// reference to `buf`, and the range is left as is. Otherwise, a new buffer
    /// is allocated, the original range bytes are copied into the new buffer,
    /// and the range is adjusted so that it matches the location of the bytes
    /// in the new buffer.
    ///
    /// The "range plus padding" construction is useful when a packet format
    /// requires a minimum body length, and the upper layers of the stack which
    /// are producing the body to be encapsulated do not have enough bytes to
    /// meet that minimum. In that case, it is necessary to add extra padding
    /// bytes after the body in order to meet the minimum.
    pub fn ensure_prefix_padding<B: AsRef<[u8]>>(
        buffer: BufferAndRange<B>, prefix: usize, range_plus_padding: usize,
    ) -> BufferAndRange<RefOrOwned<B>> {
        let (buffer, range) = buffer.into_buffer_and_range();
        let range_len = range.end - range.start;
        // normalize to guarantee that range_plus_padding >= range_len
        let range_plus_padding = cmp::max(range_plus_padding, range_len);
        if prefix <= range.start && range_plus_padding <= buffer.as_ref().len() - range.start {
            // The constraints are already satisfied, so we don't have to
            // reallocate.
            BufferAndRange::new(
                RefOrOwned {
                    inner: RefOrOwnedInner::Ref(buffer),
                },
                range,
            )
        } else {
            // TODO(joshlf): There's a third case, in which the constraints
            // aren't satisfied, but the buffer is large enough to satisfy the
            // constraints. In that case, we can avoid reallocating by simply
            // moving the range within the existing buffer.

            // The constraints aren't satisfied, and the buffer isn't large
            // enough to satisfy the constraints, so we have to reallocate.
            let range_len = range.end - range.start;
            let total_len = prefix + range_plus_padding;
            let mut vec = Vec::with_capacity(total_len);
            vec.resize(total_len, 0);
            vec[prefix..prefix + range_len].copy_from_slice(slice(buffer.as_ref(), &range));
            // adjust the range given the new prefix length
            let offset = isize::try_from(prefix).unwrap() - isize::try_from(range.start).unwrap();
            let range = translate_range(range, offset);
            BufferAndRange::new(
                RefOrOwned {
                    inner: RefOrOwnedInner::Owned(vec),
                },
                range,
            )
        }
    }

    /// Either a reference or an owned allocated buffer.
    pub struct RefOrOwned<B> {
        inner: RefOrOwnedInner<B>,
    }

    enum RefOrOwnedInner<B> {
        Ref(B),
        Owned(Vec<u8>),
    }

    impl<B: AsRef<[u8]>> AsRef<[u8]> for RefOrOwned<B> {
        fn as_ref(&self) -> &[u8] {
            match self.inner {
                RefOrOwnedInner::Ref(ref r) => r.as_ref(),
                RefOrOwnedInner::Owned(ref v) => v.as_slice(),
            }
        }
    }

    impl<B: AsMut<[u8]>> AsMut<[u8]> for RefOrOwned<B> {
        fn as_mut(&mut self) -> &mut [u8] {
            match *(&mut self.inner) {
                RefOrOwnedInner::Ref(ref mut r) => r.as_mut(),
                RefOrOwnedInner::Owned(ref mut v) => v.as_mut_slice(),
            }
        }
    }

    /// Zero a slice.
    ///
    /// Set every element of `slice` to 0.
    fn zero(slice: &mut [u8]) {
        for s in slice.iter_mut() {
            *s = 0;
        }
    }

    /// Translate a `Range<usize>` left or right.
    ///
    /// Translate a `Range<usize>` by a fixed offset. This function is
    /// equivalent to the following code, except with overflow explicitly
    /// checked:
    ///
    /// ```rust
    /// Range {
    ///     start: ((range.start as isize) + offset) as usize,
    ///     end: ((range.end as isize) + offset) as usize,
    /// }
    /// ```
    ///
    /// # Panics
    ///
    /// `translate_range` panics if any addition overflows or any conversion
    /// between signed and unsigned types fails.
    fn translate_range(range: Range<usize>, offset: isize) -> Range<usize> {
        let start = isize::try_from(range.start).unwrap();
        let end = isize::try_from(range.end).unwrap();
        Range {
            start: usize::try_from(start.checked_add(offset).unwrap()).unwrap(),
            end: usize::try_from(end.checked_add(offset).unwrap()).unwrap(),
        }
    }

    /// Get an immutable slice from a range.
    ///
    /// This is a temporary replacement for the syntax `&slc[range]` until this
    /// [issue] is fixed.
    ///
    /// [issue]: https://github.com/rust-lang/rust/issues/35729#issuecomment-394200339
    fn slice<'a, T, R: RangeBounds<usize>>(slc: &'a [T], range: &R) -> &'a [T] {
        let len = slc.len();
        &slc[canonicalize_range_infallible(len, range)]
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn test_buffer_and_range_slice() {
            let mut buf = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
            let mut buf = BufferAndRange::new(&mut buf, ..);
            assert_eq!(buf.range(), 0..10);
            assert_eq!(buf.as_ref(), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
            assert_eq!(
                buf.parts_mut(),
                (
                    &mut [][..],
                    &mut [0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..],
                    &mut [][..]
                )
            );

            buf.slice(..);
            assert_eq!(buf.range(), 0..10);
            assert_eq!(buf.as_ref(), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
            assert_eq!(
                buf.parts_mut(),
                (
                    &mut [][..],
                    &mut [0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..],
                    &mut [][..]
                )
            );

            buf.slice(2..);
            assert_eq!(buf.range(), 2..10);
            assert_eq!(buf.as_ref(), [2, 3, 4, 5, 6, 7, 8, 9]);
            assert_eq!(
                buf.parts_mut(),
                (
                    &mut [0, 1][..],
                    &mut [2, 3, 4, 5, 6, 7, 8, 9][..],
                    &mut [][..]
                )
            );

            buf.slice(..8);
            assert_eq!(buf.range(), 2..10);
            assert_eq!(buf.as_ref(), [2, 3, 4, 5, 6, 7, 8, 9]);
            assert_eq!(
                buf.parts_mut(),
                (
                    &mut [0, 1][..],
                    &mut [2, 3, 4, 5, 6, 7, 8, 9][..],
                    &mut [][..]
                )
            );

            buf.slice(..6);
            assert_eq!(buf.range(), 2..8);
            assert_eq!(buf.as_ref(), [2, 3, 4, 5, 6, 7]);
            assert_eq!(
                buf.parts_mut(),
                (
                    &mut [0, 1][..],
                    &mut [2, 3, 4, 5, 6, 7][..],
                    &mut [8, 9][..]
                )
            );

            buf.slice(2..4);
            assert_eq!(buf.range(), 4..6);
            assert_eq!(buf.as_ref(), [4, 5]);
            assert_eq!(
                buf.parts_mut(),
                (
                    &mut [0, 1, 2, 3][..],
                    &mut [4, 5][..],
                    &mut [6, 7, 8, 9][..]
                )
            );

            let (buf, range) = buf.into_buffer_and_range();
            assert_eq!(buf, &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
            assert_eq!(range, 4..6);
        }

        #[test]
        fn test_buffer_and_range_extend_backwards() {
            let buf = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
            let mut buf = BufferAndRange::new(&buf, 2..8);
            assert_eq!(buf.range(), 2..8);
            assert_eq!(buf.as_ref(), [2, 3, 4, 5, 6, 7]);
            buf.extend_backwards(1);
            assert_eq!(buf.range(), 1..8);
            assert_eq!(buf.as_ref(), [1, 2, 3, 4, 5, 6, 7]);
            buf.extend_backwards(1);
            assert_eq!(buf.range(), 0..8);
            assert_eq!(buf.as_ref(), [0, 1, 2, 3, 4, 5, 6, 7]);
        }

        #[test]
        #[should_panic]
        fn test_buffer_and_range_extend_backwards_panics() {
            let buf = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
            let mut buf = BufferAndRange::new(&buf, 2..8);
            assert_eq!(buf.as_ref(), [2, 3, 4, 5, 6, 7]);
            buf.extend_backwards(1);
            assert_eq!(buf.as_ref(), [1, 2, 3, 4, 5, 6, 7]);
            buf.extend_backwards(2);
        }

        #[test]
        fn test_ensure_prefix_padding() {
            fn verify<B: AsRef<[u8]> + AsMut<[u8]>>(
                buffer: BufferAndRange<B>, prefix: usize, range_plus_padding: usize,
            ) {
                let range_len_old = {
                    let range = buffer.range();
                    range.end - range.start
                };
                let mut range_old = Vec::with_capacity(range_len_old);
                range_old.extend_from_slice(buffer.as_ref());

                let mut buffer = ensure_prefix_padding(buffer, prefix, range_plus_padding);
                let range_len_new = {
                    let range = buffer.range();
                    range.end - range.start
                };
                assert_eq!(range_len_old, range_len_new);
                let (pfx, range, suffix) = buffer.parts_mut();
                assert!(pfx.len() >= prefix);
                assert_eq!(range.len(), range_len_new);
                assert_eq!(range_old.as_slice(), range);
                assert!(range.len() + suffix.len() >= range_plus_padding);
            }

            // Test for every valid combination of buf_len, range_start,
            // range_end, prefix, and range_plus_padding within [0, 8).
            for buf_len in 0..8 {
                for range_start in 0..buf_len {
                    for range_end in range_start..buf_len {
                        for prefix in 0..8 {
                            for range_plus_padding in 0..8 {
                                let mut vec = Vec::with_capacity(buf_len);
                                vec.resize(buf_len, 0);
                                // Initialize the vector with values 0, 1, 2,
                                // ... so that we can check to make sure that
                                // the range bytes have been properly copied if
                                // the buffer is reallocated.
                                for i in 0..vec.len() {
                                    vec[i] = i as u8;
                                }
                                verify(
                                    BufferAndRange::new(vec.as_mut_slice(), range_start..range_end),
                                    prefix,
                                    range_plus_padding,
                                );
                            }
                        }
                    }
                }
            }
        }
    }
}
