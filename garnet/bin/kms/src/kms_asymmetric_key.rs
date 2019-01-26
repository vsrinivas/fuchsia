// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{KeyRequestType, KeyType, KmsKey};
use crate::crypto_provider::{AsymmetricProviderKey, CryptoProvider};
use fidl_fuchsia_kms::{AsymmetricKeyAlgorithm, AsymmetricPrivateKeyRequest, KeyOrigin, Status};
use log::error;

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
        self.provider_key.delete().map_err(|_| -> Status { Status::InternalError })?;
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
        let provider_key =
            provider.generate_asymmetric_key(key_algorithm, key_name).map_err(|err| -> Status {
                error!("Failed to generate asymmetric key: {:?}", err);
                Status::InternalError
            })?;
        // Create the key object.
        Ok(KmsAsymmetricKey {
            provider_key,
            key_name: key_name.to_string(),
            deleted: false,
            key_origin: KeyOrigin::Generated,
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
            |err| -> Status {
                error!("Failed to import asymmetric key: {:?}", err);
                Status::ParseKeyError
            },
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
