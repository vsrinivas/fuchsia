// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate contains facilities to interact with netsvc over the
//! network.

pub mod debuglog;
pub mod netboot;

/// A witness type for a valid string backed by a [`zerocopy::ByteSlice`].
struct ValidStr<B>(B);

/// Helper to convince the compiler we're holding buffer views.
fn as_buffer_view_mut<'a, B: packet::BufferViewMut<&'a mut [u8]>>(
    v: B,
) -> impl packet::BufferViewMut<&'a mut [u8]> {
    v
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
        let split = bytes
            .as_ref()
            .iter()
            .enumerate()
            .find_map(|(index, c)| if *c == 0 { Some(index) } else { None })
            .unwrap_or(bytes.as_ref().len());
        let (bytes, rest) = bytes.split_at(split);
        (Self(bytes), rest)
    }

    fn as_str(&self) -> &str {
        // safety: ValidStr is a witness type for a valid UTF8 string that
        // keeps the byte slice reference
        unsafe { std::str::from_utf8_unchecked(self.0.as_ref()) }
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

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

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
}
