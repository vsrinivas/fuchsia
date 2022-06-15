// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::trace_duration,
    aes::{
        cipher::{generic_array::GenericArray, NewCipher, StreamCipher as _, StreamCipherSeek},
        Aes256, NewBlockCipher,
    },
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    chacha20::{ChaCha20, Key},
    serde::{
        de::{Error as SerdeError, Visitor},
        Deserialize, Deserializer, Serialize, Serializer,
    },
    std::convert::TryInto,
    xts_mode::{get_tweak_default, Xts128},
};

pub const KEY_SIZE: usize = 256 / 8;
pub const WRAPPED_KEY_SIZE: usize = KEY_SIZE + 16;

// The xts-mode crate expects a sector size. Fxfs will always use a block size >= 512 bytes, so we
// just assume a sector size of 512 bytes, which will work fine even if a different block size is
// used by Fxfs or the underlying device.
const SECTOR_SIZE: u64 = 512;

pub type KeyBytes = [u8; KEY_SIZE];

pub struct UnwrappedKey {
    key: KeyBytes,
}

impl UnwrappedKey {
    pub fn new(key: KeyBytes) -> Self {
        UnwrappedKey { key }
    }

    pub fn key(&self) -> &KeyBytes {
        &self.key
    }
}

pub type UnwrappedKeys = Vec<(u64, UnwrappedKey)>;

#[repr(transparent)]
#[derive(Clone, Debug, PartialEq)]
pub struct WrappedKeyBytes(pub [u8; WRAPPED_KEY_SIZE]);

impl std::ops::Deref for WrappedKeyBytes {
    type Target = [u8; WRAPPED_KEY_SIZE];
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl std::ops::DerefMut for WrappedKeyBytes {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

// Because default impls of Serialize/Deserialize for [T; N] are only defined for N in 0..=32, we
// have to define them ourselves.
impl Serialize for WrappedKeyBytes {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_bytes(&self[..])
    }
}

impl<'de> Deserialize<'de> for WrappedKeyBytes {
    fn deserialize<D>(deserializer: D) -> Result<WrappedKeyBytes, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct WrappedKeyVisitor;

        impl<'d> Visitor<'d> for WrappedKeyVisitor {
            type Value = WrappedKeyBytes;

            fn expecting(&self, formatter: &mut ::core::fmt::Formatter<'_>) -> ::core::fmt::Result {
                formatter.write_str("Expected wrapped keys to be 48 bytes")
            }

            fn visit_bytes<E>(self, bytes: &[u8]) -> Result<WrappedKeyBytes, E>
            where
                E: SerdeError,
            {
                self.visit_byte_buf(bytes.to_vec())
            }

            fn visit_byte_buf<E>(self, bytes: Vec<u8>) -> Result<WrappedKeyBytes, E>
            where
                E: SerdeError,
            {
                let orig_len = bytes.len();
                let bytes: [u8; WRAPPED_KEY_SIZE] =
                    bytes.try_into().map_err(|_| SerdeError::invalid_length(orig_len, &self))?;
                Ok(WrappedKeyBytes(bytes))
            }
        }
        deserializer.deserialize_byte_buf(WrappedKeyVisitor)
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct WrappedKey {
    /// The identifier of the wrapping key.  The identifier has meaning to whatever is doing the
    /// unwrapping.
    pub wrapping_key_id: u64,
    /// AES 256 requires a 512 bit key, which is made of two 256 bit keys, one for the data and one
    /// for the tweak.  Both those keys are derived from the single 256 bit key we have here.
    /// Since the key is wrapped with AES-GCM-SIV, there are an additional 16 bytes paid per key (so
    /// the actual key material is 32 bytes once unwrapped).
    pub key: WrappedKeyBytes,
}

/// To support key rolling and clones, a file can have more than one key.  Each key has an ID that
/// unique to the file.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct WrappedKeys(pub Vec<(u64, WrappedKey)>);

impl std::ops::Deref for WrappedKeys {
    type Target = [(u64, WrappedKey)];
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

struct XtsCipher {
    id: u64,
    xts: Xts128<Aes256>,
}

pub struct XtsCipherSet(Vec<XtsCipher>);

impl XtsCipherSet {
    pub fn new(keys: &UnwrappedKeys) -> Self {
        Self(
            keys.iter()
                .map(|(id, k)| XtsCipher {
                    id: *id,
                    // Note: The "128" in `Xts128` refers to the cipher block size, not the key size
                    // (and not the device sector size). AES-256, like all forms of AES, have a
                    // 128-bit block size, and so will work with `Xts128`.  The same key is used for
                    // for encrypting the data and computing the tweak.
                    xts: Xts128::<Aes256>::new(
                        Aes256::new(GenericArray::from_slice(k.key())),
                        Aes256::new(GenericArray::from_slice(k.key())),
                    ),
                })
                .collect(),
        )
    }

