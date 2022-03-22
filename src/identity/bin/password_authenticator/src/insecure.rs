// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains constants and implementations meant only for the prototype stage of
//! this program. These are placed together in a single module to make it easy to remember
//! which implementations need to be re-written with production versions and removed.

use {
    crate::keys::{EnrolledKey, Key, KeyEnrollment, KeyError, KeyRetrieval},
    async_trait::async_trait,
};

/// The hardcoded password for the singleton account.
/// This will be replaced in a future milestone with a proper password-authenticating scheme.
pub const INSECURE_EMPTY_PASSWORD: &'static str = "";

/// The hardcoded 256 bit key used to format and unseal zxcrypt volumes.
/// This will be replaced in a future milestone with a proper password-based key-derivation scheme.
pub const INSECURE_EMPTY_KEY: [u8; 32] = [0; 32];

/// A test/mock key derivation that always returns a 256 bit null key of zeroes.
pub struct NullKeySource;

#[derive(Debug, Eq, PartialEq)]
pub struct NullKeyParams;

#[async_trait]
impl KeyEnrollment<NullKeyParams> for NullKeySource {
    async fn enroll_key(&self, password: &str) -> Result<EnrolledKey<NullKeyParams>, KeyError> {
        if password == INSECURE_EMPTY_PASSWORD {
            Ok(EnrolledKey { key: INSECURE_EMPTY_KEY.clone(), enrollment_data: NullKeyParams {} })
        } else {
            Err(KeyError::PasswordError)
        }
    }
}

#[async_trait]
impl KeyRetrieval for NullKeySource {
    async fn retrieve_key(&self, password: &str) -> Result<Key, KeyError> {
        if password == INSECURE_EMPTY_PASSWORD {
            Ok(INSECURE_EMPTY_KEY.clone())
        } else {
            Err(KeyError::PasswordError)
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, assert_matches::assert_matches};

    #[fuchsia::test]
    async fn test_enroll_key() {
        let ks = NullKeySource;

        // Enrolling the empty password should succeed and yield an empty enrollment_data
        // and the null key.
        let enrolled_key = ks.enroll_key("").await.expect("enroll null password");
        assert_eq!(enrolled_key.key, INSECURE_EMPTY_KEY);
        assert_eq!(enrolled_key.enrollment_data, NullKeyParams {});

        // Enrolling any non-empty password should fail.
        assert_matches!(ks.enroll_key("nonempty").await, Err(KeyError::PasswordError));
    }

    #[fuchsia::test]
    async fn test_retrieve_key() {
        let ks = NullKeySource;
        // Retrieving a key for the null password should yield the null key.
        let res = ks.retrieve_key("").await;
        assert_matches!(res, Ok(INSECURE_EMPTY_KEY));
        // Retrieving any non-empty password should fail.
        assert_matches!(ks.retrieve_key("nonempty").await, Err(KeyError::PasswordError));
    }
}
