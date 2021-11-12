// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_identity_account as faccount, thiserror::Error};

#[derive(Debug, Error)]
#[error("failed to derive key from password")]
pub struct KeyError;

/// A 256-bit key.
pub type Key = [u8; 32];

/// The `KeyDerivation` trait provides a mechanism for deriving a key from a password.
/// The returned key is suitable for use with a zxcrypt volume.
pub trait KeyDerivation {
    /// Derive a key from the given password. The returned key will be 256 bits long.
    fn derive_key(&self, password: &str) -> Result<Key, KeyError>;
}

impl From<KeyError> for faccount::Error {
    fn from(_: KeyError) -> Self {
        faccount::Error::Unknown
    }
}
