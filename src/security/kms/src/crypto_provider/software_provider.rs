// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::crypto_provider::{
    mundane_provider::MundaneSoftwareProvider, AsymmetricProviderKey, CryptoProvider,
    CryptoProviderError, ProviderKey, SealingProviderKey,
};
use aes_gcm::{
    aead::{AeadInPlace, NewAead},
    Aes256Gcm, Key, Nonce, Tag,
};
use bincode;
use fidl_fuchsia_kms::{AsymmetricKeyAlgorithm, KeyProvider};
use mundane;
use serde::{Deserialize, Serialize};
use serde_json;
use std::convert::{TryFrom, TryInto};

const AES_KEY_SIZE: usize = 32;
const AES_IV_SIZE: usize = 12;
const AES_TAG_SIZE: usize = 16;

/// SoftwareProvider is a software based provider that supports symmetric and asymmetric operations.
///
/// SoftwareProvider uses MundaneSoftwareProvider for asymmetric operations and uses rust crypto
/// aes_gcm crate for sealing/unsealing operation. The key data is the plain text key material, so
/// its security depends on the security of the KMS module.
#[derive(Debug, Clone)]
pub struct SoftwareProvider {
    mundane_provider: MundaneSoftwareProvider,
}

impl SoftwareProvider {
    pub fn new() -> Self {
        SoftwareProvider { mundane_provider: MundaneSoftwareProvider {} }
    }
}

#[derive(Serialize, Deserialize, Debug)]
struct AesKey {
    key: [u8; AES_KEY_SIZE],
}

#[derive(Serialize, Deserialize, Debug)]
struct EncryptedData {
    iv: [u8; AES_IV_SIZE],
    cipher_text: Vec<u8>,
    tag: [u8; AES_TAG_SIZE],
}

#[derive(Debug)]
struct SoftwareSealingKey {
    key_data: Vec<u8>,
    inner_key: AesKey,
}

impl SoftwareSealingKey {
    pub fn new(inner_key: AesKey) -> Result<Self, CryptoProviderError> {
        let key_data = serde_json::to_vec(&inner_key).map_err(|err| {
            CryptoProviderError::new(&format!(
                "failed to marshal software sealing key as JSON: {:?}",
                err
            ))
        })?;
        Ok(SoftwareSealingKey { key_data, inner_key })
    }
}

impl ProviderKey for SoftwareSealingKey {
    fn delete(&mut self) -> Result<(), CryptoProviderError> {
        Ok(())
    }
    fn get_key_data(&self) -> Vec<u8> {
        self.key_data.clone()
    }
    fn get_key_provider(&self) -> KeyProvider {
        (SoftwareProvider::new()).get_name()
    }
}

impl SealingProviderKey for SoftwareSealingKey {
    fn encrypt(&self, data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        let key = Key::from_slice(&self.inner_key.key);
        let cipher = Aes256Gcm::new(key);
        let mut iv = Nonce::default();
        mundane::bytes::rand(&mut iv);
        let mut cipher_text = data.to_vec();
        // We use empty Additional Authentication Data (AAD).
        let tag = cipher
            .encrypt_in_place_detached(&iv, &[], &mut cipher_text)
            .expect("buffer is large enough");
        let output =
            bincode::serialize(&EncryptedData { iv: iv.into(), cipher_text, tag: tag.into() })
                .map_err(|err| {
                    CryptoProviderError::new(&format!(
                        "failed to serialize encrypted data: {:?}",
                        err
                    ))
                })?;
        Ok(output)
    }

    fn decrypt(&self, data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        let key = Key::from_slice(&self.inner_key.key);
        let cipher = Aes256Gcm::new(key);
        let encrypted_data: EncryptedData = bincode::deserialize(data).map_err(|err| {
            CryptoProviderError::new(&format!("failed to deserialize encrypted data: {:?}", err))
        })?;
        let iv = Nonce::from_slice(&encrypted_data.iv);
        let mut plain_text = encrypted_data.cipher_text.to_vec();
        let tag = Tag::from_slice(&encrypted_data.tag);
        // We use empty Additional Authentication Data (AAD).
        if cipher.decrypt_in_place_detached(iv, &[], &mut plain_text, tag).is_err() {
            Err(CryptoProviderError::new("failed to decrypt data"))
        } else {
            Ok(plain_text)
        }
    }
}

