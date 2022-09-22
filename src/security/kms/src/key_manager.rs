// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{DataRequest, KeyAttributes, KeyRequestType, KeyType, KmsKey};
use crate::crypto_provider::{
    optee_provider::OpteeProvider, software_provider::SoftwareProvider, CryptoProvider,
};
use crate::kms_asymmetric_key::KmsAsymmetricKey;
use crate::kms_sealing_key::{KmsSealingKey, SEALING_KEY_NAME};
use anyhow::format_err;
use base64;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_kms::{
    AsymmetricKeyAlgorithm, AsymmetricPrivateKeyMarker, Error, KeyManagerRequest, KeyOrigin,
    KeyProvider, MAX_DATA_SIZE,
};
use fidl_fuchsia_mem::Buffer;
use fuchsia_async as fasync;
use futures::prelude::*;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::io::{Error as IOError, ErrorKind};
use std::path::{Path, PathBuf};
use std::str;
use std::sync::{Arc, Mutex, RwLock};
use tracing::{error, warn};

const DEFAULT_PROVIDER: KeyProvider = KeyProvider::SoftwareProvider;
const KEY_FOLDER: &str = "/data/kms";
const USER_KEY_SUBFOLDER: &str = "user";
const INTERNAL_KEY_SUBFOLDER: &str = "internal";

#[derive(Serialize, Deserialize)]
struct KeyAttributesJson {
    pub key_algorithm: u32,
    pub key_type: KeyType,
    pub key_origin: u32,
    pub key_provider: u32,
    pub key_data: Vec<u8>,
}

/// Manages a key object map, delegates operation to key object and manages storage for key data.
///
/// KeyManager manages a key_name -> key_object map and delegates operations to key objects. When a
/// key creates, the key manager insert the key into the key map. When getting a key handle, the key
/// manager return the key object from key map. When deleting a key, the key is removed from key map
///, note that the key object itself is still alive as long as the handle is still alive, however,
/// the object would be mark deleted and any following request on that handle should return a
/// failure.
///
/// KeyManager also manages the storage for key data and key attributes.
pub struct KeyManager {
    /// A map of key_name -> key_object to store all the user keys.
    user_key_map: Arc<Mutex<HashMap<String, Arc<Mutex<dyn KmsKey>>>>>,
    /// A map of key_name -> key_object to store all the KMS internally managed keys. These keys are
    /// stored under a different folder than the user keys, thus we need a separate map to manage
    /// them in memory to be consistent.
    internal_key_map: Arc<Mutex<HashMap<String, Arc<Mutex<dyn KmsKey>>>>>,
    /// All the available crypto providers.
    crypto_provider_map: RwLock<HashMap<KeyProvider, Box<dyn CryptoProvider>>>,
    /// The path to the key folder to store key data and attributes.
    key_folder: String,
    provider: KeyProvider,
}

impl KeyManager {
    pub fn new() -> Self {
        let mut key_manager = KeyManager {
            user_key_map: Arc::new(Mutex::new(HashMap::new())),
            internal_key_map: Arc::new(Mutex::new(HashMap::new())),
            crypto_provider_map: RwLock::new(HashMap::new()),
            key_folder: KEY_FOLDER.to_string(),
            provider: DEFAULT_PROVIDER,
        };

        key_manager.add_provider(Box::new(SoftwareProvider::new()));
        key_manager.add_provider(Box::new(OpteeProvider {}));

        key_manager
    }

    pub fn set_provider(&mut self, crypto_provider: KeyProvider) -> Result<(), anyhow::Error> {
        if !self.crypto_provider_map.read().unwrap().contains_key(&crypto_provider) {
            return Err(format_err!("Invalid crypto provider name"));
        }
        self.provider = crypto_provider;
        Ok(())
    }

    #[allow(dead_code)]
    #[cfg(test)]
    pub fn set_key_folder(&mut self, key_folder: &str) {
        self.key_folder = key_folder.to_string();
    }

    pub fn handle_request(&self, req: KeyManagerRequest) -> Result<(), fidl::Error> {
        match req {
            KeyManagerRequest::GenerateAsymmetricKey { key_name, key, responder } => {
                self.with_provider(self.provider, |provider| {
                    // Default algorithm for asymmetric key is ECDSA-SHA512-P521.
                    responder.send(&mut self.generate_asymmetric_key_and_bind(
                        &key_name,
                        key,
                        AsymmetricKeyAlgorithm::EcdsaSha512P521,
                        provider.unwrap(),
                    ))
                })
            }
            KeyManagerRequest::GenerateAsymmetricKeyWithAlgorithm {
                key_name,
                key_algorithm,
                key,
                responder,
            } => self.with_provider(self.provider, |provider| {
                responder.send(&mut self.generate_asymmetric_key_and_bind(
                    &key_name,
                    key,
                    key_algorithm,
                    provider.unwrap(),
                ))
            }),
            KeyManagerRequest::GetAsymmetricPrivateKey { key_name, key, responder } => {
                responder.send(&mut self.get_asymmetric_private_key_and_bind(&key_name, key))
            }
            KeyManagerRequest::ImportAsymmetricPrivateKey {
                data,
                key_name,
                key_algorithm,
                key,
                responder,
            } => self.with_provider(self.provider, |provider| {
                responder.send(&mut self.import_asymmetric_private_key_and_bind(
                    &data,
                    &key_name,
                    key_algorithm,
                    key,
                    provider.unwrap(),
                ))
            }),
            KeyManagerRequest::SealData { plain_text, responder } => self
                .with_provider(self.provider, |provider| {
                    responder.send(&mut self.seal_data(plain_text, provider.unwrap()))
                }),
            KeyManagerRequest::UnsealData { cipher_text, responder } => self
                .with_provider(self.provider, |provider| {
                    responder.send(&mut self.unseal_data(cipher_text, provider.unwrap()))
                }),
            KeyManagerRequest::DeleteKey { key_name, responder } => {
                responder.send(&mut self.delete_key(&key_name))
            }
        }
    }

