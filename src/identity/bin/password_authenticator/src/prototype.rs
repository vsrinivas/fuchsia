// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains constants and implementations meant only for the prototype stage of
//! this program. These are placed together in a single module to make it easy to remember
//! which implementations need to be re-written with production versions.

use {
    crate::keys::{Key, KeyDerivation, KeyError},
    async_trait::async_trait,
};

/// The singleton account ID on the device.
/// For now, we only support a single account (as in the fuchsia.identity protocol).  The local
/// account, if it exists, will have a value of 1.
pub const GLOBAL_ACCOUNT_ID: u64 = 1;

/// The hardcoded password for the singleton account.
/// This will be replaced in a future milestone with a proper password-authenticating scheme.
pub const INSECURE_EMPTY_PASSWORD: &'static str = "";

/// The hardcoded 256 bit key used to format and unseal zxcrypt volumes.
/// This will be replaced in a future milestone with a proper password-based key-derivation scheme.
pub const INSECURE_EMPTY_KEY: [u8; 32] = [0; 32];

/// A test/mock key derivation that always returns a 256 bit null key of zeroes.
pub struct NullKeyDerivation;

#[async_trait]
impl KeyDerivation for NullKeyDerivation {
    async fn derive_key(&self, password: &str) -> Result<Key, KeyError> {
        if password == INSECURE_EMPTY_PASSWORD {
            Ok(INSECURE_EMPTY_KEY.clone())
        } else {
            Err(KeyError::PasswordError)
        }
    }
}
