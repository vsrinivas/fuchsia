// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    aes_gcm_siv::{
        aead::{Aead, NewAead},
        Aes128GcmSiv, Aes256GcmSiv, Key,
    },
    fuchsia_zircon::{self as zx},
    itertools::Itertools,
    serde::{Deserialize, Deserializer, Serialize, Serializer},
    std::{
        collections::{hash_map::Entry, HashMap},
        convert::{TryFrom, TryInto},
        fmt::Debug,
        ops::{Deref, DerefMut},
    },
    thiserror::Error,
};

/// KeyBag is a store for collections of wrapped keys.  This is stored in plaintext,
/// and each key is only accessible if the appropriate wrapping key is known.
#[derive(Debug, Deserialize, Serialize)]
pub struct KeyBag {
    version: u16,
    keys: HashMap<KeySlot, WrappedKey>,
}

#[derive(Error, Debug, PartialEq)]
pub enum OpenError {
    #[error("Path to keybag was invalid")]
    InvalidPath,
    #[error("Path to keybag not found")]
    KeyBagNotFound,
    #[error("Keybag failed to parse due to {0}")]
    KeyBagInvalid(String),
    #[error("Keybag was of wrong version (have {0} want {1})")]
    KeyBagVersionMismatch(u16, u16),
    #[error("Failed to persist keybag")]
    FailedToPersist,
}

impl From<OpenError> for zx::Status {
    fn from(error: OpenError) -> zx::Status {
        match error {
            OpenError::InvalidPath => zx::Status::INVALID_ARGS,
            OpenError::KeyBagNotFound => zx::Status::NOT_FOUND,
            OpenError::KeyBagInvalid(..) => zx::Status::IO_DATA_INTEGRITY,
            OpenError::KeyBagVersionMismatch(..) => zx::Status::NOT_SUPPORTED,
            OpenError::FailedToPersist => zx::Status::IO,
        }
    }
}

#[derive(Error, Debug, PartialEq)]
pub enum Error {
    #[error("Failed to persist keybag")]
    FailedToPersist,
    #[error("Key at given slot not found")]
    SlotNotFound,
    #[error("Key slot is already in use")]
    SlotAlreadyUsed,
    #[error("Internal")]
    Internal,
}

impl From<Error> for zx::Status {
    fn from(error: Error) -> zx::Status {
        match error {
            Error::FailedToPersist => zx::Status::IO,
            Error::SlotNotFound => zx::Status::NOT_FOUND,
            Error::SlotAlreadyUsed => zx::Status::ALREADY_EXISTS,
            Error::Internal => zx::Status::INTERNAL,
        }
    }
}

#[derive(Error, Debug, PartialEq)]
pub enum UnwrapError {
    #[error("Key at given slot not found")]
    SlotNotFound,
    #[error("Failed to unwrap the key, most likely due to the wrong wrapping key")]
    AccessDenied,
}

impl From<UnwrapError> for zx::Status {
    fn from(error: UnwrapError) -> zx::Status {
        match error {
            UnwrapError::SlotNotFound => zx::Status::NOT_FOUND,
            UnwrapError::AccessDenied => zx::Status::ACCESS_DENIED,
        }
    }
}

impl Default for KeyBag {
    fn default() -> Self {
        Self { version: CURRENT_VERSION, keys: Default::default() }
    }
}

/// Manages the persistence of a KeyBag.
///
/// All operations on the keybag are atomic.
pub struct KeyBagManager {
    key_bag: KeyBag,
    path: String,
}

pub const AES128_KEY_SIZE: usize = 16;
pub const AES256_KEY_SIZE: usize = 32;

const CURRENT_VERSION: u16 = 1;
const AES256_GCM_SIV_NONCE_SIZE: usize = 12;
const WRAPPED_AES256_KEY_SIZE: usize = AES256_KEY_SIZE + 16;

pub type KeySlot = u16;

/// An AES256 key which has been wrapped using an AEAD, e.g. AES256-GCM-SIV.
/// This can be safely stored in plaintext, and requires the wrapping key to be decoded.
#[derive(Deserialize, Serialize, Debug)]
pub enum WrappedKey {
    Aes128GcmSivWrapped(Nonce, KeyBytes),
    Aes256GcmSivWrapped(Nonce, KeyBytes),
}

