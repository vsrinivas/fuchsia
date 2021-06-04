// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::LSMTree,
        object_handle::INVALID_OBJECT_ID,
        object_store::{
            allocator::Allocator,
            filesystem::Mutations,
            graveyard::Graveyard,
            journal::{checksum_list::ChecksumList, JournalCheckpoint},
            merge::{self},
            transaction::{AssocObj, AssociatedObject, Mutation, Transaction, TxnMutation},
            ObjectStore,
        },
    },
    anyhow::Error,
    once_cell::sync::OnceCell,
    std::{
        collections::HashMap,
        sync::{Arc, RwLock},
    },
};

/// ObjectManager is a global loading cache for object stores and other special objects.
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

    pub fn set_root_parent_store(&self, store: Arc<ObjectStore>) {
        let mut objects = self.objects.write().unwrap();
        let store_id = store.store_object_id();
        objects.stores.insert(store_id, store);
        objects.root_parent_store_object_id = store_id;
    }

    pub fn root_store(&self) -> Arc<ObjectStore> {
        let objects = self.objects.read().unwrap();
        objects.stores.get(&objects.root_store_object_id).unwrap().clone()
    }

    pub fn set_root_store(&self, store: Arc<ObjectStore>) {
        let mut objects = self.objects.write().unwrap();
        let store_id = store.store_object_id();
        objects.stores.insert(store_id, store);
        objects.root_store_object_id = store_id;
    }

    /// When replaying the journal, we need to replay mutation records into the LSM tree, but we
    /// cannot properly open the store until all the records have been replayed since some of the
    /// records we replay might affect how we open, e.g. they might pertain to new layer files
    /// backing this store.  The store will get properly opened whenever an action is taken that
    /// needs the store to be opened (via ObjectStore::ensure_open).
    pub fn lazy_open_store(&self, store_object_id: u64) -> Arc<ObjectStore> {
        let mut objects = self.objects.write().unwrap();
        assert_ne!(store_object_id, objects.allocator_object_id);
        let root_parent_store_object_id = objects.root_parent_store_object_id;
        let root_store = objects.stores.get(&objects.root_store_object_id).unwrap().clone();
        let fs = root_store.filesystem();
        objects
            .stores
            .entry(store_object_id)
            .or_insert_with(|| {
                // This assumes that all stores are children of the root store.
                assert_ne!(store_object_id, root_parent_store_object_id);
                assert_ne!(store_object_id, root_store.store_object_id());
                ObjectStore::new(
                    Some(root_store),
                    store_object_id,
                    fs,
                    None,
                    LSMTree::new(merge::merge),
                )
            })
            .clone()
    }

    pub async fn open_store(&self, store_object_id: u64) -> Result<Arc<ObjectStore>, Error> {
        let store = self.lazy_open_store(store_object_id);
        store.ensure_open().await?;
        Ok(store)
    }

    pub fn add_store(&self, store: Arc<ObjectStore>) {
        let mut objects = self.objects.write().unwrap();
        let store_object_id = store.store_object_id();
        assert_ne!(store_object_id, objects.root_parent_store_object_id);
        assert_ne!(store_object_id, objects.root_store_object_id);
        assert_ne!(store_object_id, objects.allocator_object_id);
        objects.stores.insert(store_object_id, store);
    }

    #[cfg(test)]
    pub fn forget_store(&self, store_object_id: u64) {
        let mut objects = self.objects.write().unwrap();
        assert_ne!(store_object_id, objects.allocator_object_id);
        objects.stores.remove(&store_object_id);
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

    /// Used during replay to validate a mutation.  This should return false if the mutation is not
    /// valid and should not be applied.  This could be for benign reasons: e.g. the device flushed
    /// data out-of-order, or because of a malicious actor.  `checksum_list` contains a list of
    /// checksums that might need to be performed but cannot be performed now in case there are
    /// deallocations later.
    pub async fn validate_mutation(
        &self,
        journal_offset: u64,
        object_id: u64,
        mutation: &Mutation,
        checksum_list: &mut ChecksumList,
    ) -> Result<bool, Error> {
        if let Some(allocator) = {
            let objects = self.objects.read().unwrap();
            if object_id == objects.allocator_object_id {
                Some(objects.allocator.clone().unwrap())
            } else {
                None
            }
        } {
            allocator.validate_mutation(journal_offset, mutation, checksum_list).await
        } else {
            ObjectStore::validate_mutation(journal_offset, mutation, checksum_list).await
        }
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
        .unwrap_or_else(|| self.lazy_open_store(object_id));
        associated_object.will_apply_mutation(&mutation);
        object
            .apply_mutation(mutation, transaction, checkpoint.file_offset, associated_object)
            .await;
    }

    /// Drops a transaction.  This is called automatically when a transaction is dropped.  If the
    /// transaction has been committed, it should contain no mutations and so nothing will get rolled
    /// back.  For each mutation, drop_mutation is called to allow for roll back (e.g. the allocator
    /// will unreserve allocations).
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
