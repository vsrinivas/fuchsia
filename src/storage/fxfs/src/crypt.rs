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
    type_hash::TypeHash,
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

impl TypeHash for WrappedKeyBytes {
    fn write_hash(hasher: &mut impl std::hash::Hasher) {
        // Implementation mirrors type_hash_core.
        // (TypeHash only has generics defined for arrays up to length 32)
        hasher.write(b"[;]");
        hasher.write_usize(WRAPPED_KEY_SIZE);
        u8::write_hash(hasher);
    }
}

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

// There doesn't seem to be much point wasting entropy on fuzzing key bytes.
#[cfg(fuzz)]
impl<'a> arbitrary::Arbitrary<'a> for WrappedKeyBytes {
    fn arbitrary(_: &mut arbitrary::Unstructured<'a>) -> arbitrary::Result<Self> {
        Ok(WrappedKeyBytes([0; WRAPPED_KEY_SIZE]))
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, TypeHash, PartialEq)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub struct WrappedKey {
    /// The identifier of the wrapping key.  The identifier has meaning to whatever is doing the
    /// unwrapping.
    pub wrapping_key_id: u64,
    /// AES 256 requires a 512 bit key, which is made of two 256 bit keys, one for the data and one
    /// for the tweak.  It is safe to use the same 256 bit key for both (see
    /// https://csrc.nist.gov/CSRC/media/Projects/Block-Cipher-Techniques/documents/BCM/Comments/XTS/follow-up_XTS_comments-Ball.pdf)
    /// which is what we do here.  Since the key is wrapped with AES-GCM-SIV, there are an
    /// additional 16 bytes paid per key (so the actual key material is 32 bytes once unwrapped).
    pub key: WrappedKeyBytes,
}

/// To support key rolling and clones, a file can have more than one key.  Each key has an ID that
/// unique to the file.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, TypeHash)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
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

/// A thin wrapper around a ChaCha20 stream cipher.  This will use a zero nonce. **NOTE**: Great
/// care must be taken not to encrypt different plaintext with the same key and offset (even across
/// multiple boots), so consider if this suits your purpose before using it.
pub struct StreamCipher(ChaCha20);

impl StreamCipher {
    pub fn new(key: &UnwrappedKey, offset: u64) -> Self {
        let mut cipher =
            Self(ChaCha20::new(Key::from_slice(&key.key), /* nonce: */ &[0; 12].into()));
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
        super::{Crypt, KeyPurpose, UnwrappedKey, WrappedKey, WrappedKeyBytes},
        aes_gcm_siv::{
            aead::{Aead, NewAead},
            Aes256GcmSiv, Key, Nonce,
        },
        anyhow::{anyhow, Context, Error},
        async_trait::async_trait,
        rand::{rngs::StdRng, RngCore, SeedableRng},
        std::{collections::HashMap, convert::TryInto},
    };

    pub const DATA_KEY: [u8; 32] = [
        0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11,
        0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    ];
    pub const METADATA_KEY: [u8; 32] = [
        0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1,
        0xf0, 0xef, 0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8, 0xe7, 0xe6, 0xe5, 0xe4, 0xe3, 0xe2,
        0xe1, 0xe0,
    ];

    /// This struct provides the `Crypt` trait without any strong security.
    ///
    /// It is intended for use only in test code where actual security is inconsequential.
    #[derive(Default)]
    pub struct InsecureCrypt {
        ciphers: HashMap<u64, Aes256GcmSiv>,
        active_data_key: Option<u64>,
        active_metadata_key: Option<u64>,
    }
    impl InsecureCrypt {
        pub fn new() -> Self {
            let mut this = Self::default();
            this.ciphers.insert(0, Aes256GcmSiv::new(Key::from_slice(&DATA_KEY)));
            this.ciphers.insert(1, Aes256GcmSiv::new(Key::from_slice(&METADATA_KEY)));
            this.active_data_key = Some(0);
            this.active_metadata_key = Some(1);
            this
        }
    }

    #[async_trait]
    impl Crypt for InsecureCrypt {
        async fn create_key(
            &self,
            owner: u64,
            purpose: KeyPurpose,
        ) -> Result<(WrappedKey, UnwrappedKey), Error> {
            let wrapping_key_id = match purpose {
                KeyPurpose::Data => self.active_data_key.as_ref(),
                KeyPurpose::Metadata => self.active_metadata_key.as_ref(),
            }
            .ok_or(anyhow!("Invalid args in create_key"))?;
            let cipher = self.ciphers.get(wrapping_key_id).ok_or(anyhow!("No cipher."))?;
            let mut nonce = Nonce::default();
            nonce.as_mut_slice()[..8].copy_from_slice(&owner.to_le_bytes());

            let mut key = [0u8; 32];
            StdRng::from_entropy().fill_bytes(&mut key);

            let wrapped: Vec<u8> =
                cipher.encrypt(&nonce, &key[..]).context("Failed to wrap key")?;
            let wrapped = WrappedKeyBytes(
                wrapped.try_into().map_err(|_| anyhow!("wrapped key wrong length"))?,
            );
            Ok((
                WrappedKey { wrapping_key_id: *wrapping_key_id, key: wrapped },
                UnwrappedKey::new(key),
            ))
        }

        async fn unwrap_key(
            &self,
            wrapped_key: &WrappedKey,
            owner: u64,
        ) -> Result<UnwrappedKey, Error> {
            let cipher =
                self.ciphers.get(&wrapped_key.wrapping_key_id).ok_or(anyhow!("cipher fail"))?;
            let mut nonce = Nonce::default();
            nonce.as_mut_slice()[..8].copy_from_slice(&owner.to_le_bytes());
            Ok(UnwrappedKey::new(
                cipher
                    .decrypt(&nonce, &wrapped_key.key.0[..])
                    .map_err(|_| anyhow!("unwrap keys failed"))?
                    .try_into()
                    .map_err(|_| anyhow!("Unexpected wrapped key length"))?,
            ))
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
