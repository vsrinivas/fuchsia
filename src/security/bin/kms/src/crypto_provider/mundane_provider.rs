// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::crypto_provider::{
    AsymmetricProviderKey, CryptoProvider, CryptoProviderError, ProviderKey, SealingProviderKey,
};
use fidl_fuchsia_kms::{AsymmetricKeyAlgorithm, KeyProvider};
use mundane::hash::*;
use mundane::public::ec::ecdsa::EcdsaHash;
use mundane::public::ec::*;
use mundane::public::rsa::*;
use mundane::public::*;

/// MundaneSoftwareProvider is a software based provider that only supports asymmetric operations.
///
/// MundaneSoftwareProvider uses mundane for asymmetric crypto operations. The key_data is the key
/// material in plain text so its security depends on the security of the KMS module.
#[derive(Debug, Clone)]
pub struct MundaneSoftwareProvider {}

#[derive(Debug)]
struct MundaneAsymmetricPrivateKey {
    key_data: Vec<u8>,
    key_algorithm: AsymmetricKeyAlgorithm,
}

impl CryptoProvider for MundaneSoftwareProvider {
    fn supported_asymmetric_algorithms(&self) -> &'static [AsymmetricKeyAlgorithm] {
        &[
            AsymmetricKeyAlgorithm::EcdsaSha256P256,
            AsymmetricKeyAlgorithm::EcdsaSha512P384,
            AsymmetricKeyAlgorithm::EcdsaSha512P521,
            AsymmetricKeyAlgorithm::RsaSsaPssSha2562048,
            AsymmetricKeyAlgorithm::RsaSsaPssSha2563072,
            AsymmetricKeyAlgorithm::RsaSsaPssSha5124096,
            AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2562048,
            AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2563072,
            AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha5124096,
        ]
    }
    fn get_name(&self) -> KeyProvider {
        KeyProvider::SoftwareAsymmetricOnlyProvider
    }
    fn box_clone(&self) -> Box<dyn CryptoProvider> {
        Box::new(MundaneSoftwareProvider {})
    }
    fn generate_asymmetric_key(
        &self,
        key_algorithm: AsymmetricKeyAlgorithm,
        _key_name: &str,
    ) -> Result<Box<dyn AsymmetricProviderKey>, CryptoProviderError> {
        let key_data = match key_algorithm {
            AsymmetricKeyAlgorithm::EcdsaSha256P256 => generate_ec_key::<P256>(),
            AsymmetricKeyAlgorithm::EcdsaSha512P384 => generate_ec_key::<P384>(),
            AsymmetricKeyAlgorithm::EcdsaSha512P521 => generate_ec_key::<P521>(),
            AsymmetricKeyAlgorithm::RsaSsaPssSha2562048
            | AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2562048 => generate_rsa_key::<B2048>(),
            AsymmetricKeyAlgorithm::RsaSsaPssSha2563072
            | AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2563072 => generate_rsa_key::<B3072>(),
            AsymmetricKeyAlgorithm::RsaSsaPssSha5124096
            | AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha5124096 => generate_rsa_key::<B4096>(),
        }?;
        Ok(Box::new(MundaneAsymmetricPrivateKey { key_data, key_algorithm }))
    }
    fn import_asymmetric_key(
        &self,
        key_data: &[u8],
        key_algorithm: AsymmetricKeyAlgorithm,
        _key_name: &str,
    ) -> Result<Box<dyn AsymmetricProviderKey>, CryptoProviderError> {
        self.parse_asymmetric_key(key_data, key_algorithm)
    }

    fn parse_asymmetric_key(
        &self,
        key_data: &[u8],
        key_algorithm: AsymmetricKeyAlgorithm,
    ) -> Result<Box<dyn AsymmetricProviderKey>, CryptoProviderError> {
        match key_algorithm {
            AsymmetricKeyAlgorithm::EcdsaSha256P256 => {
                let _ec_key =
                    EcPrivKey::<P256>::parse_from_der(key_data).map_err(map_operation_error)?;
            }
            AsymmetricKeyAlgorithm::EcdsaSha512P384 => {
                let _ec_key =
                    EcPrivKey::<P384>::parse_from_der(key_data).map_err(map_operation_error)?;
            }
            AsymmetricKeyAlgorithm::EcdsaSha512P521 => {
                let _ec_key =
                    EcPrivKey::<P521>::parse_from_der(key_data).map_err(map_operation_error)?;
            }
            AsymmetricKeyAlgorithm::RsaSsaPssSha2562048
            | AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2562048 => {
                let _rsa_key =
                    RsaPrivKey::<B2048>::parse_from_der(key_data).map_err(map_operation_error)?;
            }
            AsymmetricKeyAlgorithm::RsaSsaPssSha2563072
            | AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2563072 => {
                let _rsa_key =
                    RsaPrivKey::<B3072>::parse_from_der(key_data).map_err(map_operation_error)?;
            }
            AsymmetricKeyAlgorithm::RsaSsaPssSha5124096
            | AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha5124096 => {
                let _rsa_key =
                    RsaPrivKey::<B4096>::parse_from_der(key_data).map_err(map_operation_error)?;
            }
        }
        Ok(Box::new(MundaneAsymmetricPrivateKey { key_data: key_data.to_vec(), key_algorithm }))
    }
    fn generate_sealing_key(
        &self,
        _key_name: &str,
    ) -> Result<Box<dyn SealingProviderKey>, CryptoProviderError> {
        Err(CryptoProviderError::new("Unsupported algorithm."))
    }
    fn parse_sealing_key(
        &self,
        _key_data: &[u8],
    ) -> Result<Box<dyn SealingProviderKey>, CryptoProviderError> {
        Err(CryptoProviderError::new("Unsupported algorithm."))
    }

    fn calculate_sealed_data_size(
        &self,
        _original_data_size: u64,
    ) -> Result<u64, CryptoProviderError> {
        Ok(0)
    }
}