    /// Bind an in_memory asymmetric key object to a channel initiated by user.
    ///
    /// # Arguments
    ///
    /// * `key_name` - The name for the key to bind.
    /// * `key_to_bind` - The in memory key object to bind.
    /// * `key` - The server end of the user channel to bind the key to.
    fn bind_asymmetric_key_to_server(
        &self,
        key_name: &str,
        key_to_bind: Arc<Mutex<dyn KmsKey>>,
        key: ServerEnd<AsymmetricPrivateKeyMarker>,
    ) -> Result<(), Error> {
        let mut request_stream = key.into_stream().map_err(debug_err_fn!(
            Error::InternalError,
            "Error creating AsymmetricKey request stream {:?}"
        ))?;
        // Need to clone the key_name to be move into the async function.
        let key_name = String::from(key_name);
        // Copy the key map into the async function.
        let key_map_ref = Arc::clone(&self.user_key_map);
        fasync::Task::local(
            // Spawn async job to handle requests.
            async move {
                while let Some(r) = request_stream.try_next().await? {
                    key_to_bind
                        .lock()
                        .unwrap()
                        .handle_request(KeyRequestType::AsymmetricPrivateKeyRequest(r))?;
                }
                Ok(())
            }
            .and_then(|_| async move {
                Self::clean_up(key_map_ref, &key_name);
                Ok(())
            })
            .unwrap_or_else(|e: fidl::Error| error!("Error running AsymmetricKey {:?}", e)),
        )
        .detach();
        Ok(())
    }

    /// Clean up the in memory key object if it no longer required.
    ///
    /// The key is no longer used. If it is only referenced in the key_map, remove it. This would
    // free the cached key data.
    ///
    /// # Arguments
    ///
    /// * `key_map` - The reference to the key object map.
    /// * `key_name` - The name for the key that is no longer required.
    fn clean_up(key_map: Arc<Mutex<HashMap<String, Arc<Mutex<dyn KmsKey>>>>>, key_name: &str) {
        let mut key_map = key_map.lock().unwrap();
        if key_map.contains_key(key_name) {
            let key_only_referenced_in_map = {
                let key = &key_map[key_name];
                Arc::strong_count(key) == 1
            };
            if key_only_referenced_in_map {
                key_map.remove(key_name);
            }
        }
    }

    /// Generate an asymmetric key object and bind the generated key to the user channel.
    ///
    /// # Arguments
    ///
    /// * `key_name` - The name for the key to be generated.
    /// * `key` - The server end of the user channel to bind.
    /// * `key_algorithm` - The algorithm for the key to be generated.
    /// * `provider` - The crypto provider to generate the key with.
    fn generate_asymmetric_key_and_bind(
        &self,
        key_name: &str,
        key: ServerEnd<AsymmetricPrivateKeyMarker>,
        key_algorithm: AsymmetricKeyAlgorithm,
        provider: &dyn CryptoProvider,
    ) -> Result<(), Error> {
        let key_to_bind = self.generate_asymmetric_key(key_name, key_algorithm, provider)?;
        self.bind_asymmetric_key_to_server(key_name, key_to_bind, key)
    }

    /// Generate an asymmetric key object.
    ///
    /// Generate an asymmetric key object, write the key data and key attributes to file. Insert the
    /// in memory key object into key map and return the key object.
    ///
    /// # Arguments
    ///
    /// * `key_name` - The name for the key to be generated.
    /// * `key_algorithm` - The algorithm for the key to be generated.
    /// * `provider` - The crypto provider to generate the key with.
    fn generate_asymmetric_key(
        &self,
        key_name: &str,
        key_algorithm: AsymmetricKeyAlgorithm,
        provider: &dyn CryptoProvider,
    ) -> Result<Arc<Mutex<KmsAsymmetricKey>>, Error> {
        self.generate_or_import_asymmetric_key(key_name, key_algorithm, provider, None)
    }

    /// Import an asymmetric key object and bind the imported key to the user channel.
    ///
    /// # Arguments
    ///
    /// * `data` - The imported key data.
    /// * `key_name` - The name for the key to be imported.
    /// * `key_algorithm` - The algorithm for the key to be imported.
    /// * `key` - The server end of the user channel to bind
    /// * `provider` - The crypto provider to parse the key with.
    fn import_asymmetric_private_key_and_bind(
        &self,
        data: &[u8],
        key_name: &str,
        key_algorithm: AsymmetricKeyAlgorithm,
        key: ServerEnd<AsymmetricPrivateKeyMarker>,
        provider: &dyn CryptoProvider,
    ) -> Result<(), Error> {
        let key_to_bind =
            self.import_asymmetric_private_key(data, &key_name, key_algorithm, provider)?;
        self.bind_asymmetric_key_to_server(&key_name, key_to_bind, key)
    }

