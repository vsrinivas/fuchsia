// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    minfs::{
        constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
        disk::{DiskError, DiskManager, EncryptedBlockDevice, Minfs, Partition},
        state::State,
    },
    StorageManager as StorageManagerTrait,
};
use account_common::AccountManagerError;
use async_trait::async_trait;
use fidl_fuchsia_identity_account::Error as AccountApiError;
use fidl_fuchsia_identity_account::{self as faccount};
use fidl_fuchsia_io as fio;
use fuchsia_zircon::Status;
use futures::lock::Mutex;
use futures::{Stream, StreamExt};
use std::sync::Arc;
use tracing::{error, info, warn};

pub mod constants;
pub mod disk;
mod state;

pub struct StorageManager<DM>
where
    DM: DiskManager,
{
    disk_manager: DM,
    state: Arc<Mutex<State<(DM::EncryptedBlockDevice, DM::Minfs)>>>,
}

impl<DM> StorageManager<DM>
where
    DM: DiskManager,
{
    pub fn new(disk_manager: DM) -> Self {
        Self { disk_manager, state: Arc::new(Mutex::new(State::Uninitialized)) }
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
    async fn provision(&self, key: &Self::Key) -> Result<(), AccountManagerError> {
        let block = self.find_account_partition().await.ok_or(faccount::Error::NotFound).map_err(
            |err| {
                error!("provision_new_account: couldn't find account partition to provision");
                err
            },
        )?;

        let encrypted_block =
            self.disk_manager.bind_to_encrypted_block(block).await.map_err(|err| {
                error!(
                    "provision_new_account: couldn't bind zxcrypt driver to encrypted \
                           block device: {}",
                    err
                );
                AccountManagerError::new(AccountApiError::Resource).with_cause(err)
            })?;
        encrypted_block.format(key).await.map_err(|err| {
            error!("provision_new_account: couldn't format encrypted block device: {}", err);
            AccountManagerError::new(AccountApiError::Resource).with_cause(err)
        })?;
        let unsealed_block = encrypted_block.unseal(key).await.map_err(|err| {
            error!("provision_new_account: couldn't unseal encrypted block device: {}", err);
            AccountManagerError::new(AccountApiError::Internal).with_cause(err)
        })?;
        let () = {
            let fut = self.disk_manager.format_minfs(&unsealed_block);
            fut.await.map_err(|err| {
                error!(
                    "provision_new_account: couldn't format minfs on inner block device: {}",
                    err
                );
                AccountManagerError::new(AccountApiError::Resource).with_cause(err)
            })?
        };
        let minfs = self.disk_manager.serve_minfs(unsealed_block).await.map_err(|err| {
            error!(
                "provision_new_account: couldn't serve minfs on inner unsealed block \
                       device: {}",
                err
            );
            AccountManagerError::new(AccountApiError::Resource).with_cause(err)
        })?;

        self.state
            .lock()
            .await
            .try_provision((encrypted_block, minfs))
            .map_err(|err| AccountManagerError::new(AccountApiError::Internal).with_cause(err))?;

        Ok(())
    }

    async fn unlock_storage(&self, key: &Self::Key) -> Result<(), AccountManagerError> {
        let block_device =
            self.find_account_partition().await.ok_or(faccount::Error::NotFound).map_err(
                |err| {
                    error!("unseal_account: couldn't find account partition");
                    err
                },
            )?;

        let encrypted_block =
            self.disk_manager.bind_to_encrypted_block(block_device).await.map_err(|err| {
                error!(
                    "unseal_account: couldn't bind zxcrypt driver to encrypted block device: {}",
                    err
                );
                AccountManagerError::new(AccountApiError::Resource).with_cause(err)
            })?;

        let block_device = match encrypted_block.unseal(key).await {
            Ok(block_device) => Ok(block_device),
            Err(DiskError::FailedToUnsealZxcrypt(err)) => {
                info!("unseal_account: failed to unseal zxcrypt (wrong password?): {}", err);
                Err(AccountManagerError::new(AccountApiError::Internal).with_cause(err))
            }
            Err(err) => {
                warn!("unseal_account: failed to unseal zxcrypt: {}", err);
                Err(AccountManagerError::new(AccountApiError::Internal).with_cause(err))
            }
        }?;

        let minfs = self.disk_manager.serve_minfs(block_device).await.map_err(|err| {
            error!("unseal_account: couldn't serve minfs: {}", err);
            AccountManagerError::new(AccountApiError::Resource).with_cause(err)
        })?;

        self.state
            .lock()
            .await
            .try_unlock((encrypted_block, minfs))
            .map_err(|err| AccountManagerError::new(AccountApiError::Internal).with_cause(err))?;

        Ok(())
    }

    async fn lock_storage(&self) -> Result<(), AccountManagerError> {
        let (encrypted_block, minfs) =
            self.state.lock().await.try_lock().map_err(|err| {
                AccountManagerError::new(AccountApiError::Internal).with_cause(err)
            })?;
        minfs.shutdown().await;
        let seal_fut = encrypted_block.seal();
        match seal_fut.await {
            Ok(()) => Ok(()),
            Err(DiskError::FailedToSealZxcrypt(Status::BAD_STATE)) => {
                // The block device is already sealed. We're in a bad state, but
                // technically the device is sealed, so job complete?
                warn!("block device was already sealed");
                Ok(())
            }
            Err(err) => {
                warn!("lock: failed to seal encrypted block: {:?}", err);
                Err(AccountManagerError::new(AccountApiError::Resource).with_cause(err))
            }
        }
    }

    async fn destroy(&self) -> Result<(), AccountManagerError> {
        match self.state.lock().await.try_destroy() {
            Ok(Some((encrypted_block, _minfs))) => encrypted_block.shred().await.map_err(|err| {
                warn!("remove_account: couldn't shred encrypted block device: {} (ignored)", err);
                AccountManagerError::new(AccountApiError::Resource).with_cause(err)
            }),
            Ok(None) => {
                // No error to propagate; when destroying an account which was
                // previously in the LOCKED state (rather than the AVAILABLE
                // state), try_destroy() may return Ok(None), indicating that
                // there was no internal minfs.
                Ok(())
            }
            Err(_) => {
                // As a last restort, attempt to find the remaining account
                // partition, bind to it, and shred that encrypted block.
                match self.find_account_partition().await {
                    Some(block_device) => {
                        let encrypted_block = self
                            .disk_manager
                            .bind_to_encrypted_block(block_device)
                            .await
                            .map_err(|err| {
                                error!(
                                    "remove_account: couldn't bind zxcrypt driver to encrypted \
                                    block device: {}",
                                    err
                                );
                                AccountManagerError::new(AccountApiError::Resource).with_cause(err)
                            })?;
                        match encrypted_block.shred().await {
                            Ok(()) => {}
                            Err(err) => {
                                // If shredding the encrypted block device fails
                                // here, it is because provisioning failed
                                // partway the first time. Do not propagate an
                                // error, just log and return OK.
                                warn!(
                                    "remove_account: couldn't shred encrypted block \
                                    device: {} (ignored)",
                                    err
                                );
                            }
                        }
                    }
                    None => {
                        info!("remove_account: no account partition to shred.");
                    }
                }
                Ok(())
            }
        }
    }

    async fn get_root_dir(&self) -> Result<fio::DirectoryProxy, AccountManagerError> {
        match self.state.lock().await.get_internals() {
            Some((_, minfs)) => {
                fuchsia_fs::clone_directory(minfs.root_dir(), fio::OpenFlags::CLONE_SAME_RIGHTS)
                    .map_err(|err| {
                        AccountManagerError::new(AccountApiError::Internal).with_cause(err)
                    })
            }
            None => Err(AccountManagerError::new(AccountApiError::Internal)),
        }
    }
}
