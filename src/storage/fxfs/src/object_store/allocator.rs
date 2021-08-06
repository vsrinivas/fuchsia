// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod merge;

use {
    crate::{
        debug_assert_not_too_long,
        errors::FxfsError,
        lsm_tree::{
            layers_from_handles,
            skip_list_layer::SkipListLayer,
            types::{
                BoxedLayerIterator, Item, ItemRef, Layer, LayerIterator, MutableLayer, NextKey,
                OrdLowerBound, OrdUpperBound,
            },
            LSMTree,
        },
        object_handle::{ObjectHandle, ObjectHandleExt},
        object_store::{
            filesystem::{Filesystem, Mutations, SyncOptions},
            journal::checksum_list::ChecksumList,
            round_down,
            store_object_handle::DirectWriter,
            transaction::{AllocatorMutation, AssocObj, Mutation, Options, Transaction},
            HandleOptions, ObjectStore,
        },
        trace_duration,
    },
    anyhow::{anyhow, bail, ensure, Error},
    async_trait::async_trait,
    bincode::{deserialize_from, serialize_into},
    either::Either::{Left, Right},
    interval_tree::utils::RangeOps,
    merge::merge,
    serde::{Deserialize, Serialize},
    std::{
        any::Any,
        cmp::min,
        collections::VecDeque,
        convert::TryInto,
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
    async fn deallocate(
        &self,
        transaction: &mut Transaction<'_>,
        device_range: Range<u64>,
    ) -> Result<u64, Error>;

    /// Marks the given device range as allocated.  The main use case for this at this time is for
    /// the super-block which needs to be at a fixed location on the device.
    async fn mark_allocated(
        &self,
        transaction: &mut Transaction<'_>,
        device_range: Range<u64>,
    ) -> Result<(), Error>;

    /// Adds a reference to the given device range which must already be allocated.
    fn add_ref(&self, transaction: &mut Transaction<'_>, device_range: Range<u64>);

    /// Cast to super-trait.
    fn as_mutations(self: Arc<Self>) -> Arc<dyn Mutations>;

    fn as_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync>;

    /// Called when the device has been flush and indicates what the journal log offset was when
    /// that happened.
    async fn did_flush_device(&self, flush_log_offset: u64);

    /// Returns a reservation that can be used later, or None if there is insufficient space.
    fn reserve(self: Arc<Self>, amount: u64) -> Option<Reservation>;

    /// Like reserve, but returns as much as available if not all of amount is available, which
    /// could be zero bytes.
    fn reserve_at_most(self: Arc<Self>, amount: u64) -> Reservation;

    /// Releases the reservation.
    fn release_reservation(&self, amount: u64);

    /// Returns the number of allocated bytes.
    fn get_allocated_bytes(&self) -> u64;

    /// Returns the number of allocated and reserved bytes.
    fn get_used_bytes(&self) -> u64;

    /// Used during replay to validate a mutation.  This should return false if the mutation is not
    /// valid and should not be applied.  This could be for benign reasons: e.g. the device flushed
    /// data out-of-order, or because of a malicious actor.
    async fn validate_mutation(
        &self,
        journal_offset: u64,
        mutation: &Mutation,
        checksum_list: &mut ChecksumList,
    ) -> Result<bool, Error>;
}

/// A reservation guarantees that when it comes time to actually allocate, it will not fail due to
/// lack of space.  A hold can be placed on some of the reservation, which can later be committed.
pub struct Reservation {
    allocator: Arc<dyn Allocator>,
    inner: Mutex<ReservationInner>,
}

#[derive(Debug, Default)]
struct ReservationInner {
    // Amount currently held by this reservation.
    amount: u64,

    // The amount within this reservation that is held for some purpose.
    held: u64,
}

impl std::fmt::Debug for Reservation {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.inner.lock().unwrap().fmt(f)
    }
}

impl Reservation {
    pub fn new(allocator: Arc<dyn Allocator>, amount: u64) -> Self {
        Self { allocator, inner: Mutex::new(ReservationInner { amount, held: 0 }) }
    }

    /// Returns the total amount of the reservation, not accounting for anything that might be held.
    pub fn amount(&self) -> u64 {
        self.inner.lock().unwrap().amount
    }

    /// Returns the amount available after accounting for space that is held.
    pub fn avail(&self) -> u64 {
        let inner = self.inner.lock().unwrap();
        inner.amount - inner.held
    }

    /// Adds more to the reservation.
    pub fn add(&self, amount: u64) {
        self.inner.lock().unwrap().amount += amount;
    }

    /// Places a hold an `amount` from the reservation.
    pub fn hold(&self, amount: u64) -> Result<Hold<'_>, Error> {
        let mut inner = self.inner.lock().unwrap();
        if amount > inner.amount - inner.held {
            bail!(FxfsError::NoSpace);
        }
        inner.held += amount;
        Ok(Hold { reservation: self, amount })
    }

    /// Releases some previously held amount.
    pub fn release(&self, amount: u64) {
        self.inner.lock().unwrap().held -= amount;
    }

    /// Commits a previously held amount.
    pub fn commit(&self, amount: u64) {
        let mut inner = self.inner.lock().unwrap();
        inner.amount -= amount;
        inner.held -= amount;
    }

    /// Returns the entire amount of the reservation.  The caller is responsible for maintaining
    /// consistency, i.e. updating counters, etc.
    pub fn take(&self) -> u64 {
        std::mem::take(&mut *self.inner.lock().unwrap()).amount
    }

    /// Returns some of the reservation back to the allocator.  Asserts that the amount with a hold
    /// is still valid afterwards.
    pub fn give_back(&self, amount: u64) {
        self.allocator.release_reservation(amount);
        let mut inner = self.inner.lock().unwrap();
        inner.amount -= amount;
        assert!(inner.held <= inner.amount);
    }
}

