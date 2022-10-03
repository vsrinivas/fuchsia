// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod boringssl_wrapper;
mod keysafe;

use self::boringssl_wrapper::*;
use self::keysafe::*;
use crate::crypto_provider::{
    AsymmetricProviderKey, CryptoProvider, CryptoProviderError, ProviderKey, SealingProviderKey,
};
use boringssl_sys::{NID_X9_62_prime256v1, NID_secp384r1, NID_secp521r1};
use byteorder::{ByteOrder, LittleEndian};
use fidl_fuchsia_kms::{AsymmetricKeyAlgorithm, KeyProvider};
use tee::*;

/// We use 1024 for default output buffer size since all outputs from Keysafe is
/// currently less than 1024 bytes.
const OUTPUT_BUFFER_SIZE: usize = 1024;

/// OpteeProvider is a CryptoProvider that talks with Keysafe Trusted App in OPTEE.
///
/// It wraps the key blob with a sealing key that is only known to the Keysafe App in OPTEE so that
/// the key material itself would never be exposed to rich OS side in plain text. OPTEE is
/// responsible for all crypto operations that is supported by this provider, thus this provider
/// provides a stronger protection for the keys than a software provider.
#[derive(Debug)]
pub struct OpteeProvider {}

#[derive(Debug)]
pub struct OpteeAsymmetricPrivateKey {
    key_data: Vec<u8>,
    key_algorithm: AsymmetricKeyAlgorithm,
}

impl ProviderKey for OpteeAsymmetricPrivateKey {
    fn delete(&mut self) -> Result<(), CryptoProviderError> {
        Ok(())
    }
    fn get_key_data(&self) -> Vec<u8> {
        self.key_data.clone()
    }
    fn get_key_provider(&self) -> KeyProvider {
        (OpteeProvider {}).get_name()
    }
}

#[derive(Debug)]
pub struct OpteeSealingKey {
    key_data: Vec<u8>,
}

impl ProviderKey for OpteeSealingKey {
    fn delete(&mut self) -> Result<(), CryptoProviderError> {
        Ok(())
    }
    fn get_key_data(&self) -> Vec<u8> {
        self.key_data.clone()
    }
    fn get_key_provider(&self) -> KeyProvider {
        (OpteeProvider {}).get_name()
    }
}

fn map_command_error(error_code: u32) -> CryptoProviderError {
    CryptoProviderError::new(&format!("Operation error, error code: {:}", error_code))
}

fn map_openssl_error(error_status: &'static str) -> CryptoProviderError {
    CryptoProviderError::new(error_status)
}

impl CryptoProvider for OpteeProvider {
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
        KeyProvider::OpteeProvider
    }
    fn box_clone(&self) -> Box<dyn CryptoProvider> {
        Box::new(OpteeProvider {})
    }
    fn generate_asymmetric_key(
        &self,
        key_algorithm: AsymmetricKeyAlgorithm,
        key_name: &str,
    ) -> Result<Box<dyn AsymmetricProviderKey>, CryptoProviderError> {
        let ta_algorithm = translate_algorithm(key_algorithm);
        let key_data = optee_generate_key(ta_algorithm, key_name)?;
        Ok(Box::new(OpteeAsymmetricPrivateKey { key_data, key_algorithm }))
    }
    fn import_asymmetric_key(
        &self,
        key_data: &[u8],
        key_algorithm: AsymmetricKeyAlgorithm,
        key_name: &str,
    ) -> Result<Box<dyn AsymmetricProviderKey>, CryptoProviderError> {
        let imported_key_data = optee_import_key(key_algorithm, key_data, key_name)?;
        Ok(Box::new(OpteeAsymmetricPrivateKey { key_data: imported_key_data, key_algorithm }))
    }
    fn parse_asymmetric_key(
        &self,
        key_data: &[u8],
        key_algorithm: AsymmetricKeyAlgorithm,
    ) -> Result<Box<dyn AsymmetricProviderKey>, CryptoProviderError> {
        let ta_algorithm = translate_algorithm(key_algorithm);
        optee_parse_key(key_data, ta_algorithm)?;
        Ok(Box::new(OpteeAsymmetricPrivateKey { key_data: key_data.to_vec(), key_algorithm }))
    }
    fn generate_sealing_key(
        &self,
        key_name: &str,
    ) -> Result<Box<dyn SealingProviderKey>, CryptoProviderError> {
        let ta_algorithm = TA_KEYSAFE_ALG_AES_GCM_256;
        let key_data = optee_generate_key(ta_algorithm, key_name)?;
        Ok(Box::new(OpteeSealingKey { key_data }))
    }
    fn parse_sealing_key(
        &self,
        key_data: &[u8],
    ) -> Result<Box<dyn SealingProviderKey>, CryptoProviderError> {
        let ta_algorithm = TA_KEYSAFE_ALG_AES_GCM_256;
        optee_parse_key(key_data, ta_algorithm)?;
        Ok(Box::new(OpteeSealingKey { key_data: key_data.to_vec() }))
    }

    fn calculate_sealed_data_size(
        &self,
        original_data_size: u64,
    ) -> Result<u64, CryptoProviderError> {
        // overhead is GCM IV size + GCM Tag size.
        let overhead: u64 = 12 + 16;
        original_data_size
            .checked_add(overhead)
            .ok_or(CryptoProviderError::new("original data size too large, overflow"))
    }
}

