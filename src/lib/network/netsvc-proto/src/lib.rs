// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate contains facilities to interact with netsvc over the
//! network.

pub mod debuglog;
pub mod netboot;
pub mod tftp;

use thiserror::Error;

/// A witness type for a valid string backed by a [`zerocopy::ByteSlice`].
struct ValidStr<B>(B);

/// Helper to convince the compiler we're holding buffer views.
fn as_buffer_view_mut<'a, B: packet::BufferViewMut<&'a mut [u8]>>(
    v: B,
) -> impl packet::BufferViewMut<&'a mut [u8]> {
    v
}

fn find_null_termination<B: zerocopy::ByteSlice>(b: &B) -> Option<usize> {
    b.as_ref().iter().enumerate().find_map(|(index, c)| (*c == 0).then(|| index))
}

#[derive(Debug, Eq, PartialEq, Clone, Error)]
pub enum ValidStrError {
    #[error("missing null termination")]
    NoNullTermination,
    #[error("failed to decode: {0}")]
    Encoding(std::str::Utf8Error),
}

impl<B> ValidStr<B>
where
    B: zerocopy::ByteSlice,
{
    /// Attempts to create a new `ValidStr` that wraps all the contents of
    /// `bytes`.
    fn new(bytes: B) -> Result<Self, std::str::Utf8Error> {
        // NB: map doesn't work here because of lifetimes.
        match std::str::from_utf8(bytes.as_ref()) {
            Ok(_) => Ok(Self(bytes)),
            Err(e) => Err(e),
        }
    }

    /// Splits this `ValidStr` into a valid string up to the first null
    /// character and the rest of the internal container if there is one.
    ///
    /// The returned `ValidStr` is guaranteed to not contain a null character,
    /// and the returned tail `ByteSlice` may either be a slice starting with a
    /// null character or an empty slice.
    fn truncate_null(self) -> (Self, B) {
        let Self(bytes) = self;
        let split = find_null_termination(&bytes).unwrap_or(bytes.as_ref().len());
        let (bytes, rest) = bytes.split_at(split);
        (Self(bytes), rest)
    }

    fn as_str(&self) -> &str {
        // safety: ValidStr is a witness type for a valid UTF8 string that
        // keeps the byte slice reference
        unsafe { std::str::from_utf8_unchecked(self.0.as_ref()) }
    }

    /// Attempts to create a new `ValidStr` from the provided `BufferView`,
    /// consuming the buffer until the first null termination character.
    ///
    /// The returned `ValidStr` will not contain the null character, but the
    /// null character will be consumed from `buffer`.
    ///
    /// Note that the bytes might be consumed from the buffer view even in case
    /// of errors.
    fn new_null_terminated_from_buffer<BV: packet::BufferView<B>>(
        buffer: &mut BV,
    ) -> Result<Self, ValidStrError> {
        let v = buffer.as_ref();
        let eos = find_null_termination(&v).ok_or(ValidStrError::NoNullTermination)?;
        // Unwrap is safe, we just found null termination above.
        let bytes = buffer.take_front(eos + 1).unwrap();
        let (bytes, null_char) = bytes.split_at(eos);
        // TODO(https://github.com/rust-lang/rust/issues/82775): Use
        // debug_assert_matches from std when available.
        debug_assert!(
            matches!(null_char.as_ref(), [0]),
            "bad null character value: {:?}",
            null_char.as_ref()
        );
        let _ = null_char;
        Self::new(bytes).map_err(ValidStrError::Encoding)
    }
}

impl<B> std::fmt::Debug for ValidStr<B>
where
    B: zerocopy::ByteSlice,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        self.as_str().fmt(f)
    }
}

impl<B> AsRef<str> for ValidStr<B>
where
    B: zerocopy::ByteSlice,
{
    fn as_ref(&self) -> &str {
        self.as_str()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;

    #[test]
    fn test_new_valid_str() {
        const VALID: &'static str = "some valid string";
        const INVALID: [u8; 2] = [0xc3, 0x28];
        assert_eq!(
            ValidStr::new(VALID.as_bytes()).expect("can create from valid string").as_str(),
            VALID
        );
        assert_matches!(ValidStr::new(&INVALID[..]), Err(_));
    }

    #[test]
    fn test_truncate_null() {
        const VALID: &'static str = "some valid string\x00 rest";
        let (trunc, rest) =
            ValidStr::new(VALID.as_bytes()).expect("can create from valid string").truncate_null();
        assert_eq!(trunc.as_str(), "some valid string");
        assert_eq!(rest, "\x00 rest".as_bytes());
    }

    #[test]
    fn test_get_from_bufer() {
        fn make_buffer(contents: &str) -> packet::Buf<&[u8]> {
            packet::Buf::new(contents.as_bytes(), ..)
        }
        fn get_from_buffer<'a>(
            mut bv: impl packet::BufferView<&'a [u8]>,
        ) -> (Result<ValidStr<&'a [u8]>, ValidStrError>, &'a str) {
            let valid_str = ValidStr::new_null_terminated_from_buffer(&mut bv);
            (valid_str, std::str::from_utf8(bv.into_rest()).unwrap())
        }

        let mut buffer = make_buffer("no null termination");
        let (valid_str, rest) = get_from_buffer(buffer.buffer_view());
        assert_matches!(valid_str, Err(ValidStrError::NoNullTermination));
        assert_eq!(rest, "no null termination");

        let mut buffer = make_buffer("null\x00termination");
        let (valid_str, rest) = get_from_buffer(buffer.buffer_view());
        let valid_str = valid_str.expect("can find termination");
        assert_matches!(valid_str.as_str(), "null");
        assert_eq!(rest, "termination");

        let mut buffer = make_buffer("");
        let (valid_str, rest) = get_from_buffer(buffer.buffer_view());
        assert_matches!(valid_str, Err(ValidStrError::NoNullTermination));
        assert_eq!(rest, "");
    }
}
