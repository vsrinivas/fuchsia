// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This module is responsible for flushing (a.k.a. compacting) the object store trees.

use {
    crate::{
        debug_assert_not_too_long,
        log::*,
        lsm_tree::{
            layers_from_handles,
            types::{BoxedLayerIterator, ItemRef, LayerIteratorFilter},
            LSMTree,
        },
        object_handle::{ObjectHandle, INVALID_OBJECT_ID},
        object_store::{
            extent_record::ExtentValue,
            layer_size_from_encrypted_mutations_size,
            object_manager::{ObjectManager, ReservationUpdate},
            object_record::{ObjectKey, ObjectValue},
            store_object_handle::DirectWriter,
            transaction::{AssociatedObject, LockKey, Mutation},
            tree, AssocObj, CachingObjectHandle, HandleOptions, ObjectStore, Options, StoreInfo,
            MAX_ENCRYPTED_MUTATIONS_SIZE,
        },
        serialized_types::{Version, VersionedLatest},
        trace_duration,
    },
    anyhow::Error,
    async_trait::async_trait,
    once_cell::sync::OnceCell,
    std::sync::atomic::Ordering,
};

pub enum Reason {
    /// Journal memory or space pressure.
    Journal,

    /// After unlock and replay of encrypted mutations.
    Unlock,

    /// Just prior to locking a store.
    Lock,
}