impl AsymmetricProviderKey for MundaneAsymmetricPrivateKey {
    fn sign(&self, data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        match self.key_algorithm {
            AsymmetricKeyAlgorithm::EcdsaSha256P256 => {
                sign_with_ec_key::<P256, Sha256>(&self.key_data, data)
            }
            AsymmetricKeyAlgorithm::EcdsaSha512P384 => {
                sign_with_ec_key::<P384, Sha512>(&self.key_data, data)
            }
            AsymmetricKeyAlgorithm::EcdsaSha512P521 => {
                sign_with_ec_key::<P521, Sha512>(&self.key_data, data)
            }
            AsymmetricKeyAlgorithm::RsaSsaPssSha2562048 => {
                sign_with_rsa_key::<B2048, RsaPss, Sha256>(&self.key_data, data)
            }
            AsymmetricKeyAlgorithm::RsaSsaPssSha2563072 => {
                sign_with_rsa_key::<B3072, RsaPss, Sha256>(&self.key_data, data)
            }
            AsymmetricKeyAlgorithm::RsaSsaPssSha5124096 => {
                sign_with_rsa_key::<B4096, RsaPss, Sha512>(&self.key_data, data)
            }
            AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2562048 => {
                sign_with_rsa_key::<B2048, RsaPkcs1v15, Sha256>(&self.key_data, data)
            }
            AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2563072 => {
                sign_with_rsa_key::<B3072, RsaPkcs1v15, Sha256>(&self.key_data, data)
            }
            AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha5124096 => {
                sign_with_rsa_key::<B4096, RsaPkcs1v15, Sha512>(&self.key_data, data)
            }
        }
    }

    fn get_der_public_key(&self) -> Result<Vec<u8>, CryptoProviderError> {
        match self.key_algorithm {
            AsymmetricKeyAlgorithm::EcdsaSha256P256 => {
                marshal_ec_key_to_der::<P256>(&self.key_data)
            }
            AsymmetricKeyAlgorithm::EcdsaSha512P384 => {
                marshal_ec_key_to_der::<P384>(&self.key_data)
            }
            AsymmetricKeyAlgorithm::EcdsaSha512P521 => {
                marshal_ec_key_to_der::<P521>(&self.key_data)
            }
            AsymmetricKeyAlgorithm::RsaSsaPssSha2562048
            | AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2562048 => {
                marshal_rsa_key_to_der::<B2048>(&self.key_data)
            }
            AsymmetricKeyAlgorithm::RsaSsaPssSha2563072
            | AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2563072 => {
                marshal_rsa_key_to_der::<B3072>(&self.key_data)
            }
            AsymmetricKeyAlgorithm::RsaSsaPssSha5124096
            | AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha5124096 => {
                marshal_rsa_key_to_der::<B4096>(&self.key_data)
            }
        }
    }