    /// Import an asymmetric key object.
    ///
    /// Import an asymmetric key object, write the key data and key attributes to file. Insert the
    /// in memory key object into key map and return the key object.
    ///
    /// # Arguments
    ///
    /// * `data` - The imported key data.
    /// * `key_name` - The name for the key to be imported.
    /// * `key_algorithm` - The algorithm for the key to be imported.
    /// * `provider` - The crypto provider to parse the key with.
    fn import_asymmetric_private_key(
        &self,
        data: &[u8],
        key_name: &str,
        key_algorithm: AsymmetricKeyAlgorithm,
        provider: &dyn CryptoProvider,
    ) -> Result<Arc<Mutex<KmsAsymmetricKey>>, Error> {
        self.generate_or_import_asymmetric_key(key_name, key_algorithm, provider, Some(data))
    }

    /// Generate or import an asymmetric key object depending on whether imported_key_data is None.
    fn generate_or_import_asymmetric_key(
        &self,
        key_name: &str,
        key_algorithm: AsymmetricKeyAlgorithm,
        provider: &dyn CryptoProvider,
        imported_key_data: Option<&[u8]>,
    ) -> Result<Arc<Mutex<KmsAsymmetricKey>>, Error> {
        // Check whether the algorithm is valid.
        Self::check_asymmmetric_supported_algorithms(key_algorithm, provider)?;
        // Create a new symmetric key object and store it into the key map.
        // Obtain a lock on the key map to start a critical section.
        let mut key_map = self.user_key_map.lock().unwrap();
        if key_map.contains_key(key_name) || self.key_file_exists(key_name, true) {
            return Err(Error::KeyAlreadyExists);
        }
        let new_key = match imported_key_data {
            Some(data) => KmsAsymmetricKey::import_key(provider, data, key_name, key_algorithm),
            None => KmsAsymmetricKey::new(provider, key_name, key_algorithm),
        }?;
        {
            self.write_key_attributes_to_file(
                new_key.get_key_name(),
                Some(new_key.get_key_algorithm()),
                new_key.get_key_type(),
                new_key.get_key_origin(),
                new_key.get_key_provider(),
                &new_key.get_key_data(),
                true,
            )?;
        }
        let key_to_bind = Arc::new(Mutex::new(new_key));
        let key_to_insert = Arc::clone(&key_to_bind);
        key_map.insert(key_name.to_string(), key_to_insert);

        // End critical section.
        drop(key_map);

        Ok(key_to_bind)
    }

    /// Get an asymmetric key object and bind it to the user channel.
    ///
    /// # Arguments
    ///
    /// * `key_name` - The name for the key to be find.
    /// * `key` - The server end of the user channel to bind.
    fn get_asymmetric_private_key_and_bind(
        &self,
        key_name: &str,
        key: ServerEnd<AsymmetricPrivateKeyMarker>,
    ) -> Result<(), Error> {
        let key_to_bind = self.get_asymmetric_private_key(&key_name)?;
        self.bind_asymmetric_key_to_server(&key_name, key_to_bind, key)
    }

    /// Get an asymmetric key object.
    ///
    /// Find the asymmetric key object for the key name in the key map. If none is found, try to
    /// load the key object into memory from storage.
    ///
    /// # Arguments
    ///
    /// * `key_name` - The name for the key to be find.
    fn get_asymmetric_private_key(&self, key_name: &str) -> Result<Arc<Mutex<dyn KmsKey>>, Error> {
        // Start a critical section.
        let mut key_map = self.user_key_map.lock().unwrap();
        let key_to_bind = match key_map.get(key_name) {
            Some(key) => Arc::clone(key),
            None => {
                // The key is not in key map, read it from file.
                let provider_map = self.crypto_provider_map.read().unwrap();
                let key_attributes =
                    self.read_key_attributes_from_file(key_name, &provider_map, true)?;
                let asym_key = KmsAsymmetricKey::parse_key(key_name, key_attributes)?;
                let key_to_bind = Arc::new(Mutex::new(asym_key));
                let key_to_insert = Arc::clone(&key_to_bind);
                key_map.insert(key_name.to_string(), key_to_insert);
                key_to_bind
            }
        };
        // End the critical section.
        drop(key_map);

        Ok(key_to_bind)
    }

