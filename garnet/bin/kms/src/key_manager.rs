// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{KeyAttributes, KeyRequestType, KeyType, KmsKey};
use crate::crypto_provider::CryptoProvider;
use crate::kms_asymmetric_key::KmsAsymmetricKey;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_kms::{
    AsymmetricKeyAlgorithm, AsymmetricPrivateKeyMarker, KeyManagerRequest, KeyOrigin, Status,
};
use fuchsia_async as fasync;
use futures::prelude::*;
use log::error;
use serde_derive::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::io::{Error, ErrorKind};
use std::path::{Path, PathBuf};
use std::str;
use std::sync::{Arc, Mutex};

const PROVIDER_NAME: &str = "MundaneSoftwareProvider";
const KEY_FOLDER: &str = "/data/kms";

#[derive(Serialize, Deserialize)]
struct KeyAttributesJson {
    pub key_algorithm: u32,
    pub key_type: KeyType,
    pub key_origin: u32,
    pub provider_name: String,
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
    /// A map of key_name -> key_object.
    key_map: Arc<Mutex<HashMap<String, Arc<Mutex<dyn KmsKey>>>>>,
    /// All the available crypto providers.
    crypto_provider_map: Mutex<HashMap<&'static str, Box<dyn CryptoProvider>>>,
    /// The path to the key folder to store key data and attributes.
    key_folder: String,
}

impl KeyManager {
    pub fn new() -> Self {
        KeyManager {
            key_map: Arc::new(Mutex::new(HashMap::new())),
            crypto_provider_map: Mutex::new(HashMap::new()),
            key_folder: KEY_FOLDER.to_string(),
        }
    }

    #[allow(dead_code)]
    #[cfg(test)]
    pub fn set_key_folder(&mut self, key_folder: &str) {
        self.key_folder = key_folder.to_string();
    }

    pub fn handle_request(&self, req: KeyManagerRequest) -> Result<(), fidl::Error> {
        match req {
            KeyManagerRequest::GenerateAsymmetricKey { key_name, key, responder } => {
                let provider = self.get_provider(PROVIDER_NAME).unwrap();
                // Default algorithm for asymmetric key is ECDSA-SHA512-P521.
                match self.generate_asymmetric_key_and_bind(
                    &key_name,
                    key,
                    AsymmetricKeyAlgorithm::EcdsaSha512P521,
                    provider,
                ) {
                    Ok(()) => responder.send(Status::Ok)?,
                    Err(status) => responder.send(status)?,
                }
            }
            KeyManagerRequest::GenerateAsymmetricKeyWithAlgorithm {
                key_name,
                key_algorithm,
                key,
                responder,
            } => {
                let provider = self.get_provider(PROVIDER_NAME).unwrap();
                match self.generate_asymmetric_key_and_bind(&key_name, key, key_algorithm, provider)
                {
                    Ok(()) => responder.send(Status::Ok)?,
                    Err(status) => responder.send(status)?,
                }
            }
            KeyManagerRequest::GetAsymmetricPrivateKey { key_name, key, responder } => {
                match self.get_asymmetric_private_key_and_bind(&key_name, key) {
                    Ok(()) => responder.send(Status::Ok)?,
                    Err(status) => responder.send(status)?,
                }
            }
            KeyManagerRequest::ImportAsymmetricPrivateKey {
                data: _,
                key_name: _,
                key_algorithm: _,
                key: _,
                responder,
            } => {
                responder.send(Status::Ok)?;
            }
            KeyManagerRequest::SealData { plain_text: _, responder } => {
                responder.send(Status::Ok, None)?;
            }
            KeyManagerRequest::UnsealData { cipher_text: _, responder } => {
                responder.send(Status::Ok, None)?;
            }
            KeyManagerRequest::DeleteKey { key_name: _, responder } => {
                responder.send(Status::Ok)?;
            }
        }
        Ok(())
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
    ) -> Result<(), Status> {
        let mut request_stream = key.into_stream().map_err(debug_err_fn!(
            Status::InternalError,
            "Error creating AsymmetricKey request stream {:?}"
        ))?;
        // Need to clone the key_name to be move into the async function.
        let key_name = String::from(key_name);
        // Copy the key map into the async function.
        let key_map_ref = Arc::clone(&self.key_map);
        fasync::spawn_local(
            // Spawn async job to handle requests.
            async move {
                while let Some(r) = await!(request_stream.try_next())? {
                    key_to_bind
                        .lock()
                        .unwrap()
                        .handle_request(KeyRequestType::AsymmetricPrivateKeyRequest(r))?;
                }
                Ok(())
            }
                .and_then(|_| {
                    async move {
                        Self::clean_up(key_map_ref, &key_name);
                        Ok(())
                    }
                })
                .unwrap_or_else(|e: fidl::Error| error!("Error running AsymmetricKey {:?}", e)),
        );
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
        provider: Box<dyn CryptoProvider>,
    ) -> Result<(), Status> {
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
        provider: Box<dyn CryptoProvider>,
    ) -> Result<Arc<Mutex<KmsAsymmetricKey>>, Status> {
        self.generate_or_import_asymmetric_key(key_name, key_algorithm, provider, None)
    }

