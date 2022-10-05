// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Contains testing utilities.

use crate::keys::Key;
use async_trait::async_trait;
use fuchsia_zircon::Status;
use futures::lock::Mutex;
use std::sync::Arc;
use storage_manager::minfs::disk::{
    testing::MockMinfs, DiskError, DiskManager, EncryptedBlockDevice, Partition,
};
use vfs::execution_scope::ExecutionScope;

/// A ref-counted counter that can be cloned and passed to a mock.
/// The mock can increment the counter on some event, and the test can verify that the event
/// occurred.
#[derive(Debug, Clone)]
pub struct CallCounter(Arc<std::sync::Mutex<usize>>);

impl CallCounter {
    /// Creates a new counter and initializes its value.
    pub fn new(initial: usize) -> Self {
        Self(Arc::new(std::sync::Mutex::new(initial)))
    }

    /// Read the count in the counter.
    pub fn count(&self) -> usize {
        *self.0.lock().unwrap()
    }

    /// Increment the value in the counter by one.
    pub fn increment(&self) {
        *self.0.lock().unwrap() += 1
    }
}

/// Whether a mock's input should be considered a match for the test case.
#[derive(Debug, Clone, Copy)]
pub enum Match {
    /// Any input is considered a match.
    Any,
    /// Regardless of input, there is no match.
    None,
}

/// Mock implementation of [`DiskManager`].
pub struct MockDiskManager {
    scope: ExecutionScope,
    // If no partition list is given, partitions() (from the DiskManager trait) will return
    // an error.
    maybe_partitions: Option<Vec<MockPartition>>,
    format_minfs_behavior: Result<(), fn() -> DiskError>,
    serve_minfs_fn: Arc<Mutex<dyn FnMut() -> Result<MockMinfs, DiskError> + Send>>,
}

impl Default for MockDiskManager {
    fn default() -> Self {
        let scope = ExecutionScope::build()
            .entry_constructor(vfs::directory::mutable::simple::tree_constructor(
                |_parent, _name| {
                    Ok(vfs::file::vmo::read_write(vfs::file::vmo::simple_init_vmo_with_capacity(
                        &[],
                        100,
                    )))
                },
            ))
            .new();
        Self {
            scope: scope.clone(),
            maybe_partitions: None,
            format_minfs_behavior: Ok(()),
            serve_minfs_fn: Arc::new(Mutex::new(move || Ok(MockMinfs::simple(scope.clone())))),
        }
    }
}

impl Drop for MockDiskManager {
    fn drop(&mut self) {
        self.scope.shutdown();
    }
}

#[async_trait]
impl DiskManager for MockDiskManager {
    type BlockDevice = MockBlockDevice;
    type Partition = MockPartition;
    type EncryptedBlockDevice = MockEncryptedBlockDevice;
    type Minfs = MockMinfs;

    async fn partitions(&self) -> Result<Vec<MockPartition>, DiskError> {
        self.maybe_partitions
            .clone()
            .ok_or_else(|| DiskError::GetBlockInfoFailed(Status::NOT_FOUND))
    }

    async fn has_zxcrypt_header(&self, block_dev: &MockBlockDevice) -> Result<bool, DiskError> {
        match &block_dev.zxcrypt_header_behavior {
            Ok(Match::Any) => Ok(true),
            Ok(Match::None) => Ok(false),
            Err(err_factory) => Err(err_factory()),
        }
    }

    async fn bind_to_encrypted_block(
        &self,
        block_dev: MockBlockDevice,
    ) -> Result<MockEncryptedBlockDevice, DiskError> {
        block_dev.bind_behavior.map_err(|err_factory| err_factory())
    }

    async fn format_minfs(&self, _block_dev: &MockBlockDevice) -> Result<(), DiskError> {
        self.format_minfs_behavior.clone().map_err(|err_factory| err_factory())
    }

    async fn serve_minfs(&self, _block_dev: MockBlockDevice) -> Result<MockMinfs, DiskError> {
        let mut locked_fn = self.serve_minfs_fn.lock().await;
        (*locked_fn)()
    }
}