#[derive(Debug)]
pub struct SoftwareAsymmetricPrivateKey {
    mundane_key: Box<dyn AsymmetricProviderKey>,
}

impl ProviderKey for SoftwareAsymmetricPrivateKey {
    fn delete(&mut self) -> Result<(), CryptoProviderError> {
        self.mundane_key.delete()
    }
    fn get_key_data(&self) -> Vec<u8> {
        self.mundane_key.get_key_data()
    }
    fn get_key_provider(&self) -> KeyProvider {
        SoftwareProvider::new().get_name()
    }
}

impl AsymmetricProviderKey for SoftwareAsymmetricPrivateKey {
    fn sign(&self, data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        self.mundane_key.sign(data)
    }

    fn get_der_public_key(&self) -> Result<Vec<u8>, CryptoProviderError> {
        self.mundane_key.get_der_public_key()
    }

    fn get_key_algorithm(&self) -> AsymmetricKeyAlgorithm {
        self.mundane_key.get_key_algorithm()
    }
}

impl CryptoProvider for SoftwareProvider {
    fn supported_asymmetric_algorithms(&self) -> &'static [AsymmetricKeyAlgorithm] {
        self.mundane_provider.supported_asymmetric_algorithms()
    }

    fn get_name(&self) -> KeyProvider {
        KeyProvider::SoftwareProvider
    }
    fn box_clone(&self) -> Box<dyn CryptoProvider> {
        Box::new(SoftwareProvider::new())
    }
    fn generate_asymmetric_key(
        &self,
        key_algorithm: AsymmetricKeyAlgorithm,
        key_name: &str,
    ) -> Result<Box<dyn AsymmetricProviderKey>, CryptoProviderError> {
        let mundane_key = self.mundane_provider.generate_asymmetric_key(key_algorithm, key_name)?;
        Ok(Box::new(SoftwareAsymmetricPrivateKey { mundane_key }))
    }
    fn import_asymmetric_key(
        &self,
        key_data: &[u8],
        key_algorithm: AsymmetricKeyAlgorithm,
        key_name: &str,
    ) -> Result<Box<dyn AsymmetricProviderKey>, CryptoProviderError> {
        let mundane_key =
            self.mundane_provider.import_asymmetric_key(key_data, key_algorithm, key_name)?;
        Ok(Box::new(SoftwareAsymmetricPrivateKey { mundane_key }))
    }

    fn parse_asymmetric_key(
        &self,
        key_data: &[u8],
        key_algorithm: AsymmetricKeyAlgorithm,
    ) -> Result<Box<dyn AsymmetricProviderKey>, CryptoProviderError> {
        let mundane_key = self.mundane_provider.parse_asymmetric_key(key_data, key_algorithm)?;
        Ok(Box::new(SoftwareAsymmetricPrivateKey { mundane_key }))
    }
    fn generate_sealing_key(
        &self,
        _key_name: &str,
    ) -> Result<Box<dyn SealingProviderKey>, CryptoProviderError> {
        let mut key = [0; AES_KEY_SIZE];
        mundane::bytes::rand(&mut key);
        let inner_key = AesKey { key };
        let sealing_key = SoftwareSealingKey::new(inner_key)?;
        Ok(Box::new(sealing_key))
    }
    fn parse_sealing_key(
        &self,
        key_data: &[u8],
    ) -> Result<Box<dyn SealingProviderKey>, CryptoProviderError> {
        let inner_key = serde_json::from_slice(key_data).map_err(|err| {
            CryptoProviderError::new(&format!("failed to parse key data: {:?}", err))
        })?;
        Ok(Box::new(SoftwareSealingKey { key_data: key_data.to_vec(), inner_key }))
    }

    fn calculate_sealed_data_size(
        &self,
        original_data_size: u64,
    ) -> Result<u64, CryptoProviderError> {
        let data_size = usize::try_from(original_data_size).map_err(|err| {
            CryptoProviderError::new(&format!(
                "failed to convert original data size to usize: {:?}",
                err
            ))
        })?;
        let fake_encrypted_data = EncryptedData {
            iv: [0; AES_IV_SIZE],
            cipher_text: vec![0; data_size],
            tag: [0; AES_TAG_SIZE],
        };
        let fake_sealed_data = bincode::serialize(&fake_encrypted_data).map_err(|err| {
            CryptoProviderError::new(&format!(
                "failed to serialize encrypted data to calculate size: {:?}",
                err
            ))
        })?;
        usize::try_into(fake_sealed_data.len()).map_err(|err| {
            CryptoProviderError::new(&format!(
                "failed to convert sealed data size to u64: {:?}",
                err
            ))
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common;
    static TEST_KEY_NAME: &str = "TestKey";

    #[test]
    fn test_generate_sealing_key_different_each_time() {
        let software_provider = SoftwareProvider::new();
        let sealing_key_one = software_provider.generate_sealing_key(TEST_KEY_NAME).unwrap();
        let sealing_key_two = software_provider.generate_sealing_key(TEST_KEY_NAME).unwrap();
        // Generating two different sealing key should get different key data.
        assert_ne!(sealing_key_one.get_key_data(), sealing_key_two.get_key_data());
    }

    #[test]
    // We need to make sure we use different iv for each encryption, so encrypting same data
    // multiple times would generate different result.
    fn test_sealed_data_different_each_time() {
        let software_provider = SoftwareProvider::new();
        let sealing_key = software_provider.generate_sealing_key(TEST_KEY_NAME).unwrap();
        let data = common::generate_random_data(256);
        let encrypted_data_one = sealing_key.encrypt(&data).unwrap();
        let encrypted_data_two = sealing_key.encrypt(&data).unwrap();
        assert_ne!(encrypted_data_one, encrypted_data_two);
    }

    #[test]
    fn test_sealing_key_encrypt_decrypt() {
        let software_provider = SoftwareProvider::new();
        let sealing_key = software_provider.generate_sealing_key(TEST_KEY_NAME).unwrap();
        let data = common::generate_random_data(256);
        let encrypted_data = sealing_key.encrypt(&data).unwrap();
        let decrypted_data = sealing_key.decrypt(&encrypted_data).unwrap();
        // Decrypt an encrypted data should get the original data.
        assert_eq!(data, decrypted_data);
    }

    #[test]
    fn test_parse_sealing_key() {
        let software_provider = SoftwareProvider::new();
        let sealing_key = software_provider.generate_sealing_key(TEST_KEY_NAME).unwrap();
        let data = common::generate_random_data(256);
        let encrypted_data = sealing_key.encrypt(&data).unwrap();
        let key_data = sealing_key.get_key_data();
        // Generate a new sealing key using the same key data.
        let new_sealing_key = software_provider.parse_sealing_key(&key_data).unwrap();
        assert_eq!(key_data, new_sealing_key.get_key_data());
        // The encrypted data encrypted by the previous key should be decrypted by the new key.
        let decrypted_data = new_sealing_key.decrypt(&encrypted_data).unwrap();
        assert_eq!(data, decrypted_data);
    }

    #[test]
    fn test_caculate_sealed_data_size() {
        let software_provider = SoftwareProvider::new();
        let sealing_key = software_provider.generate_sealing_key(TEST_KEY_NAME).unwrap();
        let data = common::generate_random_data(256);
        let encrypted_data = sealing_key.encrypt(&data).unwrap();
        // Calculating sealed data size for MAX_DATA_SIZE should be OK.
        let sealed_data_size = software_provider.calculate_sealed_data_size(256).unwrap();
        assert_eq!(encrypted_data.len(), sealed_data_size as usize);
    }
}
