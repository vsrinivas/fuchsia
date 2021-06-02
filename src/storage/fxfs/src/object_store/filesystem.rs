// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_handle::INVALID_OBJECT_ID,
        object_store::{
            allocator::{Allocator, Reservation},
            graveyard::Graveyard,
            journal::{super_block::SuperBlock, Journal, JournalCheckpoint},
            transaction::{
                AssocObj, AssociatedObject, LockKey, LockManager, Mutation, Options, ReadGuard,
                Transaction, TransactionHandler, TxnMutation, WriteGuard,
            },
            ObjectStore,
        },
    },
    anyhow::{bail, Error},
    async_trait::async_trait,
    fuchsia_async as fasync,
    futures::channel::oneshot::{channel, Sender},
    once_cell::sync::OnceCell,
    std::{
        collections::HashMap,
        sync::{Arc, Mutex, RwLock},
    },
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

pub struct ObjectManager {
    objects: RwLock<Objects>,
}

// We currently maintain strong references to all stores that have been opened, but there's no
// currently no mechanism for releasing stores that aren't being used.
struct Objects {
    stores: HashMap<u64, Arc<ObjectStore>>,
    root_parent_store_object_id: u64,
    root_store_object_id: u64,
    allocator_object_id: u64,
    allocator: Option<Arc<dyn Allocator>>,

    // Records dependencies on the journal for objects i.e. an entry for object ID 1, would mean it
    // has a dependency on journal records from that offset.
    journal_file_checkpoints: HashMap<u64, JournalCheckpoint>,

    graveyard: Option<Arc<Graveyard>>,
}

impl ObjectManager {
    pub fn new() -> ObjectManager {
        ObjectManager {
            objects: RwLock::new(Objects {
                stores: HashMap::new(),
                root_parent_store_object_id: INVALID_OBJECT_ID,
                root_store_object_id: INVALID_OBJECT_ID,
                allocator_object_id: INVALID_OBJECT_ID,
                allocator: None,
                journal_file_checkpoints: HashMap::new(),
                graveyard: None,
            }),
        }
    }

    pub fn store_object_ids(&self) -> Vec<u64> {
        self.objects.read().unwrap().stores.keys().cloned().collect()
    }

    pub fn root_parent_store(&self) -> Arc<ObjectStore> {
        let objects = self.objects.read().unwrap();
        objects.stores.get(&objects.root_parent_store_object_id).unwrap().clone()
    }

    pub fn set_root_parent_store_object_id(&self, object_id: u64) {
        let mut objects = self.objects.write().unwrap();
        assert!(objects.stores.contains_key(&object_id));
        objects.root_parent_store_object_id = object_id;
    }

    /// Asserts if the store's already registered.  Most callers will want |get_or_register_store|,
    /// since this call is susceptible to races; it is only appropriate (and useful to catch bugs)
    /// when we expect that only one call ought to happen to register the store (e.g. during
    /// bootstrap, or creating a new store).
    pub fn register_store_strict(&self, store: Arc<ObjectStore>) {
        let mut objects = self.objects.write().unwrap();
        assert_ne!(store.store_object_id(), objects.allocator_object_id);
        assert!(objects.stores.insert(store.store_object_id(), store).is_none());
    }

    /// Fetches the store, or invokes |create_fn| to install it and returns that object.
    pub fn get_or_register_store(
        &self,
        store_object_id: u64,
        create_fn: impl FnOnce() -> Arc<ObjectStore>,
    ) -> Arc<ObjectStore> {
        let mut objects = self.objects.write().unwrap();
        assert_ne!(store_object_id, objects.allocator_object_id);
        objects.stores.entry(store_object_id).or_insert_with(create_fn).clone()
    }

    #[cfg(test)]
    pub fn forget_store(&self, store_object_id: u64) {
        let mut objects = self.objects.write().unwrap();
        assert_ne!(store_object_id, objects.allocator_object_id);
        objects.stores.remove(&store_object_id);
    }

    pub fn store(&self, store_object_id: u64) -> Option<Arc<ObjectStore>> {
        self.objects.read().unwrap().stores.get(&store_object_id).cloned()
    }

