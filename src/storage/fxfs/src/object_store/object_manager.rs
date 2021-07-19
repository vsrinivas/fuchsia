// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        object_handle::INVALID_OBJECT_ID,
        object_store::{
            allocator::{Allocator, Reservation},
            filesystem::Mutations,
            graveyard::Graveyard,
            journal::{self, checksum_list::ChecksumList, JournalCheckpoint},
            transaction::{AssocObj, MetadataReservation, Mutation, Transaction, TxnMutation},
            ObjectStore,
        },
    },
    anyhow::Error,
    once_cell::sync::OnceCell,
    std::{
        collections::{hash_map::Entry, HashMap},
        sync::{Arc, RwLock},
    },
};

// Data written to the journal eventually needs to be flushed somewhere (typically into layer
// files).  Here we conservatively assume that could take up to twice us much space as it does in
// the journal.  In practice, it should be less than that.
fn reserved_space_from_journal_usage(journal_usage: u64) -> u64 {
    journal_usage * 2
}

/// ObjectManager is a global loading cache for object stores and other special objects.
pub struct ObjectManager {
    inner: RwLock<Inner>,
    metadata_reservation: OnceCell<Reservation>,
}

// Whilst we are flushing we need to keep track of the old checkpoint that we are hoping to flush,
// and a new one that should apply if we successfully finish the flush.
enum Checkpoints {
    Current(JournalCheckpoint),
    Old(JournalCheckpoint),
    Both(/* old: */ JournalCheckpoint, /* current: */ JournalCheckpoint),
}

impl Checkpoints {
    // Returns the earliest checkpoint (which will always be the old one if present).
    fn earliest(&self) -> &JournalCheckpoint {
        match self {
            Checkpoints::Old(x) | Checkpoints::Both(x, _) | Checkpoints::Current(x) => x,
        }
    }
}

// We currently maintain strong references to all stores that have been opened, but there's no
// currently no mechanism for releasing stores that aren't being used.
struct Inner {
    stores: HashMap<u64, Arc<ObjectStore>>,
    root_parent_store_object_id: u64,
    root_store_object_id: u64,
    allocator_object_id: u64,
    allocator: Option<Arc<dyn Allocator>>,

    // Records dependencies on the journal for objects i.e. an entry for object ID 1, would mean it
    // has a dependency on journal records from that offset.
    journal_checkpoints: HashMap<u64, Checkpoints>,

    graveyard: Option<Arc<Graveyard>>,

    // Mappings from object-id to a target reservation amount.  The object IDs here are from the
    // root store namespace, so it can be associated with any object in the root store.  A
    // reservation will be made to cover the *maximum* in this map, since it is assumed that any
    // requirement is only temporary, for the duration of a compaction, and that once compaction has
    // finished for a particular object, the space will be recovered.
    reservations: HashMap<u64, u64>,

    // The last journal end offset for a transaction that has been applied.  This is not necessarily
    // the same as the start offset for the next transaction because of padding.
    last_end_offset: u64,

    // A running counter that tracks metadata space that has been borrowed on the understanding that
    // eventually it will be recovered (potentially after a full compaction).
    borrowed_metadata_space: u64,
}

impl Inner {
    // Returns the required size of the metadata reservation assuming that no space has been
    // borrowed.  The invariant is: reservation-size + borrowed-space = required.
    fn required_reservation(&self) -> u64 {
        // Start with the maximum amount of temporary space we might need during compactions.
        self.reservations.values().max().unwrap_or(&0)

        // Account for data that has been written to the journal that will need to be written
        // to layer files when flushed.
            + self.journal_checkpoints.values().map(|c| c.earliest().file_offset).min()
            .map(|min| reserved_space_from_journal_usage(self.last_end_offset - min))
            .unwrap_or(0)

        // Add extra for temporary space that might be tied up in the journal that hasn't yet been
        // deallocated.
            + journal::RESERVED_SPACE
    }
}

impl ObjectManager {
    pub fn new() -> ObjectManager {
        ObjectManager {
            inner: RwLock::new(Inner {
                stores: HashMap::new(),
                root_parent_store_object_id: INVALID_OBJECT_ID,
                root_store_object_id: INVALID_OBJECT_ID,
                allocator_object_id: INVALID_OBJECT_ID,
                allocator: None,
                journal_checkpoints: HashMap::new(),
                graveyard: None,
                reservations: HashMap::new(),
                last_end_offset: 0,
                borrowed_metadata_space: 0,
            }),
            metadata_reservation: OnceCell::new(),
        }
    }

    pub fn store_object_ids(&self) -> Vec<u64> {
        self.inner.read().unwrap().stores.keys().cloned().collect()
    }

