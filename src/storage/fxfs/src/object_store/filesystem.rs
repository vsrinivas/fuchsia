// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        debug_assert_not_too_long,
        errors::FxfsError,
        object_store::{
            allocator::{Allocator, Hold, Reservation},
            crypt::Crypt,
            journal::{super_block::SuperBlock, Journal},
            object_manager::ObjectManager,
            trace_duration,
            transaction::{
                AssocObj, LockKey, LockManager, MetadataReservation, Mutation, Options, ReadGuard,
                Transaction, TransactionHandler, TransactionLocks, WriteGuard,
                TRANSACTION_METADATA_MAX_AMOUNT,
            },
            ObjectStore,
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    fuchsia_async as fasync,
    futures::channel::oneshot::{channel, Sender},
    once_cell::sync::OnceCell,
    std::sync::{
        atomic::{self, AtomicBool},
        Arc, Mutex,
    },
    storage_device::{Device, DeviceHolder},
};

pub const MIN_BLOCK_SIZE: u32 = 4096;

pub struct Info {
    pub total_bytes: u64,
    pub used_bytes: u64,
    pub block_size: u32,
}

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
    async fn sync(&self, options: SyncOptions<'_>) -> Result<(), Error>;

    /// Returns the filesystem block size.
    fn block_size(&self) -> u32;

    /// Returns filesystem information.
    fn get_info(&self) -> Info;

    /// Returns the crypt interface.
    // TODO(csuter): This is going to need to be per-store eventually.
    fn crypt(&self) -> &dyn Crypt;
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
pub struct SyncOptions<'a> {
    pub flush_device: bool,

    // A precondition that is evaluated whilst a lock is held that determines whether or not the
    // sync needs to proceed.
    pub precondition: Option<Box<dyn FnOnce() -> bool + 'a + Send>>,
}

pub struct OpenFxFilesystem(Arc<FxFilesystem>);

impl OpenFxFilesystem {
    /// Waits for filesystem to be dropped (so callers should ensure all direct and indirect
    /// references are dropped) and returns the device.  No attempt is made at a graceful shutdown.
    pub async fn take_device(self) -> DeviceHolder {
        let (sender, receiver) = channel::<DeviceHolder>();
        self.device_sender
            .set(sender)
            .unwrap_or_else(|_| panic!("take_device should only be called once"));
        std::mem::drop(self);
        debug_assert_not_too_long!(receiver).unwrap()
    }
}

impl From<Arc<FxFilesystem>> for OpenFxFilesystem {
    fn from(fs: Arc<FxFilesystem>) -> Self {
        Self(fs)
    }
}

impl Drop for OpenFxFilesystem {
    fn drop(&mut self) {
        if !self.read_only && !self.closed.load(atomic::Ordering::SeqCst) {
            let this = self.0.clone();
            fasync::Task::spawn(async move {
                let _ = this.close().await;
            })
            .detach();
        }
    }
}

impl std::ops::Deref for OpenFxFilesystem {
    type Target = Arc<FxFilesystem>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

pub struct FxFilesystem {
    device: OnceCell<DeviceHolder>,
    block_size: u32,
    objects: Arc<ObjectManager>,
    journal: Journal,
    lock_manager: LockManager,
    flush_task: Mutex<Option<fasync::Task<()>>>,
    device_sender: OnceCell<Sender<DeviceHolder>>,
    closed: AtomicBool,
    read_only: bool,
    crypt: Arc<dyn Crypt>,
}

#[derive(Clone, Debug, Default)]
pub struct OpenOptions {
    pub trace: bool,
    pub read_only: bool,
}

impl FxFilesystem {
    pub async fn new_empty(
        device: DeviceHolder,
        crypt: Arc<dyn Crypt>,
    ) -> Result<OpenFxFilesystem, Error> {
        let objects = Arc::new(ObjectManager::new());
        let journal = Journal::new(objects.clone());
        let block_size = std::cmp::max(device.block_size(), MIN_BLOCK_SIZE);
        assert_eq!(block_size % MIN_BLOCK_SIZE, 0);
        let filesystem = Arc::new(FxFilesystem {
            device: OnceCell::new(),
            block_size,
            objects,
            journal,
            lock_manager: LockManager::new(),
            flush_task: Mutex::new(None),
            device_sender: OnceCell::new(),
            closed: AtomicBool::new(false),
            read_only: false,
            crypt,
        });
        filesystem.device.set(device).unwrap_or_else(|_| unreachable!());
        filesystem.journal.init_empty(filesystem.clone()).await?;
        if let Some(graveyard) = filesystem.objects.graveyard() {
            // Start the graveyard's background reaping task.
            graveyard.reap_async(filesystem.journal.journal_file_offset());
        }
        Ok(filesystem.into())
    }