    pub fn set_root_store_object_id(&self, object_id: u64) {
        let mut objects = self.objects.write().unwrap();
        assert!(objects.stores.contains_key(&object_id));
        objects.root_store_object_id = object_id;
    }

    pub fn root_store(&self) -> Arc<ObjectStore> {
        let objects = self.objects.read().unwrap();
        objects.stores.get(&objects.root_store_object_id).unwrap().clone()
    }

    pub fn set_allocator(&self, allocator: Arc<dyn Allocator>) {
        let mut objects = self.objects.write().unwrap();
        assert!(!objects.stores.contains_key(&allocator.object_id()));
        objects.allocator_object_id = allocator.object_id();
        objects.allocator = Some(allocator.clone());
    }

    pub fn allocator(&self) -> Arc<dyn Allocator> {
        self.objects.read().unwrap().allocator.clone().unwrap()
    }

    /// The journaling system should call this when a mutation needs to be applied. |replay|
    /// indicates whether this is for replay.  |checkpoint| indicates the location in the journal
    /// file for this mutation and is used to keep track of each object's dependencies on the
    /// journal.
    pub async fn apply_mutation(
        &self,
        object_id: u64,
        mutation: Mutation,
        transaction: Option<&Transaction<'_>>,
        checkpoint: &JournalCheckpoint,
        associated_object: AssocObj<'_>,
    ) {
        let object = {
            let mut objects = self.objects.write().unwrap();
            objects.journal_file_checkpoints.entry(object_id).or_insert_with(|| checkpoint.clone());
            if object_id == objects.allocator_object_id {
                Some(objects.allocator.clone().unwrap().as_mutations())
            } else {
                objects.stores.get(&object_id).map(|x| x.clone() as Arc<dyn Mutations>)
            }
        }
        .unwrap_or_else(|| self.root_store().lazy_open_store(object_id));
        associated_object.will_apply_mutation(&mutation);
        object
            .apply_mutation(mutation, transaction, checkpoint.file_offset, associated_object)
            .await;
    }

    // Drops a transaction.  This is called automatically when a transaction is dropped.  If the
    // transaction has been committed, it should contain no mutations and so nothing will get rolled
    // back.  For each mutation, drop_mutation is called to allow for roll back (e.g. the allocator
    // will unreserve allocations).
    pub fn drop_transaction(&self, transaction: &mut Transaction<'_>) {
        for TxnMutation { object_id, mutation, .. } in std::mem::take(&mut transaction.mutations) {
            self.object(object_id).map(|o| o.drop_mutation(mutation, transaction));
        }
    }

    /// Returns the journal file offsets that each object depends on and the checkpoint for the
    /// minimum offset.
    pub fn journal_file_offsets(&self) -> (HashMap<u64, u64>, Option<JournalCheckpoint>) {
        let objects = self.objects.read().unwrap();
        let mut min_checkpoint = None;
        let mut offsets = HashMap::new();
        for (&object_id, checkpoint) in &objects.journal_file_checkpoints {
            match &mut min_checkpoint {
                None => min_checkpoint = Some(checkpoint),
                Some(ref mut min_checkpoint) => {
                    if checkpoint.file_offset < min_checkpoint.file_offset {
                        *min_checkpoint = checkpoint;
                    }
                }
            }
            offsets.insert(object_id, checkpoint.file_offset);
        }
        (offsets, min_checkpoint.cloned())
    }

    /// Returns true if the object identified by `object_id` is known to have updates recorded in
    /// the journal that the object depends upon.
    pub fn needs_flush(&self, object_id: u64) -> bool {
        self.objects.read().unwrap().journal_file_checkpoints.contains_key(&object_id)
    }

    pub fn graveyard(&self) -> Option<Arc<Graveyard>> {
        self.objects.read().unwrap().graveyard.clone()
    }

    pub fn register_graveyard(&self, graveyard: Arc<Graveyard>) {
        self.objects.write().unwrap().graveyard = Some(graveyard);
    }

