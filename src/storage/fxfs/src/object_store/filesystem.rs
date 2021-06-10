// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_store::{
            allocator::{Allocator, Reservation},
            journal::{super_block::SuperBlock, Journal},
            object_manager::ObjectManager,
            trace_duration,
            transaction::{
                AssocObj, LockKey, LockManager, Mutation, Options, ReadGuard, Transaction,
                TransactionHandler, WriteGuard,
            },
            ObjectStore,
        },
    },
    anyhow::{bail, Error},
    async_trait::async_trait,
    fuchsia_async as fasync,
    futures::channel::oneshot::{channel, Sender},
    once_cell::sync::OnceCell,
    std::sync::{Arc, Mutex},
    storage_device::{Device, DeviceHolder},
};

const FLUSH_RESERVATION_SIZE: u64 = 524288;

#[async_trait]
pub trait Filesystem: TransactionHandler {
    /// Returns access to the undeyling device.
    fn device(&self) -> Arc<dyn Device>;

    /// Returns the root store or panics if it is not available.
    fn root_store(&self) -> Arc<ObjectStore>;

    /// Returns the allocator or panics if it is not available.
    fn allocator(&self) -> Arc<dyn Allocator>;

    /// Returns the object manager for the filesystem.
    fn object_manager(&self) -> Arc<ObjectManager>;

    /// Flushes buffered data to the underlying device.
    async fn sync(&self, options: SyncOptions) -> Result<(), Error>;

    /// Returns a reservation to be used for flushing in-memory data.
    fn flush_reservation(&self) -> &Reservation;
}

#[async_trait]
pub trait Mutations: Send + Sync {
    /// Objects that use the journaling system to track mutations should implement this trait.  This
    /// method will get called when the transaction commits, which can either be during live
    /// operation or during journal replay, in which case transaction will be None.  Also see
    /// ObjectManager's apply_mutation method.
    async fn apply_mutation(
        &self,
        mutation: Mutation,
        transaction: Option<&Transaction<'_>>,
        log_offset: u64,
        assoc_obj: AssocObj<'_>,
    );

    /// Called when a transaction fails to commit.
    fn drop_mutation(&self, mutation: Mutation, transaction: &Transaction<'_>);

    /// Flushes in-memory changes to the device (to allow journal space to be freed).
    async fn flush(&self) -> Result<(), Error>;
}

#[derive(Default)]
pub struct SyncOptions {}

pub struct FxFilesystem {
    device: OnceCell<DeviceHolder>,
    objects: Arc<ObjectManager>,
    journal: Journal,
    lock_manager: LockManager,
    compaction: Mutex<Compaction>,
    device_sender: OnceCell<Sender<DeviceHolder>>,
    flush_reservation: OnceCell<Reservation>,
}

enum Compaction {
    Idle,
    Paused,
    Task(fasync::Task<()>),
}

#[derive(Clone, Debug, Default)]
pub struct OpenOptions {
    pub trace: bool,
    pub read_only: bool,
}

impl FxFilesystem {
    pub async fn new_empty(device: DeviceHolder) -> Result<Arc<FxFilesystem>, Error> {
        let objects = Arc::new(ObjectManager::new());
        let journal = Journal::new(objects.clone());
        let filesystem = Arc::new(FxFilesystem {
            device: OnceCell::new(),
            objects,
            journal,
            lock_manager: LockManager::new(),
            compaction: Mutex::new(Compaction::Idle),
            device_sender: OnceCell::new(),
            flush_reservation: OnceCell::new(),
        });
        filesystem.device.set(device).unwrap_or_else(|_| unreachable!());
        filesystem.journal.init_empty(filesystem.clone()).await?;
        let _ = filesystem.flush_reservation.set(filesystem.allocator().reserve(0).unwrap());
        Ok(filesystem)
    }

