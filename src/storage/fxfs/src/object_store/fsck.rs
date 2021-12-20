// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::{
            simple_persistent_layer::SimplePersistentLayer,
            skip_list_layer::SkipListLayer,
            types::{
                BoxedLayerIterator, Item, ItemRef, Key, Layer, LayerIterator, MutableLayer,
                OrdUpperBound, RangeKey, Value,
            },
        },
        object_handle::{ObjectHandle, ObjectHandleExt, INVALID_OBJECT_ID},
        object_store::{
            allocator::{
                self, Allocator, AllocatorKey, AllocatorValue, CoalescingIterator, SimpleAllocator,
            },
            constants::{SUPER_BLOCK_A_OBJECT_ID, SUPER_BLOCK_B_OBJECT_ID},
            extent_record::{ExtentKey, ExtentValue},
            filesystem::{Filesystem, FxFilesystem},
            fsck::errors::{FsckError, FsckFatal, FsckIssue, FsckWarning},
            graveyard::Graveyard,
            object_record::{ObjectDescriptor, ObjectKey, ObjectKeyData, ObjectKind, ObjectValue},
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
        sync::{
            atomic::{AtomicU64, Ordering},
            Arc,
        },
    },
};

pub mod errors;

#[cfg(test)]
mod tests;

pub struct FsckOptions<F: Fn(&FsckIssue)> {
    /// Whether to halt fsck on the first error (fatal or not).
    halt_on_error: bool,
    /// Whether to perform slower, more complete checks.
    do_slow_passes: bool,
    /// A callback to be invoked for each detected error, e.g. to log the error.
    on_error: F,
}

// TODO(csuter): for now, this just checks allocations. We should think about adding checks for:
//
//  + The root parent object store ID and root object store ID must not conflict with any other
//    stores or the allocator.
//  + Checking the sub_dirs property on directories.
//
// TODO(csuter): This currently takes a write lock on the filesystem.  It would be nice if we could
// take a snapshot.
pub async fn fsck(filesystem: &Arc<FxFilesystem>) -> Result<(), Error> {
    let options = FsckOptions {
        halt_on_error: false,
        do_slow_passes: true,
        on_error: |err: &FsckIssue| {
            if err.is_error() {
                log::error!("{:?}", err.to_string())
            } else {
                log::warn!("{:?}", err.to_string())
            }
        },
    };
    fsck_with_options(filesystem, options).await
}

pub async fn fsck_with_options<F: Fn(&FsckIssue)>(
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

    if fsck.options.do_slow_passes {
        // Scan each layer file for the allocator.
        let layer_set = allocator.tree().immutable_layer_set();
        for layer in layer_set.layers {
            fsck.check_layer_file_contents(
                allocator.object_id(),
                layer.handle().map(|h| h.object_id()).unwrap_or(INVALID_OBJECT_ID),
                layer.clone(),
            )
            .await?;
        }
    }

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
    let bs = filesystem.block_size();
    while let Some(actual_item) = actual.get() {
        if actual_item.key.device_range.start % bs > 0 || actual_item.key.device_range.end % bs > 0
        {
            fsck.error(FsckError::MisalignedAllocation(actual_item.into()))?;
        } else if actual_item.key.device_range.start >= actual_item.key.device_range.end {
            fsck.error(FsckError::MalformedAllocation(actual_item.into()))?;
        }
        match expected.get() {
            None => extra_allocations.push(actual_item.into()),
            Some(expected_item) => {
                let r = &expected_item.key.device_range;
                allocated_bytes += (r.end - r.start) as i64;
                if actual_item != expected_item {
                    fsck.error(FsckError::AllocationMismatch(
                        expected_item.into(),
                        actual_item.into(),
                    ))?;
                }
            }
        }
        try_join!(actual.advance(), expected.advance())?;
    }
    if !extra_allocations.is_empty() {
        fsck.error(FsckError::ExtraAllocations(extra_allocations))?;
    }
    if let Some(item) = expected.get() {
        fsck.error(FsckError::MissingAllocation(item.into()))?;
    }
    if allocated_bytes as u64 != allocator.get_allocated_bytes() {
        fsck.error(FsckError::AllocatedBytesMismatch(
            allocated_bytes as u64,
            allocator.get_allocated_bytes(),
        ))?;
    }
    let errors = fsck.errors();
    if errors > 0 {
        Err(anyhow!("Fsck encountered {} errors", errors))
    } else {
        Ok(())
    }
}