    /// Flushes all known objects.  This will then allow the journal space to be freed.
    pub async fn flush(&self) -> Result<(), Error> {
        let object_ids: Vec<_> =
            self.objects.read().unwrap().journal_file_checkpoints.keys().cloned().collect();
        for object_id in object_ids {
            self.object(object_id).unwrap().flush().await?;
        }
        Ok(())
    }

    fn object(&self, object_id: u64) -> Option<Arc<dyn Mutations>> {
        let objects = self.objects.read().unwrap();
        if object_id == objects.allocator_object_id {
            Some(objects.allocator.clone().unwrap().as_mutations())
        } else {
            objects.stores.get(&object_id).map(|x| x.clone() as Arc<dyn Mutations>)
        }
    }
}

/// ObjectFlush is used by objects to indicate some kind of event such that if successful, existing
/// mutation records are no longer required from the journal.  For example, for object stores, it is
/// used when the in-memory layer is persisted since once that is done the records in the journal
/// are no longer required.  Clients must make sure to call the commit function upon success; the
/// default is to roll back.
#[must_use]
pub struct ObjectFlush {
    object_manager: Arc<ObjectManager>,
    object_id: u64,
    old_journal_file_checkpoint: OnceCell<JournalCheckpoint>,
}

impl ObjectFlush {
    pub fn new(object_manager: Arc<ObjectManager>, object_id: u64) -> Self {
        Self { object_manager, object_id, old_journal_file_checkpoint: OnceCell::new() }
    }

    /// This marks the point at which the flush is beginning.  This begins a commitment (in the
    /// absence of errors) to flush _all_ mutations that were made to the object prior to this point
    /// and should therefore be called when appropriate locks are held (see the AssociatedObject
    /// implementation below).  Mutations that come after this will be preserved in the journal
    /// until the next flush.  This can panic if called more than once; it shouldn't be called
    /// directly if being used as an AssociatedObject since will_apply_mutation will call it below.
    pub fn begin(&self) {
        if let Some(checkpoint) = self
            .object_manager
            .objects
            .write()
            .unwrap()
            .journal_file_checkpoints
            .remove(&self.object_id)
        {
            self.old_journal_file_checkpoint.set(checkpoint).unwrap();
        }
    }

    pub fn commit(mut self) {
        self.old_journal_file_checkpoint.take();
    }
}

impl Drop for ObjectFlush {
    fn drop(&mut self) {
        if let Some(checkpoint) = self.old_journal_file_checkpoint.take() {
            self.object_manager
                .objects
                .write()
                .unwrap()
                .journal_file_checkpoints
                .insert(self.object_id, checkpoint);
        }
    }
}

/// ObjectFlush can be used as an associated object in a transaction such that we begin the flush at
/// the appropriate time (whilst a lock is held on the journal).
impl AssociatedObject for ObjectFlush {
    fn will_apply_mutation(&self, _: &Mutation) {
        self.begin();
    }
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

    pub async fn open_with_trace(
        device: DeviceHolder,
        trace: bool,
    ) -> Result<Arc<FxFilesystem>, Error> {
        let objects = Arc::new(ObjectManager::new());
        let journal = Journal::new(objects.clone());
        journal.set_trace(trace);
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
        let _ = filesystem.flush_reservation.set(filesystem.allocator().reserve(0).unwrap());
        Ok(filesystem)
    }

    pub fn set_trace(&self, v: bool) {
        self.journal.set_trace(v);
    }

    pub async fn open(device: DeviceHolder) -> Result<Arc<FxFilesystem>, Error> {
        Self::open_with_trace(device, false).await
    }

    pub fn root_parent_store(&self) -> Arc<ObjectStore> {
        self.objects.root_parent_store()
    }

    pub fn root_store(&self) -> Arc<ObjectStore> {
        self.objects.root_store()
    }

    pub fn store(&self, object_id: u64) -> Option<Arc<ObjectStore>> {
        self.objects.store(object_id)
    }

    pub async fn close(&self) -> Result<(), Error> {
        let compaction =
            std::mem::replace(&mut *self.compaction.lock().unwrap(), Compaction::Paused);
        if let Compaction::Task(task) = compaction {
            task.await;
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
    }
}
