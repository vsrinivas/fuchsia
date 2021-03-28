// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod merge;

use {
    crate::{
        lsm_tree::{
            skip_list_layer::SkipListLayer,
            types::{
                BoxedLayerIterator, Item, ItemRef, LayerIterator, MutableLayer, OrdLowerBound,
            },
            LSMTree,
        },
        object_handle::{ObjectHandle, ObjectHandleExt},
        object_store::{
            filesystem::{ApplyMutations, Filesystem},
            transaction::{Mutation, Transaction},
            HandleOptions,
        },
    },
    anyhow::{ensure, Error},
    async_trait::async_trait,
    bincode::{deserialize_from, serialize_into},
    merge::merge,
    serde::{Deserialize, Serialize},
    std::{
        cmp::min,
        ops::{Bound, Range},
        sync::{Arc, Mutex, Weak},
    },
};

/// Allocators must implement this.  An allocator is responsible for allocating ranges on behalf of
/// an object-store.
#[async_trait]
pub trait Allocator: Send + Sync {
    /// Returns the object ID for the allocator.
    fn object_id(&self) -> u64;

    /// Tries to allocate enough space for |object_range| in the specified object and returns the
    /// device ranges allocated.
    async fn allocate(&self, transaction: &mut Transaction, len: u64) -> Result<Range<u64>, Error>;

    /// Deallocates the given device range for the specified object.
    async fn deallocate(&self, transaction: &mut Transaction, device_range: Range<u64>);

    /// Push all in-memory structures to the device.  This is not necessary for sync since the
    /// journal will take care of it.
    async fn flush(&self, force: bool) -> Result<(), Error>;

    /// Reserve the given device range.  The main use case for this at this time is for the
    /// super-block which needs to be at a fixed location on the device.
    async fn reserve(&self, transaction: &mut Transaction, device_range: Range<u64>);

    /// Cast to super-trait.
    fn as_apply_mutations(self: Arc<Self>) -> Arc<dyn ApplyMutations>;
}