trait KeyExt: PartialEq {
    fn overlaps(&self, other: &Self) -> bool {
        self == other
    }
}

// Without https://rust-lang.github.io/rfcs/1210-impl-specialization.html, we can't have one
// behaviour for RangeKey and another for Key, unless we specify all possible Keys.
impl KeyExt for ObjectKey {}

impl<K: RangeKey + PartialEq> KeyExt for K {
    fn overlaps(&self, other: &Self) -> bool {
        RangeKey::overlaps(self, other)
    }
}

struct Fsck<F: Fn(&FsckIssue)> {
    options: FsckOptions<F>,
    allocations: Arc<SkipListLayer<AllocatorKey, AllocatorValue>>,
    errors: AtomicU64,
}

impl<F: Fn(&FsckIssue)> Fsck<F> {
    fn new(options: FsckOptions<F>) -> Self {
        // TODO(csuter): fix magic number
        Fsck { options, allocations: SkipListLayer::new(2048), errors: AtomicU64::new(0) }
    }

    fn errors(&self) -> u64 {
        self.errors.load(Ordering::Relaxed)
    }

    fn assert<V>(&self, res: Result<V, Error>, error: FsckFatal) -> Result<V, Error> {
        if res.is_err() {
            (self.options.on_error)(&FsckIssue::Fatal(error.clone()));
            return Err(anyhow!(format!("{:?}", error)).context(res.err().unwrap()));
        }
        res
    }

    fn warning(&self, error: FsckWarning) -> Result<(), Error> {
        (self.options.on_error)(&FsckIssue::Warning(error.clone()));
        Ok(())
    }

    fn error(&self, error: FsckError) -> Result<(), Error> {
        (self.options.on_error)(&FsckIssue::Error(error.clone()));
        self.errors.fetch_add(1, Ordering::Relaxed);
        if self.options.halt_on_error {
            Err(anyhow!(format!("{:?}", error)))
        } else {
            Ok(())
        }
    }