impl Drop for Reservation {
    fn drop(&mut self) {
        self.allocator.release_reservation(self.inner.get_mut().unwrap().amount);
    }
}

#[must_use]
pub struct Hold<'a> {
    reservation: &'a Reservation,
    amount: u64,
}

impl Hold<'_> {
    pub fn take(&mut self) -> u64 {
        let amount = self.amount;
        self.amount = 0;
        amount
    }

    pub fn take_some(&mut self, amount: u64) {
        self.amount -= amount;
    }

    pub fn commit(&mut self, amount: u64) {
        self.amount -= amount;
        self.reservation.commit(amount);
    }
}

impl Drop for Hold<'_> {
    fn drop(&mut self) {
        self.reservation.release(self.amount);
    }
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
    allocated_bytes: u64,
}

const MAX_ALLOCATOR_INFO_SERIALIZED_SIZE: usize = 131072;

// For now this just implements a simple strategy of returning the first gap it can find (no matter
// the size).  This is a very naiive implementation.
pub struct SimpleAllocator {
    filesystem: Weak<dyn Filesystem>,
    block_size: u32,
    device_size: u64,
    object_id: u64,
    empty: bool,
    tree: LSMTree<AllocatorKey, AllocatorValue>,
    reserved_allocations: Arc<SkipListLayer<AllocatorKey, AllocatorValue>>,
    inner: Mutex<Inner>,
    allocation_mutex: futures::lock::Mutex<()>,
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
    // `info` is the value as it was when we last flushed.  This is i64 because it can be negative
    // during replay.
    allocated_bytes: i64,
    // This value is the number of bytes allocated to uncommitted allocations.
    uncommitted_allocated_bytes: u64,
    // This value is the number of bytes allocated to reservations.
    reserved_bytes: u64,
    // Committed deallocations that we cannot use until they are flushed to the device.  Each entry
    // in this list is the log file offset at which it was committed and an array of deallocations
    // that occurred at that time.
    committed_deallocated: VecDeque<(u64, Range<u64>)>,
    // The total number of committed deallocated bytes.
    committed_deallocated_bytes: u64,
}

impl Inner {
    // Returns the amount that is not available to be allocated, which includes actually allocated
    // bytes, bytes that have been allocated for a transaction but the transaction hasn't committed
    // yet, and bytes that have been deallocated, but the device hasn't been flushed yet so we can't
    // reuse those bytes yet.
    fn unavailable_bytes(&self) -> u64 {
        self.allocated_bytes as u64
            + self.uncommitted_allocated_bytes
            + self.committed_deallocated_bytes
    }
}

impl SimpleAllocator {
    pub fn new(filesystem: Arc<dyn Filesystem>, object_id: u64, empty: bool) -> SimpleAllocator {
        SimpleAllocator {
            filesystem: Arc::downgrade(&filesystem),
            // TODO(csuter): Rather than computing the block size here, we should perhaps have the
            // filesystem determine what it should be.  Also, it eventually needs to come from the
            // super-block.
            block_size: filesystem.block_size(),
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
                uncommitted_allocated_bytes: 0,
                reserved_bytes: 0,
                committed_deallocated: VecDeque::new(),
                committed_deallocated_bytes: 0,
            }),
            allocation_mutex: futures::lock::Mutex::new(()),
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

        let _guard = debug_assert_not_too_long!(self.allocation_mutex.lock());
        {
            if self.inner.lock().unwrap().opened {
                // We lost a race.
                return Ok(());
            }
        }

