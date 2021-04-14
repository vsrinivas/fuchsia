// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::{
            skip_list_layer::SkipListLayer,
            types::{Item, Layer, LayerIterator, MutableLayer},
        },
        object_store::{
            allocator::{self, AllocatorKey, AllocatorValue, CoalescingIterator, SimpleAllocator},
            filesystem::Filesystem,
            record::ExtentValue,
        },
    },
    anyhow::{bail, Error},
    futures::try_join,
    std::ops::Bound,
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
// TODO(csuter): This currently assumes no other mutations are taking place.  It would be nice if we
// could take a snapshot or somehow lock the filesystem.
pub async fn fsck(filesystem: &impl Filesystem) -> Result<(), Error> {
    let object_manager = filesystem.object_manager();
    let skip_list = SkipListLayer::new(2048); // TODO(csuter): fix magic number

    // TODO(csuter): We could maybe iterate over stores concurrently.
    for store_id in object_manager.store_object_ids() {
        let store = object_manager.store(store_id).expect("store disappeared!");
        let layer_set = store.tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await?;
        while let Some(item_ref) = iter.get() {
            match item_ref.into() {
                Some((_, _, extent_key, ExtentValue { device_offset: Some(device_offset) })) => {
                    let item = Item::new(
                        AllocatorKey {
                            device_range: *device_offset
                                ..*device_offset + extent_key.range.end - extent_key.range.start,
                        },
                        AllocatorValue { delta: 1 },
                    );
                    let lower_bound = item.key.lower_bound_for_merge_into();
                    skip_list.merge_into(item, &lower_bound, allocator::merge::merge).await;
                }
                _ => {}
            }
            iter.advance().await?;
        }
    }
    // Now compare our regenerated allocation map with what we actually have.
    // TODO(csuter): It's a bit crude how details of SimpleAllocator are leaking here. Is there
    // a better way?
    let allocator = filesystem.allocator().as_any().downcast::<SimpleAllocator>().unwrap();
    let layer_set = allocator.tree().layer_set();
    let mut merger = layer_set.merger();
    let iter = merger.seek(Bound::Unbounded).await?;
    let mut actual = CoalescingIterator::new(Box::new(iter)).await?;
    let mut expected = CoalescingIterator::new(skip_list.seek(Bound::Unbounded).await?).await?;
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

#[cfg(test)]
mod tests {
    use {
        super::fsck,
        crate::{
            lsm_tree::types::{Item, ItemRef, LayerIterator},
            object_store::{
                allocator::{
                    Allocator, AllocatorKey, AllocatorValue, CoalescingIterator, SimpleAllocator,
                },
                filesystem::{Filesystem, FxFilesystem},
                transaction::TransactionHandler,
            },
            testing::fake_device::FakeDevice,
        },
        fuchsia_async as fasync,
        std::{ops::Bound, sync::Arc},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    #[fasync::run_singlethreaded(test)]
    async fn test_extra_allocation() {
        let fs = FxFilesystem::new_empty(Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE)))
            .await
            .expect("new_empty failed");
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        let offset = 2047 * TEST_DEVICE_BLOCK_SIZE as u64;
        fs.allocator()
            .reserve(&mut transaction, offset..offset + TEST_DEVICE_BLOCK_SIZE as u64)
            .await;
        transaction.commit().await;
        let error = format!("{}", fsck(fs.as_ref()).await.expect_err("fsck succeeded"));
        assert!(error.contains("found extra allocation"), "{}", error);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocation_mismatch() {
        let fs = FxFilesystem::new_empty(Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE)))
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
        let error = format!("{}", fsck(fs.as_ref()).await.expect_err("fsck succeeded"));
        assert!(error.contains("mismatch"), "{}", error);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_missing_allocation() {
        let fs = FxFilesystem::new_empty(Arc::new(FakeDevice::new(2048, TEST_DEVICE_BLOCK_SIZE)))
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
        let error = format!("{}", fsck(fs.as_ref()).await.expect_err("fsck succeeded"));
        assert!(error.contains("missing allocation"), "{}", error);
    }
}