fn translate_algorithm(key_algorithm: AsymmetricKeyAlgorithm) -> u32 {
    match key_algorithm {
        AsymmetricKeyAlgorithm::RsaSsaPssSha2562048 => TA_KEYSAFE_ALG_RSA_SSA_PSS_SHA256_2048,
        AsymmetricKeyAlgorithm::RsaSsaPssSha2563072 => TA_KEYSAFE_ALG_RSA_SSA_PSS_SHA256_3072,
        AsymmetricKeyAlgorithm::RsaSsaPssSha5124096 => TA_KEYSAFE_ALG_RSA_SSA_PSS_SHA512_4096,
        AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2562048 => TA_KEYSAFE_ALG_RSA_SSA_PKCS1_SHA256_2048,
        AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha2563072 => TA_KEYSAFE_ALG_RSA_SSA_PKCS1_SHA256_3072,
        AsymmetricKeyAlgorithm::RsaSsaPkcs1Sha5124096 => TA_KEYSAFE_ALG_RSA_SSA_PKCS1_SHA512_4096,
        AsymmetricKeyAlgorithm::EcdsaSha256P256 => TA_KEYSAFE_ALG_ECDSA_SHA256_P256,
        AsymmetricKeyAlgorithm::EcdsaSha512P384 => TA_KEYSAFE_ALG_ECDSA_SHA512_P384,
        AsymmetricKeyAlgorithm::EcdsaSha512P521 => TA_KEYSAFE_ALG_ECDSA_SHA512_P521,
    }
}

fn get_ec_curve_nid(key_algorithm: AsymmetricKeyAlgorithm) -> i32 {
    match key_algorithm {
        AsymmetricKeyAlgorithm::EcdsaSha256P256 => NID_X9_62_prime256v1 as i32,
        AsymmetricKeyAlgorithm::EcdsaSha512P384 => NID_secp384r1 as i32,
        AsymmetricKeyAlgorithm::EcdsaSha512P521 => NID_secp521r1 as i32,
        _ => unreachable!(),
    }
}

/// This is the same macro definition as TEEC_PARAM_TYPES in tee-client-types.h
fn teec_param_types(param0_type: u32, param1_type: u32, param2_type: u32, param3_type: u32) -> u32 {
    ((param0_type & 0xF) << 0)
        | ((param1_type & 0xF) << 4)
        | ((param2_type & 0xF) << 8)
        | ((param3_type & 0xF) << 12)
}

/// Uses Keysafe TA in OPTEE to parse a key blob previously returned by Keysafe.
///
/// Issues TA_KEYSAFE_CMD_PARSE_KEY command.CryptoProviderError
///
/// Params should be:
/// * algorithm as value input.
/// * key_name as memref input.
/// * unused
/// * unused.
fn optee_parse_key(key_data: &[u8], ta_algorithm: u32) -> Result<(), CryptoProviderError> {
    let param_type =
        teec_param_types(TEEC_VALUE_INPUT, TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE);
    let params = [
        get_value_parameter(ta_algorithm, 0),
        get_memref_input_parameter(key_data),
        get_zero_parameter(),
        get_zero_parameter(),
    ];
    let mut op = create_operation(param_type, params);
    call_command(&mut op, TA_KEYSAFE_CMD_PARSE_KEY).map_err(map_command_error)
}