    /// Delete a key object.
    ///
    /// Delete the key data and key attributes from storage and remove the key from key map. Mark
    /// the key as deleted so that all the key handles currently possessed would return
    /// KEY_NOT_FOUND in the future.
    ///
    /// # Arguments
    ///
    /// * `key_name` - The name for the key to be deleted.
    fn delete_key(&self, key_name: &str) -> Result<(), Error> {
        // Obtain a lock on the user_key_map to start a critical section to make sure there is no
        // overlapping deleting/generating/importing/getting key operations.
        let mut key_map = self.user_key_map.lock().unwrap();
        // Always remove the key from key_map first. This would have no side effect even if the
        // delete operation fails early because we could always read the key from file and put
        // it back to the key_map later.
        if let Some(key) = key_map.remove(key_name) {
            // Set the deleted bit and ask the crypto provider to delete the key. If the key is
            // currently being used, this operation would blocked. However, if the key handle is
            // possessed in another job but there is no operation ongoing with the handle, we would
            // set the deleted bit so that the handle would fail on any further operations.
            key.lock().unwrap().delete()?;
        } else {
            // The key is not currently used, create an in-memory key structure.
            let provider_map = self.crypto_provider_map.read().unwrap();
            let key_attributes =
                self.read_key_attributes_from_file(key_name, &provider_map, true)?;
            // Ask the crypto provider to delete the key.
            let mut key = KmsAsymmetricKey::parse_key(key_name, key_attributes)?;
            key.delete()?;
        }
        // Remove the key files. If the key was already deleted before, the key must not exist in
        // the key map and the key file must has been deleted thus read_key_attributes_from_file
        // would return KEY_NOT_FOUND. We would never delete a same key file twice. If somehow the
        // previous attempt to remove key files failed and the key still exists, then we would retry
        // the removal here.
        let key_attributes_path = self.get_key_attributes_path(key_name, true);
        fs::remove_file(&key_attributes_path)
            .map_err(debug_err_fn!(Error::InternalError, "Failed to remove key files: {:?}"))?;

        // End the critical section.
        drop(key_map);
        Ok(())
    }

    /// Seal a piece of data. Return the sealed data in provider-specific format.
    ///
    /// Seal a piece of data using a sealing key. If the key does not exist, create the sealing key
    /// first.
    ///
    /// # Arguments
    ///
    /// * `data` - The buffer containing the data to be sealed.
    /// * `provider` - The crypto provider to do the encryption operation.
    fn seal_data(&self, data: Buffer, provider: &dyn CryptoProvider) -> Result<Buffer, Error> {
        if data.size > MAX_DATA_SIZE.into() {
            return Err(Error::InputTooLarge);
        }
        let sealing_key = self.get_sealing_key(provider)?;
        let mut result = Err(Error::InternalError);
        // The error, if any, would be return through the result variable, handle_request is
        // guaranteed to not return any FIDL error.
        sealing_key
            .lock()
            .unwrap()
            .handle_request(KeyRequestType::SealingKeyRequest(DataRequest {
                data,
                result: &mut result,
            }))
            .expect("Sealing key should not throw fidl error!");
        result
    }

    /// Unseal a piece of data. Return the original data.
    ///
    /// Unseal a piece of data originally sealed.
    ///
    /// # Arguments
    ///
    /// * `data` - The buffer containing the data to be unsealed.
    /// * `provider` - The crypto provider to do the decryption operation.
    fn unseal_data(&self, data: Buffer, provider: &dyn CryptoProvider) -> Result<Buffer, Error> {
        let sealed_data_size =
            provider.calculate_sealed_data_size(MAX_DATA_SIZE.into()).map_err(debug_err_fn!(
                Error::InternalError,
                "MAX_DATA_SIZE {} is too large, unable to get sealed data size, err: {:?}!",
                MAX_DATA_SIZE
            ))?;
        if data.size > sealed_data_size {
            return Err(Error::InputTooLarge);
        }
        if !self.key_file_exists(SEALING_KEY_NAME, false) {
            // If no sealing key exists, something is wrong.
            return Err(Error::KeyNotFound);
        }
        let sealing_key = self.get_sealing_key(provider)?;
        let mut result = Err(Error::InternalError);
        sealing_key
            .lock()
            .unwrap()
            .handle_request(KeyRequestType::UnsealingKeyRequest(DataRequest {
                data,
                result: &mut result,
            }))
            .expect("Unsealing key should not throw fidl error!");
        result
    }

    /// Get the sealing key object. If the sealing key does not exist, create a new one.
    fn get_sealing_key(
        &self,
        provider: &dyn CryptoProvider,
    ) -> Result<Arc<Mutex<dyn KmsKey>>, Error> {
        // Sealing key is managed internally, so we store them in internal_key_map.
        // Begin a critical section.
        let mut key_map = self.internal_key_map.lock().unwrap();
        let key_name = SEALING_KEY_NAME;
        let sealing_key = if !key_map.contains_key(key_name) {
            // If sealing key is not in the key map, load it in.
            let provider_map = self.crypto_provider_map.read().unwrap();
            // Sealing key is not a user key.
            let sealing_key =
                match self.read_key_attributes_from_file(&key_name, &provider_map, false) {
                    Ok(key_attributes) => KmsSealingKey::parse_key(key_name, key_attributes),
                    Err(Error::KeyNotFound) => {
                        warn!("No sealing key found, create new key.");
                        let new_key = KmsSealingKey::new(provider)?;
                        self.write_key_attributes_to_file(
                            new_key.get_key_name(),
                            None,
                            new_key.get_key_type(),
                            KeyOrigin::Generated,
                            new_key.get_key_provider(),
                            &new_key.get_key_data(),
                            false,
                        )?;
                        Ok(new_key)
                    }
                    Err(err) => Err(err),
                }?;
            let key_to_use = Arc::new(Mutex::new(sealing_key));
            let key_to_insert = Arc::clone(&key_to_use);
            key_map.insert(key_name.to_string(), key_to_insert);
            key_to_use
        } else {
            Arc::clone(&key_map[key_name])
        };
        // End critical section.
        drop(key_map);

        Ok(sealing_key)
    }