    /// Generate or import an asymmetric key object depending on whether imported_key_data is None.
    fn generate_or_import_asymmetric_key(
        &self,
        key_name: &str,
        key_algorithm: AsymmetricKeyAlgorithm,
        provider: Box<dyn CryptoProvider>,
        imported_key_data: Option<&[u8]>,
    ) -> Result<Arc<Mutex<KmsAsymmetricKey>>, Status> {
        // Check whether the algorithm is valid.
        Self::check_asymmmetric_supported_algorithms(key_algorithm, &provider)?;
        // Create a new symmetric key object and store it into the key map. Need to make sure
        // we hold a mutable lock to the key_map during the whole operation to prevent dead lock.
        let mut key_map = self.key_map.lock().unwrap();
        if key_map.contains_key(key_name) {
            return Err(Status::KeyAlreadyExists);
        }
        if self.key_file_exists(key_name) {
            return Err(Status::KeyAlreadyExists);
        }
        let new_key = match imported_key_data {
            None => KmsAsymmetricKey::new(&(*provider), key_name, key_algorithm),
            Some(data) => KmsAsymmetricKey::import_key(&(*provider), data, key_name, key_algorithm),
        }?;
        {
            self.write_key_attributes_to_file(
                new_key.get_key_name(),
                Some(new_key.get_key_algorithm()),
                new_key.get_key_type(),
                new_key.get_key_origin(),
                new_key.get_provider_name(),
                &new_key.get_key_data(),
            )?;
        }
        let key_to_bind = Arc::new(Mutex::new(new_key));
        let key_to_insert = Arc::clone(&key_to_bind);
        key_map.insert(key_name.to_string(), key_to_insert);

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
    ) -> Result<(), Status> {
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
    fn get_asymmetric_private_key(&self, key_name: &str) -> Result<Arc<Mutex<dyn KmsKey>>, Status> {
        let mut key_map = self.key_map.lock().unwrap();
        match key_map.get(key_name) {
            Some(key) => Ok(Arc::clone(key)),
            None => {
                // The key is not in key map, read it from file.
                let key_attributes = self.read_key_attributes_from_file(key_name)?;
                let asym_key = KmsAsymmetricKey::parse_key(key_name, key_attributes)?;
                let key_to_bind = Arc::new(Mutex::new(asym_key));
                let key_to_insert = Arc::clone(&key_to_bind);
                key_map.insert(key_name.to_string(), key_to_insert);
                Ok(key_to_bind)
            }
        }
    }

    /// Check whether a key algorithm is a valid asymmetric key algorithm and supported by provider.
    fn check_asymmmetric_supported_algorithms(
        key_algorithm: AsymmetricKeyAlgorithm,
        provider: &Box<dyn CryptoProvider>,
    ) -> Result<(), Status> {
        if provider
            .supported_asymmetric_algorithms()
            .iter()
            .find(|&alg| alg == &key_algorithm)
            .is_none()
        {
            // TODO: Add logic to fall back.
            return Err(Status::InternalError);
        }
        Ok(())
    }

    fn get_key_attributes_path(&self, key_name: &str) -> PathBuf {
        Path::new(&self.key_folder).join(format!("{}.attr", key_name))
    }

    fn key_file_exists(&self, key_name: &str) -> bool {
        let key_path = self.get_key_attributes_path(key_name);
        key_path.is_file()
    }

    fn write_key_attributes(
        &self,
        key_name: &str,
        serialized_key_attributes: &str,
    ) -> Result<(), Error> {
        fs::create_dir_all(&self.key_folder)?;
        let key_attributes_path = self.get_key_attributes_path(key_name);
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
        provider_name: &str,
        key_data: &[u8],
    ) -> Result<(), Status> {
        let key_algorithm_num = match asymmetric_key_algorithm {
            Some(alg) => alg.into_primitive(),
            None => 0,
        };
        let key_attributes = KeyAttributesJson {
            key_algorithm: key_algorithm_num,
            key_type,
            key_origin: key_origin.into_primitive(),
            provider_name: provider_name.to_string(),
            key_data: key_data.to_vec(),
        };
        let key_attributes_string = serde_json::to_string(&key_attributes)
            .expect("Failed to encode key attributes to JSON format.");
        self.write_key_attributes(key_name, &key_attributes_string)
            .map_err(debug_err_fn!(Status::InternalError, "Failed to write key attributes: {:?}"))
    }

    fn read_key_attributes(&self, key_name: &str) -> Result<Vec<u8>, Error> {
        let key_attributes_path = self.get_key_attributes_path(key_name);
        Ok(fs::read(key_attributes_path)?)
    }

    fn read_key_attributes_from_file(&self, key_name: &str) -> Result<KeyAttributes, Status> {
        // Read the key attributes from file and parse it.
        let key_attributes_string = self.read_key_attributes(&key_name).map_err(|err| {
            if err.kind() == ErrorKind::NotFound {
                return Status::KeyNotFound;
            }
            debug_err!(
                Status::InternalError,
                "Failed to read key attributes from key file: {:?}",
                err
            )
        })?;
        let key_attributes_string =
            str::from_utf8(&key_attributes_string).map_err(debug_err_fn!(
                Status::InternalError,
                "Failed to parse JSON string as UTF8: {:?}! The stored key data is corrupted!"
            ))?;
        let key_attributes_json: KeyAttributesJson = serde_json::from_str(&key_attributes_string)
            .map_err(debug_err_fn!(
            Status::InternalError,
            "Failed to parse key attributes: {:?}, the stored key data is corrupted!"
        ))?;
        let provider = {
            match self.get_provider(&key_attributes_json.provider_name) {
                Some(provider) => Ok(provider),
                None => Err(debug_err!(
                    Status::InternalError,
                    "Failed to find provider! The stored key data is corrupted!"
                )),
            }
        }?;
        let key_type = key_attributes_json.key_type;
        let asymmetric_key_algorithm = Some(
            AsymmetricKeyAlgorithm::from_primitive(key_attributes_json.key_algorithm).ok_or_else(
                debug_err_fn_without_err!(
                    Status::InternalError,
                    "Failed to convert key_algortihm! The stored key data is corrupted!"
                ),
            )?,
        );
        let key_origin = KeyOrigin::from_primitive(key_attributes_json.key_origin).ok_or_else(
            debug_err_fn_without_err!(
                Status::InternalError,
                "Failed to convert key_origin! The stored key data is corrupted!"
            ),
        )?;
        Ok(KeyAttributes {
            asymmetric_key_algorithm,
            key_type,
            key_origin,
            provider,
            key_data: key_attributes_json.key_data,
        })
    }

    pub fn get_provider(&self, provider_name: &str) -> Option<Box<dyn CryptoProvider>> {
        let provider_map = &self.crypto_provider_map.lock().unwrap();
        match provider_map.get(provider_name) {
            Some(provider) => Some(provider.clone()),
            None => None,
        }
    }

    #[cfg(test)]
    pub fn add_provider(&mut self, provider: Box<dyn CryptoProvider>) {
        let provider_map = &mut self.crypto_provider_map.lock().unwrap();
        if provider_map.contains_key(provider.get_name()) {
            panic!("Two providers should not have the same name!");
        }
        provider_map.insert(provider.get_name(), provider);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::{self as common, ASYMMETRIC_KEY_ALGORITHMS};
    use crate::crypto_provider::mock_provider::MockProvider;
    use fidl_fuchsia_kms::KeyOrigin;
    use tempfile::tempdir;
    static TEST_KEY_NAME: &str = "TestKey";

    #[test]
    fn test_generate_asymmetric_key_mock_provider() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            generate_asymmetric_key_mock_provider(*algorithm);
        }
    }

