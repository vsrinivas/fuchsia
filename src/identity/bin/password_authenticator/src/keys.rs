// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {async_trait::async_trait, fidl_fuchsia_identity_account as faccount, thiserror::Error};

#[derive(Debug, Error)]
pub enum KeyError {
    // TODO(zarvox): remove once NullKey support is removed
    // This is only needed for NullKeySource -- once we no longer have a key derivation that
    // would otherwise ignore the password provided, we can simply handle all authentication
    // failures by letting the resulting derived-key simply not match what the partition will
    // require to be unsealed.
    #[error("Password did not meet precondition")]
    PasswordError,

    #[error("Failed to enroll key")]
    KeyEnrollmentError,

    #[error("Failed to retrieve key from password")]
    KeyRetrievalError,
}

/// The size, in bytes of a key.
pub const KEY_LEN: usize = 32;

/// A 256-bit key.
pub type Key = [u8; KEY_LEN];

#[derive(Debug)]
pub struct EnrolledKey<T> {
    pub key: Key,

    pub enrollment_data: T,
}

/// The `KeyEnrollment` trait provides a mechanism for generating a new key to be retrievable
/// when presented with the initially-given password.  This can include additional data specific
/// to the enrollment scheme.
#[async_trait]
pub trait KeyEnrollment<T> {
    /// Enroll a key using this key derivation scheme with the given password.
    async fn enroll_key(&self, password: &str) -> Result<EnrolledKey<T>, KeyError>;
}

/// The `KeyRetrieval` trait provides a mechanism for deriving a key from a password.
/// The returned key is suitable for use with a zxcrypt volume.
#[async_trait]
pub trait KeyRetrieval {
    /// Retrieve a key using this key derivation scheme with the given password.
    /// The returned key will be 256 bits long.
    async fn retrieve_key(&self, password: &str) -> Result<Key, KeyError>;
}

impl From<KeyError> for faccount::Error {
    fn from(e: KeyError) -> Self {
        match e {
            KeyError::PasswordError => faccount::Error::FailedAuthentication,
            KeyError::KeyRetrievalError => faccount::Error::Internal,
            KeyError::KeyEnrollmentError => faccount::Error::Internal,
        }
    }
}