    /// Check whether a key algorithm is a valid asymmetric key algorithm and supported by provider.
    fn check_asymmmetric_supported_algorithms(
        key_algorithm: AsymmetricKeyAlgorithm,
        provider: &dyn CryptoProvider,
    ) -> Result<(), Error> {
        if provider
            .supported_asymmetric_algorithms()
            .iter()
            .find(|&alg| alg == &key_algorithm)
            .is_none()
        {
            warn!("The asymmetric algorithm is not supported.");
            // TODO: Add logic to fall back.
            return Err(Error::InternalError);
        }
        Ok(())
    }

    fn get_key_folder(&self, is_user_key: bool) -> PathBuf {
        if is_user_key {
            Path::new(&self.key_folder).join(USER_KEY_SUBFOLDER)
        } else {
            Path::new(&self.key_folder).join(INTERNAL_KEY_SUBFOLDER)
        }
    }

    fn get_key_attributes_path(&self, key_name: &str, is_user_key: bool) -> PathBuf {
        // We base64 encode the key name to prevent special characters.
        self.get_key_folder(is_user_key)
            .join(format!("key_{}.attr", base64::encode_config(key_name, base64::URL_SAFE)))
    }

    fn key_file_exists(&self, key_name: &str, is_user_key: bool) -> bool {
        let key_path = self.get_key_attributes_path(key_name, is_user_key);
        key_path.is_file()
    }

    fn write_key_attributes(
        &self,
        key_name: &str,
        serialized_key_attributes: &str,
        is_user_key: bool,
    ) -> Result<(), IOError> {
        fs::create_dir_all(self.get_key_folder(is_user_key))?;
        let key_attributes_path = self.get_key_attributes_path(key_name, is_user_key);
        fs::write(key_attributes_path, serialized_key_attributes)?;
        Ok(())
    }

    /// Write the key attributes to storage.
    ///
    /// If asymmetric_key_algorithm is None, this means that this is a sealing key and we do not
    /// care about algorithm, so we just fill algorithm number with 0.
    fn write_key_attributes_to_file(
        &self,
        key_name: &str,
        asymmetric_key_algorithm: Option<AsymmetricKeyAlgorithm>,
        key_type: KeyType,
        key_origin: KeyOrigin,
        key_provider: KeyProvider,
        key_data: &[u8],
        is_user_key: bool,
    ) -> Result<(), Error> {
        let key_algorithm_num = match asymmetric_key_algorithm {
            Some(alg) => alg.into_primitive(),
            None => 0,
        };
        let key_attributes = KeyAttributesJson {
            key_algorithm: key_algorithm_num,
            key_type,
            key_origin: key_origin.into_primitive(),
            key_provider: key_provider.into_primitive(),
            key_data: key_data.to_vec(),
        };
        let key_attributes_string = serde_json::to_string(&key_attributes)
            .expect("Failed to encode key attributes to JSON format.");
        self.write_key_attributes(key_name, &key_attributes_string, is_user_key)
            .map_err(debug_err_fn!(Error::InternalError, "Failed to write key attributes: {:?}"))
    }

    fn read_key_attributes(&self, key_name: &str, is_user_key: bool) -> Result<Vec<u8>, IOError> {
        let key_attributes_path = self.get_key_attributes_path(key_name, is_user_key);
        Ok(fs::read(key_attributes_path)?)
    }

