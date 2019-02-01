// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
        use crate::testutil::new_rng;
        use crate::wire::testdata::IPV4_HEADERS;

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

    /// An implementation of an options parser which can return errors.
    ///
    /// This is split from the `OptionImpl` trait so that the associated `Error`
    /// type does not depend on the lifetime parameter to `OptionImpl`.
    /// Lifetimes aside, `OptionImplError` is conceptually part of `OptionImpl`.
    pub trait OptionImplErr {
        type Error;
    }

    // End of Options List in both IPv4 and TCP
    const END_OF_OPTIONS: u8 = 0;

    // NOP in both IPv4 and TCP
    const NOP: u8 = 1;

    /// An implementation of an options parser.
    ///
    /// `OptionImpl` provides functions to parse fixed- and variable-length
    /// options. It is required in order to construct an `Options` or
    /// `OptionIter`.
    pub trait OptionImpl<'a>: OptionImplErr {
        /// The value to multiply read lengths by.
        ///
        /// By default, this value is 1, but some options (such as NDP) this
        /// may be different.
        const OPTION_LEN_MULTIPLIER: usize = 1;

        /// The End of options type (if one exists).
        const END_OF_OPTIONS: Option<u8> = Some(END_OF_OPTIONS);

        /// The No-op type (if one exists).
        const NOP: Option<u8> = Some(NOP);

        /// The type of an option; the output from the `parse` function.
        ///
        /// For long or variable-length data, the user is advised to make
        /// `Output` a reference into the bytes passed to `parse`. This is
        /// achievable because of the lifetime parameter to this trait.
        type Output;

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
        fn parse(kind: u8, data: &'a [u8]) -> Result<Option<Self::Output>, Self::Error>;
    }

    impl<B, O> Options<B, O>
    where
        B: ByteSlice,
        O: for<'a> OptionImpl<'a>,
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
            while next::<O>(bytes.deref(), &mut idx)?.is_some() {}
            Ok(Options {
                bytes,
                _marker: PhantomData,
            })
        }
    }

    impl<B: Deref<Target = [u8]>, O> Deref for Options<B, O> {
        type Target = [u8];

        fn deref(&self) -> &[u8] {
            &self.bytes
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
        O: OptionImpl<'a>,
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
        O: OptionImpl<'a>,
    {
        type Item = O::Output;

        fn next(&mut self) -> Option<O::Output> {
            // use match rather than expect because expect requires that Err: Debug
            #[cfg_attr(feature = "cargo-clippy", allow(match_wild_err_arm))]
            match next::<O>(&self.bytes[..], &mut self.idx) {
                Ok(o) => o,
                Err(_) => panic!("already-validated options should not fail to parse"),
            }
        }
    }

    fn next<'a, O>(
        bytes: &'a [u8], idx: &mut usize,
    ) -> Result<Option<O::Output>, OptionParseErr<O::Error>>
    where
        O: OptionImpl<'a>,
    {
        // For an explanation of this format, see the "Options" section of
        // https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure
        loop {
            let bytes = &bytes[*idx..];
            if bytes.is_empty() {
                return Ok(None);
            }
            if Some(bytes[0]) == O::END_OF_OPTIONS {
                return Ok(None);
            }
            if Some(bytes[0]) == O::NOP {
                *idx += 1;
                continue;
            }
            let len = bytes[1] as usize * O::OPTION_LEN_MULTIPLIER;
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

        impl OptionImplErr for DummyOptionImpl {
            type Error = ();
        }
        impl<'a> OptionImpl<'a> for DummyOptionImpl {
            type Output = (u8, Vec<u8>);

            fn parse(kind: u8, data: &'a [u8]) -> Result<Option<Self::Output>, Self::Error> {
                let mut v = Vec::new();
                v.extend_from_slice(data);
                Ok(Some((kind, v)))
            }
        }

        #[derive(Debug)]
        struct AlwaysErrOptionImpl;

        impl OptionImplErr for AlwaysErrOptionImpl {
            type Error = ();
        }
        impl<'a> OptionImpl<'a> for AlwaysErrOptionImpl {
            type Output = ();

            fn parse(_kind: u8, _data: &'a [u8]) -> Result<Option<()>, ()> {
                Err(())
            }
        }

        #[derive(Debug)]
        struct DummyNdpOptionImpl;

        impl OptionImplErr for DummyNdpOptionImpl {
            type Error = ();
        }

        impl<'a> OptionImpl<'a> for DummyNdpOptionImpl {
            type Output = (u8, Vec<u8>);

            const OPTION_LEN_MULTIPLIER: usize = 8;

            const END_OF_OPTIONS: Option<u8> = None;

            const NOP: Option<u8> = None;

            fn parse(kind: u8, data: &'a [u8]) -> Result<Option<Self::Output>, Self::Error> {
                let mut v = Vec::with_capacity(data.len());
                v.extend_from_slice(data);
                Ok(Some((kind, v)))
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
        fn test_parse_ndp_options() {
            let mut bytes = Vec::new();
            for i in 0..16 {
                bytes.push(i);
                // NDP uses len*8 for the actual length.
                bytes.push(i + 1);
                // Write remaining 6 bytes.
                for j in 2..((i + 1) * 8) {
                    bytes.push(j)
                }
            }

            let options = Options::<_, DummyNdpOptionImpl>::parse(bytes.as_slice()).unwrap();
            for (idx, (kind, data)) in options.iter().enumerate() {
                assert_eq!(kind as usize, idx);
                assert_eq!(data.len(), ((idx + 1) * 8) - 2);
                let mut bytes = Vec::new();
                for i in (2..((idx + 1) * 8)) {
                    bytes.push(i as u8);
                }
                assert_eq!(data, bytes);
            }
        }

        #[test]
        fn test_parse_err() {
            // the length byte is too short
            let bytes = [2, 1];
            assert_eq!(
                Options::<_, DummyOptionImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr::Internal
            );

            // the length byte is 0 (similar check to above, but worth
            // explicitly testing since this was a bug in the Linux kernel:
            // https://bugzilla.redhat.com/show_bug.cgi?id=1622404)
            let bytes = [2, 0];
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