    /// Decrypt the data in `buffer`.
    ///
    /// * `offset` is the byte offset within the file.
    /// * `key_id` specifies which of the unwrapped keys to use.
    /// * `buffer` is mutated in place.
    pub fn decrypt(&self, offset: u64, key_id: u64, buffer: &mut [u8]) -> Result<(), Error> {
        trace_duration!("decrypt");
        assert_eq!(offset % SECTOR_SIZE, 0);
        self.0
            .iter()
            .find(|cipher| cipher.id == key_id)
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

    /// Encrypts data in the `buffer`.
    ///
    /// * `offset` is the byte offset within the file.
    /// * `key_id` specifies which of the unwrapped keys to use.
    /// * `buffer` is mutated in place.
    pub fn encrypt(&self, offset: u64, key_id: u64, buffer: &mut [u8]) -> Result<(), Error> {
        trace_duration!("encrypt");
        assert_eq!(offset % SECTOR_SIZE, 0);
        self.0
            .iter()
            .find(|cipher| cipher.id == key_id)
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

pub struct StreamCipher(ChaCha20);

impl StreamCipher {
    pub fn new(key: &UnwrappedKey, offset: u64) -> Self {
        let mut cipher =
            Self(ChaCha20::new(Key::from_slice(&key.key), /* nonce: */ &[0; 12].into()));
        // TODO(fxbug.dev/93354): Chacha20's 32 bit counter isn't quite big enough for our offset,
        // so we need to handle that case.
        cipher.0.seek(offset);
        cipher
    }

    pub fn encrypt(&mut self, buffer: &mut [u8]) {
        trace_duration!("StreamCipher::encrypt");
        self.0.apply_keystream(buffer);
    }

    pub fn decrypt(&mut self, buffer: &mut [u8]) {
        trace_duration!("StreamCipher::decrypt");
        self.0.apply_keystream(buffer);
    }

    pub fn offset(&self) -> u64 {
        self.0.current_pos()
    }
}

/// Different keys are used for metadata and data in order to make certain operations requiring a
/// metadata key rotation (e.g. secure erase) more efficient.
pub enum KeyPurpose {
    /// The key will be used to wrap user data.
    Data,
    /// The key will be used to wrap internal metadata.
    Metadata,
}

/// An interface trait with the ability to wrap and unwrap encryption keys.
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
        owner: u64,
        purpose: KeyPurpose,
    ) -> Result<(WrappedKey, UnwrappedKey), Error>;

    // Unwraps a single key.
    async fn unwrap_key(&self, wrapped_key: &WrappedKey, owner: u64)
        -> Result<UnwrappedKey, Error>;

    /// Unwraps the keys and stores the result in UnwrappedKeys.
    async fn unwrap_keys(&self, keys: &WrappedKeys, owner: u64) -> Result<UnwrappedKeys, Error> {
        let mut futures = vec![];
        for (key_id, key) in keys.iter() {
            futures.push(async move { Ok((*key_id, self.unwrap_key(key, owner).await?)) });
        }
        futures::future::try_join_all(futures).await
    }
}

#[cfg(any(test, feature = "insecure_crypt"))]
pub mod insecure {
    use {
        super::{
            Crypt, KeyBytes, KeyPurpose, UnwrappedKey, WrappedKey, WrappedKeyBytes, KEY_SIZE,
            WRAPPED_KEY_SIZE,
        },
        anyhow::Error,
        async_trait::async_trait,
        rand::RngCore,
    };