impl ObjectStore {
    pub async fn flush_with_reason(&self, reason: Reason) -> Result<Version, Error> {
        trace_duration!("ObjectStore::flush", "store_object_id" => self.store_object_id);
        if self.parent_store.is_none() {
            // Early exit, but still return the earliest version used by a struct in the tree
            return Ok(self.tree.get_earliest_version());
        }
        let filesystem = self.filesystem();
        let object_manager = filesystem.object_manager();

        let keys = [LockKey::flush(self.store_object_id())];
        let _guard = debug_assert_not_too_long!(filesystem.write_lock(&keys));

        match reason {
            Reason::Unlock => {
                // If we're unlocking, only flush if there are encrypted mutations currently stored
                // in a file.  We don't worry if they're in memory because a flush should get
                // triggered when the journal gets full.
                if self.store_info().encrypted_mutations_object_id == INVALID_OBJECT_ID {
                    // TODO(fxbug.dev/97078): Add earliest_version support for encrypted mutations.
                    // Early exit, but still return the earliest version used by a struct in the
                    // tree.
                    return Ok(self.tree.get_earliest_version());
                }
            }
            Reason::Journal | Reason::Lock => {
                if !object_manager.needs_flush(self.store_object_id) {
                    // Early exit, but still return the earliest version used by a struct in the
                    // tree.
                    return Ok(self.tree.get_earliest_version());
                }
            }
        }

        let trace = self.trace.load(Ordering::Relaxed);
        if trace {
            info!(store_id = self.store_object_id(), "OS: begin flush");
        }

        let parent_store = self.parent_store.as_ref().unwrap();

        let reservation = object_manager.metadata_reservation();
        let txn_options = Options {
            skip_journal_checks: true,
            borrow_metadata_space: true,
            allocator_reservation: Some(reservation),
            ..Default::default()
        };

        let roll_mutations_key = self
            .mutations_cipher
            .lock()
            .unwrap()
            .as_ref()
            .map(|cipher| {
                cipher.offset() >= self.filesystem().options().roll_metadata_key_byte_count
            })
            .unwrap_or(false);
        if roll_mutations_key {
            self.roll_mutations_key(self.crypt().unwrap().as_ref()).await?;
        }

        struct StoreInfoSnapshot<'a> {
            store: &'a ObjectStore,
            store_info: OnceCell<StoreInfo>,
        }
        impl AssociatedObject for StoreInfoSnapshot<'_> {
            fn will_apply_mutation(
                &self,
                _mutation: &Mutation,
                _object_id: u64,
                _manager: &ObjectManager,
            ) {
                let mut store_info = self.store.store_info();

                // Capture the offset in the cipher stream.
                let mutations_cipher = self.store.mutations_cipher.lock().unwrap();
                if let Some(cipher) = mutations_cipher.as_ref() {
                    store_info.mutations_cipher_offset = cipher.offset();
                }

                // This will capture object IDs that might be in transactions not yet committed.  In
                // theory, we could do better than this but it's not worth the effort.
                store_info.last_object_id = self.store.last_object_id.lock().unwrap().id;

                self.store_info.set(store_info).unwrap();
            }
        }

        let store_info_snapshot = StoreInfoSnapshot { store: self, store_info: OnceCell::new() };

        // The BeginFlush mutation must be within a transaction that has no impact on StoreInfo
        // since we want to get an accurate snapshot of StoreInfo.
        let mut transaction = filesystem.clone().new_transaction(&[], txn_options).await?;
        transaction.add_with_object(
            self.store_object_id(),
            Mutation::BeginFlush,
            AssocObj::Borrowed(&store_info_snapshot),
        );
        transaction.commit().await?;

        // There is a transaction to create objects at the start and then another transaction at the
        // end. Between those two transactions, there are transactions that write to the files.  In
        // the first transaction, objects are created in the graveyard. Upon success, the objects
        // are removed from the graveyard.
        let mut transaction = filesystem.clone().new_transaction(&[], txn_options).await?;

        let reservation_update: ReservationUpdate; // Must live longer than end_transaction.
        let handle; // Must live longer than end_transaction.
        let mut end_transaction = filesystem.clone().new_transaction(&[], txn_options).await?;

        #[async_trait]
        impl tree::MajorCompactable<ObjectKey, ObjectValue> for LSMTree<ObjectKey, ObjectValue> {
            async fn major_iter(
                iter: BoxedLayerIterator<'_, ObjectKey, ObjectValue>,
            ) -> Result<BoxedLayerIterator<'_, ObjectKey, ObjectValue>, Error> {
                Ok(Box::new(
                    iter.filter(|item: ItemRef<'_, _, _>| match item {
                        // Object Tombstone.
                        ItemRef { value: ObjectValue::None, .. } => false,
                        // Deleted extent.
                        ItemRef { value: ObjectValue::Extent(ExtentValue::None), .. } => false,
                        _ => true,
                    })
                    .await?,
                ))
            }
        }

        let mut new_store_info = store_info_snapshot.store_info.into_inner().unwrap();
        let mut total_layer_size = 0;

        let mut old_encrypted_mutations_object_id = INVALID_OBJECT_ID;

        let has_encrypted_mutations =
            self.lock_state.lock().unwrap().encrypted_mutations().is_some();
        let (old_layers, new_layers) = if has_encrypted_mutations {
            // The store is locked so we need to either write our encrypted mutations to a new file,
            // or append them to an existing one.
            handle = if new_store_info.encrypted_mutations_object_id == INVALID_OBJECT_ID {
                let handle = ObjectStore::create_object(
                    parent_store,
                    &mut transaction,
                    HandleOptions { skip_journal_checks: true, ..Default::default() },
                    None,
                )
                .await?;
                let oid = handle.object_id();
                new_store_info.encrypted_mutations_object_id = oid;
                parent_store.add_to_graveyard(&mut transaction, oid);
                parent_store.remove_from_graveyard(&mut end_transaction, oid);
                handle
            } else {
                ObjectStore::open_object(
                    parent_store,
                    new_store_info.encrypted_mutations_object_id,
                    HandleOptions { skip_journal_checks: true, ..Default::default() },
                    None,
                )
                .await?
            };
            transaction.commit().await?;

            // Append the encrypted mutations.
            let mut buffer = handle.allocate_buffer(MAX_ENCRYPTED_MUTATIONS_SIZE);
            let mut cursor = std::io::Cursor::new(buffer.as_mut_slice());
            self.lock_state
                .lock()
                .unwrap()
                .encrypted_mutations()
                .unwrap()
                .serialize_with_version(&mut cursor)?;
            let len = cursor.position() as usize;
            handle
                .txn_write(&mut end_transaction, handle.get_size(), buffer.subslice(..len))
                .await?;

            total_layer_size += layer_size_from_encrypted_mutations_size(handle.get_size())
                + self
                    .tree
                    .immutable_layer_set()
                    .layers
                    .iter()
                    .map(|l| l.handle().map(ObjectHandle::get_size).unwrap_or(0))
                    .sum::<u64>();

            // There are no changes to the layers in this case.
            (Vec::new(), None)
        } else {
            // Create and write a new layer, compacting existing layers.
            let new_object_tree_layer = ObjectStore::create_object(
                parent_store,
                &mut transaction,
                HandleOptions { skip_journal_checks: true, ..Default::default() },
                self.crypt().as_deref(),
            )
            .await?;
            let writer = DirectWriter::new(&new_object_tree_layer, txn_options);
            let new_object_tree_layer_object_id = new_object_tree_layer.object_id();
            parent_store.add_to_graveyard(&mut transaction, new_object_tree_layer_object_id);
            parent_store
                .remove_from_graveyard(&mut end_transaction, new_object_tree_layer_object_id);

            transaction.commit().await?;
            let (layers_to_keep, old_layers) = tree::flush(&self.tree, writer).await?;

            let mut new_layers =
                layers_from_handles(Box::new([CachingObjectHandle::new(new_object_tree_layer)]))
                    .await?;
            new_layers.extend(layers_to_keep.iter().map(|l| (*l).clone()));

            new_store_info.layers = Vec::new();
            for layer in &new_layers {
                if let Some(handle) = layer.handle() {
                    new_store_info.layers.push(handle.object_id());
                }
            }

            // Move the existing layers we're compacting to the graveyard at the end.
            for layer in &old_layers {
                if let Some(handle) = layer.handle() {
                    parent_store.add_to_graveyard(&mut end_transaction, handle.object_id());
                }
            }

            let object_tree_handles = new_layers.iter().map(|l| l.handle());
            total_layer_size += object_tree_handles
                .map(|h| h.map(ObjectHandle::get_size).unwrap_or(0))
                .sum::<u64>();

            old_encrypted_mutations_object_id = std::mem::replace(
                &mut new_store_info.encrypted_mutations_object_id,
                INVALID_OBJECT_ID,
            );
            if old_encrypted_mutations_object_id != INVALID_OBJECT_ID {
                parent_store
                    .add_to_graveyard(&mut end_transaction, old_encrypted_mutations_object_id);
            }

            (old_layers, Some(new_layers))
        };

        let mut serialized_info = Vec::new();
        new_store_info.serialize_with_version(&mut serialized_info)?;
        let mut buf = self.device.allocate_buffer(serialized_info.len());
        buf.as_mut_slice().copy_from_slice(&serialized_info[..]);

        self.store_info_handle
            .get()
            .unwrap()
            .txn_write(&mut end_transaction, 0u64, buf.as_ref())
            .await?;

        reservation_update =
            ReservationUpdate::new(tree::reservation_amount_from_layer_size(total_layer_size));

        end_transaction.add_with_object(
            self.store_object_id(),
            Mutation::EndFlush,
            AssocObj::Borrowed(&reservation_update),
        );

        if trace {
            info!(
                store_id = self.store_object_id(),
                old_layer_count = old_layers.len(),
                new_layer_count = new_layers.as_ref().map(|v| v.len()).unwrap_or(0),
                total_layer_size,
                "OS: compacting"
            );
        }
        end_transaction
            .commit_with_callback(|_| {
                let mut store_info = self.store_info.lock().unwrap();
                let info = store_info.info_mut().unwrap();
                info.layers = new_store_info.layers;
                info.encrypted_mutations_object_id = new_store_info.encrypted_mutations_object_id;
                info.mutations_cipher_offset = new_store_info.mutations_cipher_offset;

                if let Some(layers) = new_layers {
                    self.tree.set_layers(layers);
                }
                if let Some(m) = self.lock_state.lock().unwrap().encrypted_mutations_mut() {
                    std::mem::take(m);
                }
            })
            .await?;

        // Now close the layers and purge them.
        for layer in old_layers {
            let object_id = layer.handle().map(|h| h.object_id());
            layer.close_layer().await;
            if let Some(object_id) = object_id {
                parent_store.tombstone(object_id, txn_options).await?;
            }
        }

        if old_encrypted_mutations_object_id != INVALID_OBJECT_ID {
            parent_store.tombstone(old_encrypted_mutations_object_id, txn_options).await?;
        }

        if trace {
            info!(store_id = self.store_object_id(), "OS: end flush");
        }

        // Return the earliest version used by a struct in the tree
        Ok(self.tree.get_earliest_version())
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            crypt::insecure::InsecureCrypt,
            filesystem::{self, Filesystem, FxFilesystem},
            object_store::{
                directory::Directory,
                transaction::{Options, TransactionHandler},
                volume::root_volume,
            },
        },
        fuchsia_async as fasync,
        std::sync::Arc,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    async fn run_key_roll_test(flush_before_unlock: bool) {
        let device = DeviceHolder::new(FakeDevice::new(8192, 1024));
        let fs = FxFilesystem::new_empty(device).await.expect("new_empty failed");
        let store_id = {
            let root_volume = root_volume(fs.clone()).await.expect("root_volume failed");
            root_volume
                .new_volume("test", Some(Arc::new(InsecureCrypt::new())))
                .await
                .expect("new_volume failed")
                .store_object_id()
        };

        fs.close().await.expect("close failed");
        let device = fs.take_device().await;
        device.reopen(false);

        let filesystem_options =
            filesystem::Options { roll_metadata_key_byte_count: 512 * 1024, ..Default::default() };

        let fs = FxFilesystem::open_with_options(
            device,
            filesystem::OpenOptions { filesystem_options, ..Default::default() },
        )
        .await
        .expect("open failed");

        {
            let store = fs.object_manager().store(store_id).expect("store not found");
            store.unlock(Arc::new(InsecureCrypt::new())).await.expect("unlock failed");

            // Keep writing until we notice the key has rolled.
            let root_dir = Directory::open(&store, store.root_directory_object_id())
                .await
                .expect("open failed");

            let mut last_mutations_cipher_offset = 0;
            let mut i = 0;
            loop {
                let mut transaction = fs
                    .clone()
                    .new_transaction(&[], Options::default())
                    .await
                    .expect("new_transaction failed");
                root_dir
                    .create_child_file(&mut transaction, &format!("{:<200}", i))
                    .await
                    .expect("create_child_file failed");
                i += 1;
                transaction.commit().await.expect("commit failed");
                let cipher_offset =
                    store.mutations_cipher.lock().unwrap().as_ref().unwrap().offset();
                if cipher_offset < last_mutations_cipher_offset {
                    break;
                }
                last_mutations_cipher_offset = cipher_offset;
            }

            // Write one more file to ensure the cipher has a non-zero offset.
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            root_dir
                .create_child_file(&mut transaction, &format!("{:<200}", i))
                .await
                .expect("create_child_file failed");
            transaction.commit().await.expect("commit failed");
        }

        fs.close().await.expect("close failed");

        // Reopen and make sure replay succeeds.
        let device = fs.take_device().await;
        device.reopen(false);
        let fs = FxFilesystem::open_with_options(
            device,
            filesystem::OpenOptions {
                filesystem_options: filesystem::Options {
                    roll_metadata_key_byte_count: 512 * 1024,
                    ..Default::default()
                },
                ..Default::default()
            },
        )
        .await
        .expect("open failed");

        if flush_before_unlock {
            // Flush before unlocking the store which will see that the encrypted mutations get
            // written to a file.
            fs.object_manager().flush().await.expect("flush failed");
        }

        {
            let store = fs.object_manager().store(store_id).expect("store not found");
            store.unlock(Arc::new(InsecureCrypt::new())).await.expect("unlock failed");

            // The key should get rolled when we unlock.
            assert_eq!(store.mutations_cipher.lock().unwrap().as_ref().unwrap().offset(), 0);
        }
    }

    #[fasync::run(10, test)]
    async fn test_metadata_key_roll() {
        run_key_roll_test(/* flush_before_unlock: */ false).await;
    }

    #[fasync::run(10, test)]
    async fn test_metadata_key_roll_with_flush_before_unlock() {
        run_key_roll_test(/* flush_before_unlock: */ true).await;
    }
}
