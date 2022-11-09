// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        async_enter,
        errors::FxfsError,
        log::*,
        lsm_tree::{
            merge::{Merger, MergerIterator},
            types::{ItemRef, LayerIterator},
        },
        object_handle::INVALID_OBJECT_ID,
        object_store::{
            object_manager::ObjectManager,
            object_record::{
                ObjectAttributes, ObjectKey, ObjectKeyData, ObjectKind, ObjectValue, Timestamp,
            },
            transaction::{Mutation, Options, Transaction},
            ObjectStore,
        },
    },
    anyhow::{anyhow, bail, Context, Error},
    fuchsia_async::{self as fasync},
    futures::{
        channel::{
            mpsc::{unbounded, UnboundedReceiver, UnboundedSender},
            oneshot,
        },
        StreamExt,
    },
    std::{
        ops::Bound,
        sync::{Arc, Mutex},
    },
};

enum ReaperTask {
    None,
    Pending(UnboundedReceiver<Message>),
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
    channel: UnboundedSender<Message>,
}

enum Message {
    // Tombstone the object identified by <store-id>, <object-id>.
    Tombstone(u64, u64),

    // Trims the identified object.
    Trim(u64, u64),

    // When the flush message is processed, notifies sender.  This allows the receiver to know
    // that all preceding tombstone messages have been processed.
    Flush(oneshot::Sender<()>),
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