    /// This struct provides the `Crypt` trait without any strong security.
    ///
    /// It is intended for use only in test code where actual security is inconsequential.
    pub struct InsecureCrypt {}

    /// Used by `InsecureCrypt` as an extremely weak form of 'encryption'.
    const DATA_WRAP_XOR: [u8; 8] = [0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef];
    const METADATA_WRAP_XOR: [u8; 8] = [0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10];

    impl InsecureCrypt {
        pub fn new() -> Self {
            Self {}
        }
    }

    #[async_trait]
    impl Crypt for InsecureCrypt {
        async fn create_key(
            &self,
            owner: u64,
            purpose: KeyPurpose,
        ) -> Result<(WrappedKey, UnwrappedKey), Error> {
            let mut rng = rand::thread_rng();
            let mut key: KeyBytes = [0; KEY_SIZE];
            rng.fill_bytes(&mut key);
            let mut wrapped: WrappedKeyBytes = WrappedKeyBytes([0; WRAPPED_KEY_SIZE]);
            let owner_bytes = owner.to_le_bytes();
            let (wrap_xor, wrapping_key_id) = match purpose {
                KeyPurpose::Data => (&DATA_WRAP_XOR, 0),
                KeyPurpose::Metadata => (&METADATA_WRAP_XOR, 1),
            };
            // This intentionally leaves the extra bytes in the wrapped key as zero.  They are
            // unused.
            for i in 0..key.len() {
                let j = i % wrap_xor.len();
                wrapped[i] = key[i] ^ wrap_xor[j] ^ owner_bytes[j];
            }
            Ok((WrappedKey { wrapping_key_id, key: wrapped }, UnwrappedKey::new(key)))
        }

        async fn unwrap_key(
            &self,
            wrapped_key: &WrappedKey,
            owner: u64,
        ) -> Result<UnwrappedKey, Error> {
            let mut unwrapped: KeyBytes = [0; KEY_SIZE];
            let owner_bytes = owner.to_le_bytes();
            let wrap_xor = match wrapped_key.wrapping_key_id {
                0 => &DATA_WRAP_XOR,
                1 => &METADATA_WRAP_XOR,
                _ => panic!("Unexpected wrapping key ID for {:?}", wrapped_key),
            };
            for i in 0..unwrapped.len() {
                let j = i % wrap_xor.len();
                unwrapped[i] = wrapped_key.key[i] ^ wrap_xor[j] ^ owner_bytes[j];
            }
            Ok(UnwrappedKey::new(unwrapped))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{StreamCipher, UnwrappedKey};

    #[test]
    fn test_stream_cipher_offset() {
        let key = UnwrappedKey::new([
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
            25, 26, 27, 28, 29, 30, 31, 32,
        ]);
        let mut cipher1 = StreamCipher::new(&key, 0);
        let mut p1 = [1, 2, 3, 4];
        let mut c1 = p1.clone();
        cipher1.encrypt(&mut c1);

        let mut cipher2 = StreamCipher::new(&key, 1);
        let p2 = [5, 6, 7, 8];
        let mut c2 = p2.clone();
        cipher2.encrypt(&mut c2);

        let xor_fn = |buf1: &mut [u8], buf2| {
            for (b1, b2) in buf1.iter_mut().zip(buf2) {
                *b1 ^= b2;
            }
        };

        // Check that c1 ^ c2 != p1 ^ p2 (which would be the case if the same offset was used for
        // both ciphers).
        xor_fn(&mut c1, &c2);
        xor_fn(&mut p1, &p2);
        assert_ne!(c1, p1);
    }
}
