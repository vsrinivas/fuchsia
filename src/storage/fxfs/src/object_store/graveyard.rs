// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        lsm_tree::{
            merge::{Merger, MergerIterator},
            types::{ItemRef, LayerIterator},
        },
        object_store::{
            record::{
                ObjectAttributes, ObjectItem, ObjectKey, ObjectKeyData, ObjectKind, ObjectValue,
                Timestamp,
            },
            transaction::{Mutation, Options, Transaction},
            ObjectStore,
        },
        trace_duration,
    },
    anyhow::{bail, Context, Error},
    fuchsia_async::{self as fasync},
    futures::{
        channel::mpsc::{unbounded, UnboundedReceiver, UnboundedSender},
        StreamExt,
    },
    std::{
        ops::Bound,
        sync::{Arc, Mutex},
    },
};

enum ReaperTask {
    None,
    Pending(UnboundedReceiver<(u64, u64)>),
    Running(fasync::Task<()>),
}

/// A graveyard exists as a place to park objects that should be deleted when they are no longer in
/// use.  How objects enter and leave the graveyard is up to the caller to decide.  The intention is
/// that at mount time, any objects in the graveyard will get removed.
pub struct Graveyard {
    store: Arc<ObjectStore>,
    object_id: u64,
    reaper_task: Mutex<ReaperTask>,
    channel: UnboundedSender<(u64, u64)>,
}

impl Graveyard {
    fn new(store: Arc<ObjectStore>, object_id: u64) -> Arc<Self> {
        let (sender, receiver) = unbounded();
        Arc::new(Graveyard {
            store,
            object_id,
            reaper_task: Mutex::new(ReaperTask::Pending(receiver)),
            channel: sender,
        })
    }

    pub fn store(&self) -> &Arc<ObjectStore> {
        &self.store
    }

    pub fn object_id(&self) -> u64 {
        self.object_id
    }

    /// Creates a graveyard object in `store`.
    pub async fn create(
        transaction: &mut Transaction<'_>,
        store: &Arc<ObjectStore>,
    ) -> Result<Arc<Graveyard>, Error> {
        store.ensure_open().await?;
        let object_id = store.get_next_object_id();
        let now = Timestamp::now();
        transaction.add(
            store.store_object_id,
            Mutation::insert_object(
                ObjectKey::object(object_id),
                ObjectValue::Object {
                    kind: ObjectKind::Graveyard,
                    attributes: ObjectAttributes {
                        creation_time: now.clone(),
                        modification_time: now,
                    },
                },
            ),
        );
        Ok(Self::new(store.clone(), object_id))
    }

    /// Starts an asynchronous task to reap the graveyard for all entries older than
    /// |journal_offset| (exclusive).
    /// If a task is already started, this has no effect, even if that task was targeting an older
    /// |journal_offset|.
    pub fn reap_async(self: Arc<Self>, journal_offset: u64) {
        let mut reaper_task = self.reaper_task.lock().unwrap();
        if let ReaperTask::Pending(_) = &*reaper_task {
            if let ReaperTask::Pending(receiver) =
                std::mem::replace(&mut *reaper_task, ReaperTask::None)
            {
                *reaper_task = ReaperTask::Running(fasync::Task::spawn(
                    self.clone().reap_task(journal_offset, receiver),
                ));
            } else {
                unreachable!();
            }
        }
    }

    /// Returns a future which completes when the ongoing reap task (if it exists) completes.
    pub async fn wait_for_reap(&self) {
        self.channel.close_channel();
        let task = std::mem::replace(&mut *self.reaper_task.lock().unwrap(), ReaperTask::None);
        if let ReaperTask::Running(task) = task {
            task.await;
        }
    }

    async fn reap_task(
        self: Arc<Self>,
        journal_offset: u64,
        mut receiver: UnboundedReceiver<(u64, u64)>,
    ) {
        log::info!("Reaping graveyard starting, gen: {}", journal_offset);
        trace_duration!("Graveyard::reap");
        match self.reap_task_inner(journal_offset).await {
            Ok(deleted) => log::info!("Reaping graveyard done, removed {} elements", deleted),
            Err(e) => log::error!("Reaping graveyard encountered error: {:?}", e),
        };
        while let Some((store_id, object_id)) = receiver.next().await {
            if let Err(e) = self.tombstone(store_id, object_id).await {
                log::error!("Tombstone error for {}.{}: {:?}", store_id, object_id, e);
            }
        }
    }

    async fn reap_task_inner(self: &Arc<Self>, journal_offset: u64) -> Result<usize, Error> {
        let purge_items = {
            let mut purge_items = vec![];
            let layer_set = self.store().tree().layer_set();
            let mut merger = layer_set.merger();
            let mut iter = self.iter(&mut merger).await.expect("iter failed");
            while let Some((store_id, id, offset)) = iter.get() {
                if offset < journal_offset {
                    purge_items.push((store_id, id));
                }
                iter.advance().await?;
            }
            purge_items
        };
        let num_purged = purge_items.len();
        for (store_id, id) in purge_items {
            if let Err(e) = self.tombstone(store_id, id).await {
                log::error!("Tombstone error for {}.{}: {:?}", store_id, id, e);
            }
        }
        Ok(num_purged)
    }

