// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        async_enter,
        lsm_tree::{
            merge::{Merger, MergerIterator},
            types::{ItemRef, LayerIterator},
        },
        object_store::{
            object_manager::ObjectManager,
            object_record::{
                ObjectAttributes, ObjectKey, ObjectKeyData, ObjectKind, ObjectValue, Timestamp,
            },
            transaction::{Mutation, Options, Transaction},
            ObjectStore,
        },
    },
    anyhow::{Context, Error},
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
/// that at mount time, any objects in the graveyard will get removed.  Each object store has a
/// directory like object that contains a list of the objects within that store that are part of the
/// graveyard.  A single instance of this Graveyard struct manages *all* stores.
pub struct Graveyard {
    object_manager: Arc<ObjectManager>,
    reaper_task: Mutex<ReaperTask>,
    channel: UnboundedSender<(u64, u64)>,
}

impl Graveyard {
    /// Creates a new instance of the graveyard manager.
    pub fn new(object_manager: Arc<ObjectManager>) -> Arc<Self> {
        let (sender, receiver) = unbounded();
        Arc::new(Graveyard {
            object_manager,
            reaper_task: Mutex::new(ReaperTask::Pending(receiver)),
            channel: sender,
        })
    }

    /// Creates a graveyard object in `store`.
    pub fn create<'a>(transaction: &mut Transaction<'a>, store: &'a ObjectStore) {
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
        transaction.add(store.store_object_id, Mutation::graveyard_directory(object_id));
    }

    /// Starts an asynchronous task to reap the graveyard for all entries older than
    /// |journal_offset| (exclusive).
    /// If a task is already started, this has no effect, even if that task was targeting an older
    /// |journal_offset|.
    pub fn reap_async(self: Arc<Self>) {
        let mut reaper_task = self.reaper_task.lock().unwrap();
        if let ReaperTask::Pending(_) = &*reaper_task {
            if let ReaperTask::Pending(receiver) =
                std::mem::replace(&mut *reaper_task, ReaperTask::None)
            {
                *reaper_task =
                    ReaperTask::Running(fasync::Task::spawn(self.clone().reap_task(receiver)));
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

    async fn reap_task(self: Arc<Self>, mut receiver: UnboundedReceiver<(u64, u64)>) {
        log::info!("Reaping graveyard starting");
        // Wait and process reap requests.
        while let Some((store_id, object_id)) = receiver.next().await {
            if let Err(e) = self.tombstone(store_id, object_id).await {
                log::error!("Tombstone error for {}.{}: {:?}", store_id, object_id, e);
            }
        }
    }

    /// Performs the initial mount-time reap for the given store.
    pub async fn initial_reap(
        self: &Arc<Self>,
        store: &Arc<ObjectStore>,
        end_journal_offset: u64,
    ) -> Result<usize, Error> {
        async_enter!("Graveyard::initial_reap");
        let mut count = 0;
        let layer_set = store.tree().layer_set();
        let mut merger = layer_set.merger();
        let graveyard_object_id = store.graveyard_directory_object_id();
        let mut iter = Self::iter(graveyard_object_id, &mut merger).await?;
        while let Some((object_id, journal_offset)) = iter.get() {
            if journal_offset >= end_journal_offset {
                break;
            }
            self.queue_tombstone(store.store_object_id(), object_id);
            count += 1;
            iter.advance().await?;
        }
        Ok(count)
    }

    // Queues an object for tombstoning.
    pub fn queue_tombstone(&self, store_id: u64, object_id: u64) {
        let _ = self.channel.unbounded_send((store_id, object_id));
    }

    async fn tombstone(&self, store_id: u64, object_id: u64) -> Result<(), Error> {
        let store = self
            .object_manager
            .store(store_id)
            .context(format!("Failed to get store {}", store_id))?;
        // TODO(csuter): we shouldn't assume that all objects in the root stores use the
        // metadata reservation.
        let options = if store_id == self.object_manager.root_parent_store_object_id()
            || store_id == self.object_manager.root_store_object_id()
        {
            Options {
                skip_journal_checks: true,
                borrow_metadata_space: true,
                allocator_reservation: Some(self.object_manager.metadata_reservation()),
                ..Default::default()
            }
        } else {
            Options { skip_journal_checks: true, borrow_metadata_space: true, ..Default::default() }
        };
        store.tombstone(object_id, options).await.context("Failed to tombstone object")
    }

    /// Returns an iterator that will return graveyard entries skipping deleted ones.  Example
    /// usage:
    ///
    ///   let layer_set = graveyard.store().tree().layer_set();
    ///   let mut merger = layer_set.merger();
    ///   let mut iter = graveyard.iter(&mut merger).await?;
    ///
    pub async fn iter<'a, 'b>(
        graveyard_object_id: u64,
        merger: &'a mut Merger<'b, ObjectKey, ObjectValue>,
    ) -> Result<GraveyardIterator<'a, 'b>, Error> {
        Self::iter_from(merger, graveyard_object_id, 0).await
    }

    /// Like "iter", but seeks from a specific (store-id, object-id) tuple.  Example usage:
    ///
    ///   let layer_set = graveyard.store().tree().layer_set();
    ///   let mut merger = layer_set.merger();
    ///   let mut iter = graveyard.iter_from(&mut merger, (2, 3)).await?;
    ///
    pub async fn iter_from<'a, 'b>(
        merger: &'a mut Merger<'b, ObjectKey, ObjectValue>,
        graveyard_object_id: u64,
        from: u64,
    ) -> Result<GraveyardIterator<'a, 'b>, Error> {
        let mut iter = merger
            .seek(Bound::Included(&ObjectKey::graveyard_entry(graveyard_object_id, from)))
            .await?;
        // Skip deleted entries.
        // TODO(csuter): Remove this once we've developed a filtering iterator.
        loop {
            match iter.get() {
                Some(ItemRef {
                    key: ObjectKey { object_id, .. },
                    value: ObjectValue::None,
                    ..
                }) if *object_id == graveyard_object_id => {}
                _ => break,
            }
            iter.advance().await?;
        }
        Ok(GraveyardIterator { object_id: graveyard_object_id, iter })
    }
}

pub struct GraveyardIterator<'a, 'b> {
    object_id: u64,
    iter: MergerIterator<'a, 'b, ObjectKey, ObjectValue>,
}

impl GraveyardIterator<'_, '_> {
    /// Returns a tuple (object_id, sequence).
    pub fn get(&self) -> Option<(u64, u64)> {
        match self.iter.get() {
            Some(ItemRef {
                key: ObjectKey { object_id: oid, data: ObjectKeyData::GraveyardEntry { object_id } },
                sequence,
                ..
            }) if *oid == self.object_id => Some((*object_id, sequence)),
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
            filesystem::{Filesystem, FxFilesystem, SyncOptions},
            transaction::{Options, TransactionHandler},
        },
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run_singlethreaded(test)]
    async fn test_graveyard() {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let root_store = fs.root_store();

        // Create and add two objects to the graveyard.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");

        root_store.add_to_graveyard(&mut transaction, 3);
        root_store.add_to_graveyard(&mut transaction, 4);
        transaction.commit().await.expect("commit failed");

        // Check that we see the objects we added.
        {
            let layer_set = root_store.tree().layer_set();
            let mut merger = layer_set.merger();
            let mut iter = Graveyard::iter(root_store.graveyard_directory_object_id(), &mut merger)
                .await
                .expect("iter failed");
            assert_matches!(iter.get().expect("missing entry"), (3, _));
            iter.advance().await.expect("advance failed");
            assert_matches!(iter.get().expect("missing entry"), (4, _));
            iter.advance().await.expect("advance failed");
            assert_eq!(iter.get(), None);
        }

        // Remove one of the objects.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_store.remove_from_graveyard(&mut transaction, 4);
        transaction.commit().await.expect("commit failed");

        // Check that the graveyard has been updated as expected.
        let layer_set = root_store.tree().layer_set();
        let mut merger = layer_set.merger();
        let mut iter = Graveyard::iter(root_store.graveyard_directory_object_id(), &mut merger)
            .await
            .expect("iter failed");
        assert_matches!(iter.get().expect("missing entry"), (3, _));
        iter.advance().await.expect("advance failed");
        assert_eq!(iter.get(), None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_graveyard_sequences() {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let root_store = fs.root_store();

        // Create and add two objects to the graveyard, syncing in between.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_store.add_to_graveyard(&mut transaction, 1234);
        transaction.commit().await.expect("commit failed");

        fs.sync(SyncOptions::default()).await.expect("sync failed");

        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        root_store.add_to_graveyard(&mut transaction, 5678);
        transaction.commit().await.expect("commit failed");

        // Ensure the objects have a monotonically increasing sequence.
        let sequence = {
            let layer_set = root_store.tree().layer_set();
            let mut merger = layer_set.merger();
            let mut iter = Graveyard::iter(root_store.graveyard_directory_object_id(), &mut merger)
                .await
                .expect("iter failed");
            let (id, sequence1) = iter.get().expect("Missing entry");
            assert_eq!(id, 1234);
            iter.advance().await.expect("advance failed");
            let (id, sequence2) = iter.get().expect("Missing entry");
            assert_eq!(id, 5678);
            iter.advance().await.expect("advance failed");
            assert_eq!(iter.get(), None);

            assert!(sequence1 < sequence2, "sequence1: {}, sequence2: {}", sequence1, sequence2);

            sequence2
        };

        // Reap the graveyard of entries with offset < sequence, which should leave just the second
        // entry.
        let graveyard = fs.graveyard();
        graveyard.clone().reap_async();
        graveyard.initial_reap(&root_store, sequence).await.expect("initial_reap failed");
        graveyard.wait_for_reap().await;

        fs.sync(SyncOptions::default()).await.expect("sync failed");
        let layer_set = root_store.tree().layer_set();
        let mut merger = layer_set.merger();
        merger.set_trace(true);
        let mut iter = Graveyard::iter(root_store.graveyard_directory_object_id(), &mut merger)
            .await
            .expect("iter failed");
        let mut items = vec![];
        while let Some((id, _)) = iter.get() {
            items.push(id);
            iter.advance().await.expect("advance failed");
        }
        assert_eq!(items, [5678]);
    }
}
