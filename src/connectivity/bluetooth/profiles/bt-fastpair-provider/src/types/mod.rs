// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use aes::cipher::generic_array::GenericArray;
use aes::{Aes128, BlockDecrypt, BlockEncrypt, NewBlockCipher};
use lru_cache::LruCache;
use std::convert::{TryFrom, TryInto};
use tracing::debug;

use crate::advertisement::bloom_filter;

mod error;
pub mod keys;
pub mod packets;

pub use error::Error;

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

/// A key used during the Fast Pair Pairing Procedure.
/// This key is a temporary value that lives for the lifetime of a procedure.
#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct SharedSecret([u8; 16]);

impl SharedSecret {
    pub fn new(bytes: [u8; 16]) -> Self {
        Self(bytes)
    }

    pub fn as_bytes(&self) -> &[u8; 16] {
        &self.0
    }

    /// Decrypts the provided `message` buffer with the AccountKey using AES-128.
    /// Returns the decrypted payload.
    pub fn decrypt(&self, message: &[u8; 16]) -> [u8; 16] {
        let cipher = Aes128::new(GenericArray::from_slice(self.as_bytes()));
        let mut block = GenericArray::clone_from_slice(message);
        cipher.decrypt_block(&mut block);
        block.into()
    }

    /// Encrypts the provided `message` buffer with the AccountKey using AES-128.
    /// Returns the encrypted payload.
    pub fn encrypt(&self, message: &[u8; 16]) -> [u8; 16] {
        let cipher = Aes128::new(GenericArray::from_slice(self.as_bytes()));
        let mut block = GenericArray::clone_from_slice(message);
        cipher.encrypt_block(&mut block);
        block.into()
    }
}

/// A long-lived key that allows the Provider to be recognized as belonging to a certain user
/// account.
#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct AccountKey(SharedSecret);

impl AccountKey {
    pub fn new(bytes: [u8; 16]) -> Self {
        Self(SharedSecret::new(bytes))
    }

    pub fn as_bytes(&self) -> &[u8; 16] {
        &self.0.as_bytes()
    }

    pub fn shared_secret(&self) -> &SharedSecret {
        &self.0
    }
}

impl From<&SharedSecret> for AccountKey {
    fn from(src: &SharedSecret) -> AccountKey {
        AccountKey(src.clone())
    }
}

/// The maximum number of Account Keys that can be managed Account Keys will be evicted in an
/// LRU manner as described in the GFPS specification.
/// See https://developers.google.com/nearby/fast-pair/specifications/configuration#AccountKeyList
/// for more details.
const MAX_ACCOUNT_KEYS: usize = 10;

/// Manages the set of saved Account Keys.
/// By default, the maximum number of keys that will be saved is `MAX_ACCOUNT_KEYS`. When full, the
/// `AccountKeyList` will evict the least recently used Account Key.
pub struct AccountKeyList {
    /// The set of saved Account Keys. Keys are evicted in an LRU manner. There is no cache value
    /// as we only care about maintaining the keys.
    keys: LruCache<AccountKey, ()>,
}

impl AccountKeyList {
    pub fn new() -> Self {
        Self { keys: LruCache::new(MAX_ACCOUNT_KEYS) }
    }

    /// Returns an Iterator over the saved Account Keys.
    /// Note: Access via Iterator does not modify LRU state.
    pub fn keys(&self) -> impl Iterator<Item = &AccountKey> + ExactSizeIterator {
        self.keys.iter().map(|(k, _)| k)
    }

    /// Marks the provided `key` as used in the LRU cache.
    /// Returns Error if the key does not exist in the cache.
    pub fn mark_used(&mut self, key: &AccountKey) -> Result<(), Error> {
        self.keys.get_mut(&key).map(|_| ()).ok_or(Error::internal("no key to mark as used"))
    }

    #[cfg(test)]
    pub fn with_capacity_and_keys(capacity: usize, keys: Vec<AccountKey>) -> Self {
        let mut cache = LruCache::new(capacity);
        keys.into_iter().for_each(|k| {
            let _ = cache.insert(k, ());
        });
        Self { keys: cache }
    }

    pub fn save(&mut self, key: AccountKey) {
        // If the key has already been saved, it will be updated in the LRU cache.
        if self.keys.insert(key, ()).is_some() {
            debug!("Account Key already saved");
        }

        // TODO(fxbug.dev/97271): Save set of keys to persistent storage to persist across reboots.
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
        let account_keys_bytes = bloom_filter(self.keys(), salt)?;

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

    /// Tests AES-128 encryption & decryption using an Account Key as the Secret Key.
    /// The contents of this test case are pulled from the GFPS specification.
    /// See https://developers.google.com/nearby/fast-pair/specifications/appendix/testcases#aes_encryption
    #[test]
    fn aes_128_encryption_roundtrip() {
        let message = [
            0xF3, 0x0F, 0x4E, 0x78, 0x6C, 0x59, 0xA7, 0xBB, 0xF3, 0x87, 0x3B, 0x5A, 0x49, 0xBA,
            0x97, 0xEA,
        ];
        let account_key = AccountKey::new([
            0xA0, 0xBA, 0xF0, 0xBB, 0x95, 0x1F, 0xF7, 0xB6, 0xCF, 0x5E, 0x3F, 0x45, 0x61, 0xC3,
            0x32, 0x1D,
        ]);

        let encrypted = account_key.shared_secret().encrypt(&message);
        let expected = [
            0xAC, 0x9A, 0x16, 0xF0, 0x95, 0x3A, 0x3F, 0x22, 0x3D, 0xD1, 0x0C, 0xF5, 0x36, 0xE0,
            0x9E, 0x9C,
        ];
        assert_eq!(encrypted, expected);

        let decrypted = account_key.shared_secret().decrypt(&encrypted);
        assert_eq!(decrypted, message);
    }

    #[test]
    fn account_key_lru_eviction() {
        let mut list = AccountKeyList::new();
        let max: u8 = MAX_ACCOUNT_KEYS as u8;

        for i in 1..max + 1 {
            let key = AccountKey::new([i; 16]);
            list.save(key.clone());
            assert_eq!(list.keys().len(), i as usize);
            assert!(list.keys.contains_key(&key));
        }
        // Adding a new key results in the eviction of the LRU key.
        assert_eq!(list.keys().len(), max as usize);
        let new_key = AccountKey::new([max + 1; 16]);
        list.save(new_key.clone());
        assert_eq!(list.keys().len(), max as usize);
        assert!(list.keys.contains_key(&new_key));
        // LRU Key is no longer stored.
        let first_key = AccountKey::new([1; 16]);
        assert!(!list.keys.contains_key(&first_key));

        // Marking a key as used should "refresh" the key's position. It is no longer the LRU key
        // that will be evicted.
        let account_key2 = AccountKey::new([2; 16]);
        assert_matches!(list.mark_used(&account_key2), Ok(_));
        // Inserting a new key at capacity will evict the LRU key (not `account_key2` anymore).
        let next_key = AccountKey::new([max + 2; 16]);
        list.save(next_key.clone());
        assert_eq!(list.keys().len(), max as usize);
        assert!(list.keys.contains_key(&next_key));
        assert!(list.keys.contains_key(&account_key2));
    }

    #[test]
    fn mark_used_nonexistent_key_is_error() {
        let mut list = AccountKeyList::new();
        let key = AccountKey::new([1; 16]);
        assert_matches!(list.mark_used(&key), Err(_));
    }
}