    fn get_key_algorithm(&self) -> AsymmetricKeyAlgorithm {
        self.key_algorithm
    }
}

fn generate_ec_key<C: PCurve>() -> Result<Vec<u8>, CryptoProviderError> {
    let ec_key = EcPrivKey::<C>::generate().map_err(map_operation_error)?;
    Ok(ec_key.marshal_to_der())
}

fn generate_rsa_key<B: RsaKeyBits>() -> Result<Vec<u8>, CryptoProviderError> {
    let rsa_key = RsaPrivKey::<B>::generate().map_err(map_operation_error)?;
    Ok(rsa_key.marshal_to_der())
}

fn sign_with_ec_key<C: PCurve, H: Hasher + EcdsaHash<C>>(
    key_data: &[u8],
    data: &[u8],
) -> Result<Vec<u8>, CryptoProviderError> {
    let ec_key = EcPrivKey::<C>::parse_from_der(key_data).map_err(map_operation_error)?;
    let sig: ecdsa::EcdsaSignature<C, H> = ec_key.sign(data).map_err(map_operation_error)?;
    Ok(sig.bytes().to_vec())
}

fn sign_with_rsa_key<B: RsaKeyBits, S: RsaSignatureScheme, H: Hasher>(
    key_data: &[u8],
    data: &[u8],
) -> Result<Vec<u8>, CryptoProviderError> {
    let rsa_key = RsaPrivKey::<B>::parse_from_der(key_data).map_err(map_operation_error)?;
    let sig: rsa::RsaSignature<B, S, H> = rsa_key.sign(data).map_err(map_operation_error)?;
    Ok(sig.bytes().to_vec())
}

fn marshal_ec_key_to_der<C: PCurve>(key_data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
    let ec_key = EcPrivKey::<C>::parse_from_der(key_data).map_err(map_operation_error)?;
    Ok(ec_key.public().marshal_to_der())
}

fn marshal_rsa_key_to_der<B: RsaKeyBits>(key_data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
    let rsa_key = RsaPrivKey::<B>::parse_from_der(key_data).map_err(map_operation_error)?;
    Ok(rsa_key.public().marshal_to_der())
}

fn map_operation_error(err: mundane::Error) -> CryptoProviderError {
    CryptoProviderError::new(&format!("Operation error: {:?}.", err))
}

