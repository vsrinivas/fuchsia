// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::{TryFrom, TryInto};

use crate::advertisement::bloom_filter;
use crate::error::Error;

/// Represents the 24-bit Model ID assigned to a Fast Pair device upon registration.
#[derive(Debug, Copy, Clone, PartialEq)]
pub struct ModelId(u32);

impl TryFrom<u32> for ModelId {
    type Error = Error;

    fn try_from(src: u32) -> Result<Self, Self::Error> {
        // u24::MAX
        if src > 0xffffff {
            return Err(Error::InvalidModelId(src));
        }

        Ok(Self(src))
    }
}

impl From<ModelId> for [u8; 3] {
    fn from(src: ModelId) -> [u8; 3] {
        let mut bytes = [0; 3];
        bytes[..3].copy_from_slice(&src.0.to_be_bytes()[1..]);
        bytes
    }
}

/// A key that allows the Provider to be recognized as belonging to a certain user account.
// TODO(fxbug.dev/97271): Define a full-fledged Account Key type.
#[derive(Debug, PartialEq)]
pub struct AccountKey([u8; 16]);

impl AccountKey {
    pub fn new(bytes: [u8; 16]) -> Self {
        Self(bytes)
    }

    pub fn as_bytes(&self) -> &[u8; 16] {
        &self.0
    }
}

/// The maximum number of Account Keys that can be managed Account Keys will be evicted in an
/// LRU manner as described in the GFPS specification.
/// See https://developers.google.com/nearby/fast-pair/specifications/configuration#AccountKeyList
/// for more details.
const MAX_ACCOUNT_KEYS: u8 = 10;

/// Manages the set of saved Account Keys.
/// By default, the maximum number of keys that will be managed is `MAX_ACCOUNT_KEYS`.
// TODO(fxbug.dev/97271): Define a full-fledged Account Key container that saves the keys to
// persistent storage.
pub struct AccountKeyList {
    /// The maximum number of keys that will be maintained.
    // TODO(fxbug.dev/97271): Use `capacity` to evict in an LRU manner.
    #[allow(unused)]
    capacity: u8,
    pub keys: Vec<AccountKey>,
}

impl AccountKeyList {
    pub fn new() -> Self {
        Self { capacity: MAX_ACCOUNT_KEYS, keys: Vec::with_capacity(MAX_ACCOUNT_KEYS.into()) }
    }

    #[cfg(test)]
    pub fn with_capacity_and_keys(capacity: u8, keys: Vec<AccountKey>) -> Self {
        Self { capacity, keys }
    }

    /// Returns the service data payload associated with the current set of Account Keys.
    pub fn service_data(&self) -> Result<Vec<u8>, Error> {
        if self.keys.is_empty() {
            return Ok(vec![0x0]);
        }

        let mut salt = [0; 1];
        fuchsia_zircon::cprng_draw(&mut salt[..]);
        self.service_data_internal(salt[0])
    }

    fn service_data_internal(&self, salt: u8) -> Result<Vec<u8>, Error> {
        let account_keys_bytes = bloom_filter(&self.keys, salt)?;

        let mut result = Vec::new();
        // First byte is 0bLLLLTTTT, where L = length of the account key list, T = Type (0b0000 to
        // show UI notification, 0b0010 to hide it). The maximum amount of account key data that can
        // be represented is 15 bytes (u4::MAX).
        let length: u8 = match account_keys_bytes.len().try_into() {
            Ok(len) if len <= 15 => len,
            _ => return Err(Error::internal("Account key data too large")),
        };
        // For now, we will always request to show the UI notification (TTTT = 0b0000).
        result.push(length << 4);

        // Next n bytes are the Bloom-filtered Account Key list.
        result.extend(account_keys_bytes);

        // The descriptor value associated with the Salt section of the LE advertisement payload.
        // Formatted as 0bLLLLTTTT, where L (Length) = 0b0001 and T (Type) = 0b0001. Both are fixed.
        const SALT_DESCRIPTOR: u8 = 0x11;
        result.push(SALT_DESCRIPTOR);

        // Final byte is the Salt value.
        result.push(salt);

        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;

    #[test]
    fn model_id_from_u32() {
        let normal_id = 0x1234;
        let id = ModelId::try_from(normal_id).expect("valid id");
        let id_bytes: [u8; 3] = id.into();
        assert_eq!(id_bytes, [0x00, 0x12, 0x34]);

        let zero_id = 0;
        let id = ModelId::try_from(zero_id).expect("valid id");
        let id_bytes: [u8; 3] = id.into();
        assert_eq!(id_bytes, [0x00, 0x00, 0x00]);

        let max_id = 0xffffff;
        let id = ModelId::try_from(max_id).expect("valid id");
        let id_bytes: [u8; 3] = id.into();
        assert_eq!(id_bytes, [0xff, 0xff, 0xff]);
    }

    #[test]
    fn invalid_model_id_conversion_is_error() {
        let invalid_id = 0x1ffabcd;
        assert_matches!(ModelId::try_from(invalid_id), Err(_));
    }

    #[test]
    fn empty_account_key_list_service_data() {
        let empty = AccountKeyList::new();
        let service_data = empty.service_data().expect("can build service data");
        let expected = [0x00];
        assert_eq!(service_data, expected);
    }

    #[test]
    fn oversized_service_data_is_error() {
        // Building an AccountKeyList of 11 elements will result in an oversized service data.
        // In the future, this test will be obsolete as the AccountKeyList will be bounded in its
        // construction.
        let keys = (0..11_u8).map(|i| AccountKey::new([i; 16])).collect();
        let oversized = AccountKeyList::with_capacity_and_keys(15, keys);

        let result = oversized.service_data();
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[test]
    fn account_key_list_service_data() {
        let example_key = AccountKey::new([1; 16]);
        let keys = AccountKeyList::with_capacity_and_keys(10, vec![example_key]);

        let salt = 0x14;
        // Because the service data is generated with a random salt value, we test the internal
        // method with a controlled salt value so that the test is deterministic.
        let service_data = keys.service_data_internal(salt).expect("can build service_data");
        let expected = [
            0x40, // Length = 4, Show UI indication
            0x04, 0x33, 0x00, 0x88, // Bloom filter applied to the Account key list
            0x11, 0x14, // Salt descriptor (0x11), Fixed salt value (0x14)
        ];

        assert_eq!(service_data, expected);
    }
}
