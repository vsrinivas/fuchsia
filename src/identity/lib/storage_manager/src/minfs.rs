// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    minfs::{
        constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
        disk::{DiskError, DiskManager, Partition},
    },
    StorageManager as StorageManagerTrait,
};
use account_common::AccountManagerError;
use async_trait::async_trait;
use fidl_fuchsia_io as fio;
use futures::{Stream, StreamExt};

pub mod constants;
pub mod disk;
mod state;

pub struct StorageManager<DM>
where
    DM: DiskManager,
{
    disk_manager: DM,
}

impl<DM> StorageManager<DM>
where
    DM: DiskManager,
{
    pub fn new(disk_manager: DM) -> Self {
        Self { disk_manager }
    }

    /// Returns a stream of partitions that match the user data account GUID and label.
    /// Skips any partitions whose labels or GUIDs can't be read.
    async fn get_account_partitions(
        &self,
    ) -> Result<impl Stream<Item = DM::BlockDevice>, DiskError> {
        let partitions = self.disk_manager().partitions().await?;
        Ok(futures::stream::iter(partitions).filter_map(|partition| async {
            match partition.has_guid(FUCHSIA_DATA_GUID).await {
                Ok(true) => match partition.has_label(ACCOUNT_LABEL).await {
                    Ok(true) => Some(partition.into_block_device()),
                    _ => None,
                },
                _ => None,
            }
        }))
    }
}

// A temporary extension trait for exposing functionality from
// minfs::StorageManager to other callsites.
#[async_trait]
pub trait StorageManagerExtTrait<DM>
where
    DM: DiskManager,
{
    fn disk_manager(&self) -> &DM;

    async fn find_account_partition(&self) -> Option<DM::BlockDevice>;
}

#[async_trait]
impl<DM> StorageManagerExtTrait<DM> for StorageManager<DM>
where
    DM: DiskManager,
{
    fn disk_manager(&self) -> &DM {
        &self.disk_manager
    }

    /// Find the first partition that matches the user data account GUID and label.
    async fn find_account_partition(&self) -> Option<DM::BlockDevice> {
        let account_partitions = self.get_account_partitions().await.ok()?;
        futures::pin_mut!(account_partitions);
        // Return the first matching partition.
        account_partitions.next().await
    }
}

// TODO(https://fxbug.dev/103134): This struct should implement StorageManager.
#[async_trait]
impl<DM> StorageManagerTrait for StorageManager<DM>
where
    DM: DiskManager,
{
    type Key = [u8; 32];

    async fn provision(&self, _key: &Self::Key) -> Result<(), AccountManagerError> {
        unimplemented!();
    }

    async fn unlock(&self, _key: &Self::Key) -> Result<(), AccountManagerError> {
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