// Helper for generating serde implementations for structs like 'KeyBytes' and 'Nonce'.
// Serializes the object as a hexadecimal string, e.g. "af00178db0001200".
macro_rules! impl_serde {
    ($T:ty) => {
        impl Serialize for $T {
            fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
            where
                S: Serializer,
            {
                serializer.serialize_str(
                    &self
                        .0
                        .iter()
                        .format_with("", |item, f| f(&format_args!("{:02x}", item)))
                        .to_string(),
                )
            }
        }

        impl<'de> Deserialize<'de> for $T {
            fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
            where
                D: Deserializer<'de>,
            {
                <String>::deserialize(deserializer).and_then(|s| {
                    let mut this = Self::default();
                    let decoded = hex::decode(s).map_err(|_| {
                        serde::de::Error::custom("failed to parse byte string".to_owned())
                    })?;
                    if decoded.len() != this.0.len() {
                        return Err(serde::de::Error::custom(format!(
                            "Invalid length (have {} want {})",
                            decoded.len(),
                            this.0.len()
                        )));
                    }
                    this.0.copy_from_slice(&decoded[..]);
                    Ok(this)
                })
            }
        }
    };
}

#[derive(Debug, Default)]
pub struct Nonce([u8; AES256_GCM_SIV_NONCE_SIZE]);

impl Nonce {
    fn as_crypto_nonce(&self) -> &aes_gcm_siv::Nonce {
        aes_gcm_siv::Nonce::from_slice(&self.0)
    }
}

impl_serde!(Nonce);

/// A raw byte-string containing a wrapped AES256 key.
#[derive(Debug)]
pub struct KeyBytes([u8; WRAPPED_AES256_KEY_SIZE]);

