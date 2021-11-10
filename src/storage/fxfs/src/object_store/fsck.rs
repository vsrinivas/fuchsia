// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::{
            skip_list_layer::SkipListLayer,
            types::{Item, ItemRef, Layer, LayerIterator, MutableLayer},
        },
        object_handle::{ObjectHandle, ObjectHandleExt},
        object_store::{
            allocator::{
                self, Allocator, AllocatorKey, AllocatorValue, CoalescingIterator, SimpleAllocator,
            },
            constants::{SUPER_BLOCK_A_OBJECT_ID, SUPER_BLOCK_B_OBJECT_ID},
            filesystem::{Filesystem, FxFilesystem},
            fsck::errors::{FsckError, FsckFatal, FsckWarning},
            graveyard::Graveyard,
            extent_record::{ExtentKey, ExtentValue},
            object_record::{ObjectKey, ObjectKeyData, ObjectKind, ObjectValue},
            transaction::{LockKey, TransactionHandler},
            volume::root_volume,
            HandleOptions, ObjectStore, StoreInfo, MAX_STORE_INFO_SERIALIZED_SIZE,
        },
    },
    anyhow::{anyhow, Context, Error},
    bincode::deserialize_from,
    futures::try_join,
    std::{
        collections::hash_map::{Entry, HashMap},
        ops::Bound,
        sync::Arc,
    },
};

pub mod errors;

#[cfg(test)]
mod tests;

pub struct FsckOptions<F: Fn(&FsckError)> {
    /// Whether to halt fsck on the first warning.
    halt_on_warning: bool,
    /// A callback to be invoked for each detected error (fatal or not).
    on_error: F,
}

// TODO(csuter): for now, this just checks allocations. We should think about adding checks for:
//
//  + Keys should be in-order.
//  + Objects should either be <object>[<attribute>[<extent>...]...], or <tombstone>.
//  + Values need to match keys.
//  + There should be no orphaned objects, dangling object references (in directory entries), or
//    other object reference mismatches.
//  + No overlapping keys within a single layer.
//  + We might want to individually check layers.
//  + Extents should be aligned and end > start.
//  + No child volumes in anything other than the root store.
//  + The root parent object store ID and root object store ID must not conflict with any other
//    stores or the allocator.
//  + Checking the sub_dirs property on directories.
//
// TODO(csuter): This currently takes a write lock on the filesystem.  It would be nice if we could
// take a snapshot.
pub async fn fsck(filesystem: &Arc<FxFilesystem>) -> Result<(), Error> {
    let options = FsckOptions {
        halt_on_warning: false,
        on_error: |err: &FsckError| {
            if err.is_fatal() {
                log::error!("{:?}", err.to_string())
            } else {
                log::warn!("{:?}", err.to_string())
            }
        },
    };
    fsck_with_options(filesystem, options).await
}