    // Queues an object for tombstoning.
    pub fn queue_tombstone(&self, store_id: u64, object_id: u64) {
        let _ = self.channel.unbounded_send((store_id, object_id));
    }

    async fn tombstone(&self, store_id: u64, object_id: u64) -> Result<(), Error> {
        let fs = self.store().filesystem();
        let object_manager = fs.object_manager();
        let store = object_manager
            .open_store(store_id)
            .await
            .context(format!("Failed to open store {}", store_id))?;
        // TODO(csuter): we shouldn't assume that all objects in the root stores use the
        // metadata reservation.
        let options = if store_id == object_manager.root_parent_store_object_id()
            || store_id == object_manager.root_store_object_id()
        {
            Options {
                skip_journal_checks: true,
                borrow_metadata_space: true,
                allocator_reservation: Some(object_manager.metadata_reservation()),
                ..Default::default()
            }
        } else {
            Options { skip_journal_checks: true, borrow_metadata_space: true, ..Default::default() }
        };
        store.tombstone(object_id, options).await.context("Failed to tombstone object")
    }

    /// Opens a graveyard object in `store`.
    pub async fn open(store: &Arc<ObjectStore>, object_id: u64) -> Result<Arc<Graveyard>, Error> {
        store.ensure_open().await?;
        if let ObjectItem {
            value: ObjectValue::Object { kind: ObjectKind::Graveyard, .. }, ..
        } = store.tree.find(&ObjectKey::object(object_id)).await?.ok_or(FxfsError::NotFound)?
        {
            Ok(Self::new(store.clone(), object_id))
        } else {
            bail!("Found an object, but it's not a graveyard");
        }
    }

    /// Adds an object to the graveyard.
    pub fn add(&self, transaction: &mut Transaction<'_>, store_object_id: u64, object_id: u64) {
        transaction.add(
            self.store.store_object_id(),
            Mutation::replace_or_insert_object(
                ObjectKey::graveyard_entry(self.object_id, store_object_id, object_id),
                ObjectValue::Some,
            ),
        );
    }

    /// Removes an object from the graveyard.
    pub fn remove(&self, transaction: &mut Transaction<'_>, store_object_id: u64, object_id: u64) {
        transaction.add(
            self.store.store_object_id(),
            Mutation::replace_or_insert_object(
                ObjectKey::graveyard_entry(self.object_id, store_object_id, object_id),
                ObjectValue::None,
            ),
        );
    }

    /// Returns an iterator that will return graveyard entries skipping deleted ones.  Example
    /// usage:
    ///
    ///   let layer_set = graveyard.store().tree().layer_set();
    ///   let mut merger = layer_set.merger();
    ///   let mut iter = graveyard.iter(&mut merger).await?;
    ///
    pub async fn iter<'a, 'b>(
        &self,
        merger: &'a mut Merger<'b, ObjectKey, ObjectValue>,
    ) -> Result<GraveyardIterator<'a, 'b>, Error> {
        self.iter_from(merger, (0, 0)).await
    }

    /// Like "iter", but seeks from a specific (store-id, object-id) tuple.  Example usage:
    ///
    ///   let layer_set = graveyard.store().tree().layer_set();
    ///   let mut merger = layer_set.merger();
    ///   let mut iter = graveyard.iter_from(&mut merger, (2, 3)).await?;
    ///
    pub async fn iter_from<'a, 'b>(
        &self,
        merger: &'a mut Merger<'b, ObjectKey, ObjectValue>,
        from: (u64, u64),
    ) -> Result<GraveyardIterator<'a, 'b>, Error> {
        let mut iter = merger
            .seek(Bound::Included(&ObjectKey::graveyard_entry(self.object_id, from.0, from.1)))
            .await?;
        // Skip deleted entries.
        // TODO(csuter): Remove this once we've developed a filtering iterator.
        loop {
            match iter.get() {
                Some(ItemRef {
                    key: ObjectKey { object_id, .. },
                    value: ObjectValue::None,
                    ..
                }) if *object_id == self.object_id => {}
                _ => break,
            }
            iter.advance().await?;
        }
        Ok(GraveyardIterator { object_id: self.object_id, iter })
    }
}

pub struct GraveyardIterator<'a, 'b> {
    object_id: u64,
    iter: MergerIterator<'a, 'b, ObjectKey, ObjectValue>,
}

impl GraveyardIterator<'_, '_> {
    /// Returns a tuple (store_object_id, object_id, sequence).
    pub fn get(&self) -> Option<(u64, u64, u64)> {
        match self.iter.get() {
            Some(ItemRef {
                key:
                    ObjectKey {
                        object_id: oid,
                        data: ObjectKeyData::GraveyardEntry { store_object_id, object_id },
                    },
                sequence,
                ..
            }) if *oid == self.object_id => Some((*store_object_id, *object_id, sequence)),
            _ => None,
        }
    }

