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
            current_time,
            record::{
                ObjectAttributes, ObjectItem, ObjectKey, ObjectKeyData, ObjectKind, ObjectValue,
            },
            transaction::{Mutation, Transaction},
            ObjectStore,
        },
    },
    anyhow::{bail, Error},
    std::{ops::Bound, sync::Arc},
};

/// A graveyard exists as a place to park objects that should be deleted when they are no longer in
/// use.  How objects enter and leave the graveyard is up to the caller to decide.  The intention is
/// that at mount time, any objects in the graveyard will get removed.
pub struct Graveyard {
    store: Arc<ObjectStore>,
    object_id: u64,
}

impl Graveyard {
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
    ) -> Result<Graveyard, Error> {
        store.ensure_open().await?;
        let object_id = store.get_next_object_id();
        let now = current_time();
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
        Ok(Graveyard { store: store.clone(), object_id })
    }

    /// Opens a graveyard object in `store`.
    pub async fn open(store: &Arc<ObjectStore>, object_id: u64) -> Result<Graveyard, Error> {
        store.ensure_open().await?;
        if let ObjectItem {
            value: ObjectValue::Object { kind: ObjectKind::Graveyard, .. }, ..
        } = store.tree.find(&ObjectKey::object(object_id)).await?.ok_or(FxfsError::NotFound)?
        {
            Ok(Graveyard { store: store.clone(), object_id })
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
    pub fn get(&self) -> Option<(u64, u64)> {
        match self.iter.get() {
            Some(ItemRef {
                key:
                    ObjectKey {
                        object_id: oid,
                        data: ObjectKeyData::GraveyardEntry { store_object_id, object_id },
                    },
                ..
            }) if *oid == self.object_id => Some((*store_object_id, *object_id)),
            _ => None,
        }
    }

    pub async fn advance(&mut self) -> Result<(), Error> {
        loop {
            self.iter.advance().await?;
            // Skip deleted entries.
            match self.iter.get() {
                Some(ItemRef { key: ObjectKey { object_id, .. }, value: ObjectValue::None })
                    if *object_id == self.object_id => {}
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
            filesystem::FxFilesystem,
            transaction::{Options, TransactionHandler},
        },
        fuchsia_async as fasync,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run_singlethreaded(test)]
    async fn test_graveyard() {
        let device = DeviceHolder::new(FakeDevice::new(4096, TEST_DEVICE_BLOCK_SIZE));
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
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
        transaction.commit().await;

        // Reopen the graveyard and check that we see the objects we added.
        let graveyard =
            Graveyard::open(&root_store, graveyard.object_id()).await.expect("open failed");
        {
            let layer_set = graveyard.store().tree().layer_set();
            let mut merger = layer_set.merger();
            let mut iter = graveyard.iter(&mut merger).await.expect("iter failed");
            assert_eq!(iter.get().expect("missing entry"), (2, 3));
            iter.advance().await.expect("advance failed");
            assert_eq!(iter.get().expect("missing entry"), (3, 4));
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
        transaction.commit().await;

        // Check that the graveyard has been updated as expected.
        let layer_set = graveyard.store().tree().layer_set();
        let mut merger = layer_set.merger();
        let mut iter = graveyard.iter_from(&mut merger, (2, 3)).await.expect("iter failed");
        assert_eq!(iter.get().expect("missing entry"), (2, 3));
        iter.advance().await.expect("advance failed");
        assert_eq!(iter.get(), None);
    }
}