pub async fn fsck_with_options<F: Fn(&FsckError)>(
    filesystem: &Arc<FxFilesystem>,
    options: FsckOptions<F>,
) -> Result<(), Error> {
    log::info!("Starting fsck");
    let _guard = filesystem.write_lock(&[LockKey::Filesystem]).await;

    let fsck = Fsck::new(options);

    let object_manager = filesystem.object_manager();
    // The graveyard not being present would prevent the filesystem from mounting at all.
    let graveyard = object_manager.graveyard().ok_or(anyhow!("Missing graveyard!")).unwrap();
    let super_block = filesystem.super_block();

    // Scan the root parent object store.
    let mut root_objects = vec![super_block.root_store_object_id, super_block.journal_object_id];
    root_objects.append(&mut object_manager.root_store().parent_objects());
    fsck.scan_store(&object_manager.root_parent_store(), &root_objects, &graveyard).await?;

    let root_store = &object_manager.root_store();
    let mut root_store_root_objects = Vec::new();
    root_store_root_objects.append(&mut vec![
        super_block.allocator_object_id,
        SUPER_BLOCK_A_OBJECT_ID,
        SUPER_BLOCK_B_OBJECT_ID,
    ]);
    root_store_root_objects.append(&mut root_store.root_objects());

    if let Some(root_volume) = root_volume(filesystem).await? {
        let volume_directory = root_volume.volume_directory();
        let layer_set = volume_directory.store().tree().layer_set();
        let mut merger = layer_set.merger();
        let mut iter = volume_directory.iter(&mut merger).await?;

        // TODO(csuter): We could maybe iterate over stores concurrently.
        while let Some((_, store_id, _)) = iter.get() {
            fsck.check_child_store(&filesystem, &graveyard, store_id, &mut root_store_root_objects)
                .await?;
            iter.advance().await?;
        }
    }

    // TODO(csuter): It's a bit crude how details of SimpleAllocator are leaking here. Is there
    // a better way?
    let allocator = filesystem.allocator().as_any().downcast::<SimpleAllocator>().unwrap();
    root_store_root_objects.append(&mut allocator.parent_objects());

    // Finally scan the root object store.
    fsck.scan_store(root_store, &root_store_root_objects, &graveyard).await?;

    // Now compare our regenerated allocation map with what we actually have.
    let layer_set = allocator.tree().layer_set();
    let mut merger = layer_set.merger();
    let iter = merger.seek(Bound::Unbounded).await?;
    let mut actual = CoalescingIterator::new(Box::new(iter)).await?;
    let mut expected =
        CoalescingIterator::new(fsck.allocations.seek(Bound::Unbounded).await?).await?;
    let mut allocated_bytes = 0;
    let mut extra_allocations: Vec<errors::Allocation> = vec![];
    while let Some(actual_item) = actual.get() {
        match expected.get() {
            None => extra_allocations.push(actual_item.into()),
            Some(expected_item) => {
                let r = &expected_item.key.device_range;
                allocated_bytes += (r.end - r.start) as i64;
                if actual_item != expected_item {
                    fsck.error(FsckFatal::AllocationMismatch(
                        expected_item.into(),
                        actual_item.into(),
                    ))?;
                }
            }
        }
        try_join!(actual.advance(), expected.advance())?;
    }
    if !extra_allocations.is_empty() {
        fsck.error(FsckFatal::ExtraAllocations(extra_allocations))?;
    }
    if let Some(item) = expected.get() {
        fsck.error(FsckFatal::MissingAllocation(item.into()))?;
    }
    if allocated_bytes as u64 != allocator.get_allocated_bytes() {
        fsck.error(FsckFatal::AllocatedBytesMismatch(
            allocated_bytes as u64,
            allocator.get_allocated_bytes(),
        ))?;
    }
    Ok(())
}

struct Fsck<F: Fn(&FsckError)> {
    options: FsckOptions<F>,
    allocations: Arc<SkipListLayer<AllocatorKey, AllocatorValue>>,
}

impl<F: Fn(&FsckError)> Fsck<F> {
    fn new(options: FsckOptions<F>) -> Self {
        Fsck { options, allocations: SkipListLayer::new(2048) } // TODO(csuter): fix magic number
    }

    fn assert<V>(&self, res: Result<V, Error>, error: FsckFatal) -> Result<V, Error> {
        if res.is_err() {
            (self.options.on_error)(&FsckError::Fatal(error.clone()));
            return Err(anyhow!(format!("{:?}", error)).context(res.err().unwrap()));
        }
        res
    }

    fn warning(&self, error: FsckWarning) -> Result<(), Error> {
        (self.options.on_error)(&FsckError::Warning(error.clone()));
        if self.options.halt_on_warning {
            Err(anyhow!(format!("{:?}", error)))
        } else {
            Ok(())
        }
    }

    fn error(&self, error: FsckFatal) -> Result<(), Error> {
        (self.options.on_error)(&FsckError::Fatal(error.clone()));
        Err(anyhow!(format!("{:?}", error)))
    }

    async fn check_child_store(
        &self,
        filesystem: &FxFilesystem,
        graveyard: &Graveyard,
        store_id: u64,
        root_store_root_objects: &mut Vec<u64>,
    ) -> Result<(), Error> {
        let root_store = filesystem.root_store();
        match filesystem.object_manager().open_store(store_id).await {
            Ok(store) => {
                self.scan_store(&store, &store.root_objects(), graveyard).await?;
                let mut parent_objects = store.parent_objects();
                root_store_root_objects.append(&mut parent_objects);
                Ok(())
            }
            Err(e) => {
                // Try to find out more about why the store failed to open.
                let handle = self.assert(
                    ObjectStore::open_object(&root_store, store_id, HandleOptions::default()).await,
                    FsckFatal::MissingStoreInfo(store_id),
                )?;
                let layer_file_object_ids = {
                    let info = if handle.get_size() > 0 {
                        let serialized_info =
                            handle.contents(MAX_STORE_INFO_SERIALIZED_SIZE).await?;
                        self.assert(
                            deserialize_from(&serialized_info[..])
                                .context("Failed to deserialize StoreInfo"),
                            FsckFatal::MalformedStore(store_id),
                        )?
                    } else {
                        // The store_info will be absent for a newly created and empty object store.
                        StoreInfo::default()
                    };
                    // We don't replay the store ReplayInfo here, since it doesn't affect what we
                    // want to check (mainly the existence of the layer files).  If that changes,
                    // we'll need to update this.
                    let mut object_ids = vec![];
                    object_ids.extend_from_slice(&info.object_tree_layers[..]);
                    object_ids.extend_from_slice(&info.extent_tree_layers[..]);
                    object_ids
                };
                for layer_file_object_id in layer_file_object_ids {
                    self.assert(
                        ObjectStore::open_object(
                            &root_store,
                            layer_file_object_id,
                            HandleOptions::default(),
                        )
                        .await,
                        FsckFatal::MissingLayerFile(store_id, layer_file_object_id),
                    )?;
                    // TODO(fxbug.dev/87381): Check the individual layers files.
                }
                log::warn!("Object store failed to open but no issues were detected by fsck.");
                Err(e)
            }
        }
    }