    pub async fn advance(&mut self) -> Result<(), Error> {
        loop {
            self.iter.advance().await?;
            // Skip deleted entries.
            match self.iter.get() {
                Some(ItemRef {
                    key: ObjectKey { object_id, .. },
                    value: ObjectValue::None,
                    ..
                }) if *object_id == self.object_id => {}
                _ => return Ok(()),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::Graveyard,
        crate::object_store::{
            crypt::InsecureCrypt,
            filesystem::{Filesystem, FxFilesystem, SyncOptions},
            transaction::{Options, TransactionHandler},
        },
        fuchsia_async as fasync,
        matches::assert_matches,
        std::sync::Arc,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run_singlethreaded(test)]
    async fn test_graveyard() {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device, Arc::new(InsecureCrypt::new()))
            .await
            .expect("new_empty failed");
        let root_store = fs.root_store();

        // Create and add two objects to the graveyard.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let graveyard =
            Graveyard::create(&mut transaction, &root_store).await.expect("create failed");
        graveyard.add(&mut transaction, 2, 3);
        graveyard.add(&mut transaction, 3, 4);
        transaction.commit().await.expect("commit failed");

        // Reopen the graveyard and check that we see the objects we added.
        let graveyard =
            Graveyard::open(&root_store, graveyard.object_id()).await.expect("open failed");
        {
            let layer_set = graveyard.store().tree().layer_set();
            let mut merger = layer_set.merger();
            let mut iter = graveyard.iter(&mut merger).await.expect("iter failed");
            assert_matches!(iter.get().expect("missing entry"), (2, 3, _));
            iter.advance().await.expect("advance failed");
            assert_matches!(iter.get().expect("missing entry"), (3, 4, _));
            iter.advance().await.expect("advance failed");
            assert_eq!(iter.get(), None);
        }

        // Remove one of the objects.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        graveyard.remove(&mut transaction, 3, 4);
        transaction.commit().await.expect("commit failed");

        // Check that the graveyard has been updated as expected.
        let layer_set = graveyard.store().tree().layer_set();
        let mut merger = layer_set.merger();
        let mut iter = graveyard.iter_from(&mut merger, (2, 3)).await.expect("iter failed");
        assert_matches!(iter.get().expect("missing entry"), (2, 3, _));
        iter.advance().await.expect("advance failed");
        assert_eq!(iter.get(), None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_graveyard_sequences() {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device, Arc::new(InsecureCrypt::new()))
            .await
            .expect("new_empty failed");
        let root_store = fs.root_store();

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let child_store = root_store
            .create_child_store(&mut transaction)
            .await
            .expect("create_child_store failed");
        transaction.commit().await.expect("commit failed");

        let graveyard = fs.object_manager().graveyard().unwrap();

        // Create and add two objects to the graveyard, syncing in between.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        graveyard.add(&mut transaction, root_store.store_object_id(), 1234);
        transaction.commit().await.expect("commit failed");

        fs.sync(SyncOptions::default()).await.expect("sync failed");

        let graveyard =
            Graveyard::open(&root_store, graveyard.object_id()).await.expect("open failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        graveyard.add(&mut transaction, child_store.store_object_id(), 5678);
        transaction.commit().await.expect("commit failed");

        // Ensure the objects have a monotonically increasing sequence.
        let graveyard =
            Graveyard::open(&root_store, graveyard.object_id()).await.expect("open failed");
        let sequence = {
            let layer_set = graveyard.store().tree().layer_set();
            let mut merger = layer_set.merger();
            let mut iter = graveyard.iter(&mut merger).await.expect("iter failed");
            let (store_id, id, sequence1) = iter.get().expect("Missing entry");
            assert_eq!(store_id, root_store.store_object_id());
            assert_eq!(id, 1234);
            iter.advance().await.expect("advance failed");
            let (store_id, id, sequence2) = iter.get().expect("Missing entry");
            assert_eq!(store_id, child_store.store_object_id());
            assert_eq!(id, 5678);
            iter.advance().await.expect("advance failed");
            assert_eq!(iter.get(), None);

            assert!(sequence1 < sequence2, "sequence1: {}, sequence2: {}", sequence1, sequence2);

            sequence2
        };

        // Reap the graveyard of entries with offset < sequence, which should leave just the second
        // entry.
        graveyard.clone().reap_async(sequence);
        graveyard.wait_for_reap().await;

        fs.sync(SyncOptions::default()).await.expect("sync failed");
        let layer_set = graveyard.store().tree().layer_set();
        let mut merger = layer_set.merger();
        merger.set_trace(true);
        let mut iter = graveyard.iter(&mut merger).await.expect("iter failed");
        let mut items = vec![];
        while let Some((store_id, id, _)) = iter.get() {
            items.push((store_id, id));
            iter.advance().await.expect("advance failed");
        }
        assert_eq!(items, [(child_store.store_object_id(), 5678)]);
    }
}
