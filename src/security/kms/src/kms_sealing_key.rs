// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{self as common, DataRequest, KeyAttributes, KeyRequestType, KeyType, KmsKey};
use crate::crypto_provider::{CryptoProvider, SealingProviderKey};
use fidl_fuchsia_kms::{Error, KeyProvider};
use fidl_fuchsia_mem::Buffer;
use tracing::error;

pub static SEALING_KEY_NAME: &str = ".SealingKey";

/// A sealing key object.
#[derive(Debug)]
pub struct KmsSealingKey {
    provider_key: Box<dyn SealingProviderKey>,
    key_name: String,
}

impl KmsKey for KmsSealingKey {
    fn get_key_name(&self) -> &str {
        &self.key_name
    }

    fn is_deleted(&self) -> bool {
        // Sealing key would never be deleted.
        false
    }

    /// Handles a seal_key/unseal_key request.
    ///
    /// The request type is expected to be SealingKeyRequest or UnsealingKeyRequest. The request is
    /// constructed by KeyManager and is not from user thus it would not contain a channel to send
    /// the request back to user. As a result, this function would never return a FIDL error. The
    /// result for the request is returned through the result pointer in the request object.
    ///
    /// # Panics
    ///
    /// Panics if the request is neither SealingKeyRequest or UnsealingKeyRequest.
    fn handle_request(&self, req: KeyRequestType<'_>) -> Result<(), fidl::Error> {
        if let KeyRequestType::SealingKeyRequest(DataRequest { data, result }) = req {
            *result = self.seal_data(data);
        } else if let KeyRequestType::UnsealingKeyRequest(DataRequest { data, result }) = req {
            *result = self.unseal_data(data);
        } else {
            panic!("Invalid request!");
        }
        Ok(())
    }

    fn get_key_type(&self) -> KeyType {
        KeyType::SealingKey
    }

    fn get_key_provider(&self) -> KeyProvider {
        self.provider_key.get_key_provider()
    }

    fn get_key_data(&self) -> Vec<u8> {
        self.provider_key.get_key_data()
    }

    fn delete(&mut self) -> Result<(), Error> {
        panic!("Sealing key should never be deleted!")
    }
}

impl KmsSealingKey {
    /// Generates a new sealing key object.
    pub fn new(provider: &dyn CryptoProvider) -> Result<Self, Error> {
        let provider_key = provider.generate_sealing_key(SEALING_KEY_NAME).map_err(
            debug_err_fn!(Error::InternalError, "Failed to generate sealing key, err: {:?}."),
        )?;
        Ok(KmsSealingKey { provider_key, key_name: SEALING_KEY_NAME.to_string() })
    }

    /// Parse the key data and generate a new KmsSealingKey object.
    ///
    /// # Arguments
    ///
    /// * `key_name` - The name for the new key.
    /// * `key_attributes` - The attributes for the new key.
    pub fn parse_key(key_name: &str, key_attributes: KeyAttributes<'_>) -> Result<Self, Error> {
        if key_attributes.key_type != KeyType::SealingKey {
            // The key is a different type. This should not happen unless the key file is corrupted.
            error!("The sealing key file is corrupted!");
            return Err(Error::InternalError);
        }

        let provider_key = key_attributes
            .provider
            .parse_sealing_key(&key_attributes.key_data)
            .map_err(debug_err_fn!(Error::ParseKeyError, "Failed to parse sealing key: {:?}."))?;
        Ok(KmsSealingKey { provider_key, key_name: key_name.to_string() })
    }

    /// Use the key to seal data.
    ///
    /// #  Arguments:
    ///
    /// * `buffer` - The vmo buffer containing the data to be sealed.
    fn seal_data(&self, buffer: Buffer) -> Result<Buffer, Error> {
        let input = common::buffer_to_data(buffer)?;
        let output = self
            .provider_key
            .encrypt(&input)
            .map_err(debug_err_fn!(Error::InternalError, "Failed to encrypt data: {:?}"))?;
        Ok(common::data_to_buffer(&output)?)
    }

    /// Use the key to unseal data.
    ///
    /// #  Arguments:
    ///
    /// * `buffer` - The vmo buffer containing the data to be unsealed.
    fn unseal_data(&self, buffer: Buffer) -> Result<Buffer, Error> {
        let input = common::buffer_to_data(buffer)?;
        let output = self
            .provider_key
            .decrypt(&input)
            .map_err(debug_err_fn!(Error::InternalError, "Failed to decrypt data: {:?}"))?;
        Ok(common::data_to_buffer(&output)?)
    }
}