/// Uses Keysafe TA in OPTEE to generate key.
///
/// Issues TA_KEYSAFE_CMD_GENERATE_KEY command.
///
/// Params should be:
/// * algorithm as value input.
/// * key_name as memref input.
/// * unused
/// * output key data as memref output.
fn optee_generate_key(ta_algorithm: u32, key_name: &str) -> Result<Vec<u8>, CryptoProviderError> {
    let key_name_bytes = key_name.as_bytes();
    let mut output_buffer = vec![0; OUTPUT_BUFFER_SIZE];

    let param_type = teec_param_types(
        TEEC_VALUE_INPUT,
        TEEC_MEMREF_TEMP_INPUT,
        TEEC_NONE,
        TEEC_MEMREF_TEMP_OUTPUT,
    );
    let params = [
        get_value_parameter(ta_algorithm, 0),
        get_memref_input_parameter(&key_name_bytes),
        get_zero_parameter(),
        get_memref_output_parameter(&mut output_buffer),
    ];
    let mut op = create_operation(param_type, params);
    call_command(&mut op, TA_KEYSAFE_CMD_GENERATE_KEY).map_err(map_command_error)?;
    let output_size = unsafe { op.params[3].tmpref.size } as usize;
    if output_size > OUTPUT_BUFFER_SIZE {
        return Err(CryptoProviderError::new("invalid returned output buffer size"));
    }
    output_buffer.truncate(output_size);
    Ok(output_buffer)
}

/// Import key and uses OPTEE Keysafe TA to create a key object.
///
/// Parses the key data and read key information out.
///
/// * public_key_x, public_key_y, private_key for ECDSA
/// * public_exponent, private_exponent, modulus for RSA
///
/// Encode the key information and issues TA_KEYSAFE_CMD_IMPORT_KEY command. We have to encode
/// key information because OPTEE could not parse ASN1 format and we do not have enough fields
/// to pass each information separately.
///
/// Params should be:
/// * algorithm as value input.
/// * key_name as memref input.
/// * encoded key data as memref input.
/// * output key data as memref output.
fn optee_import_key(
    key_algorithm: AsymmetricKeyAlgorithm,
    key_data: &[u8],
    key_name: &str,
) -> Result<Vec<u8>, CryptoProviderError> {
    let ta_algorithm = translate_algorithm(key_algorithm);
    let encoded_buffer = {
        if key_algorithm == AsymmetricKeyAlgorithm::EcdsaSha256P256
            || key_algorithm == AsymmetricKeyAlgorithm::EcdsaSha512P384
            || key_algorithm == AsymmetricKeyAlgorithm::EcdsaSha512P521
        {
            let ec_key = EcPrivateKey::new(get_ec_curve_nid(key_algorithm), key_data)
                .map_err(map_openssl_error)?;
            let public_key_x = ec_key.get_public_key_x().map_err(map_openssl_error)?;
            let public_key_y = ec_key.get_public_key_y().map_err(map_openssl_error)?;
            let private_key = ec_key.get_private_key().map_err(map_openssl_error)?;
            let encoded_buffer = encode_private_key(public_key_x, public_key_y, private_key);
            encoded_buffer
        } else {
            let rsa_key = RsaPrivateKey::new(key_data).map_err(map_openssl_error)?;
            let modulus = rsa_key.get_modulus().map_err(map_openssl_error)?;
            let public_exponent = rsa_key.get_public_exponent().map_err(map_openssl_error)?;
            let private_exponent = rsa_key.get_private_exponent().map_err(map_openssl_error)?;
            let encoded_buffer = encode_private_key(modulus, public_exponent, private_exponent);
            encoded_buffer
        }
    };

    let key_name_bytes = key_name.as_bytes();
    let mut output_buffer = vec![0; OUTPUT_BUFFER_SIZE];
    let param_type = teec_param_types(
        TEEC_VALUE_INPUT,
        TEEC_MEMREF_TEMP_INPUT,
        TEEC_MEMREF_TEMP_INPUT,
        TEEC_MEMREF_TEMP_OUTPUT,
    );
    let params = [
        get_value_parameter(ta_algorithm, 0),
        get_memref_input_parameter(&key_name_bytes),
        get_memref_input_parameter(&encoded_buffer),
        get_memref_output_parameter(&mut output_buffer),
    ];
    let mut op = create_operation(param_type, params);

    call_command(&mut op, TA_KEYSAFE_CMD_IMPORT_KEY).map_err(map_command_error)?;

    let output_size = unsafe { op.params[3].tmpref.size } as usize;
    if output_size > OUTPUT_BUFFER_SIZE {
        return Err(CryptoProviderError::new("invalid returned output buffer size"));
    }
    output_buffer.truncate(output_size);
    Ok(output_buffer)
}

