// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        device::Device,
        object_store::{
            allocator::Allocator,
            filesystem::{Filesystem, ObjectManager, ObjectSync},
            journal::JournalCheckpoint,
            transaction::{
                LockKey, LockManager, ReadGuard, Transaction, TransactionHandler, TxnMutation,
            },
            ObjectStore,
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    std::sync::Arc,
};

pub struct FakeFilesystem {
    device: Arc<dyn Device>,
    object_manager: Arc<ObjectManager>,
    lock_manager: LockManager,
}

impl FakeFilesystem {
    pub fn new(device: Arc<dyn Device>) -> Arc<Self> {
        let object_manager = Arc::new(ObjectManager::new());
        Arc::new(FakeFilesystem { device, object_manager, lock_manager: LockManager::new() })
    }

    pub fn object_manager(&self) -> &Arc<ObjectManager> {
        &self.object_manager
    }
}

#[async_trait]
impl Filesystem for FakeFilesystem {
    fn register_store(&self, store: &Arc<ObjectStore>) {
        self.object_manager.register_store(store);
    }

    fn begin_object_sync(&self, object_id: u64) -> ObjectSync {
        self.object_manager.begin_object_sync(object_id)
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
}

#[async_trait]
impl TransactionHandler for FakeFilesystem {
    async fn new_transaction<'a>(
        self: Arc<Self>,
        locks: &[LockKey],
    ) -> Result<Transaction<'a>, Error> {
        let mut locks: Vec<_> = locks.iter().cloned().collect();
        locks.sort_unstable();
        self.lock_manager.lock(&locks).await;
        Ok(Transaction::new(self, locks))
    }

    async fn commit_transaction(&self, mut transaction: Transaction<'_>) {
        self.lock_manager.commit_prepare(&transaction).await;
        for TxnMutation { object_id, mutation, associated_object } in
            std::mem::take(&mut transaction.mutations)
        {
            self.object_manager
                .apply_mutation(
                    object_id,
                    mutation,
                    false,
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