    async fn open_with_options(
        device: DeviceHolder,
        options: OpenOptions,
        crypt: Arc<dyn Crypt>,
    ) -> Result<OpenFxFilesystem, Error> {
        let objects = Arc::new(ObjectManager::new());
        let journal = Journal::new(objects.clone());
        let block_size = std::cmp::max(device.block_size(), MIN_BLOCK_SIZE);
        assert_eq!(block_size % MIN_BLOCK_SIZE, 0);
        let filesystem = Arc::new(FxFilesystem {
            device: OnceCell::new(),
            block_size,
            objects,
            journal,
            lock_manager: LockManager::new(),
            flush_task: Mutex::new(None),
            device_sender: OnceCell::new(),
            closed: AtomicBool::new(false),
            read_only: options.read_only,
            crypt,
        });
        filesystem.device.set(device).unwrap_or_else(|_| unreachable!());
        filesystem.journal.replay(filesystem.clone()).await?;
        if !options.read_only {
            if let Some(graveyard) = filesystem.objects.graveyard() {
                // Purge the graveyard of old entries in a background task; once that's done the
                // reaper will continue to run and purge newly received entries.
                graveyard.reap_async(filesystem.journal.journal_file_offset());
            }
        }
        Ok(filesystem.into())
    }

    pub async fn open(
        device: DeviceHolder,
        crypt: Arc<dyn Crypt>,
    ) -> Result<OpenFxFilesystem, Error> {
        Self::open_with_options(device, OpenOptions { trace: false, read_only: false }, crypt).await
    }

    pub async fn open_read_only(
        device: DeviceHolder,
        crypt: Arc<dyn Crypt>,
    ) -> Result<OpenFxFilesystem, Error> {
        Self::open_with_options(device, OpenOptions { trace: false, read_only: true }, crypt).await
    }

    pub fn root_parent_store(&self) -> Arc<ObjectStore> {
        self.objects.root_parent_store()
    }

    pub fn root_store(&self) -> Arc<ObjectStore> {
        self.objects.root_store()
    }

    pub async fn close(&self) -> Result<(), Error> {
        assert_eq!(self.closed.swap(true, atomic::Ordering::SeqCst), false);
        if let Some(graveyard) = self.objects.graveyard() {
            debug_assert_not_too_long!(graveyard.wait_for_reap());
        }
        self.journal.stop_compactions().await;
        let sync_status =
            self.journal.sync(SyncOptions { flush_device: true, ..Default::default() }).await;
        if sync_status.is_err() {
            log::error!("Failed to sync filesystem; data may be lost: {:?}", sync_status);
        }
        self.journal.terminate();
        let flush_task = self.flush_task.lock().unwrap().take();
        if let Some(task) = flush_task {
            debug_assert_not_too_long!(task);
        }
        // Regardless of whether sync succeeds, we should close the device, since otherwise we will
        // crash instead of exiting gracefully.
        self.device().close().await.expect("Failed to close device");
        sync_status.map(|_| ())
    }

    pub fn super_block(&self) -> SuperBlock {
        self.journal.super_block()
    }