/// Encode private key information into one string.
///
/// Because we only have limited fields for input, we need to encode the key information
/// in one field. We use the following format:
///
/// RSA:
/// | public_key_x_size | public_key_x | public_key_y_size | public_key_y | private_key_size |
/// private_key |
///
/// ECDSA:
/// | modulus_size | modulus | public_exponent_size | public_exponent | private_exponent_size |
/// private_exponent |
fn encode_private_key(field_1: Vec<u8>, field_2: Vec<u8>, field_3: Vec<u8>) -> Vec<u8> {
    let mut encoded_buffer: Vec<u8> = vec![];
    let mut length_buffer = [0; 4];
    LittleEndian::write_u32(&mut length_buffer, field_1.len() as u32);
    encoded_buffer.extend_from_slice(&length_buffer);
    encoded_buffer.extend_from_slice(&field_1);
    LittleEndian::write_u32(&mut length_buffer, field_2.len() as u32);
    encoded_buffer.extend_from_slice(&length_buffer);
    encoded_buffer.extend_from_slice(&field_2);
    LittleEndian::write_u32(&mut length_buffer, field_3.len() as u32);
    encoded_buffer.extend_from_slice(&length_buffer);
    encoded_buffer.extend_from_slice(&field_3);
    encoded_buffer
}

impl AsymmetricProviderKey for OpteeAsymmetricPrivateKey {
    /// Issues TA_KEYSAFE_CMD_SIGN command.
    ///
    /// Params should be:
    /// * algorithm as value input.
    /// * key_data as memref input.
    /// * data_to_be_signed as memref input.
    /// * output signature as memref output.
    fn sign(&self, data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        let ta_algorithm = translate_algorithm(self.key_algorithm);
        let mut output_buffer = vec![0; OUTPUT_BUFFER_SIZE];

        let param_type = teec_param_types(
            TEEC_VALUE_INPUT,
            TEEC_MEMREF_TEMP_INPUT,
            TEEC_MEMREF_TEMP_INPUT,
            TEEC_MEMREF_TEMP_OUTPUT,
        );
        let params = [
            get_value_parameter(ta_algorithm, 0),
            get_memref_input_parameter(&self.key_data),
            get_memref_input_parameter(data),
            get_memref_output_parameter(&mut output_buffer),
        ];
        let mut op = create_operation(param_type, params);

        call_command(&mut op, TA_KEYSAFE_CMD_SIGN).map_err(map_command_error)?;

        let output_size = unsafe { op.params[3].tmpref.size } as usize;
        if output_size > OUTPUT_BUFFER_SIZE {
            return Err(CryptoProviderError::new("invalid returned output buffer size"));
        }
        output_buffer.truncate(output_size);
        Ok(output_buffer)
    }

