// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{self as common, KeyAttributes, KeyRequestType, KeyType, KmsKey};
use crate::crypto_provider::{AsymmetricProviderKey, CryptoProvider};
use fidl_fuchsia_kms::{
    AsymmetricKeyAlgorithm, AsymmetricPrivateKeyRequest, Error, KeyOrigin, KeyProvider, PublicKey,
    Signature, MAX_DATA_SIZE,
};
use fidl_fuchsia_mem::Buffer;

#[derive(Debug)]
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

    fn handle_request(&self, req: KeyRequestType<'_>) -> Result<(), fidl::Error> {
        if let KeyRequestType::AsymmetricPrivateKeyRequest(req) = req {
            self.handle_asym_request(req)?;
        } else {
            panic!("Invalid request!");
        }
        Ok(())
    }

    fn get_key_type(&self) -> KeyType {
        KeyType::AsymmetricPrivateKey
    }

    fn get_key_provider(&self) -> KeyProvider {
        self.provider_key.get_key_provider()
    }

    fn get_key_data(&self) -> Vec<u8> {
        self.provider_key.get_key_data()
    }

    fn delete(&mut self) -> Result<(), Error> {
        // Inform the provider to delete key.
        self.provider_key.delete().map_err(|_| Error::InternalError)?;
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
    ) -> Result<Self, Error> {
        // Ask the provider to generate a provider key.
        let provider_key = provider.generate_asymmetric_key(key_algorithm, key_name).map_err(
            debug_err_fn!(Error::InternalError, "Failed to generate asymmetric key: {:?}"),
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
    pub fn parse_key(key_name: &str, key_attributes: KeyAttributes<'_>) -> Result<Self, Error> {
        if key_attributes.key_type != KeyType::AsymmetricPrivateKey {
            // The key is a different type.
            return Err(Error::KeyNotFound);
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
                Error::ParseKeyError,
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
    ) -> Result<Self, Error> {
        let provider_key = provider.import_asymmetric_key(data, key_algorithm, key_name).map_err(
            debug_err_fn!(Error::ParseKeyError, "Failed to import asymmetric key: {:?}"),
        )?;
        // Create the key object.
        Ok(KmsAsymmetricKey {
            provider_key,
            key_name: key_name.to_string(),
            deleted: false,
            key_origin: KeyOrigin::Imported,
        })
    }

    /// Sign a piece of data. Return the signature.
    ///
    /// # Arguments
    ///
    /// * `buffer` - The input data buffer, need to be less than 32k bytes. Otherwise InputTooLarge
    ///     would be returned.
    fn sign(&self, buffer: Buffer) -> Result<Vec<u8>, Error> {
        if buffer.size > MAX_DATA_SIZE.into() {
            return Err(Error::InputTooLarge);
        }
        let input = common::buffer_to_data(buffer)?;
        if self.is_deleted() {
            return Err(Error::KeyNotFound);
        }
        self.provider_key
            .sign(&input)
            .map_err(debug_err_fn!(Error::InternalError, "Failed to sign data: {:?}"))
    }

    pub fn get_key_algorithm(&self) -> AsymmetricKeyAlgorithm {
        self.provider_key.get_key_algorithm()
    }

    pub fn get_key_origin(&self) -> KeyOrigin {
        self.key_origin
    }

    /// Get the DER encoded public key.
    fn get_der_public_key(&self) -> Result<Vec<u8>, Error> {
        if self.is_deleted() {
            return Err(Error::KeyNotFound);
        }
        Ok(self
            .provider_key
            .get_der_public_key()
            .map_err(debug_err_fn!(Error::InternalError, "Failed to get public key: {:?}"))?)
    }

    pub fn handle_asym_request(&self, req: AsymmetricPrivateKeyRequest) -> Result<(), fidl::Error> {
        match req {
            AsymmetricPrivateKeyRequest::Sign { data, responder } => {
                responder.send(&mut self.sign(data).map(|signature| Signature { bytes: signature }))
            }
            AsymmetricPrivateKeyRequest::GetPublicKey { responder } => responder.send(
                &mut self.get_der_public_key().map(|public_key| PublicKey { bytes: public_key }),
            ),
            AsymmetricPrivateKeyRequest::GetKeyAlgorithm { responder } => {
                let key_algorithm = self.get_key_algorithm();
                if self.is_deleted() {
                    responder.send(&mut Err(Error::KeyNotFound))
                } else {
                    responder.send(&mut Ok(key_algorithm))
                }
            }
            AsymmetricPrivateKeyRequest::GetKeyOrigin { responder } => {
                let origin = self.get_key_origin();
                if self.is_deleted() {
                    responder.send(&mut Err(Error::KeyNotFound))
                } else {
                    responder.send(&mut Ok(origin))
                }
            }
            AsymmetricPrivateKeyRequest::GetKeyProvider { responder } => {
                let provider_name = self.get_key_provider();
                if self.is_deleted() {
                    responder.send(&mut Err(Error::KeyNotFound))
                } else {
                    responder.send(&mut Ok(provider_name))
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::{self as common, ASYMMETRIC_KEY_ALGORITHMS};
    use crate::crypto_provider::mock_provider::MockProvider;
    static TEST_KEY_NAME: &str = "TestKey";

    #[test]
    fn test_sign_mock_provider() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            sign_mock_provider(*algorithm);
        }
    }

    fn sign_mock_provider(key_algorithm: AsymmetricKeyAlgorithm) {
        let test_output_data = common::generate_random_data(32);
        let test_key_data = common::generate_random_data(32);
        let mock_provider = Box::new(MockProvider::new());
        mock_provider.set_result(&test_key_data);
        mock_provider.set_key_result(Ok(test_output_data.clone()));
        let test_key =
            KmsAsymmetricKey::new(&*mock_provider, TEST_KEY_NAME, key_algorithm).unwrap();

        let test_sign_data = common::generate_random_data(32);
        let signature = test_key.sign(common::data_to_buffer(&test_sign_data).unwrap()).unwrap();
        assert_eq!(test_key.get_key_data(), test_key_data);
        assert_eq!(signature, test_output_data);
    }

    #[test]
    fn sign_mock_provider_input_too_large() {
        let key_algorithm = AsymmetricKeyAlgorithm::EcdsaSha256P256;
        let test_output_data = common::generate_random_data(32);
        let test_key_data = common::generate_random_data(32);
        let mock_provider = Box::new(MockProvider::new());
        mock_provider.set_result(&test_key_data);
        mock_provider.set_key_result(Ok(test_output_data.clone()));
        let test_key =
            KmsAsymmetricKey::new(&*mock_provider, TEST_KEY_NAME, key_algorithm).unwrap();

        // Input is 1 byte larger than the maximum data size.
        let test_sign_data = common::generate_random_data(MAX_DATA_SIZE + 1);
        let result = test_key.sign(common::data_to_buffer(&test_sign_data).unwrap());
        if Err(Error::InputTooLarge) == result {
            assert!(true);
        } else {
            assert!(false);
        }
    }

    #[test]
    fn sign_mock_provider_key_deleted() {
        let key_algorithm = AsymmetricKeyAlgorithm::EcdsaSha256P256;
        let test_key_data = common::generate_random_data(32);
        let mock_provider = Box::new(MockProvider::new());
        mock_provider.set_result(&test_key_data);
        // Make sure the delete operation on the key succeed.
        mock_provider.set_key_result(Ok(Vec::new()));
        let mut test_key =
            KmsAsymmetricKey::new(&*mock_provider, TEST_KEY_NAME, key_algorithm).unwrap();
        test_key.delete().unwrap();

        let test_sign_data = common::generate_random_data(32);
        let result = test_key.sign(common::data_to_buffer(&test_sign_data).unwrap());
        if Err(Error::KeyNotFound) == result {
            assert!(true);
        } else {
            assert!(false);
        }
    }

    #[test]
    fn get_get_public_key_mock_provider() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            get_public_key_mock_provider(*algorithm);
        }
    }

    fn get_public_key_mock_provider(key_algorithm: AsymmetricKeyAlgorithm) {
        let test_key_data = common::generate_random_data(32);
        let test_output_data = common::generate_random_data(32);
        let mock_provider = Box::new(MockProvider::new());
        mock_provider.set_result(&test_key_data);
        mock_provider.set_key_result(Ok(test_output_data.clone()));
        let test_key =
            KmsAsymmetricKey::new(&*mock_provider, TEST_KEY_NAME, key_algorithm).unwrap();

        let public_key = test_key.get_der_public_key().unwrap();
        // The key data should be the output data from generate_key call.
        assert_eq!(test_key.get_key_data(), test_key_data);
        assert_eq!(public_key, test_output_data);
    }
}