    async fn open_with_options(
        device: DeviceHolder,
        options: OpenOptions,
    ) -> Result<Arc<FxFilesystem>, Error> {
        let objects = Arc::new(ObjectManager::new());
        let journal = Journal::new(objects.clone());
        journal.set_trace(options.trace);
        let filesystem = Arc::new(FxFilesystem {
            device: OnceCell::new(),
            objects,
            journal,
            lock_manager: LockManager::new(),
            compaction: Mutex::new(Compaction::Idle),
            device_sender: OnceCell::new(),
            flush_reservation: OnceCell::new(),
        });
        filesystem.device.set(device).unwrap_or_else(|_| unreachable!());
        filesystem.journal.replay(filesystem.clone()).await?;
        let _ = filesystem.flush_reservation.set(filesystem.allocator().reserve_at_most(0));
        if !options.read_only {
            if let Some(graveyard) = filesystem.objects.graveyard() {
                // Purge the graveyard of old entries in a background task.
                graveyard.reap_async(filesystem.journal.journal_file_offset());
            }
        }
        Ok(filesystem)
    }

    pub fn set_trace(&self, v: bool) {
        self.journal.set_trace(v);
    }

    pub async fn open(device: DeviceHolder) -> Result<Arc<FxFilesystem>, Error> {
        Self::open_with_options(device, OpenOptions { trace: false, read_only: false }).await
    }

    pub async fn open_read_only(device: DeviceHolder) -> Result<Arc<FxFilesystem>, Error> {
        Self::open_with_options(device, OpenOptions { trace: false, read_only: true }).await
    }

    pub fn root_parent_store(&self) -> Arc<ObjectStore> {
        self.objects.root_parent_store()
    }

    pub fn root_store(&self) -> Arc<ObjectStore> {
        self.objects.root_store()
    }

    pub async fn close(&self) -> Result<(), Error> {
        let compaction =
            std::mem::replace(&mut *self.compaction.lock().unwrap(), Compaction::Paused);
        if let Compaction::Task(task) = compaction {
            task.await;
        }
        if let Some(graveyard) = self.objects.graveyard() {
            graveyard.wait_for_reap().await;
        }
        // Regardless of whether sync succeeds, we should close the device, since otherwise we will
        // crash instead of exiting gracefully.
        let sync_status = self.journal.sync(SyncOptions::default()).await;
        if sync_status.is_err() {
            log::error!("Failed to sync filesystem; data may be lost: {:?}", sync_status);
        }
        self.device().close().await.expect("Failed to close device");
        sync_status
    }

    async fn compact(self: Arc<Self>) {
        loop {
            log::debug!("Compaction starting");
            trace_duration!("FxFilesystem::compact");
            if let Err(e) = self.objects.flush().await {
                log::error!("Compaction encountered error: {:?}", e);
                return;
            }
            if let Err(e) = self.journal.write_super_block().await {
                log::error!("Error writing journal super-block: {:?}", e);
                return;
            }
            let mut compaction = self.compaction.lock().unwrap();
            log::debug!("Compaction finished");
            if let Compaction::Paused = *compaction {
                break;
            }
            // Check to see if we need to do another compaction immediately.
            if !self.journal.should_flush() {
                *compaction = Compaction::Idle;
                break;
            }
        }
    }

    /// Waits for filesystem to be dropped (so callers should ensure all direct and indirect
    /// references are dropped) and returns the device.  No attempt is made at a graceful shutdown.
    pub async fn take_device(self: Arc<FxFilesystem>) -> DeviceHolder {
        let (sender, receiver) = channel::<DeviceHolder>();
        self.device_sender
            .set(sender)
            .unwrap_or_else(|_| panic!("take_device should only be called once"));
        std::mem::drop(self);
        receiver.await.unwrap()
    }

    pub fn super_block(&self) -> SuperBlock {
        self.journal.super_block()
    }

    // Returns the reservation, and a bool where true means the reservation is at its target size
    // and false means it's not (i.e. we are in a low space condition).
    fn update_flush_reservation(&self) -> (&Reservation, bool) {
        let flush_reservation = self.flush_reservation.get().unwrap();
        (flush_reservation, flush_reservation.try_top_up(FLUSH_RESERVATION_SIZE))
    }
}

impl Drop for FxFilesystem {
    fn drop(&mut self) {
        if let Some(sender) = self.device_sender.take() {
            // We don't care if this fails to send.
            let _ = sender.send(self.device.take().unwrap());
        }
    }
}

#[async_trait]
impl Filesystem for FxFilesystem {
    fn device(&self) -> Arc<dyn Device> {
        Arc::clone(self.device.get().unwrap())
    }

