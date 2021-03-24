// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod merge;

use {
    crate::{
        lsm_tree::types::{BoxedLayerIterator, Item, ItemRef, LayerIterator, OrdLowerBound},
        object_store::transaction::Transaction,
    },
    anyhow::Error,
    async_trait::async_trait,
    serde::{Deserialize, Serialize},
};

/// Allocators must implement this.  An allocator is responsible for allocating ranges on behalf of
/// an object-store.
#[async_trait]
pub trait Allocator: Send + Sync {
    /// Returns the object ID for the allocator.
    fn object_id(&self) -> u64;

    /// Tries to allocate enough space for |object_range| in the specified object and returns the
    /// device ranges allocated.
    async fn allocate(
        &self,
        transaction: &mut Transaction,
        store_object_id: u64,
        object_id: u64,
        attribute_id: u64,
        object_range: std::ops::Range<u64>,
    ) -> Result<std::ops::Range<u64>, Error>;

    /// Deallocates the given device range for the specified object.
    async fn deallocate(
        &self,
        transaction: &mut Transaction,
        store_object_id: u64,
        object_id: u64,
        attribute_id: u64,
        device_range: std::ops::Range<u64>,
        file_offset: u64,
    );
}

// Our allocator implementation tracks extents with a reference count.  At time of writing, these
// reference counts should never exceed 1, but that might change with snapshots and clones.

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AllocatorKey {
    device_range: std::ops::Range<u64>,
}

impl Ord for AllocatorKey {
    fn cmp(&self, other: &AllocatorKey) -> std::cmp::Ordering {
        self.device_range.end.cmp(&other.device_range.end)
    }
}

impl PartialOrd for AllocatorKey {
    fn partial_cmp(&self, other: &AllocatorKey) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl OrdLowerBound for AllocatorKey {
    fn cmp_lower_bound(&self, other: &AllocatorKey) -> std::cmp::Ordering {
        self.device_range.start.cmp(&other.device_range.start)
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AllocatorValue {
    // This is the delta on a reference count for the extent.
    delta: i64,
}

pub type AllocatorItem = Item<AllocatorKey, AllocatorValue>;

// The merger is unable to merge extents that exist like the following:
//
//     |----- +1 -----|
//                    |----- -1 -----|
//                    |----- +2 -----|
//
// It cannot coalesce them because it has to emit the +1 record so that it can move on and merge the
// -1 and +2 records. To address this, we add another stage that applies after merging which
// coalesces records after they have been emitted.  This is a bit simpler than merging because the
// records cannot overlap, so it's just a question of merging adjacent records if they happen to
// have the same delta.

struct CoalescingIterator<'a> {
    iter: BoxedLayerIterator<'a, AllocatorKey, AllocatorValue>,
    item: Option<AllocatorItem>,
}

impl<'a> CoalescingIterator<'a> {
    async fn new(
        iter: BoxedLayerIterator<'a, AllocatorKey, AllocatorValue>,
    ) -> Result<CoalescingIterator<'a>, Error> {
        let mut iter = Self { iter, item: None };
        iter.advance().await?;
        Ok(iter)
    }
}

#[async_trait]
impl LayerIterator<AllocatorKey, AllocatorValue> for CoalescingIterator<'_> {
    async fn advance(&mut self) -> Result<(), Error> {
        self.item = self.iter.get().map(|x| x.cloned());
        if self.item.is_none() {
            return Ok(());
        }
        let left = self.item.as_mut().unwrap();
        loop {
            self.iter.advance().await?;
            match self.iter.get() {
                None => return Ok(()),
                Some(right) => {
                    // The two records cannot overlap.
                    assert!(left.key.device_range.end <= right.key.device_range.start);
                    // We can only coalesce the records if they are touching and have the same
                    // delta.
                    if left.key.device_range.end < right.key.device_range.start
                        || left.value.delta != right.value.delta
                    {
                        return Ok(());
                    }
                    left.key.device_range.end = right.key.device_range.end;
                }
            }
        }
    }

    fn get(&self) -> Option<ItemRef<'_, AllocatorKey, AllocatorValue>> {
        self.item.as_ref().map(|x| x.as_item_ref())
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            lsm_tree::{
                skip_list_layer::SkipListLayer,
                types::{Item, ItemRef, Layer, LayerIterator, MutableLayer},
                LSMTree,
            },
            object_store::allocator::{
                merge::merge, AllocatorKey, AllocatorValue, CoalescingIterator,
            },
        },
        fuchsia_async as fasync,
        std::ops::Bound,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_coalescing_iterator() {
        let skip_list = SkipListLayer::new(100);
        let items = [
            Item::new(AllocatorKey { device_range: 0..100 }, AllocatorValue { delta: 1 }),
            Item::new(AllocatorKey { device_range: 100..200 }, AllocatorValue { delta: 1 }),
        ];
        skip_list.insert(items[1].clone()).await;
        skip_list.insert(items[0].clone()).await;
        let mut iter =
            CoalescingIterator::new(skip_list.seek(Bound::Unbounded).await.expect("seek failed"))
                .await
                .expect("new failed");
        let ItemRef { key, value } = iter.get().expect("get failed");
        assert_eq!(
            (key, value),
            (&AllocatorKey { device_range: 0..200 }, &AllocatorValue { delta: 1 })
        );
        iter.advance().await.expect("advance failed");
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_and_coalesce_across_three_layers() {
        let lsm_tree = LSMTree::new(merge);
        lsm_tree
            .insert(Item::new(AllocatorKey { device_range: 100..200 }, AllocatorValue { delta: 2 }))
            .await;
        lsm_tree.seal();
        lsm_tree
            .insert(Item::new(
                AllocatorKey { device_range: 100..200 },
                AllocatorValue { delta: -1 },
            ))
            .await;
        lsm_tree.seal();
        lsm_tree
            .insert(Item::new(AllocatorKey { device_range: 0..100 }, AllocatorValue { delta: 1 }))
            .await;

        let layer_set = lsm_tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = CoalescingIterator::new(Box::new(
            merger.seek(Bound::Unbounded).await.expect("seek failed"),
        ))
        .await
        .expect("new failed");
        let ItemRef { key, value } = iter.get().expect("get failed");
        assert_eq!(
            (key, value),
            (&AllocatorKey { device_range: 0..200 }, &AllocatorValue { delta: 1 })
        );
        iter.advance().await.expect("advance failed");
        assert!(iter.get().is_none());
    }
}
