// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{
        allocator::Allocator,
        filesystem::{Filesystem, ObjectManager, SyncOptions},
        journal::JournalCheckpoint,
        transaction::{
            LockKey, LockManager, Options, ReadGuard, Transaction, TransactionHandler, TxnMutation,
        },
        ObjectStore,
    },
    anyhow::Error,
    async_trait::async_trait,
    std::sync::Arc,
    storage_device::{Device, DeviceHolder},
};

pub struct FakeFilesystem {
    device: DeviceHolder,
    object_manager: Arc<ObjectManager>,
    lock_manager: LockManager,
}

impl FakeFilesystem {
    pub fn new(device: DeviceHolder) -> Arc<Self> {
        let object_manager = Arc::new(ObjectManager::new());
        Arc::new(FakeFilesystem { device, object_manager, lock_manager: LockManager::new() })
    }
}

#[async_trait]
impl Filesystem for FakeFilesystem {
    fn register_store(&self, store: &Arc<ObjectStore>) {
        self.object_manager.register_store(store);
    }

    fn device(&self) -> Arc<dyn Device> {
        self.device.clone()
    }

    fn root_store(&self) -> Arc<ObjectStore> {
        self.object_manager.root_store()
    }

    fn allocator(&self) -> Arc<dyn Allocator> {
        self.object_manager.allocator()
    }

    fn object_manager(&self) -> Arc<ObjectManager> {
        self.object_manager.clone()
    }

    async fn sync(&self, _: SyncOptions) -> Result<(), Error> {
        Ok(())
    }
}

#[async_trait]
impl TransactionHandler for FakeFilesystem {
    async fn new_transaction<'a>(
        self: Arc<Self>,
        locks: &[LockKey],
        _options: Options,
    ) -> Result<Transaction<'a>, Error> {
        Ok(Transaction::new(self, &[], locks).await)
    }

    async fn commit_transaction(self: Arc<Self>, transaction: &mut Transaction<'_>) {
        self.lock_manager.commit_prepare(transaction).await;
        for TxnMutation { object_id, mutation, associated_object } in
            std::mem::take(&mut transaction.mutations)
        {
            self.object_manager
                .apply_mutation(
                    object_id,
                    mutation,
                    Some(transaction),
                    &JournalCheckpoint::default(),
                    associated_object,
                )
                .await;
        }
    }

    fn drop_transaction(&self, transaction: &mut Transaction<'_>) {
        self.object_manager.drop_transaction(transaction);
        self.lock_manager.drop_transaction(transaction);
    }

    async fn read_lock<'a>(&'a self, lock_keys: &[LockKey]) -> ReadGuard<'a> {
        self.lock_manager.read_lock(lock_keys).await
    }
}

impl AsRef<LockManager> for FakeFilesystem {
    fn as_ref(&self) -> &LockManager {
        &self.lock_manager
    }
}