    fn read_key_attributes_from_file<'a>(
        &self,
        key_name: &str,
        provider_map: &'a HashMap<KeyProvider, Box<dyn CryptoProvider>>,
        is_user_key: bool,
    ) -> Result<KeyAttributes<'a>, Error> {
        // Read the key attributes from file and parse it.
        let key_attributes_string =
            self.read_key_attributes(&key_name, is_user_key).map_err(|err| {
                if err.kind() == ErrorKind::NotFound {
                    return Error::KeyNotFound;
                }
                debug_err!(
                    Error::InternalError,
                    "Failed to read key attributes from key file: {:?}",
                    err
                )
            })?;
        let key_attributes_string =
            str::from_utf8(&key_attributes_string).map_err(debug_err_fn!(
                Error::InternalError,
                "Failed to parse JSON string as UTF8: {:?}! The stored key data is corrupted!"
            ))?;
        let key_attributes_json: KeyAttributesJson = serde_json::from_str(&key_attributes_string)
            .map_err(debug_err_fn!(
            Error::InternalError,
            "Failed to parse key attributes: {:?}, the stored key data is corrupted!"
        ))?;
        let key_provider = KeyProvider::from_primitive(key_attributes_json.key_provider)
            .ok_or_else(debug_err_fn_no_argument!(
                Error::InternalError,
                "Failed to convert key_provider! The stored key data is corrupted!"
            ))?;
        let provider = provider_map.get(&key_provider).ok_or_else(debug_err_fn_no_argument!(
            Error::InternalError,
            "Failed to find provider! The stored key data is corrupted!"
        ))?;
        let key_type = key_attributes_json.key_type;
        let asymmetric_key_algorithm = {
            if key_type == KeyType::AsymmetricPrivateKey {
                Some(
                    AsymmetricKeyAlgorithm::from_primitive(key_attributes_json.key_algorithm)
                        .ok_or_else(debug_err_fn_no_argument!(
                            Error::InternalError,
                            "Failed to convert key_algortihm! The stored key data is corrupted!"
                        ))?,
                )
            } else {
                None
            }
        };
        let key_origin = KeyOrigin::from_primitive(key_attributes_json.key_origin).ok_or_else(
            debug_err_fn_no_argument!(
                Error::InternalError,
                "Failed to convert key_origin! The stored key data is corrupted!"
            ),
        )?;
        Ok(KeyAttributes {
            asymmetric_key_algorithm,
            key_type,
            key_origin,
            provider: provider.as_ref(),
            key_data: key_attributes_json.key_data,
        })
    }

    /// Get a crypto provider according to name and pass that provider to a function.
    ///
    /// The input function takes an Option<&dyn CryptoProvider> as argument and the output would
    /// be the output of this function. During the function, a read lock to the crypto_provider_map
    /// would be hold to prevent modification to the map.
    pub fn with_provider<O, F: FnOnce(Option<&dyn CryptoProvider>) -> O>(
        &self,
        name: KeyProvider,
        f: F,
    ) -> O {
        f(self.crypto_provider_map.read().unwrap().get(&name).map(|provider| provider.as_ref()))
    }

    pub fn add_provider(&mut self, provider: Box<dyn CryptoProvider>) {
        let provider_map = &mut self.crypto_provider_map.write().unwrap();
        if provider_map.contains_key(&provider.get_name()) {
            panic!("Two providers should not have the same name!");
        }
        provider_map.insert(provider.get_name(), provider);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::{self as common, ASYMMETRIC_KEY_ALGORITHMS};
    use crate::crypto_provider::{mock_provider::MockProvider, CryptoProviderError};
    use fidl_fuchsia_kms::KeyOrigin;
    use tempfile::tempdir;
    static TEST_KEY_NAME: &str = "TestKey";

    /// Define a test case structure. It would do the necessary set up for a test case.
    struct TestCase {
        key_manager: KeyManager,
        mock_provider: Box<MockProvider>,
    }

    impl TestCase {
        /// Create a new test case. This would create a new key manager and set its provider to a
        /// new mock provider.
        fn new() -> Self {
            let tmp_key_folder = tempdir().unwrap();
            let mut key_manager = KeyManager::new();
            key_manager.set_key_folder(tmp_key_folder.path().to_str().unwrap());
            let mock_provider = Box::new(MockProvider::new());
            key_manager.add_provider(mock_provider.box_clone());
            TestCase { key_manager, mock_provider }
        }

        /// Get the mock provider set for this test case.
        fn get_mock_provider(&self) -> &MockProvider {
            &self.mock_provider
        }

        /// Get the key manager set for this test case.
        fn get_key_manager(&self) -> &KeyManager {
            return &self.key_manager;
        }
    }

    #[test]
    fn test_generate_asymmetric_key_mock_provider() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            generate_asymmetric_key_mock_provider(*algorithm);
        }
    }

    fn generate_asymmetric_key_mock_provider(key_algorithm: AsymmetricKeyAlgorithm) {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_output_data = common::generate_random_data(32);
        mock_provider.set_result(&test_output_data);
        let key = key_manager
            .generate_asymmetric_key(TEST_KEY_NAME, key_algorithm, mock_provider)
            .unwrap();
        let key = key.lock().unwrap();
        assert_eq!(TEST_KEY_NAME, key.get_key_name());
        assert_eq!(key_algorithm, key.get_key_algorithm());
        assert_eq!(test_output_data, key.get_key_data());
        assert_eq!(KeyOrigin::Generated, key.get_key_origin());
        assert_eq!(mock_provider.get_name(), key.get_key_provider());
        assert_eq!(mock_provider.get_called_key_name(), TEST_KEY_NAME);
    }

    #[test]
    fn test_generate_asymmetric_key_mock_provider_error() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            generate_asymmetric_key_mock_provider_error(*algorithm);
        }
    }

    fn generate_asymmetric_key_mock_provider_error(key_algorithm: AsymmetricKeyAlgorithm) {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        mock_provider.set_error();
        let result =
            key_manager.generate_asymmetric_key(TEST_KEY_NAME, key_algorithm, mock_provider);
        assert_eq!(Error::InternalError, result.unwrap_err());
    }

    #[test]
    fn test_get_asymmetric_key_mock_provider() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            get_asymmetric_key_mock_provider(*algorithm);
        }
    }

    fn get_asymmetric_key_mock_provider(key_algorithm: AsymmetricKeyAlgorithm) {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_key_data = common::generate_random_data(32);
        mock_provider.set_result(&test_key_data);
        let key_info = {
            let key = key_manager
                .generate_asymmetric_key(TEST_KEY_NAME, key_algorithm, mock_provider)
                .unwrap();
            let key_info = {
                let key = key.lock().unwrap();
                (key.get_key_name().to_string(), key.get_key_data())
            };
            let same_key = key_manager.get_asymmetric_private_key(TEST_KEY_NAME).unwrap();
            let same_key_lock = same_key.lock().unwrap();
            assert_eq!(same_key_lock.get_key_name(), &key_info.0);
            assert_eq!(same_key_lock.get_key_data(), key_info.1);
            key_info
        };

        // Clean up the cache
        KeyManager::clean_up(Arc::clone(&key_manager.user_key_map), TEST_KEY_NAME);

        // If we read again, it should read from file.
        let new_key_to_bind = key_manager.get_asymmetric_private_key(TEST_KEY_NAME).unwrap();
        let same_key_lock = new_key_to_bind.lock().unwrap();
        assert_eq!(same_key_lock.get_key_name(), &key_info.0);
        assert_eq!(same_key_lock.get_key_data(), key_info.1);
        assert_eq!(mock_provider.get_called_key_data(), key_info.1);
    }

    #[test]
    fn test_get_asymmetric_key_mock_provider_non_exists() {
        let test_case = TestCase::new();
        let key_manager = test_case.get_key_manager();
        // If we read again, it should read from file.
        let result = key_manager.get_asymmetric_private_key(TEST_KEY_NAME);
        assert_eq!(Error::KeyNotFound, result.unwrap_err());
    }

    #[test]
    fn test_import_asymmetric_key_mock_provider() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            import_asymmetric_key_mock_provider(*algorithm);
        }
    }

    fn import_asymmetric_key_mock_provider(key_algorithm: AsymmetricKeyAlgorithm) {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_input_data = common::generate_random_data(32);
        let test_output_data = common::generate_random_data(32);
        mock_provider.set_result(&test_output_data);
        let key = key_manager
            .import_asymmetric_private_key(
                &test_input_data,
                TEST_KEY_NAME,
                key_algorithm,
                mock_provider,
            )
            .unwrap();
        let key = key.lock().unwrap();
        assert_eq!(TEST_KEY_NAME, key.get_key_name());
        assert_eq!(test_output_data, key.get_key_data());
        assert_eq!(key_algorithm, key.get_key_algorithm());
        assert_eq!(KeyOrigin::Imported, key.get_key_origin());
        assert_eq!(mock_provider.get_name(), key.get_key_provider());
        assert_eq!(mock_provider.get_called_key_data(), test_input_data);
        assert_eq!(mock_provider.get_called_key_name(), TEST_KEY_NAME);
    }

    #[test]
    fn test_import_asymmetric_key_mock_already_exists() {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let key_algorithm = AsymmetricKeyAlgorithm::EcdsaSha512P521;
        let test_input_data = common::generate_random_data(32);
        let test_output_data = common::generate_random_data(32);
        mock_provider.set_result(&test_output_data);
        let _key_to_bind = key_manager.import_asymmetric_private_key(
            &test_input_data,
            TEST_KEY_NAME,
            key_algorithm,
            mock_provider,
        );
        let result = key_manager.import_asymmetric_private_key(
            &test_input_data,
            TEST_KEY_NAME,
            key_algorithm,
            mock_provider,
        );
        assert_eq!(Error::KeyAlreadyExists, result.unwrap_err());
    }

    #[test]
    fn test_import_asymmetric_key_mock_already_exists_generated() {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let key_algorithm = AsymmetricKeyAlgorithm::EcdsaSha512P521;
        let test_input_data = common::generate_random_data(32);
        let test_output_data = common::generate_random_data(32);
        mock_provider.set_result(&test_output_data);
        let _key_to_bind = key_manager
            .generate_asymmetric_key(TEST_KEY_NAME, key_algorithm, mock_provider)
            .unwrap();
        let result = key_manager.import_asymmetric_private_key(
            &test_input_data,
            TEST_KEY_NAME,
            key_algorithm,
            mock_provider,
        );
        assert_eq!(Error::KeyAlreadyExists, result.unwrap_err());
    }

    #[test]
    fn test_delete_key_mock_provider() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            delete_key_mock_provider(*algorithm);
        }
    }

    fn delete_key_mock_provider(key_algorithm: AsymmetricKeyAlgorithm) {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_input_data = common::generate_random_data(32);
        mock_provider.set_result(&test_input_data);
        // This make sure that key.delete would succeed.
        mock_provider.set_key_result(Ok(Vec::new()));
        {
            let key = key_manager
                .generate_asymmetric_key(TEST_KEY_NAME, key_algorithm, mock_provider)
                .unwrap();
            key_manager.delete_key(TEST_KEY_NAME).unwrap();
            // Try deleting the key again, should fail.
            assert_eq!(Error::KeyNotFound, key_manager.delete_key(TEST_KEY_NAME).unwrap_err());
            assert_eq!(true, key.lock().unwrap().is_deleted());
            // Try getting the deleted key, should get key_not_found.
            assert_eq!(
                Error::KeyNotFound,
                key_manager.get_asymmetric_private_key(TEST_KEY_NAME).unwrap_err()
            );
        }
        // Try getting the deleted key after all reference to the deleted key is freed, should get
        // key_not_found.
        assert_eq!(
            Error::KeyNotFound,
            key_manager.get_asymmetric_private_key(TEST_KEY_NAME).unwrap_err()
        );
    }

    #[test]
    fn delete_key_mock_provider_non_exists() {
        let test_case = TestCase::new();
        let key_manager = test_case.get_key_manager();
        let result = key_manager.delete_key(TEST_KEY_NAME);
        assert_eq!(Error::KeyNotFound, result.unwrap_err());
    }

    #[test]
    fn delete_key_mock_provider_error() {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_input_data = common::generate_random_data(32);
        mock_provider.set_result(&test_input_data);
        // This make sure that key.delete would succeed.
        mock_provider.set_key_result(Ok(Vec::new()));
        let _key_to_bind = key_manager
            .generate_asymmetric_key(
                TEST_KEY_NAME,
                AsymmetricKeyAlgorithm::EcdsaSha512P521,
                mock_provider,
            )
            .unwrap();
        mock_provider.set_key_operation_error();
        let result = key_manager.delete_key(TEST_KEY_NAME);
        assert_eq!(Err(Error::InternalError), result);
    }

    #[test]
    fn test_seal_data_mock_provider() {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_input_data = common::generate_random_data(256);
        let test_output_data = common::generate_random_data(256);
        let test_key_data = common::generate_random_data(32);
        mock_provider.set_result(&test_key_data);
        mock_provider.set_key_result(Ok(test_output_data.clone()));
        let output = key_manager
            .seal_data(common::data_to_buffer(&test_input_data).unwrap(), mock_provider)
            .unwrap();
        assert_eq!(test_output_data, common::buffer_to_data(output).unwrap());
    }

    #[test]
    fn test_seal_data_input_too_large() {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_input_data = common::generate_random_data(MAX_DATA_SIZE + 1);
        let test_output_data = common::generate_random_data(256);
        let test_key_data = common::generate_random_data(32);
        mock_provider.set_result(&test_key_data);
        mock_provider.set_key_result(Ok(test_output_data.clone()));
        let result =
            key_manager.seal_data(common::data_to_buffer(&test_input_data).unwrap(), mock_provider);
        assert_eq!(Err(Error::InputTooLarge), result);
    }

    #[test]
    fn test_unseal_data_mock_provider() {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_input_data = common::generate_random_data(256);
        let test_output_data = common::generate_random_data(256);
        let test_key_data = common::generate_random_data(32);
        mock_provider.set_result(&test_key_data);
        mock_provider.set_key_result(Ok(test_output_data.clone()));
        let encrypted_buffer = key_manager
            .seal_data(common::data_to_buffer(&test_input_data).unwrap(), mock_provider)
            .unwrap();
        let output = key_manager.unseal_data(encrypted_buffer, mock_provider).unwrap();
        assert_eq!(test_output_data, common::buffer_to_data(output).unwrap());
    }

    #[test]
    fn test_unseal_data_input_too_large() {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        // For mock provider, the sealed data size is the same as the original data size.
        let test_input_data = common::generate_random_data(MAX_DATA_SIZE + 1);
        let test_output_data = common::generate_random_data(256);
        let test_key_data = common::generate_random_data(32);
        mock_provider.set_result(&test_key_data);
        mock_provider.set_key_result(Ok(test_output_data.clone()));
        let result = key_manager
            .unseal_data(common::data_to_buffer(&test_input_data).unwrap(), mock_provider);
        assert_eq!(Err(Error::InputTooLarge), result);
    }

    #[test]
    fn test_seal_unseal_data_mock_provider_error() {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_input_data = common::generate_random_data(256);
        let test_key_data = common::generate_random_data(256);
        mock_provider.set_result(&test_key_data);
        mock_provider.set_key_result(Err(CryptoProviderError::new("")));
        let result =
            key_manager.seal_data(common::data_to_buffer(&test_input_data).unwrap(), mock_provider);
        assert_eq!(Error::InternalError, result.unwrap_err());
    }

    #[test]
    fn test_delete_sealing_key_mock_provider() {
        let test_case = TestCase::new();
        let mock_provider = test_case.get_mock_provider();
        let key_manager = test_case.get_key_manager();
        let test_input_data = common::generate_random_data(256);
        let test_output_data = common::generate_random_data(256);
        mock_provider.set_result(&test_output_data);
        // This make sure that key.delete would succeed.
        mock_provider.set_key_result(Ok(Vec::new()));
        let _output = key_manager
            .seal_data(common::data_to_buffer(&test_input_data).unwrap(), mock_provider)
            .unwrap();

        // The sealing key should be in a separate name space than user keys.
        let result = key_manager.delete_key(SEALING_KEY_NAME);
        assert_eq!(Error::KeyNotFound, result.unwrap_err());
    }

    #[test]
    fn test_get_default_provider() {
        let key_manager = KeyManager::new();
        key_manager.with_provider(DEFAULT_PROVIDER, |provider| {
            assert_eq!(false, provider.is_none());
        });
    }
}