impl Deref for KeyBytes {
    type Target = [u8; WRAPPED_AES256_KEY_SIZE];
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for KeyBytes {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl TryFrom<Vec<u8>> for KeyBytes {
    type Error = Vec<u8>;
    fn try_from(value: Vec<u8>) -> Result<Self, Self::Error> {
        if value.len() == WRAPPED_AES256_KEY_SIZE {
            let mut key = KeyBytes::default();
            key.0.copy_from_slice(&value[..]);
            Ok(key)
        } else {
            Err(value)
        }
    }
}

impl Default for KeyBytes {
    fn default() -> Self {
        Self([0u8; WRAPPED_AES256_KEY_SIZE])
    }
}

impl_serde!(KeyBytes);

#[repr(C)]
#[derive(Default, PartialEq)]
pub struct Aes256Key([u8; AES256_KEY_SIZE]);

impl Aes256Key {
    pub const fn create(data: [u8; AES256_KEY_SIZE]) -> Self {
        Self(data)
    }
}

impl Deref for Aes256Key {
    type Target = [u8; AES256_KEY_SIZE];
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for Aes256Key {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl Debug for Aes256Key {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("Aes256Key")
    }
}

#[repr(C)]
#[derive(PartialEq)]
pub enum WrappingKey {
    Aes128([u8; AES128_KEY_SIZE]),
    Aes256([u8; AES256_KEY_SIZE]),
}

fn generate_key() -> Aes256Key {
    let mut key = Aes256Key::default();
    zx::cprng_draw(&mut key.0);
    key
}

fn generate_nonce() -> Nonce {
    let mut nonce = Nonce::default();
    zx::cprng_draw(&mut nonce.0);
    nonce
}

impl KeyBagManager {
    /// Opens a key-bag file.  If the keybag doesn't exist, creates it.
    pub fn open(path: &std::path::Path) -> Result<Self, OpenError> {
        let path_str = path.to_str().map(str::to_string).ok_or(OpenError::InvalidPath)?;
        let is_empty = match std::fs::metadata(path) {
            Ok(m) if m.len() > 0 => false,
            _ => true,
        };
        if is_empty {
            let mut this = Self { key_bag: KeyBag::default(), path: path_str };
            this.commit().map_err(|_| OpenError::FailedToPersist)?;
            return Ok(this);
        }
        let reader = std::io::BufReader::new(
            std::fs::File::open(path).map_err(|_| OpenError::KeyBagNotFound)?,
        );
        let key_bag: KeyBag =
            serde_json::from_reader(reader).map_err(|e| OpenError::KeyBagInvalid(e.to_string()))?;
        if key_bag.version != CURRENT_VERSION {
            return Err(OpenError::KeyBagVersionMismatch(key_bag.version, CURRENT_VERSION));
        }
        Ok(Self { key_bag, path: path_str })
    }
    /// Generates and stores a new key in the key-bag, based on |wrapping_key|.  Returns the
    /// unwrapped key contents.
    pub fn new_key(
        &mut self,
        slot: KeySlot,
        wrapping_key: &WrappingKey,
    ) -> Result<Aes256Key, Error> {
        let key = match self.key_bag.keys.entry(slot) {
            Entry::Occupied(_) => return Err(Error::SlotAlreadyUsed),
            Entry::Vacant(v) => {
                let key = generate_key();
                let nonce = generate_nonce();

                let entry = match wrapping_key {
                    WrappingKey::Aes128(bytes) => {
                        let cipher = Aes128GcmSiv::new(Key::from_slice(bytes));
                        let wrapped = cipher
                            .encrypt(nonce.as_crypto_nonce(), &key.0[..])
                            .map_err(|_| Error::Internal)
                            .and_then(|k| k.try_into().map_err(|_| Error::Internal))?;
                        WrappedKey::Aes128GcmSivWrapped(nonce, wrapped)
                    }
                    WrappingKey::Aes256(bytes) => {
                        let cipher = Aes256GcmSiv::new(Key::from_slice(bytes));
                        let wrapped = cipher
                            .encrypt(nonce.as_crypto_nonce(), &key.0[..])
                            .map_err(|_| Error::Internal)
                            .and_then(|k| k.try_into().map_err(|_| Error::Internal))?;
                        WrappedKey::Aes256GcmSivWrapped(nonce, wrapped)
                    }
                };
                v.insert(entry);
                key
            }
        };

        self.commit().map(|_| key)
    }
    /// Removes a key from the key bag.
    pub fn remove_key(&mut self, slot: KeySlot) -> Result<(), Error> {
        if let None = self.key_bag.keys.remove(&slot) {
            return Err(Error::SlotNotFound);
        }
        self.commit()
    }
    /// Attempts to unwrap a key at |slot| with |wrapping_key|.
    pub fn unwrap_key(
        &self,
        slot: KeySlot,
        wrapping_key: &WrappingKey,
    ) -> Result<Aes256Key, UnwrapError> {
        let key = self.key_bag.keys.get(&slot).ok_or(UnwrapError::SlotNotFound)?;
        let (nonce, bytes) = match key {
            WrappedKey::Aes128GcmSivWrapped(nonce, bytes) => (nonce, bytes),
            WrappedKey::Aes256GcmSivWrapped(nonce, bytes) => (nonce, bytes),
        };
        // Intentionally don't check that the algorithms match; we want AccessDenied to be returned
        // if the wrong key type is specified, to minimize information leakage.
        let decrypt_res = match wrapping_key {
            WrappingKey::Aes128(wrap_bytes) => {
                let cipher = Aes128GcmSiv::new(Key::from_slice(wrap_bytes));
                cipher.decrypt(nonce.as_crypto_nonce(), &bytes[..])
            }
            WrappingKey::Aes256(wrap_bytes) => {
                let cipher = Aes256GcmSiv::new(Key::from_slice(wrap_bytes));
                cipher.decrypt(nonce.as_crypto_nonce(), &bytes[..])
            }
        };
        match decrypt_res {
            Ok(unwrapped) => {
                let mut key = Aes256Key([0u8; 32]);
                key.0.copy_from_slice(&unwrapped[..]);
                Ok(key)
            }
            Err(_) => Err(UnwrapError::AccessDenied),
        }
    }
    fn commit(&mut self) -> Result<(), Error> {
        let path = std::path::Path::new(&self.path);
        let tmp_path = path.with_extension("tmp");
        let _ = std::fs::remove_file(&tmp_path);
        {
            let tmpfile = std::io::BufWriter::new(
                std::fs::File::create(&tmp_path).map_err(|_| Error::FailedToPersist)?,
            );
            serde_json::to_writer(tmpfile, &self.key_bag).map_err(|_| Error::FailedToPersist)?;
        }
        std::fs::rename(&tmp_path, &path).map_err(|_| Error::FailedToPersist)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{Aes256Key, Error, KeyBagManager, UnwrapError, WrappingKey},
        assert_matches::assert_matches,
        tempfile::NamedTempFile,
    };

    #[test]
    fn nonexistent_keybag() {
        let owned_path = NamedTempFile::new().unwrap().into_temp_path();
        let path: &std::path::Path = owned_path.as_ref();
        std::fs::remove_file(path).expect("unlink failed");
        let keybag = KeyBagManager::open(path).expect("Open nonexistent keybag failed");
        assert!(keybag.key_bag.keys.is_empty());
    }

    #[test]
    fn empty_keybag() {
        let owned_path = NamedTempFile::new().unwrap().into_temp_path();
        let path: &std::path::Path = owned_path.as_ref();
        let keybag = KeyBagManager::open(path).expect("Open empty keybag failed");
        assert!(keybag.key_bag.keys.is_empty());
    }

    #[test]
    fn add_remove_key() {
        let owned_path = NamedTempFile::new().unwrap().into_temp_path();
        let path: &std::path::Path = owned_path.as_ref();
        {
            let mut keybag = KeyBagManager::open(path).expect("Open empty keybag failed");
            let key = WrappingKey::Aes256([0u8; 32]);
            keybag.new_key(0, &key).expect("new key failed");
            assert_eq!(
                Error::SlotAlreadyUsed,
                keybag.new_key(0, &key).expect_err("new key on used slot failed")
            );
        }
        {
            let mut keybag = KeyBagManager::open(path).expect("Open keybag failed");
            keybag.remove_key(0).expect("remove_key failed");
            assert_eq!(
                Error::SlotNotFound,
                keybag.remove_key(1).expect_err("remove_key with invalid key specified failed")
            );
        }
        let keybag = KeyBagManager::open(path).expect("Open keybag failed");
        assert!(keybag.key_bag.keys.is_empty());
    }

    #[test]
    fn unwrap_key() {
        let owned_path = NamedTempFile::new().unwrap().into_temp_path();
        let path: &std::path::Path = owned_path.as_ref();
        let mut keybag = KeyBagManager::open(path).expect("Open empty keybag failed");

        let key = WrappingKey::Aes256([3u8; 32]);
        let key2 = WrappingKey::Aes128([0xffu8; 16]);

        keybag.new_key(0, &key).expect("new_key failed");
        keybag.new_key(1, &key2).expect("new_key failed");
        keybag.new_key(2, &key).expect("new_key failed");

        assert_matches!(keybag.unwrap_key(0, &key), Ok(_));
        assert_eq!(keybag.unwrap_key(1, &key), Err(UnwrapError::AccessDenied));
        assert_matches!(keybag.unwrap_key(2, &key), Ok(_));
        assert_eq!(keybag.unwrap_key(3, &key), Err(UnwrapError::SlotNotFound));

        assert_eq!(keybag.unwrap_key(0, &key2), Err(UnwrapError::AccessDenied));
        assert_matches!(keybag.unwrap_key(1, &key2), Ok(_));
        assert_eq!(keybag.unwrap_key(2, &key2), Err(UnwrapError::AccessDenied));
        assert_eq!(keybag.unwrap_key(3, &key2), Err(UnwrapError::SlotNotFound));
    }

    #[test]
    fn from_testdata() {
        // The testdata file contains three keys, all of which have the same plaintext value
        // ("secret\0..\0").
        // Slots 0,2 are encrypted with a null AES256 key, and 1 is encrypted with something else.
        let path = std::path::Path::new("/pkg/data/key_bag.json");
        let keybag = KeyBagManager::open(path).expect("Open keybag failed");

        let mut expected = Aes256Key::default();
        expected.0[..6].copy_from_slice(b"secret");

        let key = WrappingKey::Aes256([0u8; 32]);
        assert_eq!(keybag.unwrap_key(0, &key).as_ref().map(|s| &s.0), Ok(&expected.0));
        assert_eq!(keybag.unwrap_key(1, &key), Err(UnwrapError::AccessDenied));
        assert_eq!(keybag.unwrap_key(2, &key), Ok(expected));
        assert_eq!(keybag.unwrap_key(3, &key), Err(UnwrapError::SlotNotFound));
    }
}