impl MockDiskManager {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with_partition(mut self, partition: MockPartition) -> Self {
        self.maybe_partitions.get_or_insert_with(Vec::new).push(partition);
        self
    }

    pub fn with_serve_minfs<F>(mut self, serve_minfs: F) -> Self
    where
        F: FnMut() -> Result<MockMinfs, DiskError> + Send + 'static,
    {
        self.serve_minfs_fn = Arc::new(Mutex::new(serve_minfs));
        self
    }
}

/// A mock implementation of [`Partition`].
#[derive(Debug, Clone)]
pub struct MockPartition {
    // Whether the mock's `has_guid` method will match any given GUID, or produce an error.
    pub guid_behavior: Result<Match, fn() -> DiskError>,

    // Whether the mock's `has_label` method will match any given label, or produce an error.
    pub label_behavior: Result<Match, fn() -> DiskError>,

    // BlockDevice representing the partition data.
    pub block: MockBlockDevice,
}

#[async_trait]
impl Partition for MockPartition {
    type BlockDevice = MockBlockDevice;

    async fn has_guid(&self, _desired_guid: [u8; 16]) -> Result<bool, DiskError> {
        match &self.guid_behavior {
            Ok(Match::Any) => Ok(true),
            Ok(Match::None) => Ok(false),
            Err(err_factory) => Err(err_factory()),
        }
    }

    async fn has_label(&self, _desired_label: &str) -> Result<bool, DiskError> {
        match &self.label_behavior {
            Ok(Match::Any) => Ok(true),
            Ok(Match::None) => Ok(false),
            Err(err_factory) => Err(err_factory()),
        }
    }

    fn into_block_device(self) -> MockBlockDevice {
        self.block
    }
}

#[derive(Debug, Clone)]
pub struct MockBlockDevice {
    // Whether or not the block device has a zxcrypt header in the first block.
    pub zxcrypt_header_behavior: Result<Match, fn() -> DiskError>,
    // Whether or not the block device should succeed in binding zxcrypt
    pub bind_behavior: Result<MockEncryptedBlockDevice, fn() -> DiskError>,
}

#[derive(Debug, Clone)]
pub enum UnsealBehavior {
    AcceptAnyKey(Box<MockBlockDevice>),
    AcceptExactKeys((Vec<Key>, Box<MockBlockDevice>)),
    RejectWithError(fn() -> DiskError),
}

/// A mock implementation of [`EncryptedBlockDevice`].
#[derive(Debug, Clone)]
pub struct MockEncryptedBlockDevice {
    // Whether the block encrypted block device can format successfully.
    pub format_behavior: Result<(), fn() -> DiskError>,
    // What behavior the encrypted block device should have when unseal is attempted.
    pub unseal_behavior: UnsealBehavior,
    // Whether the block encrypted block device can be shredded successfully
    pub shred_behavior: Result<(), fn() -> DiskError>,
}

#[async_trait]
impl EncryptedBlockDevice for MockEncryptedBlockDevice {
    type BlockDevice = MockBlockDevice;

    async fn format(&self, _key: &Key) -> Result<(), DiskError> {
        self.format_behavior.clone().map_err(|err_factory| err_factory())
    }

    async fn unseal(&self, key: &Key) -> Result<MockBlockDevice, DiskError> {
        match &self.unseal_behavior {
            UnsealBehavior::AcceptAnyKey(b) => Ok(*b.clone()),
            UnsealBehavior::AcceptExactKeys((keys, b)) => {
                if keys.contains(&key) {
                    Ok(*b.clone())
                } else {
                    Err(DiskError::FailedToUnsealZxcrypt(Status::ACCESS_DENIED))
                }
            }
            UnsealBehavior::RejectWithError(err_factory) => Err(err_factory()),
        }
    }

    async fn seal(&self) -> Result<(), DiskError> {
        Ok(())
    }

    async fn shred(&self) -> Result<(), DiskError> {
        self.shred_behavior.clone().map_err(|err_factory| err_factory())
    }
}