    /// Issues TA_KEYSAFE_CMD_GET_PUBLIC_KEY command.
    ///
    /// Params should be:
    /// * algorithm as value input.
    /// * key_data as memref input.
    /// * first public value as memref output.
    /// * second public value as memref output.
    fn get_der_public_key(&self) -> Result<Vec<u8>, CryptoProviderError> {
        let ta_algorithm = translate_algorithm(self.key_algorithm);
        let mut first_output = [0; OUTPUT_BUFFER_SIZE];
        let mut second_output = [0; OUTPUT_BUFFER_SIZE];
        let param_type = teec_param_types(
            TEEC_VALUE_INPUT,
            TEEC_MEMREF_TEMP_INPUT,
            TEEC_MEMREF_TEMP_OUTPUT,
            TEEC_MEMREF_TEMP_OUTPUT,
        );
        let params = [
            get_value_parameter(ta_algorithm, 0),
            get_memref_input_parameter(&self.key_data),
            get_memref_output_parameter(&mut first_output),
            get_memref_output_parameter(&mut second_output),
        ];
        let mut op = create_operation(param_type, params);

        call_command(&mut op, TA_KEYSAFE_CMD_GET_PUBLIC_KEY).map_err(map_command_error)?;

        if self.key_algorithm == AsymmetricKeyAlgorithm::EcdsaSha256P256
            || self.key_algorithm == AsymmetricKeyAlgorithm::EcdsaSha512P384
            || self.key_algorithm == AsymmetricKeyAlgorithm::EcdsaSha512P521
        {
            let (public_x_size, public_y_size) = unsafe {
                let public_x_size = op.params[2].tmpref.size as usize;
                if public_x_size > OUTPUT_BUFFER_SIZE {
                    return Err(CryptoProviderError::new("invalid returned output buffer size"));
                }
                let public_y_size = op.params[3].tmpref.size as usize;
                if public_y_size > OUTPUT_BUFFER_SIZE {
                    return Err(CryptoProviderError::new("invalid returned output buffer size"));
                }
                Ok((public_x_size, public_y_size))
            }?;
            let mut ec_key = EcPublicKey::new(
                get_ec_curve_nid(self.key_algorithm),
                &first_output[0..public_x_size],
                &second_output[0..public_y_size],
            )
            .map_err(map_openssl_error)?;
            Ok(ec_key.marshal_public_key().map_err(map_openssl_error)?)
        } else {
            let (modulus_size, public_exponent_size) = unsafe {
                let modulus_size = op.params[2].tmpref.size as usize;
                if modulus_size > OUTPUT_BUFFER_SIZE {
                    return Err(CryptoProviderError::new("invalid returned output buffer size"));
                }
                let public_exponent_size = op.params[3].tmpref.size as usize;
                if public_exponent_size > OUTPUT_BUFFER_SIZE {
                    return Err(CryptoProviderError::new("invalid returned output buffer size"));
                }
                (modulus_size, public_exponent_size)
            };
            let mut rsa = RsaPublicKey::new(
                &first_output[0..modulus_size],
                &second_output[0..public_exponent_size],
            )
            .map_err(map_openssl_error)?;
            Ok(rsa.marshal_public_key().map_err(map_openssl_error)?)
        }
    }

    fn get_key_algorithm(&self) -> AsymmetricKeyAlgorithm {
        self.key_algorithm
    }
}

impl SealingProviderKey for OpteeSealingKey {
    fn encrypt(&self, data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        self.encrypt_decrypt(data, true)
    }
    fn decrypt(&self, data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        self.encrypt_decrypt(data, false)
    }
}

impl OpteeSealingKey {
    /// Issues TA_KEYSAFE_CMD_ENCRYPT_DATA / TA_KEYSAFE_CMD_DECRYPT_DATA command.
    ///
    /// Params should be:
    /// * algorithm as value input.
    /// * key_data as memref input.
    /// * data as memref input.
    /// * encrypted/decrypted data as memref output.
    fn encrypt_decrypt(&self, data: &[u8], encrypt: bool) -> Result<Vec<u8>, CryptoProviderError> {
        // IV is 16 byte, tag is 12 byte, 64 byte is large enough to hold them.
        let output_buffer_size = data.len() + 64;
        let mut output_buffer = vec![0; output_buffer_size];
        let param_type = teec_param_types(
            TEEC_VALUE_INPUT,
            TEEC_MEMREF_TEMP_INPUT,
            TEEC_MEMREF_TEMP_INPUT,
            TEEC_MEMREF_TEMP_OUTPUT,
        );
        let params = [
            get_value_parameter(TA_KEYSAFE_ALG_AES_GCM_256, 0),
            get_memref_input_parameter(&self.key_data),
            get_memref_input_parameter(data),
            get_memref_output_parameter(&mut output_buffer),
        ];
        let mut op = create_operation(param_type, params);
        let command_id =
            if encrypt { TA_KEYSAFE_CMD_ENCRYPT_DATA } else { TA_KEYSAFE_CMD_DECRYPT_DATA };

        call_command(&mut op, command_id).map_err(map_command_error)?;

        let output_size = unsafe { op.params[3].tmpref.size } as usize;
        if output_size > output_buffer_size {
            return Err(CryptoProviderError::new("invalid returned output buffer size"));
        }
        output_buffer.truncate(output_size);
        Ok(output_buffer)
    }
}
