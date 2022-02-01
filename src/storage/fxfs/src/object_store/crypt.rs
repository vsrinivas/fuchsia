// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::trace_duration,
    aes::{cipher::generic_array::GenericArray, Aes256, NewBlockCipher},
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    rand::RngCore,
    xts_mode::{get_tweak_default, Xts128},
};

pub use crate::object_store::object_record::AES256XTSKeys;

const KEY_SIZE: usize = 256 / 8;

// The xts-mode crate expects a sector size. Fxfs will always use a block size >= 512 bytes, so we
// just assume a sector size of 512 bytes, which will work fine even if a different block size is
// used by Fxfs or the underlying device.
const SECTOR_SIZE: u64 = 512;

type KeyBytes = [u8; KEY_SIZE];

pub struct UnwrappedKey {
    id: u64,
    xts: Xts128<Aes256>,
}

/// The `Crypt` trait is used to wrap and unwrap `AES256XTSKeys` into `UnwrappedKeys`.
///
/// Unwrapped keys contain the actual keydata used to encrypt and decrypt content.
pub struct UnwrappedKeys {
    keys: Vec<UnwrappedKey>,
}

impl UnwrappedKeys {
    pub fn new(keys: impl IntoIterator<Item = (u64, KeyBytes)>) -> Self {
        Self {
            keys: keys
                .into_iter()
                .map(|(id, key)| UnwrappedKey { id, xts: Self::get_xts(&key) })
                .collect(),
        }
    }

    // Note: The "128" in `Xts128` refers to the cipher block size, not the key
    // size (and not the device sector size). AES-256, like all forms of AES,
    // have a 128-bit block size, and so will work with `Xts128`.
    fn get_xts(key: &[u8; 32]) -> Xts128<Aes256> {
        // The same key is used for for encrypting the data and computing the tweak.
        Xts128::<Aes256>::new(
            Aes256::new(GenericArray::from_slice(key)),
            Aes256::new(GenericArray::from_slice(key)),
        )
    }

    /// Decrypt the data in `buffer` using `UnwrappedKeys`.
    ///
    /// * `offset` is the byte offset within the file.
    /// * `key_id` specifies which of the unwrapped keys to use.
    /// * `buffer` is mutated in place.
    pub fn decrypt(&self, offset: u64, key_id: u64, buffer: &mut [u8]) -> Result<(), Error> {
        trace_duration!("decrypt");
        assert_eq!(offset % SECTOR_SIZE, 0);
        self.keys
            .iter()
            .find(|unwrapped| unwrapped.id == key_id)
            .ok_or(anyhow!("Key not found"))?
            .xts
            .decrypt_area(
                buffer,
                SECTOR_SIZE as usize,
                (offset / SECTOR_SIZE).into(),
                get_tweak_default,
            );
        Ok(())
    }

    /// Encrypts data in the `buffer` using `UnwrappedKeys`.
    ///
    /// * `offset` is the byte offset within the file.
    /// * `key_id` specifies which of the unwrapped keys to use.
    /// * `buffer` is mutated in place.
    pub fn encrypt(&self, offset: u64, key_id: u64, buffer: &mut [u8]) -> Result<(), Error> {
        trace_duration!("encrypt");
        assert_eq!(offset % SECTOR_SIZE, 0);
        self.keys
            .iter()
            .find(|unwrapped| unwrapped.id == key_id)
            .ok_or(anyhow!("Key not found"))?
            .xts
            .encrypt_area(
                buffer,
                SECTOR_SIZE as usize,
                (offset / SECTOR_SIZE).into(),
                get_tweak_default,
            );
        Ok(())
    }
}

// TODO(csuter): Implement
pub struct StreamCipher(u64);

impl StreamCipher {
    pub fn new(offset: u64) -> Self {
        Self(offset)
    }

    pub fn encrypt(&mut self, buffer: &mut [u8]) {
        trace_duration!("StreamCipher::encrypt");
        // TODO(csuter): Change this to use ChaCha20.
        for b in buffer {
            *b ^= 0xa7 ^ self.0 as u8;
            self.0 += 1;
        }
    }

    pub fn decrypt(&mut self, buffer: &mut [u8]) {
        trace_duration!("StreamCipher::decrypt");
        for b in buffer {
            *b ^= 0xa7 ^ self.0 as u8;
            self.0 += 1;
        }
    }

    pub fn offset(&self) -> u64 {
        self.0
    }
}

/// An interface trait with the ability to wrap and unwrap AES256XTS formatted encryption keys.
///
/// Note that existence of this trait does not imply that an object will **securely**
/// wrap and unwrap keys; rather just that it presents an interface for wrapping operations.
#[async_trait]
pub trait Crypt: Send + Sync {
    /// `owner` is intended to be used such that when the key is wrapped, it appears to be different
    /// to that of the same key wrapped by a different owner.  In this way, keys can be shared
    /// amongst different filesystem objects (e.g. for clones), but it is not possible to tell just
    /// by looking at the wrapped keys.
    async fn create_key(
        &self,
        wrapping_key_id: u64,
        owner: u64,
    ) -> Result<(AES256XTSKeys, UnwrappedKeys), Error>;

    /// Unwraps the keys and stores the result in UnwrappedKeys.
    async fn unwrap_keys(&self, keys: &AES256XTSKeys, owner: u64) -> Result<UnwrappedKeys, Error>;
}

/// This struct provides the `Crypt` trait without any strong security.
///
/// It is intended for use only in test code where actual security is inconsequential.
pub struct InsecureCrypt {}

/// Used by `InsecureCrypt` as an extremely weak form of 'encryption'.
const WRAP_XOR: [u8; 8] = [0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef];

impl InsecureCrypt {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Crypt for InsecureCrypt {
    async fn create_key(
        &self,
        wrapping_key_id: u64,
        owner: u64,
    ) -> Result<(AES256XTSKeys, UnwrappedKeys), Error> {
        assert_eq!(wrapping_key_id, 0);
        let mut rng = rand::thread_rng();
        let mut key: KeyBytes = [0; KEY_SIZE];
        rng.fill_bytes(&mut key);
        let mut wrapped: KeyBytes = [0; KEY_SIZE];
        let owner_bytes = owner.to_le_bytes();
        for i in 0..wrapped.len() {
            let j = i % WRAP_XOR.len();
            wrapped[i] = key[i] ^ WRAP_XOR[j] ^ owner_bytes[j];
        }
        Ok((
            AES256XTSKeys { wrapping_key_id, keys: vec![(0, wrapped)] },
            UnwrappedKeys::new([(0, key)]),
        ))
    }

    /// Unwraps the keys and stores the result in UnwrappedKeys.
    async fn unwrap_keys(&self, keys: &AES256XTSKeys, owner: u64) -> Result<UnwrappedKeys, Error> {
        Ok(UnwrappedKeys::new(keys.keys.iter().map(|(id, key)| {
            let mut unwrapped: KeyBytes = [0; KEY_SIZE];
            let owner_bytes = owner.to_le_bytes();
            for i in 0..unwrapped.len() {
                let j = i % WRAP_XOR.len();
                unwrapped[i] = key[i] ^ WRAP_XOR[j] ^ owner_bytes[j];
            }
            (*id, unwrapped)
        })))
    }
}
