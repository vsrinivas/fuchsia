// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Key, StorageManager};
use account_common::{AccountId, AccountManagerError};
use async_trait::async_trait;
use fidl_fuchsia_identity_account::Error as ApiError;
use fidl_fuchsia_io as fio;

/// A `StorageManager` that manages access to a minfs filesystem backed by an
/// FVM partition encrypted with zxcrypt.
pub struct EncryptedVolumeStorageManager;

#[async_trait(?Send)]
impl StorageManager for EncryptedVolumeStorageManager {
    async fn provision(&self, _key: &Key) -> Result<(), AccountManagerError> {
        Err(AccountManagerError::new(ApiError::UnsupportedOperation))
    }

    async fn unlock(&self, _key: &Key) -> Result<(), AccountManagerError> {
        Err(AccountManagerError::new(ApiError::UnsupportedOperation))
    }

    async fn lock(&self) -> Result<(), AccountManagerError> {
        Err(AccountManagerError::new(ApiError::UnsupportedOperation))
    }

    async fn destroy(&self) -> Result<(), AccountManagerError> {
        Err(AccountManagerError::new(ApiError::UnsupportedOperation))
    }

    async fn get_root_dir(&self) -> Result<fio::DirectoryProxy, AccountManagerError> {
        Err(AccountManagerError::new(ApiError::UnsupportedOperation))
    }
}

impl EncryptedVolumeStorageManager {
    /// Create a new `DirectoryStorageManager`, using the given account id to
    /// identify the correct volume.
    pub fn new(_account_id: &AccountId) -> Result<Self, AccountManagerError> {
        Ok(Self {})
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref ACCOUNT_ID: AccountId = AccountId::new(45);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_volume_storage_manager_provision_unimplemented() {
        let manager = EncryptedVolumeStorageManager::new(&ACCOUNT_ID).unwrap();
        assert_eq!(
            manager.provision(&Key::NoSpecifiedKey).await.unwrap_err().api_error,
            ApiError::UnsupportedOperation
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn test_volume_storage_manager_unlock_unimplemented() {
        let manager = EncryptedVolumeStorageManager::new(&ACCOUNT_ID).unwrap();
        assert_eq!(
            manager.unlock(&Key::NoSpecifiedKey).await.unwrap_err().api_error,
            ApiError::UnsupportedOperation
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn test_volume_storage_manager_lock_unimplemented() {
        let manager = EncryptedVolumeStorageManager::new(&ACCOUNT_ID).unwrap();
        assert_eq!(manager.lock().await.unwrap_err().api_error, ApiError::UnsupportedOperation)
    }

    #[fasync::run_until_stalled(test)]
    async fn test_volume_storage_manager_destroy_unimplemented() {
        let manager = EncryptedVolumeStorageManager::new(&ACCOUNT_ID).unwrap();
        assert_eq!(manager.destroy().await.unwrap_err().api_error, ApiError::UnsupportedOperation)
    }

    #[fasync::run_until_stalled(test)]
    async fn test_volume_storage_manager_get_root_dir_unimplemented() {
        let manager = EncryptedVolumeStorageManager::new(&ACCOUNT_ID).unwrap();
        assert_eq!(
            manager.get_root_dir().await.unwrap_err().api_error,
            ApiError::UnsupportedOperation
        )
    }
}
