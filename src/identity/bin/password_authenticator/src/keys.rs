// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {async_trait::async_trait, fidl_fuchsia_identity_account as faccount, thiserror::Error};

#[derive(Debug, Error)]
pub enum KeyError {
    // TODO(zarvox): remove once NullKey support is removed
    // This is only needed for NullKeyDerivation -- once we no longer have a key derivation that
    // would otherwise ignore the password provided, we can simply handle all authentication
    // failures by letting the resulting derived-key simply not match what the partition will
    // require to be unsealed.
    #[error("Password did not meet precondition")]
    PasswordError,

    #[error("Failed to derive key from password")]
    KeyDerivationError,
}

/// A 256-bit key.
pub type Key = [u8; 32];

/// The `KeyDerivation` trait provides a mechanism for deriving a key from a password.
/// The returned key is suitable for use with a zxcrypt volume.

#[async_trait]
pub trait KeyDerivation {
    /// Derive a key from the given password. The returned key will be 256 bits long.
    async fn derive_key(&self, password: &str) -> Result<Key, KeyError>;
}

impl From<KeyError> for faccount::Error {
    fn from(e: KeyError) -> Self {
        match e {
            KeyError::PasswordError => faccount::Error::FailedAuthentication,
            KeyError::KeyDerivationError => faccount::Error::Internal,
        }
    }
}
