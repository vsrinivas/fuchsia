// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(csuter): Make this secure.  For now these are just some stubs that need to be filled out
// with real implementations.

use {
    crate::trace_duration,
    anyhow::{anyhow, ensure, Error},
    async_trait::async_trait,
    byteorder::{ByteOrder, LittleEndian},
    rand::RngCore,
};

pub use crate::object_store::object_record::AES256XTSKeys;

/// The `Crypt` trait is used to wrap and unwrap `AES256XTSKeys` into `UnwrappedKeys`.
///
/// Unwrapped keys contain the actual keydata used to encrypt and decrypt content.
///
/// For now, the format used just makes it convenient for the simple XOR scheme we are
/// using, but going forward, this can take whatever form is suitable.
pub struct UnwrappedKeys {
    keys: Vec<(u64, [u64; 4])>,
}

impl UnwrappedKeys {
    pub fn new<'a, T: IntoIterator<Item = (u64, &'a [u8])>>(in_keys: T) -> Result<Self, Error> {
        // Convert key into 4 u64's to make encrypt/decrypt easy whilst we have the simple
        // implementation that we do.
        let mut keys = Vec::new();
        for (id, key) in in_keys.into_iter() {
            ensure!(key.len() == 32, "Unexpected key length!");
            let mut k = [0; 4];
            for (chunk, k) in key.chunks_exact(8).zip(k.iter_mut()) {
                *k = LittleEndian::read_u64(chunk);
            }
            keys.push((id, k));
        }
        Ok(Self { keys })
    }

    // Stub routine that just xors the data.
    fn xor(&self, mut offset: u64, buffer: &mut [u8], key: &[u64; 4]) {
        assert_eq!(buffer.len() % 16, 0);
        assert_eq!(offset % 8, 0);
        let mut i = (offset / 8 % 4) as usize;
        for chunk in buffer.chunks_exact_mut(8) {
            LittleEndian::write_u64(chunk, LittleEndian::read_u64(chunk) ^ key[i] ^ offset);
            i = (i + 1) & 3;
            offset += 8;
        }
    }

    /// Decrypt the data in `buffer` using `UnwrappedKeys`.
    ///
    /// * `offset` is the tweak.
    /// * `key_id` specifies which of the unwrapped keys to use.
    /// * `buffer` is mutated in-place.
    pub fn decrypt(&self, offset: u64, key_id: u64, buffer: &mut [u8]) -> Result<(), Error> {
        trace_duration!("decrypt");
        self.xor(
            offset,
            buffer,
            &self.keys.iter().find(|(id, _)| *id == key_id).ok_or(anyhow!("Key not found"))?.1,
        );
        Ok(())
    }

    /// Encrypts data in the `buffer` using `UnwrappedKeys`.
    ///
    /// * `offset` is the tweak.
    /// * `key_id` specifies which of the unwrapped keys to use.
    /// * `buffer` is mutated in-place.
    pub fn encrypt(&self, offset: u64, key_id: u64, buffer: &mut [u8]) -> Result<(), Error> {
        trace_duration!("encrypt");
        // For now, always use the first key.
        self.xor(
            offset,
            buffer,
            &self.keys.iter().find(|(id, _)| *id == key_id).ok_or(anyhow!("Key not found"))?.1,
        );
        Ok(())
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
const WRAP_XOR: u64 = 0x012345678abcdef;

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
        let mut key = [0; 32];
        rng.fill_bytes(&mut key);
        let mut unwrapped = [0; 4];
        let mut wrapped = [0; 32];
        for (i, chunk) in key.chunks_exact(8).enumerate() {
            let u = LittleEndian::read_u64(chunk);
            unwrapped[i] = u;
            LittleEndian::write_u64(&mut wrapped[i * 8..i * 8 + 8], u ^ WRAP_XOR ^ owner);
        }
        Ok((
            AES256XTSKeys { wrapping_key_id, keys: vec![(0, wrapped)] },
            UnwrappedKeys { keys: vec![(0, unwrapped)] },
        ))
    }

    /// Unwraps the keys and stores the result in UnwrappedKeys.
    async fn unwrap_keys(&self, keys: &AES256XTSKeys, owner: u64) -> Result<UnwrappedKeys, Error> {
        Ok(UnwrappedKeys {
            keys: keys
                .keys
                .iter()
                .map(|(id, key)| {
                    let mut unwrapped = [0; 4];
                    for (i, chunk) in key.chunks_exact(8).enumerate() {
                        unwrapped[i] = LittleEndian::read_u64(chunk) ^ WRAP_XOR ^ owner;
                    }
                    (*id, unwrapped)
                })
                .collect(),
        })
    }
}