    pub fn root_parent_store_object_id(&self) -> u64 {
        self.inner.read().unwrap().root_parent_store_object_id
    }

    pub fn root_parent_store(&self) -> Arc<ObjectStore> {
        let inner = self.inner.read().unwrap();
        inner.stores.get(&inner.root_parent_store_object_id).unwrap().clone()
    }

    pub fn set_root_parent_store(&self, store: Arc<ObjectStore>) {
        let mut inner = self.inner.write().unwrap();
        let store_id = store.store_object_id();
        inner.stores.insert(store_id, store);
        inner.root_parent_store_object_id = store_id;
    }

    pub fn root_store_object_id(&self) -> u64 {
        self.inner.read().unwrap().root_store_object_id
    }

    pub fn root_store(&self) -> Arc<ObjectStore> {
        let inner = self.inner.read().unwrap();
        inner.stores.get(&inner.root_store_object_id).unwrap().clone()
    }

    pub fn set_root_store(&self, store: Arc<ObjectStore>) {
        let mut inner = self.inner.write().unwrap();
        let store_id = store.store_object_id();
        inner.stores.insert(store_id, store);
        inner.root_store_object_id = store_id;
    }

    /// When replaying the journal, we need to replay mutation records into the LSM tree, but we
    /// cannot properly open the store until all the records have been replayed since some of the
    /// records we replay might affect how we open, e.g. they might pertain to new layer files
    /// backing this store.  The store will get properly opened whenever an action is taken that
    /// needs the store to be opened (via ObjectStore::ensure_open).
    pub fn lazy_open_store(&self, store_object_id: u64) -> Arc<ObjectStore> {
        let mut inner = self.inner.write().unwrap();
        assert_ne!(store_object_id, inner.allocator_object_id);
        let root_parent_store_object_id = inner.root_parent_store_object_id;
        let root_store = inner.stores.get(&inner.root_store_object_id).unwrap().clone();
        let fs = root_store.filesystem();
        inner
            .stores
            .entry(store_object_id)
            .or_insert_with(|| {
                // This assumes that all stores are children of the root store.
                assert_ne!(store_object_id, root_parent_store_object_id);
                assert_ne!(store_object_id, root_store.store_object_id());
                ObjectStore::new(Some(root_store), store_object_id, fs, None)
            })
            .clone()
    }

    pub async fn open_store(&self, store_object_id: u64) -> Result<Arc<ObjectStore>, Error> {
        let store = self.lazy_open_store(store_object_id);
        store.ensure_open().await?;
        Ok(store)
    }

    pub fn add_store(&self, store: Arc<ObjectStore>) {
        let mut inner = self.inner.write().unwrap();
        let store_object_id = store.store_object_id();
        assert_ne!(store_object_id, inner.root_parent_store_object_id);
        assert_ne!(store_object_id, inner.root_store_object_id);
        assert_ne!(store_object_id, inner.allocator_object_id);
        inner.stores.insert(store_object_id, store);
    }

    #[cfg(test)]
    pub fn forget_store(&self, store_object_id: u64) {
        let mut inner = self.inner.write().unwrap();
        assert_ne!(store_object_id, inner.allocator_object_id);
        inner.stores.remove(&store_object_id);
        inner.reservations.remove(&store_object_id);
    }

    pub fn set_allocator(&self, allocator: Arc<dyn Allocator>) {
        let mut inner = self.inner.write().unwrap();
        assert!(!inner.stores.contains_key(&allocator.object_id()));
        inner.allocator_object_id = allocator.object_id();
        inner.allocator = Some(allocator.clone());
    }

