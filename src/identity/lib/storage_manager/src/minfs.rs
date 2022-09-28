// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Key, StorageManager as StorageManagerTrait};
use account_common::AccountManagerError;
use async_trait::async_trait;
use fidl_fuchsia_io as fio;

pub mod constants;
pub mod disk;

pub struct StorageManager {}

// TODO(https://fxbug.dev/103134): This struct should implement StorageManager.
#[async_trait(?Send)]
impl StorageManagerTrait for StorageManager {
    async fn provision(&self, _key: &Key) -> Result<(), AccountManagerError> {
        unimplemented!();
    }

    async fn unlock(&self, _key: &Key) -> Result<(), AccountManagerError> {
        unimplemented!();
    }

    async fn lock(&self) -> Result<(), AccountManagerError> {
        unimplemented!();
    }

    async fn destroy(&self) -> Result<(), AccountManagerError> {
        unimplemented!();
    }

    async fn get_root_dir(&self) -> Result<fio::DirectoryProxy, AccountManagerError> {
        unimplemented!();
    }
}
