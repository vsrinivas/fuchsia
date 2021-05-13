// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::{
            skip_list_layer::SkipListLayer,
            types::{Item, ItemRef, Layer, LayerIterator, MutableLayer},
        },
        object_store::{
            allocator::{self, AllocatorKey, AllocatorValue, CoalescingIterator, SimpleAllocator},
            constants::SUPER_BLOCK_OBJECT_ID,
            filesystem::{Filesystem, FxFilesystem},
            graveyard::Graveyard,
            record::{
                AttributeKey, ExtentValue, ObjectKey, ObjectKeyData, ObjectKind, ObjectValue,
            },
            transaction::LockKey,
            ObjectStore,
        },
    },
    anyhow::{anyhow, bail, Error},
    futures::try_join,
    std::{
        collections::hash_map::{Entry, HashMap},
        ops::Bound,
        sync::Arc,
    },
};

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
//
// TODO(csuter): This currently takes a write lock on the filesystem.  It would be nice if we could
// take a snapshot.
pub async fn fsck(filesystem: &FxFilesystem) -> Result<(), Error> {
    log::info!("Starting fsck");
    let _guard = filesystem.write_lock(&[LockKey::Filesystem]).await;

    let object_manager = filesystem.object_manager();
    let graveyard = object_manager.graveyard().ok_or(anyhow!("Missing graveyard!"))?;
    let fsck = Fsck::new();
    let super_block = filesystem.super_block();

    // Scan the root parent object store.
    let mut root_objects = vec![super_block.root_store_object_id, super_block.journal_object_id];
    root_objects.append(&mut object_manager.root_store().parent_objects());
    fsck.scan_store(&object_manager.root_parent_store(), &root_objects, &graveyard).await?;

    let root_store = &object_manager.root_store();
    let mut root_store_root_objects = Vec::new();
    root_store_root_objects
        .append(&mut vec![super_block.allocator_object_id, SUPER_BLOCK_OBJECT_ID]);
    root_store_root_objects.append(&mut root_store.root_objects());

    // TODO(csuter): We could maybe iterate over stores concurrently.
    for store_id in object_manager.store_object_ids() {
        if store_id == super_block.root_parent_store_object_id
            || store_id == super_block.root_store_object_id
        {
            continue;
        }
        let store = object_manager.store(store_id).expect("store disappeared!");
        store.ensure_open().await?;
        fsck.scan_store(&store, &store.root_objects(), &graveyard).await?;
        let mut parent_objects = store.parent_objects();
        root_store_root_objects.append(&mut parent_objects);
    }

    // TODO(csuter): It's a bit crude how details of SimpleAllocator are leaking here. Is there
    // a better way?
    let allocator = filesystem.allocator().as_any().downcast::<SimpleAllocator>().unwrap();
    allocator.ensure_open().await?;
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
    while let Some(actual_item) = actual.get() {
        match expected.get() {
            None => bail!("found extra allocation {:?}", actual_item),
            Some(expected_item) => {
                if actual_item != expected_item {
                    bail!("mismatch: actual ({:?}) != expected ({:?})", actual_item, expected_item);
                }
            }
        }
        try_join!(actual.advance(), expected.advance())?;
    }
    if let Some(item) = expected.get() {
        bail!("missing allocation {:?}", item);
    }
    Ok(())
}

struct Fsck {
    allocations: Arc<SkipListLayer<AllocatorKey, AllocatorValue>>,
}

impl Fsck {
    fn new() -> Self {
        Fsck { allocations: SkipListLayer::new(2048) } // TODO(csuter): fix magic number
    }

