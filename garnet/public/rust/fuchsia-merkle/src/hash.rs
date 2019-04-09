// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate hex;

use crate::util::HASH_SIZE;
use failure::{format_err, Error};
use std::fmt;
use std::str;

/// A SHA-256 hash.
#[derive(Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash, Debug)]
pub struct Hash([u8; HASH_SIZE]);

impl Hash {
    /// Obtain a slice of the bytes representing the hash.
    pub fn as_bytes(&self) -> &[u8] {
        &self.0[..]
    }
}

impl str::FromStr for Hash {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let bytes = hex::decode(s)?;
        if bytes.len() != HASH_SIZE {
            return Err(format_err!("expected {} hex bytes, got {}", HASH_SIZE, bytes.len()));
        }
        let mut res: [u8; HASH_SIZE] = [0; HASH_SIZE];
        res.copy_from_slice(&bytes[..]);
        Ok(Hash(res))
    }
}

impl From<[u8; HASH_SIZE]> for Hash {
    fn from(bytes: [u8; HASH_SIZE]) -> Self {
        Hash(bytes)
    }
}

impl fmt::Display for Hash {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(&hex::encode(self.0))
    }
}
