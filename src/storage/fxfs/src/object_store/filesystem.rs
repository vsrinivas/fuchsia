// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        device::Device,
        object_store::{
            allocator::Allocator,
            constants::INVALID_OBJECT_ID,
            journal::{Journal, JournalCheckpoint},
            transaction::{
                AssociatedObject, LockKey, LockManager, Mutation, Transaction, TransactionHandler,
                TxnMutation,
            },
            ObjectStore,
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    std::{
        collections::HashMap,
        sync::{Arc, RwLock},
    },
};

#[async_trait]
pub trait Filesystem: TransactionHandler {
    /// Informs the journaling system that a new store has been created so that when a transaction
    /// is committed or replayed, mutations can be routed to the correct store.
    fn register_store(&self, store: &Arc<ObjectStore>);

    /// Informs the journaling system that the given object ID is about to flush in-memory data.  If
    /// successful, all mutations pertinent to this object can be discarded, but any mutations that
    /// follow will still be kept.
    fn begin_object_sync(&self, object_id: u64) -> ObjectSync;

    /// Returns access to the undeyling device.
    fn device(&self) -> Arc<dyn Device>;

    /// Returns the root store or panics if it is not available.
    fn root_store(&self) -> Arc<ObjectStore>;

    /// Returns the allocator or panics if it is not available.
    fn allocator(&self) -> Arc<dyn Allocator>;
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
            }),
        }
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

    pub fn register_store(&self, store: &Arc<ObjectStore>) {
        let mut objects = self.objects.write().unwrap();
        assert_ne!(store.store_object_id(), objects.allocator_object_id);
        assert!(objects.stores.insert(store.store_object_id(), store.clone()).is_none());
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
        replay: bool,
        checkpoint: &JournalCheckpoint,
        object: Option<AssociatedObject<'_>>,
    ) {
        {
            let mut objects = self.objects.write().unwrap();
            objects.journal_file_checkpoints.entry(object_id).or_insert_with(|| checkpoint.clone());
            if object_id == objects.allocator_object_id {
                Some(objects.allocator.clone().unwrap().as_mutations())
            } else {
                objects.stores.get(&object_id).map(|x| x.clone() as Arc<dyn Mutations>)
            }
        }
        .unwrap_or_else(|| self.root_store().lazy_open_store(object_id))
        .apply_mutation(mutation, replay, object)
        .await;
    }

    // Drops a transaction.  This is called automatically when a transaction is dropped.  If the
    // transaction has been committed, it should contain no mutations and so nothing will get rolled
    // back.  For each mutation, drop_mutation is called to allow for roll back (e.g. the allocator
    // will unreserve allocations).
    pub fn drop_transaction(&self, transaction: &mut Transaction<'_>) {
        for TxnMutation { object_id, mutation, .. } in std::mem::take(&mut transaction.mutations) {
            {
                let objects = self.objects.read().unwrap();
                if object_id == objects.allocator_object_id {
                    Some(objects.allocator.clone().unwrap().as_mutations())
                } else {
                    objects.stores.get(&object_id).map(|x| x.clone() as Arc<dyn Mutations>)
                }
            }
            .map(|o| o.drop_mutation(mutation));
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

    pub fn begin_object_sync(self: &Arc<Self>, object_id: u64) -> ObjectSync {
        let old_journal_file_checkpoint =
            self.objects.write().unwrap().journal_file_checkpoints.remove(&object_id);
        ObjectSync { object_manager: self.clone(), object_id, old_journal_file_checkpoint }
    }
}

/// ObjectSync is used by objects to indicate some kind of event such that if successful, existing
/// mutation records are no longer required from the journal.  For example, for object stores, it is
/// used when the in-memory layer is persisted since once that is done the records in the journal
/// are no longer required.  Clients must make sure to call the commit function upon success; the
/// default is to roll back.
#[must_use]
pub struct ObjectSync {
    object_manager: Arc<ObjectManager>,
    object_id: u64,
    old_journal_file_checkpoint: Option<JournalCheckpoint>,
}

impl ObjectSync {
    pub fn needs_sync(&self) -> bool {
        self.old_journal_file_checkpoint.is_some()
    }

    pub fn commit(mut self) {
        self.old_journal_file_checkpoint = None;
    }
}

impl Drop for ObjectSync {
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

#[async_trait]
pub trait Mutations: Send + Sync {
    /// Objects that use the journaling system to track mutations should implement this trait.  This
    /// method will get called when the transaction commits, which can either be during live
    /// operation or during journal replay, in which case |replay| will be true.  Also see
    /// ObjectManager's apply_mutation method.
    async fn apply_mutation(
        &self,
        mutation: Mutation,
        replay: bool,
        object: Option<AssociatedObject<'_>>,
    );

    /// Called when a transaction fails to commit.
    fn drop_mutation(&self, mutation: Mutation);
}

#[derive(Default)]
pub struct SyncOptions {}

pub struct FxFilesystem {
    device: Arc<dyn Device>,
    objects: Arc<ObjectManager>,
    journal: Journal,
    lock_manager: LockManager,
}

impl FxFilesystem {
    pub async fn new_empty(device: Arc<dyn Device>) -> Result<Arc<FxFilesystem>, Error> {
        let objects = Arc::new(ObjectManager::new());
        let journal = Journal::new(objects.clone());
        let filesystem = Arc::new(FxFilesystem {
            device: device.clone(),
            objects: objects.clone(),
            journal,
            lock_manager: LockManager::new(),
        });
        filesystem.journal.init_empty(filesystem.clone()).await?;
        Ok(filesystem)
    }

    pub async fn open(device: Arc<dyn Device>) -> Result<Arc<FxFilesystem>, Error> {
        let objects = Arc::new(ObjectManager::new());
        let journal = Journal::new(objects.clone());
        let filesystem = Arc::new(FxFilesystem {
            device: device.clone(),
            objects: objects.clone(),
            journal,
            lock_manager: LockManager::new(),
        });
        filesystem.journal.replay(filesystem.clone()).await?;
        Ok(filesystem)
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

    pub async fn sync(&self, options: SyncOptions) -> Result<(), Error> {
        self.journal.sync(options).await
    }

    pub fn volume_info_object_id(&self) -> u64 {
        self.journal.volume_info_object_id()
    }

    pub fn set_volume_info_object_id(&self, object_id: u64) {
        self.journal.set_volume_info_object_id(object_id);
    }

    pub async fn close(&self) -> Result<(), Error> {
        // Regardless of whether sync succeeds, we should close the device, since otherwise we will
        // crash instead of exiting gracefully.
        let sync_status = self.journal.sync(SyncOptions::default()).await;
        if sync_status.is_err() {
            log::error!("Failed to sync filesystem; data may be lost: {:?}", sync_status);
        }
        self.device.close().await.expect("Failed to close device");
        sync_status
    }
}

#[async_trait]
impl Filesystem for FxFilesystem {
    fn register_store(&self, store: &Arc<ObjectStore>) {
        self.objects.register_store(store);
    }

    fn begin_object_sync(&self, object_id: u64) -> ObjectSync {
        self.objects.begin_object_sync(object_id)
    }

    fn device(&self) -> Arc<dyn Device> {
        self.device.clone()
    }

    fn root_store(&self) -> Arc<ObjectStore> {
        self.objects.root_store()
    }

    fn allocator(&self) -> Arc<dyn Allocator> {
        self.objects.allocator()
    }
}

#[async_trait]
impl TransactionHandler for FxFilesystem {
    async fn new_transaction<'a>(
        self: Arc<Self>,
        locks: &[LockKey],
    ) -> Result<Transaction<'a>, Error> {
        let mut locks: Vec<_> = locks.iter().cloned().collect();
        locks.sort_unstable();
        self.lock_manager.lock(&locks).await;
        Ok(Transaction::new(self, locks))
    }

    async fn commit_transaction(&self, transaction: Transaction<'_>) {
        self.journal.commit(transaction).await;
    }

    fn drop_transaction(&self, transaction: &mut Transaction<'_>) {
        self.objects.drop_transaction(transaction);
        self.lock_manager.drop_transaction(transaction);
    }
}

// TODO(csuter): How do we ensure sync prior to drop?
