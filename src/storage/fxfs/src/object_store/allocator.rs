// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod merge;

use {
    crate::{
        errors::FxfsError,
        lsm_tree::{
            skip_list_layer::SkipListLayer,
            types::{
                BoxedLayerIterator, Item, ItemRef, LayerIterator, MutableLayer, NextKey,
                OrdLowerBound, OrdUpperBound,
            },
            LSMTree,
        },
        object_handle::{ObjectHandle, ObjectHandleExt},
        object_store::{
            filesystem::{Filesystem, Mutations, ObjectFlush},
            transaction::{AllocatedBytesMutation, AllocatorMutation, Mutation, Transaction},
            HandleOptions, ObjectStore,
        },
    },
    anyhow::{anyhow, bail, ensure, Error},
    async_trait::async_trait,
    bincode::{deserialize_from, serialize_into},
    interval_tree::utils::RangeOps,
    merge::merge,
    serde::{Deserialize, Serialize},
    std::{
        any::Any,
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
    /// TODO(csuter): We need to think about how to deal with fragmentation e.g. returning an array
    /// of allocations, or returning a partial allocation request for situations where that makes
    /// sense.
    async fn allocate(
        &self,
        transaction: &mut Transaction<'_>,
        len: u64,
    ) -> Result<Range<u64>, Error>;

    /// Deallocates the given device range for the specified object.
    async fn deallocate(&self, transaction: &mut Transaction<'_>, device_range: Range<u64>);

    /// Reserve the given device range.  The main use case for this at this time is for the
    /// super-block which needs to be at a fixed location on the device.
    async fn reserve(&self, transaction: &mut Transaction<'_>, device_range: Range<u64>);

    /// Cast to super-trait.
    fn as_mutations(self: Arc<Self>) -> Arc<dyn Mutations>;

    fn as_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync>;
}

// Our allocator implementation tracks extents with a reference count.  At time of writing, these
// reference counts should never exceed 1, but that might change with snapshots and clones.

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AllocatorKey {
    pub device_range: Range<u64>,
}

impl AllocatorKey {
    /// Returns a new key that is a lower bound suitable for use with merge_into.
    pub fn lower_bound_for_merge_into(self: &AllocatorKey) -> AllocatorKey {
        AllocatorKey { device_range: 0..self.device_range.start }
    }
}

impl NextKey for AllocatorKey {}

impl OrdUpperBound for AllocatorKey {
    fn cmp_upper_bound(&self, other: &AllocatorKey) -> std::cmp::Ordering {
        self.device_range.end.cmp(&other.device_range.end)
    }
}

impl OrdLowerBound for AllocatorKey {
    fn cmp_lower_bound(&self, other: &AllocatorKey) -> std::cmp::Ordering {
        self.device_range.start.cmp(&other.device_range.start)
    }
}

impl Ord for AllocatorKey {
    fn cmp(&self, other: &AllocatorKey) -> std::cmp::Ordering {
        self.device_range
            .start
            .cmp(&other.device_range.start)
            .then(self.device_range.end.cmp(&other.device_range.end))
    }
}

impl PartialOrd for AllocatorKey {
    fn partial_cmp(&self, other: &AllocatorKey) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AllocatorValue {
    // This is the delta on a reference count for the extent.
    pub delta: i64,
}

pub type AllocatorItem = Item<AllocatorKey, AllocatorValue>;

#[derive(Debug, Default, Deserialize, Serialize)]
struct AllocatorInfo {
    layers: Vec<u64>,
    allocated_bytes: i64,
}

const MAX_ALLOCATOR_INFO_SERIALIZED_SIZE: usize = 131072;

// For now this just implements a first-fit strategy.  This is a very naiive implementation.
pub struct SimpleAllocator {
    filesystem: Weak<dyn Filesystem>,
    block_size: u32,
    device_size: usize,
    object_id: u64,
    empty: bool,
    tree: LSMTree<AllocatorKey, AllocatorValue>,
    reserved_allocations: Arc<SkipListLayer<AllocatorKey, AllocatorValue>>,
    inner: Mutex<Inner>,
    allocation_lock: futures::lock::Mutex<()>,
}

struct Inner {
    info: AllocatorInfo,
    // The allocator can only be opened if there have been no allocations and it has not already
    // been opened or initialized.
    opened: bool,
    // When a transaction is dropped, we need to release the reservation, but that requires the use
    // of async methods which we can't use when called from drop.  To workaround that, we keep an
    // array of dropped_allocations and update reserved_allocations the next time we try to
    // allocate.
    dropped_allocations: Vec<AllocatorItem>,
    // This value is the up-to-date count of the number of allocated bytes whereas the value in
    // `info` is the value as it was when we last flushed.
    allocated_bytes: i64,
}

impl SimpleAllocator {
    pub fn new(filesystem: Arc<dyn Filesystem>, object_id: u64, empty: bool) -> SimpleAllocator {
        SimpleAllocator {
            filesystem: Arc::downgrade(&filesystem),
            block_size: filesystem.device().block_size(),
            device_size: filesystem.device().size(),
            object_id,
            empty,
            tree: LSMTree::new(merge),
            reserved_allocations: SkipListLayer::new(1024), // TODO(csuter): magic numbers
            inner: Mutex::new(Inner {
                info: AllocatorInfo::default(),
                opened: false,
                dropped_allocations: Vec::new(),
                allocated_bytes: 0,
            }),
            allocation_lock: futures::lock::Mutex::new(()),
        }
    }

    pub fn tree(&self) -> &LSMTree<AllocatorKey, AllocatorValue> {
        assert!(self.inner.lock().unwrap().opened);
        &self.tree
    }

    // Ensures the allocator is open.  If empty, create the object in the root object store,
    // otherwise load and initialise the LSM tree.
    pub async fn ensure_open(&self) -> Result<(), Error> {
        {
            if self.inner.lock().unwrap().opened {
                return Ok(());
            }
        }

        let _guard = self.allocation_lock.lock().await;
        {
            if self.inner.lock().unwrap().opened {
                // We lost a race.
                return Ok(());
            }
        }

        let filesystem = self.filesystem.upgrade().unwrap();
        let root_store = filesystem.root_store();

        if self.empty {
            let mut transaction = filesystem.clone().new_transaction(&[]).await?;
            ObjectStore::create_object_with_id(
                &root_store,
                &mut transaction,
                self.object_id(),
                HandleOptions::default(),
            )
            .await?;
            transaction.commit().await;
        } else {
            let handle =
                ObjectStore::open_object(&root_store, self.object_id, HandleOptions::default())
                    .await?;

            if handle.get_size() > 0 {
                let serialized_info = handle.contents(MAX_ALLOCATOR_INFO_SERIALIZED_SIZE).await?;
                let info: AllocatorInfo = deserialize_from(&serialized_info[..])?;
                let mut handles = Vec::new();
                for object_id in &info.layers {
                    handles.push(
                        ObjectStore::open_object(&root_store, *object_id, HandleOptions::default())
                            .await?,
                    );
                }
                {
                    let mut inner = self.inner.lock().unwrap();
                    // After replaying, allocated_bytes should include all the deltas since the time
                    // the allocator was last flushed, so here we just need to add whatever is
                    // recorded in info.
                    inner.allocated_bytes += info.allocated_bytes;
                    inner.info = info;
                }
                self.tree.append_layers(handles.into_boxed_slice()).await?;
            }
        }

        self.inner.lock().unwrap().opened = true;
        Ok(())
    }

    /// Returns all objects that exist in the parent store that pertain to this allocator.
    pub fn parent_objects(&self) -> Vec<u64> {
        // The allocator tree needs to store a file for each of the layers in the tree, so we return
        // those, since nothing else references them.
        self.inner.lock().unwrap().info.layers.clone()
    }

    /// Returns the number of allocated bytes.
    #[cfg(test)]
    pub fn get_allocated_bytes(&self) -> i64 {
        self.inner.lock().unwrap().allocated_bytes
    }

    // Updates the allocated_bytes delta within a transaction so that it includes `delta`.
    fn update_allocated_bytes_delta(&self, transaction: &mut Transaction<'_>, mut delta: i64) {
        if let Some(existing_delta) = transaction.get_allocated_bytes_delta(self.object_id()) {
            delta += existing_delta;
        }
        if delta == 0 {
            transaction
                .remove(self.object_id(), Mutation::AllocatedBytes(AllocatedBytesMutation(0)));
        } else {
            transaction
                .add(self.object_id(), Mutation::AllocatedBytes(AllocatedBytesMutation(delta)));
        }
    }
}

#[async_trait]
impl Allocator for SimpleAllocator {
    fn object_id(&self) -> u64 {
        self.object_id
    }

    async fn allocate(
        &self,
        transaction: &mut Transaction<'_>,
        len: u64,
    ) -> Result<Range<u64>, Error> {
        ensure!(len % self.block_size as u64 == 0);

        self.ensure_open().await?;

        let _guard = self.allocation_lock.lock().await;

        // Update reserved_allocations using dropped_allocations.
        let dropped_allocations =
            std::mem::take(&mut self.inner.lock().unwrap().dropped_allocations);
        for item in dropped_allocations {
            self.reserved_allocations.erase(item.as_item_ref()).await;
        }

        let result = {
            let tree = &self.tree;
            let mut layer_set = tree.empty_layer_set();
            layer_set.add_layer(self.reserved_allocations.clone());
            tree.add_all_layers_to_layer_set(&mut layer_set);
            let mut merger = layer_set.merger();
            let mut iter = merger.seek(Bound::Unbounded).await?;
            let mut last_offset = 0;
            loop {
                if last_offset + len >= self.device_size as u64 {
                    bail!(FxfsError::NoSpace);
                }
                match iter.get() {
                    None => {
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

    async fn reserve(&self, transaction: &mut Transaction<'_>, device_range: Range<u64>) {
        self.update_allocated_bytes_delta(transaction, device_range.length() as i64);
        let item = AllocatorItem::new(AllocatorKey { device_range }, AllocatorValue { delta: 1 });
        self.reserved_allocations.insert(item.clone()).await;
        transaction.add(self.object_id(), Mutation::allocation(item));
    }

    async fn deallocate(&self, transaction: &mut Transaction<'_>, dealloc_range: Range<u64>) {
        log::debug!("deallocate {:?}", dealloc_range);
        transaction.add(
            self.object_id(),
            Mutation::allocation(Item::new(
                AllocatorKey { device_range: dealloc_range.clone() },
                AllocatorValue { delta: -1 },
            )),
        );

        // Update number of allocated bytes available in the filesystem after cleaning up dropped
        // allocations.
        let mut layer_set = self.tree.empty_layer_set();
        self.tree.add_all_layers_to_layer_set(&mut layer_set);
        let mut merger = layer_set.merger();
        // The precise search key that we choose here is important.  We need to perform a full merge
        // across all layers because we want the precise value of delta, so we must ensure that we
        // query all layers, which is done by setting the lower bound to zero (the merger consults
        // iterators until it encounters a key whose lower-bound is not greater than the search
        // key).  The upper bound is used to search each individual layer, and we want to start with
        // an extent that covers the first byte of the range we're deallocating.
        let mut iter = merger
            .seek(Bound::Included(&AllocatorKey { device_range: 0..dealloc_range.start + 1 }))
            .await
            .unwrap();
        let mut allocated_bytes_delta = 0;
        while let Some(ItemRef {
            key: AllocatorKey { device_range, .. },
            value: AllocatorValue { delta, .. },
        }) = iter.get()
        {
            if device_range.start >= dealloc_range.end {
                break;
            }
            if *delta == 1 {
                if let Some(overlap) = dealloc_range.intersect(&device_range) {
                    allocated_bytes_delta -= overlap.length() as i64;
                }
            }
            iter.advance().await.unwrap();
        }
        if allocated_bytes_delta != 0 {
            self.update_allocated_bytes_delta(transaction, allocated_bytes_delta);
        }
    }

    fn as_mutations(self: Arc<Self>) -> Arc<dyn Mutations> {
        self
    }

    fn as_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync> {
        self
    }
}

#[async_trait]
impl Mutations for SimpleAllocator {
    async fn apply_mutation(&self, mutation: Mutation, replay: bool) {
        match mutation {
            Mutation::Allocator(AllocatorMutation(item)) => {
                let lower_bound = item.key.lower_bound_for_merge_into();
                self.tree.merge_into(item.clone(), &lower_bound).await;
                // Whilst it might be tempting to just remove the item from reserved_allocations
                // directly here, it's not safe to do so since there is no synchronisation with
                // allocate that ensures it will definitely see the insertion if we've removed the
                // item from reserved_allocations.  If we use dropped_allocations, then it is
                // guaranteed because the merge_into above is observable when a new merger is
                // created.
                if item.value.delta > 0 {
                    self.inner.lock().unwrap().dropped_allocations.push(item);
                }
            }
            // TODO(csuter): Since Seal and Compact are no longer being used for just trees, we
            // should consider changing the names to something else, maybe FlushBegin and
            // FlushCommit to match ObjectFlush, and maybe ObjectFlush::commit should be responsible
            // for adding it to a transaction.
            Mutation::TreeSeal => {
                self.tree.seal().await;
                // Transfer our running count for allocated_bytes so that it gets written to the new
                // info file when flush completes.
                let mut inner = self.inner.lock().unwrap();
                inner.info.allocated_bytes = inner.allocated_bytes;
            }
            Mutation::TreeCompact => {
                if replay {
                    self.tree.reset_immutable_layers();
                    // AllocatorInfo is written in the same transaction and will contain the count
                    // at the point TreeSeal was applied, so we need to adjust allocated_bytes so
                    // that it just covers the delta from that point.  Later, when we properly open
                    // the allocator, we'll add this back.
                    let mut inner = self.inner.lock().unwrap();
                    inner.allocated_bytes -= inner.info.allocated_bytes;
                }
            }
            Mutation::AllocatedBytes(AllocatedBytesMutation(delta)) => {
                // TODO(csuter): If the volume is inconsistent, this has the potential to wrap; we
                // should add checks somewhere to prevent that from happening here.
                self.inner.lock().unwrap().allocated_bytes += delta;
            }
            _ => panic!("unexpected mutation! {:?}", mutation), // TODO(csuter): This can't panic
        }
    }

    fn drop_mutation(&self, mutation: Mutation) {
        match mutation {
            Mutation::Allocator(AllocatorMutation(item)) if item.value.delta > 0 => {
                self.inner.lock().unwrap().dropped_allocations.push(item);
            }
            _ => {}
        }
    }

    async fn flush(&self) -> Result<(), Error> {
        self.ensure_open().await?;

        let filesystem = self.filesystem.upgrade().unwrap();
        let object_manager = filesystem.object_manager();
        if !object_manager.needs_flush(self.object_id()) {
            return Ok(());
        }
        let graveyard = object_manager.graveyard().ok_or(anyhow!("Missing graveyard!"))?;
        let object_sync = ObjectFlush::new(object_manager, self.object_id());
        // TODO(csuter): This all needs to be atomic somehow. We'll need to use different
        // transactions for each stage, but we need make sure objects are cleaned up if there's a
        // failure.
        let mut transaction = filesystem.clone().new_transaction(&[]).await?;

        let root_store = self.filesystem.upgrade().unwrap().root_store();
        let layer_object_handle =
            ObjectStore::create_object(&root_store, &mut transaction, HandleOptions::default())
                .await?;
        let object_id = layer_object_handle.object_id();
        graveyard.add(&mut transaction, root_store.store_object_id(), object_id);
        // It's important that this transaction does not include any allocations because we use
        // TreeSeal as a snapshot point for mutations to the tree: other allocator mutations within
        // this transaction might get applied before seal (which would be OK), but they could
        // equally get applied afterwards (since Transaction makes no guarantees about the order in
        // which mutations are applied whilst committing), in which case they'd get lost on replay
        // because the journal will only send mutations that follow this transaction.
        transaction.add_with_object(self.object_id(), Mutation::TreeSeal, &object_sync);
        transaction.commit().await;

        let layer_set = self.tree.immutable_layer_set();
        let mut merger = layer_set.merger();
        self.tree
            .compact_with_iterator(
                CoalescingIterator::new(Box::new(merger.seek(Bound::Unbounded).await?)).await?,
                &layer_object_handle,
            )
            .await?;

        log::debug!("using {} for allocator layer file", object_id);
        let object_handle =
            ObjectStore::open_object(&root_store, self.object_id(), HandleOptions::default())
                .await?;

        // TODO(jfsulliv): Can we preallocate the buffer instead of doing a bounce? Do we know the
        // size up front?
        let mut transaction = filesystem.clone().new_transaction(&[]).await?;
        let mut serialized_info = Vec::new();
        {
            let mut inner = self.inner.lock().unwrap();

            // Move all the existing layers to the graveyard.
            for object_id in &inner.info.layers {
                graveyard.add(&mut transaction, root_store.store_object_id(), *object_id);
            }

            inner.info.layers = vec![object_id];
            serialize_into(&mut serialized_info, &inner.info)?;
        }
        let mut buf = object_handle.allocate_buffer(serialized_info.len());
        buf.as_mut_slice()[..serialized_info.len()].copy_from_slice(&serialized_info[..]);
        object_handle.txn_write(&mut transaction, 0u64, buf.as_ref()).await?;

        // It's important that TreeCompact is in the same transaction that we write AllocatorInfo,
        // because we use TreeCompact to make the required adjustments to allocated_bytes.
        transaction.add(self.object_id(), Mutation::TreeCompact);
        graveyard.remove(&mut transaction, root_store.store_object_id(), object_id);
        transaction.commit().await;

        // TODO(csuter): what if this fails.
        self.tree.set_layers(Box::new([layer_object_handle])).await?;

        object_sync.commit();
        Ok(())
    }
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

pub struct CoalescingIterator<'a> {
    iter: BoxedLayerIterator<'a, AllocatorKey, AllocatorValue>,
    item: Option<AllocatorItem>,
}

impl<'a> CoalescingIterator<'a> {
    pub async fn new(
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
                filesystem::{Filesystem, Mutations},
                graveyard::Graveyard,
                testing::fake_filesystem::FakeFilesystem,
                transaction::TransactionHandler,
                ObjectStore,
            },
        },
        fuchsia_async as fasync,
        std::{
            cmp::{max, min},
            ops::{Bound, Range},
            sync::Arc,
        },
        storage_device::{fake_device::FakeDevice, DeviceHolder},
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
        lsm_tree.seal().await;
        lsm_tree
            .insert(Item::new(
                AllocatorKey { device_range: 100..200 },
                AllocatorValue { delta: -1 },
            ))
            .await;
        lsm_tree.seal().await;
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
        let device = DeviceHolder::new(FakeDevice::new(1024, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let _store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        let mut transaction = fs.clone().new_transaction(&[]).await.expect("new failed");
        let mut device_ranges = Vec::new();
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        assert!(range_len(device_ranges.last().unwrap()) == 512);
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        assert!(range_len(device_ranges.last().unwrap()) == 512);
        assert_eq!(overlap(&device_ranges[0], &device_ranges[1]), 0);
        transaction.commit().await;
        let mut transaction = fs.clone().new_transaction(&[]).await.expect("new failed");
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        assert!(range_len(&device_ranges[2]) == 512);
        assert_eq!(overlap(&device_ranges[0], &device_ranges[2]), 0);
        assert_eq!(overlap(&device_ranges[1], &device_ranges[2]), 0);
        transaction.commit().await;

        check_allocations(&allocator, &device_ranges).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_deallocations() {
        let device = DeviceHolder::new(FakeDevice::new(1024, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let _store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        let mut transaction = fs.clone().new_transaction(&[]).await.expect("new failed");
        let device_range1 =
            allocator.allocate(&mut transaction, 512).await.expect("allocate failed");
        assert!(range_len(&device_range1) == 512);
        transaction.commit().await;

        let mut transaction = fs.clone().new_transaction(&[]).await.expect("new failed");
        allocator.deallocate(&mut transaction, device_range1).await;
        transaction.commit().await;

        check_allocations(&allocator, &[]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reserve() {
        let device = DeviceHolder::new(FakeDevice::new(1024, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let _store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        let mut transaction = fs.clone().new_transaction(&[]).await.expect("new failed");
        let mut device_ranges = Vec::new();
        device_ranges.push(0..512);
        allocator.reserve(&mut transaction, device_ranges.last().unwrap().clone()).await;
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        assert!(range_len(device_ranges.last().unwrap()) == 512);
        assert_eq!(overlap(&device_ranges[0], &device_ranges[1]), 0);
        transaction.commit().await;

        check_allocations(&allocator, &device_ranges).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush() {
        let device = DeviceHolder::new(FakeDevice::new(1024, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        allocator.ensure_open().await.expect("ensure_open failed");
        let mut transaction = fs.clone().new_transaction(&[]).await.expect("new failed");
        let graveyard =
            Arc::new(Graveyard::create(&mut transaction, &store).await.expect("create failed"));
        fs.object_manager().register_graveyard(graveyard);
        let mut device_ranges = Vec::new();
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        transaction.commit().await;

        allocator.flush().await.expect("flush failed");

        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, false));
        fs.object_manager().set_allocator(allocator.clone());
        // When we flushed the allocator, it would have been written to the device somewhere but
        // without a journal, we will be missing those records, so this next allocation will likely
        // be on top of those objects.  That won't matter for the purposes of this test, since we
        // are not writing anything to these ranges.
        let mut transaction = fs.clone().new_transaction(&[]).await.expect("new failed");
        device_ranges
            .push(allocator.allocate(&mut transaction, 512).await.expect("allocate failed"));
        for r in &device_ranges[..3] {
            assert_eq!(overlap(r, device_ranges.last().unwrap()), 0);
        }
        transaction.commit().await;
        check_allocations(&allocator, &device_ranges).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_dropped_transaction() {
        let device = DeviceHolder::new(FakeDevice::new(1024, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let _store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        let allocated_range = {
            let mut transaction =
                fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
            allocator.allocate(&mut transaction, 512).await.expect("allocate failed")
        };
        // After dropping the transaction and attempting to allocate again, we should end up with
        // the same range because the reservation should have been released.
        let mut transaction =
            fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
        assert_eq!(
            allocator.allocate(&mut transaction, 512).await.expect("allocate failed"),
            allocated_range
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocated_bytes() {
        const BLOCK_COUNT: u32 = 1024;
        const BLOCK_SIZE: u32 = 512;
        let device = DeviceHolder::new(FakeDevice::new(BLOCK_COUNT.into(), BLOCK_SIZE));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let _store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store_object_id(2);
        assert_eq!(allocator.get_allocated_bytes(), 0);

        // Verify allocated_bytes reflects allocation changes.
        const ALLOCATED_BYTES: u64 = 512;
        let allocated_range = {
            let mut transaction =
                fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
            let range = allocator
                .allocate(&mut transaction, ALLOCATED_BYTES)
                .await
                .expect("allocate failed");
            transaction.commit().await;
            assert_eq!(allocator.get_allocated_bytes(), ALLOCATED_BYTES as i64);
            range
        };

        {
            let mut transaction =
                fs.clone().new_transaction(&[]).await.expect("new_transaction failed");
            allocator.allocate(&mut transaction, 512).await.expect("allocate failed");

            // Prior to commiiting, the count of allocated bytes shouldn't change.
            assert_eq!(allocator.get_allocated_bytes(), ALLOCATED_BYTES as i64);
        }

        // After dropping the prior transaction, the allocated bytes still shouldn't have changed.
        assert_eq!(allocator.get_allocated_bytes(), ALLOCATED_BYTES as i64);

        // Verify allocated_bytes reflects deallocations.
        let deallocate_range = allocated_range.start + 20..allocated_range.end - 20;
        let mut transaction = fs.clone().new_transaction(&[]).await.expect("new failed");
        allocator.deallocate(&mut transaction, deallocate_range).await;

        // Before committing, there should be no change.
        assert_eq!(allocator.get_allocated_bytes(), ALLOCATED_BYTES as i64);

        transaction.commit().await;

        // After committing, all but 40 bytes should remain allocated.
        assert_eq!(allocator.get_allocated_bytes(), 40);
    }
}

// TODO(csuter): deallocations can't be used until mutations have been written to the device and the
// device has been flushed.