    pub async fn scan_store(
        &self,
        store: &ObjectStore,
        root_objects: &[u64],
        graveyard: &Graveyard,
    ) -> Result<(), Error> {
        let mut object_refs: HashMap<u64, (u64, u64)> = HashMap::new();

        // Add all the graveyard references.
        let layer_set = graveyard.store().tree().layer_set();
        let mut merger = layer_set.merger();
        let mut iter = graveyard.iter_from(&mut merger, (store.store_object_id(), 0)).await?;
        while let Some((store_object_id, object_id)) = iter.get() {
            if store_object_id != store.store_object_id() {
                break;
            }
            object_refs.insert(object_id, (0, 1));
            iter.advance().await?;
        }

        let layer_set = store.tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await?;
        for root_object in root_objects {
            object_refs.insert(*root_object, (0, 1));
        }
        while let Some(ItemRef { key, value }) = iter.get() {
            match (key, value) {
                (
                    ObjectKey { object_id, data: ObjectKeyData::Object },
                    ObjectValue::Object { kind, .. },
                ) => {
                    let refs = match kind {
                        ObjectKind::File { refs, .. } => *refs,
                        ObjectKind::Directory | ObjectKind::Graveyard => 1,
                    };
                    match object_refs.entry(*object_id) {
                        Entry::Occupied(mut occupied) => {
                            occupied.get_mut().0 += refs;
                        }
                        Entry::Vacant(vacant) => {
                            vacant.insert((refs, 0));
                        }
                    }
                }
                (
                    ObjectKey {
                        data: ObjectKeyData::Attribute(_, AttributeKey::Extent(extent_key)),
                        ..
                    },
                    ObjectValue::Extent(ExtentValue { device_offset: Some(device_offset) }),
                ) => {
                    let item = Item::new(
                        AllocatorKey {
                            device_range: *device_offset
                                ..*device_offset + extent_key.range.end - extent_key.range.start,
                        },
                        AllocatorValue { delta: 1 },
                    );
                    let lower_bound = item.key.lower_bound_for_merge_into();
                    self.allocations.merge_into(item, &lower_bound, allocator::merge::merge).await;
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
            iter.advance().await?;
        }
        // Check object reference counts.
        for (object_id, (count, references)) in object_refs {
            if count != references {
                bail!(
                    "object {}.{} reference count mismatch: actual: {}, expected: {}",
                    store.store_object_id(),
                    object_id,
                    count,
                    references
                );
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::fsck,
        crate::{
            device::DeviceHolder,
            lsm_tree::types::{Item, ItemRef, LayerIterator},
            object_handle::ObjectHandle,
            object_store::{
                allocator::{
                    Allocator, AllocatorKey, AllocatorValue, CoalescingIterator, SimpleAllocator,
                },
                directory::Directory,
                filesystem::{Filesystem, FxFilesystem},
                record::ObjectDescriptor,
                transaction::TransactionHandler,
                HandleOptions, ObjectStore,
            },
            testing::fake_device::FakeDevice,
        },
        fuchsia_async as fasync,
        std::ops::Bound,
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run_singlethreaded(test)]
    async fn test_extra_allocation() {
        let fs = FxFilesystem::new_empty(DeviceHolder::new(FakeDevice::new(
            2048,
            TEST_DEVICE_BLOCK_SIZE,
        )))
        .await
        .expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let offset = 2047 * TEST_DEVICE_BLOCK_SIZE as u64;
        fs.allocator()
            .reserve(&mut transaction, offset..offset + TEST_DEVICE_BLOCK_SIZE as u64)
            .await;
        transaction.commit().await;
        let error = format!("{}", fsck(&fs).await.expect_err("fsck succeeded"));
        assert!(error.contains("found extra allocation"), "{}", error);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocation_mismatch() {
        let fs = FxFilesystem::new_empty(DeviceHolder::new(FakeDevice::new(
            2048,
            TEST_DEVICE_BLOCK_SIZE,
        )))
        .await
        .expect("new_empty failed");
        let allocator = fs.allocator().as_any().downcast::<SimpleAllocator>().unwrap();
        let range = {
            let layer_set = allocator.tree().layer_set();
            let mut merger = layer_set.merger();
            let iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
            let ItemRef { key: AllocatorKey { device_range }, .. } =
                iter.get().expect("missing item");
            device_range.clone()
        };
        // This should just change the delta.
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        allocator.reserve(&mut transaction, range).await;
        transaction.commit().await;
        let error = format!("{}", fsck(&fs).await.expect_err("fsck succeeded"));
        assert!(error.contains("mismatch"), "{}", error);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_missing_allocation() {
        let fs = FxFilesystem::new_empty(DeviceHolder::new(FakeDevice::new(
            2048,
            TEST_DEVICE_BLOCK_SIZE,
        )))
        .await
        .expect("new_empty failed");
        let allocator = fs.allocator().as_any().downcast::<SimpleAllocator>().unwrap();
        let key = {
            let layer_set = allocator.tree().layer_set();
            let mut merger = layer_set.merger();
            let iter = CoalescingIterator::new(Box::new(
                merger.seek(Bound::Unbounded).await.expect("seek failed"),
            ))
            .await
            .expect("new failed");
            let ItemRef { key, .. } = iter.get().expect("missing item");
            key.clone()
        };
        let lower_bound = key.lower_bound_for_merge_into();
        allocator
            .tree()
            .merge_into(Item::new(key, AllocatorValue { delta: -1 }), &lower_bound)
            .await;
        let error = format!("{}", fsck(&fs).await.expect_err("fsck succeeded"));
        assert!(error.contains("missing allocation"), "{}", error);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_too_many_object_refs() {
        let fs = FxFilesystem::new_empty(DeviceHolder::new(FakeDevice::new(
            2048,
            TEST_DEVICE_BLOCK_SIZE,
        )))
        .await
        .expect("new_empty failed");

        let root_store = fs.root_store();
        let root_directory = Directory::open(&root_store, root_store.root_directory_object_id())
            .await
            .expect("open failed");

        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let child_file = root_directory
            .create_child_file(&mut transaction, "child_file")
            .await
            .expect("create_child_file failed");
        let child_dir = root_directory
            .create_child_dir(&mut transaction, "child_dir")
            .await
            .expect("create_child_directory failed");

        // Add an extra reference to the child file.
        child_dir.insert_child(
            &mut transaction,
            "test",
            child_file.object_id(),
            ObjectDescriptor::File,
        );
        transaction.commit().await;

        let error = format!("{}", fsck(&fs).await.expect_err("fsck succeeded"));
        assert!(error.contains("reference count mismatch"), "{}", error);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_too_few_object_refs() {
        let fs = FxFilesystem::new_empty(DeviceHolder::new(FakeDevice::new(
            2048,
            TEST_DEVICE_BLOCK_SIZE,
        )))
        .await
        .expect("new_empty failed");

        let root_store = fs.root_store();

        // Create an object but no directory entry referencing that object, so it will end up with a
        // reference count of one, but zero references.
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        ObjectStore::create_object(&root_store, &mut transaction, HandleOptions::default())
            .await
            .expect("create_object failed");
        transaction.commit().await;

        let error = format!("{}", fsck(&fs).await.expect_err("fsck succeeded"));
        assert!(error.contains("reference count mismatch"), "{}", error);
    }
}