    fn root_store(&self) -> Arc<ObjectStore> {
        self.objects.root_store()
    }

    fn allocator(&self) -> Arc<dyn Allocator> {
        self.objects.allocator()
    }

    fn object_manager(&self) -> Arc<ObjectManager> {
        self.objects.clone()
    }

    async fn sync(&self, options: SyncOptions) -> Result<(), Error> {
        self.journal.sync(options).await
    }

    fn flush_reservation(&self) -> &Reservation {
        self.update_flush_reservation().0
    }
}

#[async_trait]
impl TransactionHandler for FxFilesystem {
    async fn new_transaction<'a>(
        self: Arc<Self>,
        locks: &[LockKey],
        options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        if !options.skip_journal_checks {
            // TODO(csuter): for now, we don't allow for transactions that might be inflight but
            // not committed.  In theory, if there are a large number of them, it would be possible
            // to run out of journal space.  We should probably have an in-flight limit.
            self.journal.check_journal_space().await;
            if options.allocator_reservation.is_none() {
                if !self.update_flush_reservation().1 && !options.skip_space_checks {
                    bail!(FxfsError::NoSpace);
                }
            }
        }
        let mut transaction = Transaction::new(self, &[LockKey::Filesystem], locks).await;
        transaction.allocator_reservation = options.allocator_reservation;
        Ok(transaction)
    }

    async fn commit_transaction(self: Arc<Self>, transaction: &mut Transaction<'_>) {
        trace_duration!("FxFilesystem::commit_transaction");
        self.lock_manager.commit_prepare(transaction).await;
        self.journal.commit(transaction).await;
        let mut compaction = self.compaction.lock().unwrap();
        if let Compaction::Idle = *compaction {
            if self.journal.should_flush() {
                *compaction = Compaction::Task(fasync::Task::spawn(self.clone().compact()));
            }
        }
    }

    fn drop_transaction(&self, transaction: &mut Transaction<'_>) {
        self.objects.drop_transaction(transaction);
        self.lock_manager.drop_transaction(transaction);
    }

    async fn read_lock<'a>(&'a self, lock_keys: &[LockKey]) -> ReadGuard<'a> {
        self.lock_manager.read_lock(lock_keys).await
    }

    async fn write_lock<'a>(&'a self, lock_keys: &[LockKey]) -> WriteGuard<'a> {
        self.lock_manager.write_lock(lock_keys).await
    }
}

impl AsRef<LockManager> for FxFilesystem {
    fn as_ref(&self) -> &LockManager {
        &self.lock_manager
    }
}

// TODO(csuter): How do we ensure sync prior to drop?

#[cfg(test)]
mod tests {
    use {
        super::{Filesystem, FxFilesystem, SyncOptions},
        crate::{
            object_handle::{ObjectHandle, ObjectHandleExt},
            object_store::{
                directory::Directory,
                fsck::fsck,
                transaction::{Options, TransactionHandler},
            },
        },
        fuchsia_async as fasync,
        futures::future::join_all,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run(10, test)]
    async fn test_compaction() {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));

        // If compaction is not working correctly, this test will run out of space.
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let root_store = fs.root_store();
        let root_directory = Directory::open(&root_store, root_store.root_directory_object_id())
            .await
            .expect("open failed");

        let mut tasks = Vec::new();
        for i in 0..2 {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let handle = root_directory
                .create_child_file(&mut transaction, &format!("{}", i))
                .await
                .expect("create_child_file failed");
            transaction.commit().await;
            tasks.push(fasync::Task::spawn(async move {
                const TEST_DATA: &[u8] = b"hello";
                let mut buf = handle.allocate_buffer(TEST_DATA.len());
                buf.as_mut_slice().copy_from_slice(TEST_DATA);
                for _ in 0..1500 {
                    handle.write(0, buf.as_ref()).await.expect("write failed");
                }
            }));
        }
        join_all(tasks).await;
        fs.sync(SyncOptions::default()).await.expect("sync failed");

        fsck(&fs).await.expect("fsck failed");
        fs.close().await.expect("Close failed");
    }
}