// Our allocator implementation tracks extents with a reference count.  At time of writing, these
// reference counts should never exceed 1, but that might change with snapshots and clones.

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AllocatorKey {
    device_range: Range<u64>,
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

#[derive(Debug, Default, Deserialize, Serialize)]
struct AllocatorInfo {
    layers: Vec<u64>,
}

const MAX_ALLOCATOR_INFO_SERIALIZED_SIZE: usize = 131072;

// For now this just implements a first-fit strategy.  This is a very naiive implementation.
pub struct SimpleAllocator {
    filesystem: Weak<dyn Filesystem>,
    block_size: u32,
    object_id: u64,
    empty: bool,
    tree: LSMTree<AllocatorKey, AllocatorValue>,
    reserved_allocations: Arc<SkipListLayer<AllocatorKey, AllocatorValue>>,
    inner: Mutex<Inner>,
}

struct Inner {
    info: AllocatorInfo,
    // The allocator can only be opened if there have been no allocations and it has not already
    // been opened or initialized.
    opened: bool,
}

impl SimpleAllocator {
    pub fn new(filesystem: Arc<dyn Filesystem>, object_id: u64, empty: bool) -> SimpleAllocator {
        SimpleAllocator {
            filesystem: Arc::downgrade(&filesystem),
            block_size: filesystem.device().block_size(),
            object_id,
            empty,
            tree: LSMTree::new(merge),
            reserved_allocations: SkipListLayer::new(1024), // TODO: magic numbers
            inner: Mutex::new(Inner { info: AllocatorInfo::default(), opened: false }),
        }
    }

    // Ensures the allocator is open.  If empty, create the object in the root object store,
    // otherwise load and initialise the LSM tree.
    async fn ensure_open(&self) -> Result<(), Error> {
        {
            if self.inner.lock().unwrap().opened {
                return Ok(());
            }
        }

        let root_store = self.filesystem.upgrade().unwrap().root_store();

        if self.empty {
            let mut transaction = Transaction::new();
            root_store
                .create_object_with_id(&mut transaction, self.object_id(), HandleOptions::default())
                .await?;
            self.filesystem.upgrade().unwrap().commit_transaction(transaction).await;
        } else {
            let handle = root_store.open_object(self.object_id, HandleOptions::default()).await?;

            let serialized_info = handle.contents(MAX_ALLOCATOR_INFO_SERIALIZED_SIZE).await?;
            let info: AllocatorInfo = deserialize_from(&serialized_info[..])?;
            let mut handles = Vec::new();
            for object_id in &info.layers {
                handles.push(root_store.open_object(*object_id, HandleOptions::default()).await?);
            }
            let mut inner = self.inner.lock().unwrap();

            inner.info = info;
            self.tree.set_layers(handles.into_boxed_slice());
            inner.opened = true;
        }

        self.inner.lock().unwrap().opened = true;
        Ok(())
    }
}

#[async_trait]
impl Allocator for SimpleAllocator {
    fn object_id(&self) -> u64 {
        self.object_id
    }

    // TODO(csuter): this should return a reservation object rather than just a range so that it can
    // get cleaned up when dropped.
    async fn allocate(&self, transaction: &mut Transaction, len: u64) -> Result<Range<u64>, Error> {
        ensure!(len % self.block_size as u64 == 0);

        self.ensure_open().await?;

        let tree = &self.tree;
        let result = {
            let mut layer_set = tree.layer_set();
            layer_set.add_layer(self.reserved_allocations.clone());
            let mut merger = layer_set.merger();
            let mut iter = merger.seek(Bound::Unbounded).await?;
            let mut last_offset = 0;
            loop {
                let next = iter.get();
                match next {
                    None => {
                        // TODO(csuter): Don't assume infinite device size.
                        break last_offset..last_offset + len;
                    }
                    Some(ItemRef { key: AllocatorKey { device_range, .. }, .. }) => {
                        if device_range.start > last_offset {
                            break last_offset..min(last_offset + len, device_range.start);
                        }
                        last_offset = device_range.end;
                    }
                }
                iter.advance().await?;
            }
        };
        log::debug!("allocate {:?}", result);
        self.reserve(transaction, result.clone()).await;
        Ok(result)
    }

    async fn reserve(&self, transaction: &mut Transaction, device_range: Range<u64>) {
        let item = AllocatorItem::new(AllocatorKey { device_range }, AllocatorValue { delta: 1 });
        self.reserved_allocations.insert(item.clone()).await;
        transaction.add(self.object_id(), Mutation::Allocate(item));
    }

    async fn deallocate(&self, transaction: &mut Transaction, device_range: Range<u64>) {
        log::debug!("deallocate {:?}", device_range);

        transaction.add(
            self.object_id(),
            Mutation::Deallocate(AllocatorItem {
                key: AllocatorKey { device_range },
                value: AllocatorValue { delta: -1 },
            }),
        );
    }

    async fn flush(&self, force: bool) -> Result<(), Error> {
        self.ensure_open().await?;

        let filesystem = self.filesystem.upgrade().unwrap();
        let object_sync = filesystem.begin_object_sync(self.object_id());
        if !force && !object_sync.needs_sync() {
            return Ok(());
        }

        // TODO(csuter): This all needs to be atomic somehow. We'll need to use different
        // transactions for each stage, but we need make sure objects are cleaned up if there's a
        // failure.
        let mut transaction = Transaction::new();

        let root_store = self.filesystem.upgrade().unwrap().root_store();
        let layer_object_handle =
            root_store.create_object(&mut transaction, HandleOptions::default()).await?;

        transaction.add(self.object_id(), Mutation::TreeSeal);
        filesystem.commit_transaction(transaction).await;

        let object_id = layer_object_handle.object_id();
        let layer_set = self.tree.immutable_layer_set();
        let mut merger = layer_set.merger();
        self.tree
            .compact_with_iterator(
                CoalescingIterator::new(Box::new(merger.seek(Bound::Unbounded).await?)).await?,
                layer_object_handle,
            )
            .await?;

        log::debug!("using {} for allocator layer file", object_id);
        let object_handle =
            root_store.open_object(self.object_id(), HandleOptions::default()).await?;
        // TODO(jfsulliv): Can we preallocate the buffer instead of doing a bounce? Do we know the
        // size up front?
        let mut serialized_info = Vec::new();
        {
            let mut inner = self.inner.lock().unwrap();
            inner.info.layers.push(object_id);
            serialize_into(&mut serialized_info, &inner.info)?;
        }
        let mut buf = object_handle.allocate_buffer(serialized_info.len());
        buf.as_mut_slice()[..serialized_info.len()].copy_from_slice(&serialized_info[..]);
        object_handle.write(0u64, buf.as_ref()).await?;

        let mut transaction = Transaction::new();
        transaction.add(self.object_id(), Mutation::TreeCompact);
        filesystem.commit_transaction(transaction).await;

        object_sync.commit();
        Ok(())
    }

    fn as_apply_mutations(self: Arc<Self>) -> Arc<dyn ApplyMutations> {
        self
    }
}

#[async_trait]
impl ApplyMutations for SimpleAllocator {
    async fn apply_mutation(&self, mutation: Mutation, replay: bool) {
        match mutation {
            Mutation::Allocate(item) => {
                self.reserved_allocations.erase(item.as_item_ref()).await;
                let lower_bound = lower_bound_for_replace_range(&item.key);
                self.tree.merge_into(item, &lower_bound).await;
            }
            Mutation::Deallocate(item) => {
                self.reserved_allocations.erase(item.as_item_ref()).await;
                let lower_bound = lower_bound_for_replace_range(&item.key);
                self.tree.merge_into(item, &lower_bound).await;
            }
            Mutation::TreeSeal => self.tree.seal(),
            Mutation::TreeCompact => {
                if replay {
                    self.tree.reset_immutable_layers();
                }
            }
            _ => panic!("unexpected mutation! {:?}", mutation),
        }
    }
}

fn lower_bound_for_replace_range(key: &AllocatorKey) -> AllocatorKey {
    AllocatorKey { device_range: 0..key.device_range.start }
}

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
            object_store::{
                allocator::{
                    merge::merge, Allocator, AllocatorKey, AllocatorValue, CoalescingIterator,
                    SimpleAllocator,
                },
                filesystem::Filesystem,
                testing::fake_filesystem::FakeFilesystem,
                transaction::Transaction,
                ObjectStore,
            },
            testing::fake_device::FakeDevice,
        },
        fuchsia_async as fasync,
        std::{
            cmp::{max, min},
            ops::{Bound, Range},
            sync::Arc,
        },
    };

    // TODO(jfsulliv): move to a range_utils module or something similar.
    fn range_len(r: &Range<u64>) -> u64 {
        r.end - r.start
    }

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

    fn overlap(a: &Range<u64>, b: &Range<u64>) -> u64 {
        if a.end > b.start && a.start < b.end {
            min(a.end, b.end) - max(a.start, b.start)
        } else {
            0
        }
    }

    async fn check_allocations(allocator: &SimpleAllocator, expected_allocations: &[Range<u64>]) {
        let layer_set = allocator.tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        let mut found = 0;
        while let Some(ItemRef { key: AllocatorKey { device_range }, .. }) = iter.get() {
            let mut l = range_len(device_range);
            found += l;
            // Make sure that the entire range we have found completely overlaps with all the
            // allocations we expect to find.
            for range in expected_allocations {
                l -= overlap(range, device_range);
                if l == 0 {
                    break;
                }
            }
            assert_eq!(l, 0);
            iter.advance().await.expect("advance failed");
        }
        // Make sure the total we found adds up to what we expect.
        assert_eq!(found, expected_allocations.iter().map(|r| range_len(r)).sum());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocations() {
        let device = Arc::new(FakeDevice::new(1024, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let _store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        let mut transaction = Transaction::new();
        let mut device_ranges = Vec::new();
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        assert!(range_len(device_ranges.last().unwrap()) == 512);
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        assert!(range_len(device_ranges.last().unwrap()) == 512);
        assert_eq!(overlap(&device_ranges[0], &device_ranges[1]), 0);
        fs.commit_transaction(transaction).await;
        let mut transaction = Transaction::new();
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        assert!(range_len(&device_ranges[2]) == 512);
        assert_eq!(overlap(&device_ranges[0], &device_ranges[2]), 0);
        assert_eq!(overlap(&device_ranges[1], &device_ranges[2]), 0);
        fs.commit_transaction(transaction).await;

        check_allocations(&allocator, &device_ranges).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_deallocations() {
        let device = Arc::new(FakeDevice::new(1024, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let _store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        let mut transaction = Transaction::new();
        let device_range1 =
            allocator.allocate(&mut transaction, 512).await.expect("allocate failed");
        assert!(range_len(&device_range1) == 512);
        fs.commit_transaction(transaction).await;

        let mut transaction = Transaction::new();
        allocator.deallocate(&mut transaction, device_range1).await;
        fs.commit_transaction(transaction).await;

        check_allocations(&allocator, &[]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reserve() {
        let device = Arc::new(FakeDevice::new(1024, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let _store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        let mut transaction = Transaction::new();
        let mut device_ranges = Vec::new();
        device_ranges.push(0..512);
        allocator.reserve(&mut transaction, device_ranges.last().unwrap().clone()).await;
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        assert!(range_len(device_ranges.last().unwrap()) == 512);
        assert_eq!(overlap(&device_ranges[0], &device_ranges[1]), 0);
        fs.commit_transaction(transaction).await;

        check_allocations(&allocator, &device_ranges).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush() {
        let device = Arc::new(FakeDevice::new(1024, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let _store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        let mut transaction = Transaction::new();
        let mut device_ranges = Vec::new();
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        fs.commit_transaction(transaction).await;

        allocator.flush(false).await.expect("flush failed");

        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, false));
        fs.object_manager().set_allocator(allocator.clone());
        // When we flushed the allocator, it would have been written to the device somewhere but
        // without a journal, we will be missing those records, so this next allocation will likely
        // be on top of those objects.  That won't matter for the purposes of this test, since we
        // are not writing anything to these ranges.
        let mut transaction = Transaction::new();
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        for r in &device_ranges[..3] {
            assert_eq!(overlap(r, device_ranges.last().unwrap()), 0);
        }
        fs.commit_transaction(transaction).await;
        check_allocations(&allocator, &device_ranges).await;
    }
}

// TODO(csuter): deallocations can't be used until mutations have been written to the device and the
// device has been flushed.

// TODO(csuter): the locking needs to be investigated and fixed here.  There are likely problems
// with ensure_open, and allocate.