impl ProviderKey for MundaneAsymmetricPrivateKey {
    fn delete(&mut self) -> Result<(), CryptoProviderError> {
        self.key_data.clear();
        Ok(())
    }
    /// Get the data for the key.
    fn get_key_data(&self) -> Vec<u8> {
        self.key_data.clone()
    }
    /// Get the crypto provider for the key.
    fn get_key_provider(&self) -> KeyProvider {
        (MundaneSoftwareProvider {}).get_name()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common;
    static TEST_KEY_NAME: &str = "TestKey";

    #[test]
    fn test_mundane_provider_sign() {
        // Right now only this algorithm is supported.
        test_mundane_provider_sign_ec_key::<P256, Sha256>(AsymmetricKeyAlgorithm::EcdsaSha256P256);
        test_mundane_provider_sign_ec_key::<P384, Sha512>(AsymmetricKeyAlgorithm::EcdsaSha512P384);
        test_mundane_provider_sign_ec_key::<P521, Sha512>(AsymmetricKeyAlgorithm::EcdsaSha512P521);
        test_mundane_provider_sign_rsa_key::<B2048, RsaPss, Sha256>(
            AsymmetricKeyAlgorithm::RsaSsaPssSha2562048,
        );
        test_mundane_provider_sign_rsa_key::<B3072, RsaPss, Sha256>(
            AsymmetricKeyAlgorithm::RsaSsaPssSha2563072,
        );
        test_mundane_provider_sign_rsa_key::<B4096, RsaPss, Sha512>(
            AsymmetricKeyAlgorithm::RsaSsaPssSha5124096,
        );
        test_mundane_provider_sign_rsa_key::<B2048, RsaPkcs1v15, Sha256>(
            AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2562048,
        );
        test_mundane_provider_sign_rsa_key::<B3072, RsaPkcs1v15, Sha256>(
            AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2563072,
        );
        test_mundane_provider_sign_rsa_key::<B4096, RsaPkcs1v15, Sha512>(
            AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha5124096,
        );
    }

    fn test_mundane_provider_sign_ec_key<C: PCurve, H: Hasher + EcdsaHash<C>>(
        key_algorithm: AsymmetricKeyAlgorithm,
    ) {
        let mundane_provider = MundaneSoftwareProvider {};
        let key = mundane_provider.generate_asymmetric_key(key_algorithm, TEST_KEY_NAME).unwrap();
        let test_input_data = common::generate_random_data(256);
        let signature = key.sign(&test_input_data).unwrap();
        let public_key = key.get_der_public_key().unwrap();
        let ec_key = EcPubKey::<C>::parse_from_der(&public_key).unwrap();
        assert_eq!(
            true,
            ecdsa::EcdsaSignature::<C, H>::from_bytes(&signature)
                .is_valid(&ec_key, &test_input_data)
        );
    }

    fn test_mundane_provider_sign_rsa_key<B: RsaKeyBits, S: RsaSignatureScheme, H: Hasher>(
        key_algorithm: AsymmetricKeyAlgorithm,
    ) {
        let mundane_provider = MundaneSoftwareProvider {};
        let key = mundane_provider.generate_asymmetric_key(key_algorithm, TEST_KEY_NAME).unwrap();
        let test_input_data = common::generate_random_data(256);
        let signature = key.sign(&test_input_data).unwrap();
        let public_key = key.get_der_public_key().unwrap();
        let rsa_key = RsaPubKey::<B>::parse_from_der(&public_key).unwrap();
        assert_eq!(
            true,
            rsa::RsaSignature::<B, S, H>::from_bytes(&signature)
                .is_valid(&rsa_key, &test_input_data)
        );
    }

    #[test]
    fn test_mundane_provider_parse_key() {
        test_mundane_provider_parse_ec_key::<P256>(AsymmetricKeyAlgorithm::EcdsaSha256P256);
        test_mundane_provider_parse_ec_key::<P384>(AsymmetricKeyAlgorithm::EcdsaSha512P384);
        test_mundane_provider_parse_ec_key::<P521>(AsymmetricKeyAlgorithm::EcdsaSha512P521);
        test_mundane_provider_parse_rsa_key::<B2048>(AsymmetricKeyAlgorithm::RsaSsaPssSha2562048);
        test_mundane_provider_parse_rsa_key::<B2048>(AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2562048);
        test_mundane_provider_parse_rsa_key::<B3072>(AsymmetricKeyAlgorithm::RsaSsaPssSha2563072);
        test_mundane_provider_parse_rsa_key::<B3072>(AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2563072);
        test_mundane_provider_parse_rsa_key::<B4096>(AsymmetricKeyAlgorithm::RsaSsaPssSha5124096);
        test_mundane_provider_parse_rsa_key::<B4096>(AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha5124096);
    }

    fn test_mundane_provider_parse_ec_key<C: PCurve>(key_algorithm: AsymmetricKeyAlgorithm) {
        let mundane_provider = MundaneSoftwareProvider {};
        let ec_key = EcPrivKey::<C>::generate().unwrap();
        let ec_key_data = ec_key.marshal_to_der();
        let asymmetric_key =
            mundane_provider.parse_asymmetric_key(&ec_key_data, key_algorithm).unwrap();
        assert_eq!(ec_key_data, asymmetric_key.get_key_data());
    }

    fn test_mundane_provider_parse_rsa_key<B: RsaKeyBits>(key_algorithm: AsymmetricKeyAlgorithm) {
        let mundane_provider = MundaneSoftwareProvider {};
        let rsa_key = RsaPrivKey::<B>::generate().unwrap();
        let rsa_key_data = rsa_key.marshal_to_der();
        let asymmetric_key =
            mundane_provider.parse_asymmetric_key(&rsa_key_data, key_algorithm).unwrap();
        assert_eq!(rsa_key_data, asymmetric_key.get_key_data());
    }
}