    fn generate_asymmetric_key_mock_provider(key_algorithm: AsymmetricKeyAlgorithm) {
        let tmp_key_folder = tempdir().unwrap();
        let mut key_manager = KeyManager::new();
        key_manager.set_key_folder(tmp_key_folder.path().to_str().unwrap());
        let test_output_data = common::generate_random_data(32);
        let mock_provider = Box::new(MockProvider::new());
        mock_provider.set_result(&test_output_data);
        let key = key_manager
            .generate_asymmetric_key(TEST_KEY_NAME, key_algorithm, mock_provider.box_clone())
            .unwrap();
        let key = key.lock().unwrap();
        assert_eq!(TEST_KEY_NAME, key.get_key_name());
        assert_eq!(key_algorithm, key.get_key_algorithm());
        assert_eq!(test_output_data, key.get_key_data());
        assert_eq!(KeyOrigin::Generated, key.get_key_origin());
        assert_eq!(mock_provider.get_name(), key.get_provider_name());
        assert_eq!(mock_provider.get_called_key_name(), TEST_KEY_NAME);
        tmp_key_folder.close().unwrap();
    }

    #[test]
    fn test_generate_asymmetric_key_mock_provider_error() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            generate_asymmetric_key_mock_provider_error(*algorithm);
        }
    }

    fn generate_asymmetric_key_mock_provider_error(key_algorithm: AsymmetricKeyAlgorithm) {
        let tmp_key_folder = tempdir().unwrap();
        let mut key_manager = KeyManager::new();
        key_manager.set_key_folder(tmp_key_folder.path().to_str().unwrap());
        let mock_provider = Box::new(MockProvider::new());
        mock_provider.set_error();
        let result = key_manager.generate_asymmetric_key(
            TEST_KEY_NAME,
            key_algorithm,
            mock_provider.box_clone(),
        );
        if let Err(Status::InternalError) = result {
            assert!(true);
        } else {
            assert!(false);
        }
        tmp_key_folder.close().unwrap();
    }

    #[test]
    fn test_get_asymmetric_key_mock_provider() {
        for algorithm in ASYMMETRIC_KEY_ALGORITHMS.iter() {
            get_asymmetric_key_mock_provider(*algorithm);
        }
    }

    fn get_asymmetric_key_mock_provider(key_algorithm: AsymmetricKeyAlgorithm) {
        let tmp_key_folder = tempdir().unwrap();
        let mut key_manager = KeyManager::new();
        let mock_provider = Box::new(MockProvider::new());
        let test_key_data = common::generate_random_data(32);
        mock_provider.set_result(&test_key_data);
        key_manager.add_provider(mock_provider.box_clone());
        key_manager.set_key_folder(tmp_key_folder.path().to_str().unwrap());
        let key_info = {
            let key = key_manager
                .generate_asymmetric_key(TEST_KEY_NAME, key_algorithm, mock_provider.box_clone())
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
        KeyManager::clean_up(Arc::clone(&key_manager.key_map), TEST_KEY_NAME);

        // If we read again, it should read from file.
        let new_key_to_bind = key_manager.get_asymmetric_private_key(TEST_KEY_NAME).unwrap();
        let same_key_lock = new_key_to_bind.lock().unwrap();
        assert_eq!(same_key_lock.get_key_name(), &key_info.0);
        assert_eq!(same_key_lock.get_key_data(), key_info.1);
        assert_eq!(mock_provider.get_called_key_data(), key_info.1);
        tmp_key_folder.close().unwrap();
    }

    #[test]
    fn test_get_asymmetric_key_mock_provider_non_exists() {
        let tmp_key_folder = tempdir().unwrap();
        let mut key_manager = KeyManager::new();
        key_manager.add_provider(Box::new(MockProvider::new()));
        key_manager.set_key_folder(tmp_key_folder.path().to_str().unwrap());
        // If we read again, it should read from file.
        let result = key_manager.get_asymmetric_private_key(TEST_KEY_NAME);
        if let Err(Status::KeyNotFound) = result {
            assert!(true);
        } else {
            assert!(false);
        }
        tmp_key_folder.close().unwrap();
    }
}