    pub fn allocator(&self) -> Arc<dyn Allocator> {
        self.inner.read().unwrap().allocator.clone().unwrap()
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
            let inner = self.inner.read().unwrap();
            if object_id == inner.allocator_object_id {
                Some(inner.allocator.clone().unwrap())
            } else {
                None
            }
        } {
            allocator.validate_mutation(journal_offset, mutation, checksum_list).await
        } else {
            ObjectStore::validate_mutation(journal_offset, mutation, checksum_list).await
        }
    }

    async fn apply_mutation(
        &self,
        object_id: u64,
        mutation: Mutation,
        transaction: Option<&Transaction<'_>>,
        checkpoint: &JournalCheckpoint,
        associated_object: AssocObj<'_>,
    ) {
        log::debug!("applying mutation: {}: {:?}", object_id, mutation);
        let object = {
            let mut inner = self.inner.write().unwrap();
            match mutation {
                Mutation::BeginFlush => {
                    if let Some(entry) = inner.journal_checkpoints.get_mut(&object_id) {
                        match entry {
                            Checkpoints::Current(x) | Checkpoints::Both(x, _) => {
                                *entry = Checkpoints::Old(x.clone());
                            }
                            _ => {}
                        }
                    }
                }
                Mutation::EndFlush => {
                    if let Entry::Occupied(mut o) = inner.journal_checkpoints.entry(object_id) {
                        let entry = o.get_mut();
                        match entry {
                            Checkpoints::Old(_) => {
                                o.remove();
                            }
                            Checkpoints::Both(_, x) => {
                                *entry = Checkpoints::Current(x.clone());
                            }
                            _ => {}
                        }
                    }
                }
                _ => {
                    if object_id != inner.root_parent_store_object_id {
                        inner
                            .journal_checkpoints
                            .entry(object_id)
                            .and_modify(|entry| {
                                if let Checkpoints::Old(x) = entry {
                                    *entry = Checkpoints::Both(x.clone(), checkpoint.clone());
                                }
                            })
                            .or_insert_with(|| Checkpoints::Current(checkpoint.clone()));
                    }
                }
            }
            if object_id == inner.allocator_object_id {
                Some(inner.allocator.clone().unwrap().as_mutations())
            } else {
                inner.stores.get(&object_id).map(|x| x.clone() as Arc<dyn Mutations>)
            }
        }
        .unwrap_or_else(|| self.lazy_open_store(object_id));
        associated_object.will_apply_mutation(&mutation);
        object
            .apply_mutation(mutation, transaction, checkpoint.file_offset, associated_object)
            .await;
    }

    /// Called by the journaling system to replay the given mutations.  `checkpoint` indicates the
    /// location in the journal file for this transaction and `end_offset` is the ending journal
    /// offset.
    pub async fn replay_mutations(
        &self,
        mutations: Vec<(u64, Mutation)>,
        journal_file_checkpoint: JournalCheckpoint,
        end_offset: u64,
    ) {
        log::debug!("REPLAY {}", journal_file_checkpoint.file_offset);
        let txn_size = {
            let mut inner = self.inner.write().unwrap();
            if end_offset > inner.last_end_offset {
                Some(end_offset - std::mem::replace(&mut inner.last_end_offset, end_offset))
            } else {
                None
            }
        };
        for (object_id, mutation) in mutations {
            if let Mutation::UpdateBorrowed(borrowed) = mutation {
                if let Some(txn_size) = txn_size {
                    self.inner.write().unwrap().borrowed_metadata_space =
                        borrowed + reserved_space_from_journal_usage(txn_size);
                }
                continue;
            }
            self.apply_mutation(
                object_id,
                mutation,
                None,
                &journal_file_checkpoint,
                AssocObj::None,
            )
            .await;
        }
    }

    /// Called by the journaling system to apply a transaction.  `checkpoint` indicates the location
    /// in the journal file for this transaction.  Returns an optional mutation to be written to be
    /// included with the transaction.
    pub async fn apply_transaction(
        &self,
        transaction: &mut Transaction<'_>,
        checkpoint: &JournalCheckpoint,
    ) -> Option<Mutation> {
        // Record old values so we can see what changes as a result of this transaction.
        let old_amount = self.metadata_reservation().amount();
        let old_required = self.inner.read().unwrap().required_reservation();

        log::debug!("BEGIN TXN {}", checkpoint.file_offset);
        let mutations = std::mem::take(&mut transaction.mutations);
        for TxnMutation { object_id, mutation, associated_object } in mutations {
            self.apply_mutation(
                object_id,
                mutation,
                Some(transaction),
                &checkpoint,
                associated_object,
            )
            .await;
        }
        log::debug!("END TXN");

        if let MetadataReservation::Borrowed = transaction.metadata_reservation {
            // If this transaction is borrowing metadata, figure out what has changed and return a
            // mutation with the updated value for borrowed.  The transaction might have allocated
            // or deallocated some data from the metadata reservation, or it might have made a
            // change that means we need to reserve more or less space (e.g. we compacted).
            let new_amount = self.metadata_reservation().amount();
            let mut inner = self.inner.write().unwrap();
            let new_required = inner.required_reservation();
            let add = old_amount + new_required;
            let sub = new_amount + old_required;
            if add >= sub {
                inner.borrowed_metadata_space += add - sub;
            } else {
                inner.borrowed_metadata_space =
                    inner.borrowed_metadata_space.saturating_sub(sub - add);
            }
            Some(Mutation::UpdateBorrowed(inner.borrowed_metadata_space))
        } else {
            // This transaction should have had no impact on the metadata reservation or the amount
            // we need to reserve.
            debug_assert_eq!(self.metadata_reservation().amount(), old_amount);
            debug_assert_eq!(self.inner.read().unwrap().required_reservation(), old_required);
            None
        }
    }

    /// Called by the journaling system after a transaction has been written providing the end
    /// offset for the transaction so that we can adjust borrowed metadata space accordingly.
    pub fn did_commit_transaction(
        &self,
        transaction: &mut Transaction<'_>,
        _checkpoint: &JournalCheckpoint,
        end_offset: u64,
    ) {
        let reservation = self.metadata_reservation();
        let mut inner = self.inner.write().unwrap();
        let txn_space = reserved_space_from_journal_usage(
            end_offset - std::mem::replace(&mut inner.last_end_offset, end_offset),
        );
        match &mut transaction.metadata_reservation {
            MetadataReservation::Borrowed => {
                // Account for the amount we need to borrow for the transaction itself now that we
                // know the transaction size.
                inner.borrowed_metadata_space += txn_space;

                // This transaction borrowed metadata space, but it might have returned space to the
                // transaction that we can now give back to the allocator.
                let to_give_back = (reservation.amount() + inner.borrowed_metadata_space)
                    .saturating_sub(inner.required_reservation());
                if to_give_back > 0 {
                    reservation.give_back(to_give_back);
                }
            }
            MetadataReservation::Hold(hold_amount) => {
                // Transfer reserved space into the metadata reservation.
                let txn_reservation = transaction.allocator_reservation.unwrap();
                assert_ne!(
                    txn_reservation as *const _, reservation as *const _,
                    "MetadataReservation::Borrowed should be used."
                );
                txn_reservation.commit(txn_space);
                *hold_amount -= txn_space;
                reservation.add(txn_space);
            }
            MetadataReservation::Reservation(txn_reservation) => {
                // Transfer reserved space into the metadata reservation.
                txn_reservation.hold(txn_space).unwrap().commit(txn_space);
                reservation.add(txn_space);
            }
        }
        // Check that our invariant holds true.
        debug_assert_eq!(
            reservation.amount() + inner.borrowed_metadata_space,
            inner.required_reservation(),
            "txn_space: {}, reservation_amount: {}, borrowed: {}, required: {}",
            txn_space,
            reservation.amount(),
            inner.borrowed_metadata_space,
            inner.required_reservation(),
        );
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
        let inner = self.inner.read().unwrap();
        let mut min_checkpoint = None;
        let mut offsets = HashMap::new();
        for (&object_id, checkpoint) in &inner.journal_checkpoints {
            let checkpoint = checkpoint.earliest();
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
        self.inner.read().unwrap().journal_checkpoints.contains_key(&object_id)
    }

    pub fn graveyard(&self) -> Option<Arc<Graveyard>> {
        self.inner.read().unwrap().graveyard.clone()
    }

    pub fn register_graveyard(&self, graveyard: Arc<Graveyard>) {
        self.inner.write().unwrap().graveyard = Some(graveyard);
    }

    /// Flushes all known objects.  This will then allow the journal space to be freed.
    pub async fn flush(&self) -> Result<(), Error> {
        let object_ids: Vec<_> =
            self.inner.read().unwrap().journal_checkpoints.keys().cloned().collect();
        for object_id in object_ids {
            self.object(object_id).unwrap().flush().await?;
        }
        Ok(())
    }

    fn object(&self, object_id: u64) -> Option<Arc<dyn Mutations>> {
        let inner = self.inner.read().unwrap();
        if object_id == inner.allocator_object_id {
            Some(inner.allocator.clone().unwrap().as_mutations())
        } else {
            inner.stores.get(&object_id).map(|x| x.clone() as Arc<dyn Mutations>)
        }
    }

    pub fn metadata_reservation(&self) -> &Reservation {
        self.metadata_reservation.get_or_init(|| {
            let inner = self.inner.read().unwrap();
            // TODO(csuter): Find a way to gracefully recover here.
            self.allocator()
                .reserve(inner.required_reservation() - inner.borrowed_metadata_space)
                .unwrap()
        })
    }

    pub fn update_reservation(&self, object_id: u64, amount: u64) {
        self.inner.write().unwrap().reservations.insert(object_id, amount);
    }

    pub fn last_end_offset(&self) -> u64 {
        self.inner.read().unwrap().last_end_offset
    }

    pub fn set_last_end_offset(&self, v: u64) {
        self.inner.write().unwrap().last_end_offset = v;
    }

    pub fn borrowed_metadata_space(&self) -> u64 {
        self.inner.read().unwrap().borrowed_metadata_space
    }

    pub fn set_borrowed_metadata_space(&self, v: u64) {
        self.inner.write().unwrap().borrowed_metadata_space = v;
    }
}
