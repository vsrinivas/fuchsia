// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use hex::{FromHex, FromHexError, ToHex};
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use std::fmt;
use std::str;
use thiserror::Error;

mod iter;
pub use iter::*;

/// The size of a hash in bytes.
pub const HASH_SIZE: usize = 32;

/// A SHA-256 hash.
#[derive(Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct Hash([u8; HASH_SIZE]);

impl Hash {
    /// Obtain a slice of the bytes representing the hash.
    pub fn as_bytes(&self) -> &[u8] {
        &self.0[..]
    }
}

impl str::FromStr for Hash {
    type Err = ParseHashError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(Self(FromHex::from_hex(s)?))
    }
}

impl<'de> Deserialize<'de> for Hash {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        str::FromStr::from_str(&s).map_err(serde::de::Error::custom)
    }
}

impl From<[u8; HASH_SIZE]> for Hash {
    fn from(bytes: [u8; HASH_SIZE]) -> Self {
        Hash(bytes)
    }
}

impl From<Hash> for [u8; HASH_SIZE] {
    fn from(hash: Hash) -> Self {
        hash.0
    }
}

impl fmt::Display for Hash {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.write_hex(f)
    }
}

impl Serialize for Hash {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_str(&self.to_string())
    }
}

impl fmt::Debug for Hash {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("Hash").field(&self.to_string()).finish()
    }
}

/// An error encountered while parsing a [`Hash`].
#[derive(Copy, Clone, Debug, Error, PartialEq)]
pub struct ParseHashError(FromHexError);

impl fmt::Display for ParseHashError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.0 {
            FromHexError::InvalidStringLength => {
                write!(f, "{}, expected {} hex encoded bytes", self.0, HASH_SIZE)
            }
            _ => write!(f, "{}", self.0),
        }
    }
}

impl From<FromHexError> for ParseHashError {
    fn from(e: FromHexError) -> Self {
        ParseHashError(e)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;
    use std::str::FromStr;

    proptest! {
        #[test]
        fn test_from_str_display(ref s in "[[:xdigit:]]{64}") {
            let hash = Hash::from_str(s).unwrap();
            let display = format!("{}", hash);
            prop_assert_eq!(s.to_ascii_lowercase(), display);
        }

        #[test]
        fn test_rejects_odd_length_strings(ref s in "[[:xdigit:]][[:xdigit:]]{2}{0,128}") {
            prop_assert_eq!(Err(FromHexError::OddLength.into()), Hash::from_str(s));
        }

        #[test]
        fn test_rejects_incorrect_byte_count(ref s in "[[:xdigit:]]{2}{0,128}") {
            prop_assume!(s.len() != HASH_SIZE * 2);
            prop_assert_eq!(Err(FromHexError::InvalidStringLength.into()), Hash::from_str(s));
        }
    }
}