    /// Creates a graveyard object in `store`.  Returns the object ID for the graveyard object.
    pub fn create(transaction: &mut Transaction<'_>, store: &ObjectStore) -> u64 {
        let object_id = store.maybe_get_next_object_id();
        // This is OK because we only ever create a graveyard as we are creating a new store so
        // maybe_get_next_object_id will never fail here due to a lack of an object ID cipher.
        assert_ne!(object_id, INVALID_OBJECT_ID);
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
        object_id
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

    async fn reap_task(self: Arc<Self>, mut receiver: UnboundedReceiver<Message>) {
        // Wait and process reap requests.
        while let Some(message) = receiver.next().await {
            match message {
                Message::Tombstone(store_id, object_id) => {
                    if let Err(e) = self.tombstone(store_id, object_id).await {
                        error!(error = e.as_value(), store_id, oid = object_id, "Tombstone error");
                    }
                }
                Message::Trim(store_id, object_id) => {
                    if let Err(e) = self.trim(store_id, object_id).await {
                        error!(error = e.as_value(), store_id, oid = object_id, "Tombstone error");
                    }
                }
                Message::Flush(sender) => {
                    let _ = sender.send(());
                }
            }
        }
    }

    /// Performs the initial mount-time reap for the given store.  This will queue all items in the
    /// graveyard.  Concurrently adding more entries to the graveyard will lead to undefined
    /// behaviour: the entries might or might not be immediately tombstoned, so callers should wait
    /// for this to return before changing to a state where more entries can be added.  Once this
    /// has returned, entries will be tombstoned in the background.
    pub async fn initial_reap(self: &Arc<Self>, store: &ObjectStore) -> Result<usize, Error> {
        if store.filesystem().options().skip_initial_reap {
            return Ok(0);
        }
        async_enter!("Graveyard::initial_reap");
        let mut count = 0;
        let layer_set = store.tree().layer_set();
        let mut merger = layer_set.merger();
        let graveyard_object_id = store.graveyard_directory_object_id();
        let mut iter = Self::iter(graveyard_object_id, &mut merger).await?;
        let store_id = store.store_object_id();
        while let Some((object_id, _, value)) = iter.get() {
            match value {
                ObjectValue::Some => self.queue_tombstone(store_id, object_id),
                ObjectValue::Trim => self.queue_trim(store_id, object_id),
                _ => bail!(anyhow!(FxfsError::Inconsistent).context("Bad graveyard value")),
            }
            count += 1;
            iter.advance().await?;
        }
        Ok(count)
    }

    /// Queues an object for tombstoning.
    pub fn queue_tombstone(&self, store_id: u64, object_id: u64) {
        let _ = self.channel.unbounded_send(Message::Tombstone(store_id, object_id));
    }

    fn queue_trim(&self, store_id: u64, object_id: u64) {
        let _ = self.channel.unbounded_send(Message::Trim(store_id, object_id));
    }

    /// Waits for all preceding queued tombstones to finish.
    pub async fn flush(&self) {
        let (sender, receiver) = oneshot::channel::<()>();
        self.channel.unbounded_send(Message::Flush(sender)).unwrap();
        receiver.await.unwrap();
    }

    /// Immediately tombstones (discards) an object in the graveyard.
    /// NB: Code should generally use |queue_tombstone| instead.
    pub async fn tombstone(&self, store_id: u64, object_id: u64) -> Result<(), Error> {
        let store = self
            .object_manager
            .store(store_id)
            .context(format!("Failed to get store {}", store_id))?;
        // For now, it's safe to assume that all objects in the root parent and root store should
        // return space to the metadata reservation, but we might have to revisit that if we end up
        // with objects that are in other stores.
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

    async fn trim(&self, store_id: u64, object_id: u64) -> Result<(), Error> {
        let store = self
            .object_manager
            .store(store_id)
            .context(format!("Failed to get store {}", store_id))?;
        store.trim(object_id).await.context("Failed to trim object")
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
    async fn iter_from<'a, 'b>(
        merger: &'a mut Merger<'b, ObjectKey, ObjectValue>,
        graveyard_object_id: u64,
        from: u64,
    ) -> Result<GraveyardIterator<'a, 'b>, Error> {
        GraveyardIterator::new(
            graveyard_object_id,
            merger
                .seek(Bound::Included(&ObjectKey::graveyard_entry(graveyard_object_id, from)))
                .await?,
        )
        .await
    }
}

pub struct GraveyardIterator<'a, 'b> {
    object_id: u64,
    iter: MergerIterator<'a, 'b, ObjectKey, ObjectValue>,
}

impl<'a, 'b> GraveyardIterator<'a, 'b> {
    async fn new(
        object_id: u64,
        iter: MergerIterator<'a, 'b, ObjectKey, ObjectValue>,
    ) -> Result<GraveyardIterator<'a, 'b>, Error> {
        let mut iter = GraveyardIterator { object_id, iter };
        iter.skip_deleted_entries().await?;
        Ok(iter)
    }

    async fn skip_deleted_entries(&mut self) -> Result<(), Error> {
        loop {
            match self.iter.get() {
                Some(ItemRef {
                    key: ObjectKey { object_id, .. },
                    value: ObjectValue::None,
                    ..
                }) if *object_id == self.object_id => {}
                _ => return Ok(()),
            }
            self.iter.advance().await?;
        }
    }

    /// Returns a tuple (object_id, sequence, value).
    pub fn get(&self) -> Option<(u64, u64, ObjectValue)> {
        match self.iter.get() {
            Some(ItemRef {
                key: ObjectKey { object_id: oid, data: ObjectKeyData::GraveyardEntry { object_id } },
                value,
                sequence,
                ..
            }) if *oid == self.object_id => Some((*object_id, sequence, value.clone())),
            _ => None,
        }
    }

    pub async fn advance(&mut self) -> Result<(), Error> {
        self.iter.advance().await?;
        self.skip_deleted_entries().await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::Graveyard,
        crate::{
            filesystem::{Filesystem, FxFilesystem},
            object_store::object_record::ObjectValue,
            object_store::transaction::{Options, TransactionHandler},
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
            assert_matches!(iter.get().expect("missing entry"), (3, _, ObjectValue::Some));
            iter.advance().await.expect("advance failed");
            assert_matches!(iter.get().expect("missing entry"), (4, _, ObjectValue::Some));
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
        assert_matches!(iter.get().expect("missing entry"), (3, _, ObjectValue::Some));
        iter.advance().await.expect("advance failed");
        assert_eq!(iter.get(), None);
    }
}