        let filesystem = self.filesystem.upgrade().unwrap();
        let root_store = filesystem.root_store();

        if self.empty {
            let mut transaction = filesystem
                .clone()
                .new_transaction(&[], Options { skip_journal_checks: true, ..Default::default() })
                .await?;
            ObjectStore::create_object_with_id(
                &root_store,
                &mut transaction,
                self.object_id(),
                HandleOptions::default(),
                Some(0),
            )
            .await?;
            transaction.commit().await?;
        } else {
            let handle =
                ObjectStore::open_object(&root_store, self.object_id, HandleOptions::default())
                    .await?;

            if handle.get_size() > 0 {
                let serialized_info = handle.contents(MAX_ALLOCATOR_INFO_SERIALIZED_SIZE).await?;
                let info: AllocatorInfo = deserialize_from(&serialized_info[..])?;
                let mut handles = Vec::new();
                let mut total_size = 0;
                for object_id in &info.layers {
                    let handle =
                        ObjectStore::open_object(&root_store, *object_id, HandleOptions::default())
                            .await?;
                    total_size += handle.get_size();
                    handles.push(handle);
                }
                {
                    let mut inner = self.inner.lock().unwrap();
                    // After replaying, allocated_bytes should include all the deltas since the time
                    // the allocator was last flushed, so here we just need to add whatever is
                    // recorded in info.
                    let amount: i64 =
                        info.allocated_bytes.try_into().map_err(|_| FxfsError::Inconsistent)?;
                    inner.allocated_bytes += amount;
                    if inner.allocated_bytes < 0 || inner.allocated_bytes as u64 > self.device_size
                    {
                        bail!(anyhow!(FxfsError::Inconsistent)
                            .context("Allocated bytes inconsistent"));
                    }
                    inner.info = info;
                }
                self.tree.append_layers(handles.into_boxed_slice()).await?;
                self.filesystem
                    .upgrade()
                    .unwrap()
                    .object_manager()
                    .update_reservation(self.object_id, total_size);
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

    fn needs_sync(&self) -> bool {
        // TODO(csuter): This will only trigger if *all* free space is taken up with committed
        // deallocated bytes, but we might want to trigger a sync if we're low and there happens to
        // be a lot of deallocated bytes as that might mean we can fully satisfy allocation
        // requests.
        self.inner.lock().unwrap().unavailable_bytes() >= self.device_size
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
        mut len: u64,
    ) -> Result<Range<u64>, Error> {
        assert_eq!(len % self.block_size as u64, 0);

        self.ensure_open().await?;

        let hold = if let Some(reservation) = transaction.allocator_reservation {
            Left(reservation.hold(len)?)
        } else {
            // We can't use a Reservation here because it needs Arc<Self>, so we use a simple RAII
            // object instead.
            struct AllocatorHold<'a> {
                allocator: &'a SimpleAllocator,
                amount: u64,
            }

            impl Drop for AllocatorHold<'_> {
                fn drop(&mut self) {
                    self.allocator.release_reservation(self.amount);
                }
            }

            let mut inner = self.inner.lock().unwrap();
            // We must take care not to use up space that might be reserved.
            len = round_down(
                std::cmp::min(
                    len,
                    self.device_size
                        - inner.allocated_bytes as u64
                        - inner.reserved_bytes
                        - inner.uncommitted_allocated_bytes,
                ),
                self.block_size,
            );
            ensure!(len > 0, FxfsError::NoSpace);
            inner.reserved_bytes += len;
            Right(AllocatorHold { allocator: self, amount: len })
        };

        let _guard = loop {
            {
                let guard = self.allocation_mutex.lock().await;

                if !self.needs_sync() {
                    break guard;
                }
            }

            // All the free space is currently tied up with deallocations, so we need to sync
            // and flush the device to free that up.
            //
            // We can't hold the allocation lock whilst we sync here because the allocation lock is
            // also taken in apply_mutations, which is called when journal locks are held, and we
            // call sync here which takes those same locks, so it would have the potential to result
            // in a deadlock.  Sync holds its own lock to guard against multiple syncs occurring at
            // the same time, and we can supply a precondition that is evaluated under that lock to
            // ensure we don't sync twice if we don't need to.
            self.filesystem
                .upgrade()
                .unwrap()
                .sync(SyncOptions {
                    flush_device: true,
                    precondition: Some(Box::new(|| self.needs_sync())),
                    ..Default::default()
                })
                .await?;
        };

        let dropped_allocations =
            std::mem::take(&mut self.inner.lock().unwrap().dropped_allocations);

        // Update reserved_allocations using dropped_allocations.
        for item in dropped_allocations {
            self.reserved_allocations.erase(item.as_item_ref()).await;
        }

        let result = {
            let tree = &self.tree;
            let mut layer_set = tree.empty_layer_set();
            layer_set
                .layers
                .push((self.reserved_allocations.clone() as Arc<dyn Layer<_, _>>).into());
            tree.add_all_layers_to_layer_set(&mut layer_set);
            let mut merger = layer_set.merger();
            let mut iter = merger.seek(Bound::Unbounded).await?;
            let mut last_offset = 0;
            loop {
                match iter.get() {
                    None => {
                        let end = std::cmp::min(last_offset + len, self.device_size);
                        if end <= last_offset {
                            // This is unexpected since we reserved space above.  It would suggest
                            // that our counters are confused somehow.
                            bail!(anyhow!(FxfsError::NoSpace)
                                .context("Unexpectedly found no space after search"));
                        }
                        break last_offset..end;
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

        let len = result.length();
        hold.either(|mut h| h.take_some(len), |mut h| h.amount -= len);
        {
            let mut inner = self.inner.lock().unwrap();
            inner.reserved_bytes -= result.length();
            inner.uncommitted_allocated_bytes += result.length();
        }

        let item = AllocatorItem::new(
            AllocatorKey { device_range: result.clone() },
            AllocatorValue { delta: 1 },
        );
        self.reserved_allocations.insert(item.clone()).await;
        transaction.add(self.object_id(), Mutation::allocation(item));

        Ok(result)
    }

    async fn mark_allocated(
        &self,
        transaction: &mut Transaction<'_>,
        device_range: Range<u64>,
    ) -> Result<(), Error> {
        ensure!(device_range.end <= self.device_size, FxfsError::NoSpace);
        if let Some(reservation) = &mut transaction.allocator_reservation {
            // The transaction takes ownership of this hold.
            reservation.hold(device_range.length())?.take();
        }
        self.inner.lock().unwrap().uncommitted_allocated_bytes += device_range.length();
        let item = AllocatorItem::new(AllocatorKey { device_range }, AllocatorValue { delta: 1 });
        self.reserved_allocations.insert(item.clone()).await;
        transaction.add(self.object_id(), Mutation::allocation(item));
        Ok(())
    }

    fn add_ref(&self, transaction: &mut Transaction<'_>, device_range: Range<u64>) {
        transaction.add(
            self.object_id(),
            Mutation::allocation_ref(AllocatorItem::new(
                AllocatorKey { device_range },
                AllocatorValue { delta: 1 },
            )),
        );
    }

    async fn deallocate(
        &self,
        transaction: &mut Transaction<'_>,
        mut dealloc_range: Range<u64>,
    ) -> Result<u64, Error> {
        log::debug!("deallocate {:?}", dealloc_range);
        trace_duration!("SimpleAllocator::deallocate");

        // We need to determine whether this deallocation actually frees the range or is just a
        // reference count adjustment.  We separate the two kinds into two different mutation types
        // so that we can adjust our counts correctly at commit time.
        let layer_set = self.tree.layer_set();
        let mut merger = layer_set.merger();
        // The precise search key that we choose here is important.  We need to perform a full merge
        // across all layers because we want the precise value of delta, so we must ensure that we
        // query all layers, which is done by setting the lower bound to zero (the merger consults
        // iterators until it encounters a key whose lower-bound is not greater than the search
        // key).  The upper bound is used to search each individual layer, and we want to start with
        // an extent that covers the first byte of the range we're deallocating.
        let mut iter = merger
            .seek(Bound::Included(&AllocatorKey { device_range: 0..dealloc_range.start + 1 }))
            .await?;
        let mut deallocated = 0;
        let mut mutation = None;
        while let Some(ItemRef {
            key: AllocatorKey { device_range, .. },
            value: AllocatorValue { delta, .. },
            ..
        }) = iter.get()
        {
            if device_range.start > dealloc_range.start {
                // We expect the entire range to be allocated.
                bail!(anyhow!(FxfsError::Inconsistent)
                    .context("Attempt to deallocate unallocated range"));
            }
            let end = std::cmp::min(device_range.end, dealloc_range.end);
            if *delta == 1 {
                // In this branch, we know that we're freeing data, so we want an Allocator
                // mutation.
                if let Some(Mutation::AllocatorRef(_)) = mutation {
                    transaction.add(self.object_id(), mutation.take().unwrap());
                }
                match &mut mutation {
                    None => {
                        mutation = Some(Mutation::allocation(Item::new(
                            AllocatorKey { device_range: dealloc_range.start..end },
                            AllocatorValue { delta: -1 },
                        )));
                    }
                    Some(Mutation::Allocator(AllocatorMutation(AllocatorItem { key, .. }))) => {
                        key.device_range.end = end;
                    }
                    _ => unreachable!(),
                }
                deallocated += end - dealloc_range.start;
            } else {
                // In this branch, we know that we're not freeing data, so we want an AllocatorRef
                // mutation.
                if let Some(Mutation::Allocator(_)) = mutation {
                    transaction.add(self.object_id(), mutation.take().unwrap());
                }
                match &mut mutation {
                    None => {
                        mutation = Some(Mutation::allocation_ref(Item::new(
                            AllocatorKey { device_range: dealloc_range.start..end },
                            AllocatorValue { delta: -1 },
                        )));
                    }
                    Some(Mutation::AllocatorRef(AllocatorMutation(AllocatorItem {
                        key, ..
                    }))) => {
                        key.device_range.end = end;
                    }
                    _ => unreachable!(),
                }
            }
            if end == dealloc_range.end {
                break;
            }
            dealloc_range.start = end;
            iter.advance().await?;
        }
        if let Some(mutation) = mutation {
            transaction.add(self.object_id(), mutation);
        }
        Ok(deallocated)
    }

    fn as_mutations(self: Arc<Self>) -> Arc<dyn Mutations> {
        self
    }

    fn as_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync> {
        self
    }

    async fn did_flush_device(&self, flush_log_offset: u64) {
        // First take out the deallocations that we now know to be flushed.  The list is maintained
        // in order, so we can stop on the first entry that we find that should not be unreserved
        // yet.
        let deallocs = 'outer: loop {
            let mut inner = self.inner.lock().unwrap();
            for (index, (dealloc_log_offset, _)) in inner.committed_deallocated.iter().enumerate() {
                if *dealloc_log_offset >= flush_log_offset {
                    let mut deallocs = inner.committed_deallocated.split_off(index);
                    // Swap because we want the opposite of what split_off does.
                    std::mem::swap(&mut inner.committed_deallocated, &mut deallocs);
                    break 'outer deallocs;
                }
            }
            break std::mem::take(&mut inner.committed_deallocated);
        };
        // Now we can erase those elements from reserved_allocations (whilst we're not holding the
        // lock on inner).
        let mut total = 0;
        for (_, device_range) in deallocs {
            total += device_range.length();
            self.reserved_allocations
                .erase(
                    Item::new(AllocatorKey { device_range }, AllocatorValue { delta: 0 })
                        .as_item_ref(),
                )
                .await;
        }
        // This *must* come after we've removed the records from reserved reservations because the
        // allocator uses this value to decide whether or not a device-flush is required and it must
        // be possible to find free space if it thinks no device-flush is required.
        self.inner.lock().unwrap().committed_deallocated_bytes -= total;
    }

    fn reserve(self: Arc<Self>, amount: u64) -> Option<Reservation> {
        {
            let mut inner = self.inner.lock().unwrap();
            if self.device_size - inner.allocated_bytes as u64 - inner.reserved_bytes < amount {
                return None;
            }
            inner.reserved_bytes += amount;
        }
        Some(Reservation::new(self, amount))
    }

    fn reserve_at_most(self: Arc<Self>, mut amount: u64) -> Reservation {
        {
            let mut inner = self.inner.lock().unwrap();
            amount = std::cmp::min(
                self.device_size - inner.allocated_bytes as u64 - inner.reserved_bytes,
                amount,
            );
            inner.reserved_bytes += amount;
        }
        Reservation::new(self, amount)
    }

    fn release_reservation(&self, amount: u64) {
        self.inner.lock().unwrap().reserved_bytes -= amount;
    }

    fn get_allocated_bytes(&self) -> u64 {
        self.inner.lock().unwrap().allocated_bytes as u64
    }

    fn get_used_bytes(&self) -> u64 {
        let inner = self.inner.lock().unwrap();
        inner.allocated_bytes as u64 + inner.reserved_bytes
    }

    async fn validate_mutation(
        &self,
        journal_offset: u64,
        mutation: &Mutation,
        checksum_list: &mut ChecksumList,
    ) -> Result<bool, Error> {
        match mutation {
            Mutation::Allocator(AllocatorMutation(AllocatorItem {
                key: AllocatorKey { device_range },
                value: AllocatorValue { delta },
                ..
            })) if *delta < 0 => {
                checksum_list.mark_deallocated(journal_offset, device_range.clone());
            }
            _ => {}
        }
        Ok(true)
    }
}

#[async_trait]
impl Mutations for SimpleAllocator {
    async fn apply_mutation(
        &self,
        mutation: Mutation,
        transaction: Option<&Transaction<'_>>,
        log_offset: u64,
        _assoc_obj: AssocObj<'_>,
    ) {
        match mutation {
            Mutation::Allocator(AllocatorMutation(mut item)) => {
                item.sequence = log_offset;
                // We currently rely on barriers here between inserting/removing from reserved
                // allocations and merging into the tree.  These barriers are present whilst we use
                // skip_list_layer's commit_and_wait method, rather than just commit.
                let len = item.key.device_range.length();
                if item.value.delta < 0 {
                    if transaction.is_some() {
                        let mut item = item.clone();
                        item.value.delta = 1;
                        self.reserved_allocations.insert(item).await;
                    }

                    let mut inner = self.inner.lock().unwrap();
                    inner.allocated_bytes = inner.allocated_bytes.saturating_sub(len as i64);

                    if transaction.is_some() {
                        inner
                            .committed_deallocated
                            .push_back((log_offset, item.key.device_range.clone()));
                        inner.committed_deallocated_bytes += item.key.device_range.length();
                    }

                    if let Some(Transaction { allocator_reservation: Some(reservation), .. }) =
                        transaction
                    {
                        inner.reserved_bytes += len;
                        reservation.add(len);
                    }
                }
                let lower_bound = item.key.lower_bound_for_merge_into();
                self.tree.merge_into(item.clone(), &lower_bound).await;
                if item.value.delta > 0 {
                    if transaction.is_some() {
                        self.reserved_allocations.erase(item.as_item_ref()).await;
                    }
                    let mut inner = self.inner.lock().unwrap();
                    inner.allocated_bytes = inner.allocated_bytes.saturating_add(len as i64);
                    if let Some(transaction) = transaction {
                        inner.uncommitted_allocated_bytes -= len;
                        if let Some(reservation) = transaction.allocator_reservation {
                            reservation.commit(len);
                        }
                    }
                }
            }
            Mutation::AllocatorRef(AllocatorMutation(mut item)) => {
                item.sequence = log_offset;
                let lower_bound = item.key.lower_bound_for_merge_into();
                self.tree.merge_into(item, &lower_bound).await;
            }
            Mutation::BeginFlush => {
                {
                    // After we seal the tree, we will start adding mutations to the new mutable
                    // layer, but we cannot safely do that whilst we are attempting to allocate
                    // because there is a chance it might miss an allocation and also not see the
                    // allocation in reserved_allocations.
                    let _guard = debug_assert_not_too_long!(self.allocation_mutex.lock());
                    self.tree.seal().await;
                }
                // Transfer our running count for allocated_bytes so that it gets written to the new
                // info file when flush completes.
                let mut inner = self.inner.lock().unwrap();
                inner.info.allocated_bytes = inner.allocated_bytes as u64;
            }
            Mutation::EndFlush => {
                if transaction.is_none() {
                    self.tree.reset_immutable_layers();
                    // AllocatorInfo is written in the same transaction and will contain the count
                    // at the point BeginFlush was applied, so we need to adjust allocated_bytes so
                    // that it just covers the delta from that point.  Later, when we properly open
                    // the allocator, we'll add this back.
                    let mut inner = self.inner.lock().unwrap();
                    inner.allocated_bytes -= inner.info.allocated_bytes as i64;
                } else {
                    let layers = self.tree.immutable_layer_set();
                    self.filesystem.upgrade().unwrap().object_manager().update_reservation(
                        self.object_id,
                        layers
                            .layers
                            .iter()
                            .map(|l| l.handle().map(|h| h.get_size()).unwrap_or(0))
                            .sum(),
                    );
                }
            }
            _ => panic!("unexpected mutation! {:?}", mutation), // TODO(csuter): This can't panic
        }
    }

    fn drop_mutation(&self, mutation: Mutation, transaction: &Transaction<'_>) {
        match mutation {
            Mutation::Allocator(AllocatorMutation(item)) => {
                if item.value.delta > 0 {
                    let mut inner = self.inner.lock().unwrap();
                    let len = item.key.device_range.length();
                    inner.uncommitted_allocated_bytes -= len;
                    if let Some(reservation) = transaction.allocator_reservation {
                        reservation.release(len);
                        inner.reserved_bytes += len;
                    }
                    inner.dropped_allocations.push(item);
                }
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
        // TODO(csuter): This all needs to be atomic somehow. We'll need to use different
        // transactions for each stage, but we need make sure objects are cleaned up if there's a
        // failure.
        let reservation = object_manager.metadata_reservation();
        let txn_options = Options {
            skip_journal_checks: true,
            borrow_metadata_space: true,
            allocator_reservation: Some(reservation),
            ..Default::default()
        };
        let mut transaction = filesystem.clone().new_transaction(&[], txn_options).await?;

        let root_store = self.filesystem.upgrade().unwrap().root_store();
        let layer_object_handle = ObjectStore::create_object(
            &root_store,
            &mut transaction,
            HandleOptions { skip_journal_checks: true, ..Default::default() },
            Some(0),
        )
        .await?;
        let object_id = layer_object_handle.object_id();
        graveyard.add(&mut transaction, root_store.store_object_id(), object_id);
        // It's important that this transaction does not include any allocations because we use
        // BeginFlush as a snapshot point for mutations to the tree: other allocator mutations
        // within this transaction might get applied before seal (which would be OK), but they could
        // equally get applied afterwards (since Transaction makes no guarantees about the order in
        // which mutations are applied whilst committing), in which case they'd get lost on replay
        // because the journal will only send mutations that follow this transaction.
        transaction.add(self.object_id(), Mutation::BeginFlush);
        transaction.commit().await?;

        let layer_set = self.tree.immutable_layer_set();
        {
            let mut merger = layer_set.merger();
            self.tree
                .compact_with_iterator(
                    CoalescingIterator::new(Box::new(merger.seek(Bound::Unbounded).await?)).await?,
                    DirectWriter::new(&layer_object_handle, txn_options),
                    layer_object_handle.block_size(),
                )
                .await?;
        }

        log::debug!("using {} for allocator layer file", object_id);
        let object_handle =
            ObjectStore::open_object(&root_store, self.object_id(), HandleOptions::default())
                .await?;

        // TODO(jfsulliv): Can we preallocate the buffer instead of doing a bounce? Do we know the
        // size up front?
        let mut transaction = filesystem.clone().new_transaction(&[], txn_options).await?;
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

        // It's important that EndFlush is in the same transaction that we write AllocatorInfo,
        // because we use EndFlush to make the required adjustments to allocated_bytes.
        transaction.add(self.object_id(), Mutation::EndFlush);
        graveyard.remove(&mut transaction, root_store.store_object_id(), object_id);

        // TODO(csuter): what if this fails.
        let layers = layers_from_handles(Box::new([layer_object_handle])).await?;
        transaction.commit_with_callback(|_| self.tree.set_layers(layers)).await?;

        // Now close the layers and purge them.
        for layer in layer_set.layers {
            let object_id = layer.handle().map(|h| h.object_id());
            layer.close_layer().await;
            if let Some(object_id) = object_id {
                root_store.tombstone(object_id, txn_options).await?;
            }
        }

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
                transaction::{Options, TransactionHandler},
                ObjectStore,
            },
        },
        fuchsia_async as fasync,
        interval_tree::utils::RangeOps,
        std::{
            cmp::{max, min},
            ops::{Bound, Range},
            sync::Arc,
        },
        storage_device::{fake_device::FakeDevice, DeviceHolder},
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
        let ItemRef { key, value, .. } = iter.get().expect("get failed");
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
        let ItemRef { key, value, .. } = iter.get().expect("get failed");
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
            let mut l = device_range.length();
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
        assert_eq!(found, expected_allocations.iter().map(|r| r.length()).sum());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocations() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store(store.clone());
        let mut transaction =
            fs.clone().new_transaction(&[], Options::default()).await.expect("new failed");
        let mut device_ranges = Vec::new();
        device_ranges.push(
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed"),
        );
        assert_eq!(device_ranges.last().unwrap().length(), fs.block_size() as u64);
        device_ranges.push(
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed"),
        );
        assert_eq!(device_ranges.last().unwrap().length(), fs.block_size() as u64);
        assert_eq!(overlap(&device_ranges[0], &device_ranges[1]), 0);
        transaction.commit().await.expect("commit failed");
        let mut transaction =
            fs.clone().new_transaction(&[], Options::default()).await.expect("new failed");
        device_ranges.push(
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed"),
        );
        assert_eq!(device_ranges[2].length(), fs.block_size() as u64);
        assert_eq!(overlap(&device_ranges[0], &device_ranges[2]), 0);
        assert_eq!(overlap(&device_ranges[1], &device_ranges[2]), 0);
        transaction.commit().await.expect("commit failed");

        check_allocations(&allocator, &device_ranges).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_deallocations() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store(store.clone());
        let mut transaction =
            fs.clone().new_transaction(&[], Options::default()).await.expect("new failed");
        let device_range1 = allocator
            .allocate(&mut transaction, fs.block_size() as u64)
            .await
            .expect("allocate failed");
        assert_eq!(device_range1.length(), fs.block_size() as u64);
        transaction.commit().await.expect("commit failed");

        let mut transaction =
            fs.clone().new_transaction(&[], Options::default()).await.expect("new failed");
        allocator.deallocate(&mut transaction, device_range1).await.expect("deallocate failed");
        transaction.commit().await.expect("commit failed");

        check_allocations(&allocator, &[]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mark_allocated() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store(store.clone());
        let mut transaction =
            fs.clone().new_transaction(&[], Options::default()).await.expect("new failed");
        let mut device_ranges = Vec::new();
        device_ranges.push(0..fs.block_size() as u64);
        allocator
            .mark_allocated(&mut transaction, device_ranges.last().unwrap().clone())
            .await
            .expect("mark_allocated failed");
        device_ranges.push(
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed"),
        );
        assert_eq!(device_ranges.last().unwrap().length(), fs.block_size() as u64);
        assert_eq!(overlap(&device_ranges[0], &device_ranges[1]), 0);
        transaction.commit().await.expect("commit failed");

        check_allocations(&allocator, &device_ranges).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store(store.clone());
        allocator.ensure_open().await.expect("ensure_open failed");
        let mut transaction =
            fs.clone().new_transaction(&[], Options::default()).await.expect("new failed");
        let graveyard = Graveyard::create(&mut transaction, &store).await.expect("create failed");
        fs.object_manager().register_graveyard(graveyard);
        let mut device_ranges = Vec::new();
        device_ranges.push(
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed"),
        );
        device_ranges.push(
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed"),
        );
        device_ranges.push(
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed"),
        );
        transaction.commit().await.expect("commit failed");

        allocator.flush().await.expect("flush failed");

        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, false));
        fs.object_manager().set_allocator(allocator.clone());
        // When we flushed the allocator, it would have been written to the device somewhere but
        // without a journal, we will be missing those records, so this next allocation will likely
        // be on top of those objects.  That won't matter for the purposes of this test, since we
        // are not writing anything to these ranges.
        let mut transaction =
            fs.clone().new_transaction(&[], Options::default()).await.expect("new failed");
        device_ranges.push(
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed"),
        );
        for r in &device_ranges[..3] {
            assert_eq!(overlap(r, device_ranges.last().unwrap()), 0);
        }
        transaction.commit().await.expect("commit failed");
        check_allocations(&allocator, &device_ranges).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_dropped_transaction() {
        let device = DeviceHolder::new(FakeDevice::new(4096, 512));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store(store.clone());
        let allocated_range = {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed")
        };
        // After dropping the transaction and attempting to allocate again, we should end up with
        // the same range because the reservation should have been released.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        assert_eq!(
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed"),
            allocated_range
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocated_bytes() {
        const BLOCK_COUNT: u32 = 4096;
        const BLOCK_SIZE: u32 = 512;
        let device = DeviceHolder::new(FakeDevice::new(BLOCK_COUNT.into(), BLOCK_SIZE));
        let fs = FakeFilesystem::new(device);
        let allocator = Arc::new(SimpleAllocator::new(fs.clone(), 1, true));
        fs.object_manager().set_allocator(allocator.clone());
        let store = ObjectStore::new_empty(None, 2, fs.clone());
        fs.object_manager().set_root_store(store.clone());
        assert_eq!(allocator.get_allocated_bytes(), 0);

        // Verify allocated_bytes reflects allocation changes.
        let allocated_bytes = fs.block_size() as u64;
        let allocated_range = {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let range = allocator
                .allocate(&mut transaction, allocated_bytes)
                .await
                .expect("allocate failed");
            transaction.commit().await.expect("commit failed");
            assert_eq!(allocator.get_allocated_bytes(), allocated_bytes);
            range
        };

        {
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            allocator
                .allocate(&mut transaction, fs.block_size() as u64)
                .await
                .expect("allocate failed");

            // Prior to commiiting, the count of allocated bytes shouldn't change.
            assert_eq!(allocator.get_allocated_bytes(), allocated_bytes);
        }

        // After dropping the prior transaction, the allocated bytes still shouldn't have changed.
        assert_eq!(allocator.get_allocated_bytes(), allocated_bytes);

        // Verify allocated_bytes reflects deallocations.
        let deallocate_range = allocated_range.start + 20..allocated_range.end - 20;
        let mut transaction =
            fs.clone().new_transaction(&[], Options::default()).await.expect("new failed");
        allocator.deallocate(&mut transaction, deallocate_range).await.expect("deallocate failed");

        // Before committing, there should be no change.
        assert_eq!(allocator.get_allocated_bytes(), allocated_bytes);

        transaction.commit().await.expect("commit failed");

        // After committing, all but 40 bytes should remain allocated.
        assert_eq!(allocator.get_allocated_bytes(), 40);
    }
}