    async fn reservation_for_transaction<'a>(
        self: &Arc<Self>,
        options: Options<'a>,
    ) -> Result<(MetadataReservation, Option<&'a Reservation>, Option<Hold<'a>>), Error> {
        if !options.skip_journal_checks {
            // TODO(csuter): for now, we don't allow for transactions that might be inflight but
            // not committed.  In theory, if there are a large number of them, it would be possible
            // to run out of journal space.  We should probably have an in-flight limit.
            self.journal.check_journal_space().await?;
        }

        // We support three options for metadata space reservation:
        //
        //   1. We can borrow from the filesystem's metadata reservation.  This should only be
        //      be used on the understanding that eventually, potentially after a full compaction,
        //      there should be no net increase in space used.  For example, unlinking an object
        //      should eventually decrease the amount of space used and setting most attributes
        //      should not result in any change.
        //
        //   2. A reservation is provided in which case we'll place a hold on some of it for
        //      metadata.
        //
        //   3. No reservation is supplied, so we try and reserve space with the allocator now,
        //      and will return NoSpace if that fails.
        let mut hold = None;
        let metadata_reservation = if options.borrow_metadata_space {
            MetadataReservation::Borrowed
        } else {
            match options.allocator_reservation {
                Some(reservation) => {
                    hold = Some(reservation.hold(TRANSACTION_METADATA_MAX_AMOUNT)?);
                    MetadataReservation::Hold(TRANSACTION_METADATA_MAX_AMOUNT)
                }
                None => {
                    let reservation = self
                        .allocator()
                        .reserve(TRANSACTION_METADATA_MAX_AMOUNT)
                        .ok_or(FxfsError::NoSpace)?;
                    MetadataReservation::Reservation(reservation)
                }
            }
        };
        Ok((metadata_reservation, options.allocator_reservation, hold))
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

    async fn sync(&self, options: SyncOptions<'_>) -> Result<(), Error> {
        self.journal.sync(options).await.map(|_| ())
    }

    fn block_size(&self) -> u32 {
        self.block_size
    }

    fn get_info(&self) -> Info {
        Info {
            total_bytes: self.device.get().unwrap().size(),
            used_bytes: self.object_manager().allocator().get_used_bytes(),
            block_size: self.block_size(),
        }
    }

    fn crypt(&self) -> &dyn Crypt {
        self.crypt.as_ref()
    }
}

#[async_trait]
impl TransactionHandler for FxFilesystem {
    async fn new_transaction<'a>(
        self: Arc<Self>,
        locks: &[LockKey],
        options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        let (metadata_reservation, allocator_reservation, hold) =
            self.reservation_for_transaction(options).await?;
        let mut transaction =
            Transaction::new(self, metadata_reservation, &[LockKey::Filesystem], locks).await;
        hold.map(|mut h| h.take()); // Transaction takes ownership from here on.
        transaction.allocator_reservation = allocator_reservation;
        Ok(transaction)
    }

    async fn new_transaction_with_locks<'a>(
        self: Arc<Self>,
        locks: TransactionLocks<'_>,
        options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        let (metadata_reservation, allocator_reservation, hold) =
            self.reservation_for_transaction(options).await?;
        let mut transaction =
            Transaction::new_with_locks(self, metadata_reservation, &[LockKey::Filesystem], locks)
                .await;
        hold.map(|mut h| h.take()); // Transaction takes ownership from here on.
        transaction.allocator_reservation = allocator_reservation;
        Ok(transaction)
    }

    async fn transaction_lock<'a>(&'a self, lock_keys: &[LockKey]) -> TransactionLocks<'a> {
        let lock_manager: &LockManager = self.as_ref();
        TransactionLocks(lock_manager.txn_lock(lock_keys).await)
    }

    async fn commit_transaction(
        self: Arc<Self>,
        transaction: &mut Transaction<'_>,
    ) -> Result<u64, Error> {
        trace_duration!("FxFilesystem::commit_transaction");
        debug_assert_not_too_long!(self.lock_manager.commit_prepare(transaction));
        {
            let mut flush_task = self.flush_task.lock().unwrap();
            if flush_task.is_none() {
                let this = self.clone();
                *flush_task = Some(fasync::Task::spawn(async move {
                    this.journal.flush_task().await;
                }));
            }
        }
        let journal_offset = self.journal.commit(transaction).await?;
        Ok(journal_offset)
    }

    fn drop_transaction(&self, transaction: &mut Transaction<'_>) {
        // If we placed a hold for metadata space, return it now.
        if let MetadataReservation::Hold(hold_amount) = &mut transaction.metadata_reservation {
            transaction.allocator_reservation.unwrap().release(*hold_amount);
            *hold_amount = 0;
        }
        self.objects.drop_transaction(transaction);
        self.lock_manager.drop_transaction(transaction);
    }

    async fn read_lock<'a>(&'a self, lock_keys: &[LockKey]) -> ReadGuard<'a> {
        debug_assert_not_too_long!(self.lock_manager.read_lock(lock_keys))
    }

    async fn write_lock<'a>(&'a self, lock_keys: &[LockKey]) -> WriteGuard<'a> {
        debug_assert_not_too_long!(self.lock_manager.write_lock(lock_keys))
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
                crypt::InsecureCrypt,
                directory::Directory,
                fsck::fsck,
                transaction::{Options, TransactionHandler},
            },
        },
        fuchsia_async as fasync,
        futures::future::join_all,
        std::sync::Arc,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run(10, test)]
    async fn test_compaction() {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));

        // If compaction is not working correctly, this test will run out of space.
        let fs = FxFilesystem::new_empty(device, Arc::new(InsecureCrypt::new()))
            .await
            .expect("new_empty failed");
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
            transaction.commit().await.expect("commit failed");
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