    async fn scan_store(
        &self,
        store: &ObjectStore,
        root_objects: &[u64],
        graveyard: &Graveyard,
    ) -> Result<(), Error> {
        let mut object_refs: HashMap<u64, (u64, u64)> = HashMap::new();

        let store_id = store.store_object_id();

        // Add all the graveyard references.
        let layer_set = graveyard.store().tree().layer_set();
        let mut merger = layer_set.merger();
        let mut iter = self.assert(
            graveyard.iter_from(&mut merger, (store.store_object_id(), 0)).await,
            FsckFatal::MalformedGraveyard,
        )?;
        while let Some((store_object_id, object_id, _)) = iter.get() {
            if store_object_id != store.store_object_id() {
                break;
            }
            object_refs.insert(object_id, (0, 1));
            self.assert(iter.advance().await, FsckFatal::MalformedGraveyard)?;
        }

        let layer_set = store.tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter =
            self.assert(merger.seek(Bound::Unbounded).await, FsckFatal::MalformedStore(store_id))?;
        for root_object in root_objects {
            object_refs.insert(*root_object, (0, 1));
        }
        let mut object_count = 0;
        while let Some(ItemRef { key, value, .. }) = iter.get() {
            match (key, value) {
                (
                    ObjectKey { object_id, data: ObjectKeyData::Object },
                    ObjectValue::Object { kind, .. },
                ) => {
                    let refs = match kind {
                        ObjectKind::File { refs, .. } => *refs,
                        ObjectKind::Directory { .. } | ObjectKind::Graveyard => 1,
                    };
                    match object_refs.entry(*object_id) {
                        Entry::Occupied(mut occupied) => {
                            occupied.get_mut().0 += refs;
                        }
                        Entry::Vacant(vacant) => {
                            vacant.insert((refs, 0));
                        }
                    }
                    object_count += 1;
                }
                (
                    ObjectKey { data: ObjectKeyData::Child { .. }, .. },
                    ObjectValue::Child { object_id, .. },
                ) => match object_refs.entry(*object_id) {
                    Entry::Occupied(mut occupied) => {
                        occupied.get_mut().1 += 1;
                    }
                    Entry::Vacant(vacant) => {
                        vacant.insert((0, 1));
                    }
                },
                _ => {}
            }
            self.assert(iter.advance().await, FsckFatal::MalformedStore(store_id))?;
        }

        let layer_set = store.extent_tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await?;
        while let Some(ItemRef { key: ExtentKey { range, .. }, value, .. }) = iter.get() {
            if let ExtentValue::Some { device_offset, .. } = value {
                let item = Item::new(
                    AllocatorKey {
                        device_range: *device_offset..*device_offset + range.end - range.start,
                    },
                    AllocatorValue { delta: 1 },
                );
                let lower_bound = item.key.lower_bound_for_merge_into();
                self.allocations.merge_into(item, &lower_bound, allocator::merge::merge).await;
            }
            iter.advance().await?;
        }

        // Check object reference counts.
        for (object_id, (count, references)) in object_refs {
            if count != references {
                // Special case for an orphaned node
                if references == 0 {
                    self.warning(FsckWarning::OrphanedObject(store_id, object_id))?;
                } else {
                    self.error(FsckFatal::RefCountMismatch(object_id, references, count))?;
                }
            }
        }

        if object_count != store.object_count() {
            self.error(FsckFatal::ObjectCountMismatch(
                store_id,
                store.object_count(),
                object_count,
            ))?;
        }

        Ok(())
    }
}