    fn fatal(&self, error: FsckFatal) -> Result<(), Error> {
        (self.options.on_error)(&FsckIssue::Fatal(error.clone()));
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

        // Manually open the store so we can do our own validation.  Later, we will call open_store
        // to get a regular ObjectStore wrapper.
        let handle = self.assert(
            ObjectStore::open_object(&root_store, store_id, HandleOptions::default()).await,
            FsckFatal::MissingStoreInfo(store_id),
        )?;
        let (object_layer_file_object_ids, extent_layer_file_object_ids) = {
            let info = if handle.get_size() > 0 {
                let serialized_info = handle.contents(MAX_STORE_INFO_SERIALIZED_SIZE).await?;
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
            (info.object_tree_layers.clone(), info.extent_tree_layers.clone())
        };
        for layer_file_object_id in object_layer_file_object_ids {
            self.check_layer_file::<ObjectKey, ObjectValue>(
                &root_store,
                store_id,
                layer_file_object_id,
            )
            .await?;
        }
        for layer_file_object_id in extent_layer_file_object_ids {
            self.check_layer_file::<ExtentKey, ExtentValue>(
                &root_store,
                store_id,
                layer_file_object_id,
            )
            .await?;
        }

        let store = filesystem.object_manager().open_store(store_id).await?;
        self.scan_store(&store, &store.root_objects(), graveyard).await?;
        let mut parent_objects = store.parent_objects();
        root_store_root_objects.append(&mut parent_objects);
        Ok(())
    }

    async fn check_layer_file<
        K: Key + KeyExt + OrdUpperBound + std::fmt::Debug,
        V: Value + std::fmt::Debug,
    >(
        &self,
        root_store: &Arc<ObjectStore>,
        store_object_id: u64,
        layer_file_object_id: u64,
    ) -> Result<(), Error> {
        let layer_file = self.assert(
            ObjectStore::open_object(root_store, layer_file_object_id, HandleOptions::default())
                .await,
            FsckFatal::MissingLayerFile(store_object_id, layer_file_object_id),
        )?;
        if self.options.do_slow_passes {
            // TODO(ripper): When we have multiple layer file formats, we'll need some way of
            // detecting which format we are dealing with.  For now, we just assume it's
            // SimplePersistentLayer.
            let layer = SimplePersistentLayer::open(layer_file).await?;
            self.check_layer_file_contents(
                store_object_id,
                layer_file_object_id,
                layer as Arc<dyn Layer<K, V>>,
            )
            .await?;
        }
        Ok(())
    }

    async fn check_layer_file_contents<
        K: Key + KeyExt + OrdUpperBound + std::fmt::Debug,
        V: Value + std::fmt::Debug,
    >(
        &self,
        store_object_id: u64,
        layer_file_object_id: u64,
        layer: Arc<dyn Layer<K, V>>,
    ) -> Result<(), Error> {
        let mut iter: BoxedLayerIterator<'_, K, V> = self.assert(
            layer.seek(Bound::Unbounded).await,
            FsckFatal::MalformedLayerFile(store_object_id, layer_file_object_id),
        )?;

        let mut last_item: Option<Item<K, V>> = None;
        while let Some(item) = iter.get() {
            if let Some(last) = last_item {
                if !last.key.cmp_upper_bound(&item.key).is_le() {
                    self.fatal(FsckFatal::MisOrderedLayerFile(
                        store_object_id,
                        layer_file_object_id,
                    ))?;
                }
                if last.key.overlaps(&item.key) {
                    self.fatal(FsckFatal::OverlappingKeysInLayerFile(
                        store_object_id,
                        layer_file_object_id,
                        item.into(),
                        last.as_item_ref().into(),
                    ))?;
                }
            }
            last_item = Some(item.cloned());
            self.assert(
                iter.advance().await,
                FsckFatal::MalformedLayerFile(store_object_id, layer_file_object_id),
            )?;
        }
        Ok(())
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
        while let Some(item) = iter.get() {
            self.process_object_item(store, item, &mut object_refs, &mut object_count)?;
            self.assert(iter.advance().await, FsckFatal::MalformedStore(store_id))?;
        }

        let bs = store.block_size();
        let layer_set = store.extent_tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await?;
        while let Some(ItemRef { key: ExtentKey { object_id, range, .. }, value, .. }) = iter.get()
        {
            if range.start % bs > 0 || range.end % bs > 0 {
                self.error(FsckError::MisalignedExtent(store_id, *object_id, range.clone(), 0))?;
            } else if range.start >= range.end {
                self.error(FsckError::MalformedExtent(store_id, *object_id, range.clone(), 0))?;
            }
            if let ExtentValue::Some { device_offset, .. } = value {
                if device_offset % bs > 0 {
                    self.error(FsckError::MisalignedExtent(
                        store_id,
                        *object_id,
                        range.clone(),
                        *device_offset,
                    ))?;
                }
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
                    self.error(FsckError::RefCountMismatch(object_id, references, count))?;
                }
            }
        }

        if object_count != store.object_count() {
            self.error(FsckError::ObjectCountMismatch(
                store_id,
                store.object_count(),
                object_count,
            ))?;
        }

        Ok(())
    }

    fn process_object_item<'a>(
        &self,
        store: &ObjectStore,
        item: ItemRef<'a, ObjectKey, ObjectValue>,
        object_refs: &mut HashMap<u64, (u64, u64)>,
        object_count: &mut u64,
    ) -> Result<(), Error> {
        let (key, value) = (item.key, item.value);
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
                *object_count += 1;
            }
            (
                ObjectKey { data: ObjectKeyData::Child { .. }, .. },
                ObjectValue::Child { object_id, object_descriptor },
            ) => {
                match object_refs.entry(*object_id) {
                    Entry::Occupied(mut occupied) => {
                        occupied.get_mut().1 += 1;
                    }
                    Entry::Vacant(vacant) => {
                        vacant.insert((0, 1));
                    }
                }
                match object_descriptor {
                    ObjectDescriptor::File | ObjectDescriptor::Directory => {}
                    ObjectDescriptor::Volume => {
                        if !store.is_root() {
                            self.error(FsckError::VolumeInChildStore(
                                store.store_object_id(),
                                *object_id,
                            ))?;
                        }
                    }
                }
            }
            _ => {}
        }
        Ok(())
    }
}
