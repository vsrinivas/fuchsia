// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{KeyAttributes, KeyRequestType, KeyType, KmsKey};
use crate::crypto_provider::{AsymmetricProviderKey, CryptoProvider};
use fidl_fuchsia_kms::{AsymmetricKeyAlgorithm, AsymmetricPrivateKeyRequest, KeyOrigin, Status};

pub struct KmsAsymmetricKey {
    provider_key: Box<dyn AsymmetricProviderKey>,
    key_name: String,
    deleted: bool,
    key_origin: KeyOrigin,
}

impl KmsKey for KmsAsymmetricKey {
    fn get_key_name(&self) -> &str {
        &self.key_name
    }

    fn is_deleted(&self) -> bool {
        self.deleted
    }

    fn handle_request(&self, req: KeyRequestType) -> Result<(), fidl::Error> {
        let KeyRequestType::AsymmetricPrivateKeyRequest(req) = req;
        self.handle_asym_request(req)?;
        Ok(())
    }

    fn get_key_type(&self) -> KeyType {
        KeyType::AsymmetricPrivateKey
    }

    fn get_provider_name(&self) -> &str {
        self.provider_key.get_provider_name()
    }

    fn get_key_data(&self) -> Vec<u8> {
        self.provider_key.get_key_data()
    }

    fn delete(&mut self) -> Result<(), Status> {
        // Inform the provider to delete key.
        self.provider_key.delete().map_err(|_| Status::InternalError)?;
        self.deleted = true;
        Ok(())
    }
}

impl KmsAsymmetricKey {
    /// Create a new KmsAsymmetricKey object.
    ///
    /// # Arguments
    ///
    /// * `provider` - The crypto provider to generate the key with.
    /// * `key_name` - The name for the new key.
    /// * `key_algorithm` - The algorithm for the new key.
    pub fn new<C: CryptoProvider + ?Sized>(
        provider: &C,
        key_name: &str,
        key_algorithm: AsymmetricKeyAlgorithm,
    ) -> Result<Self, Status> {
        // Ask the provider to generate a provider key.
        let provider_key = provider.generate_asymmetric_key(key_algorithm, key_name).map_err(
            debug_err_fn!(Status::InternalError, "Failed to generate asymmetric key: {:?}"),
        )?;
        // Create the key object.
        Ok(KmsAsymmetricKey {
            provider_key,
            key_name: key_name.to_string(),
            deleted: false,
            key_origin: KeyOrigin::Generated,
        })
    }

    /// Parse the key data and generate a new KmsAsymmetricKey object.
    ///
    /// # Arguments
    ///
    /// * `key_name` - The name for the new key.
    /// * `key_attributes` - The attributes for the new key.
    pub fn parse_key(key_name: &str, key_attributes: KeyAttributes) -> Result<Self, Status> {
        if key_attributes.key_type != KeyType::AsymmetricPrivateKey {
            // The key is a different type.
            return Err(Status::KeyNotFound);
        }
        // It is safe to unwrap here because KeyType would always be AsymmetricPrivateKey when
        // we called read_key_attributes_from_file in key_manager and asymmetric_key_algorithm
        // would never be None.
        let key_algorithm = key_attributes.asymmetric_key_algorithm.unwrap();
        // Ask the provider to parse the key.
        let provider_key = key_attributes
            .provider
            .parse_asymmetric_key(&key_attributes.key_data, key_algorithm)
            .map_err(debug_err_fn!(
                Status::ParseKeyError,
                "Failed to parse asymmetric key data: {:?}"
            ))?;

        // Create the key object.
        Ok(KmsAsymmetricKey {
            provider_key,
            key_name: key_name.to_string(),
            deleted: false,
            key_origin: key_attributes.key_origin,
        })
    }

    /// Import an asymmetric private key and returns the imported key object.
    ///
    /// # Arguments
    ///
    /// * `provider` - The crypto provider to parse the private key data.
    /// * `data` - The PKCS8 DER encoded private key data.
    /// * `key_name` - The name for the new key.
    /// * `key_algorithm` - The algorithm for the new key.
    pub fn import_key<C: CryptoProvider + ?Sized>(
        provider: &C,
        data: &[u8],
        key_name: &str,
        key_algorithm: AsymmetricKeyAlgorithm,
    ) -> Result<Self, Status> {
        let provider_key = provider.import_asymmetric_key(data, key_algorithm, key_name).map_err(
            debug_err_fn!(Status::ParseKeyError, "Failed to import asymmetric key: {:?}"),
        )?;
        // Create the key object.
        Ok(KmsAsymmetricKey {
            provider_key,
            key_name: key_name.to_string(),
            deleted: false,
            key_origin: KeyOrigin::Imported,
        })
    }

    pub fn get_key_algorithm(&self) -> AsymmetricKeyAlgorithm {
        self.provider_key.get_key_algorithm()
    }

    pub fn get_key_origin(&self) -> KeyOrigin {
        self.key_origin
    }

    pub fn handle_asym_request(
        &self,
        _req: AsymmetricPrivateKeyRequest,
    ) -> Result<(), fidl::Error> {
        Ok(())
    }
}
