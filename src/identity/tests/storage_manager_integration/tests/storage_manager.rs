// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::LocalAccountId;
use fidl_fuchsia_identity_account::Error as ApiError;
use lazy_static::lazy_static;
use storage_manager::{EncryptedVolumeStorageManager, Key, StorageManager};

lazy_static! {
    static ref TEST_ACCOUNT_ID: LocalAccountId = LocalAccountId::new(459);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_unimplemented() {
    let manager = EncryptedVolumeStorageManager::new(&*TEST_ACCOUNT_ID).unwrap();
    assert_eq!(
        manager.provision(&Key::NoSpecifiedKey).await.unwrap_err().api_error,
        ApiError::UnsupportedOperation
    );
}
