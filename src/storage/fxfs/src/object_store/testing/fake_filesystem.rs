// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{
        allocator::Allocator,
        crypt::{Crypt, InsecureCrypt},
        filesystem::{Filesystem, Info, SyncOptions},
        journal::JournalCheckpoint,
        object_manager::ObjectManager,
        transaction::{
            LockKey, LockManager, MetadataReservation, Options, ReadGuard, Transaction,
            TransactionHandler, TransactionLocks, WriteGuard,
        },
        ObjectStore,
    },
    anyhow::Error,
    async_trait::async_trait,
    std::sync::{
        atomic::{AtomicU64, Ordering},
        Arc,
    },
    storage_device::{Device, DeviceHolder},
};

pub struct FakeFilesystem {
    device: DeviceHolder,
    object_manager: Arc<ObjectManager>,
    lock_manager: LockManager,
    num_syncs: AtomicU64,
    crypt: InsecureCrypt,
}

impl FakeFilesystem {
    pub fn new(device: DeviceHolder) -> Arc<Self> {
        let object_manager = Arc::new(ObjectManager::new());
        Arc::new(FakeFilesystem {
            device,
            object_manager,
            lock_manager: LockManager::new(),
            num_syncs: AtomicU64::new(0),
            crypt: InsecureCrypt::new(),
        })
    }
}

#[async_trait]
impl Filesystem for FakeFilesystem {
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

    async fn sync(&self, _: SyncOptions<'_>) -> Result<(), Error> {
        self.num_syncs.fetch_add(1u64, Ordering::Relaxed);
        Ok(())
    }

    fn block_size(&self) -> u32 {
        4096
    }

    fn get_info(&self) -> Info {
        Info { total_bytes: 1024 * 1024, used_bytes: 0, block_size: 4096 }
    }

    fn crypt(&self) -> &dyn Crypt {
        &self.crypt
    }
}

#[async_trait]
impl TransactionHandler for FakeFilesystem {
    async fn new_transaction<'a>(
        self: Arc<Self>,
        locks: &[LockKey],
        options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        let reservation = if options.borrow_metadata_space {
            MetadataReservation::Borrowed
        } else {
            MetadataReservation::Reservation(self.allocator().reserve_at_most(10000))
        };
        Ok(Transaction::new(self, reservation, &[], locks).await)
    }

    async fn new_transaction_with_locks<'a>(
        self: Arc<Self>,
        locks: TransactionLocks<'_>,
        options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        let reservation = if options.borrow_metadata_space {
            MetadataReservation::Borrowed
        } else {
            MetadataReservation::Reservation(self.allocator().reserve_at_most(10000))
        };
        Ok(Transaction::new_with_locks(self, reservation, &[], locks).await)
    }

    async fn transaction_lock<'a>(&'a self, lock_keys: &[LockKey]) -> TransactionLocks<'a> {
        let lock_manager: &LockManager = self.as_ref();
        TransactionLocks(lock_manager.txn_lock(lock_keys).await)
    }

    async fn commit_transaction(
        self: Arc<Self>,
        transaction: &mut Transaction<'_>,
    ) -> Result<u64, Error> {
        let checkpoint =
            JournalCheckpoint { file_offset: self.num_syncs.load(Ordering::Relaxed), checksum: 0 };
        self.lock_manager.commit_prepare(transaction).await;
        self.object_manager.apply_transaction(transaction, &checkpoint).await;
        Ok(0)
    }

    fn drop_transaction(&self, transaction: &mut Transaction<'_>) {
        self.object_manager.drop_transaction(transaction);
        self.lock_manager.drop_transaction(transaction);
    }

    async fn read_lock<'a>(&'a self, lock_keys: &[LockKey]) -> ReadGuard<'a> {
        self.lock_manager.read_lock(lock_keys).await
    }

    async fn write_lock<'a>(&'a self, lock_keys: &[LockKey]) -> WriteGuard<'a> {
        self.lock_manager.write_lock(lock_keys).await
    }
}

impl AsRef<LockManager> for FakeFilesystem {
    fn as_ref(&self) -> &LockManager {
        &self.lock_manager
    }
}
