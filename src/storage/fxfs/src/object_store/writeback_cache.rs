// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        data_buffer::DataBuffer,
        errors::FxfsError,
        filesystem::MAX_FILE_SIZE,
        object_handle::ReadObjectHandle,
        object_store::allocator::{self},
        round::{round_down, round_up},
    },
    anyhow::{ensure, Error},
    interval_tree::{interval::Interval, interval_tree::IntervalTree, utils::RangeOps},
    std::ops::Range,
    std::sync::Mutex,
    std::time::Duration,
    storage_device::buffer::Buffer,
};

// This module contains an implementation of a writeback cache.
//
// The writeback cache has two important constants associated with it:
// - The block size (BS), which is established by the filesystem, and
// - The read-ahead size (RAS), which is established by the cache (and must be a multiple of block
//   size).
//
// # State management
//
// Data in the cache is managed in BS-aligned chunks. Each of these chunks can be in a few states:
// - Absent (The cache doesn't know what is there, and needs to fetch from disk -- even if the range
//   is sparse, the cache needs to attempt a fetch to find out)
// - Clean (The cache has loaded the contents from disk into memory)
// - Dirty (There is a pending write of the chunk)
// - Flushing (There is a pending write of the chunk which is being flushed by some task)
//
// For either of the Dirty states, the cache will have a backing storage reservation for that chunk.
// (While Flushing, there's also a reservation held by the flush task.)
//
// # Resizing
//
// Extending the cache will result in all of the new pages being Clean and immediately
// cache-readable (the cache may, in fact, be sparse).
//
// Shrinking the cache will clear any of the affected ranges (even those which are Dirty or
// Flushing). If an in-progress flush is affected by the truncate, the flush will still write out
// the truncated ranges, and they will be deleted on the next flush.
//
// # Flushing
//
// Flushing is separated into two parts: the first copies out all flushable data/metadata, and the
// second marks that flush as complete (or aborts the flush).  At most one task can be flushing at a
// time, and flushing does not block any other cache operations.

/// When reading into the cache, this is the minimum granularity that we load data at.
pub const CACHE_READ_AHEAD_SIZE: u64 = 32_768;

/// StorageReservation should be implemented by filesystems to provide in-memory reservation of
/// blocks for pending writes.
pub trait StorageReservation: Send + Sync {
    /// Returns the number of bytes needed to sync |amount| bytes of dirty data.  This will take
    /// into account sync overhead, and if a flush needs to be split up into several syncs, each
    /// should be taken into account.
    fn reservation_needed(&self, data: u64) -> u64;
    /// Reserves at least |amount| bytes in the filesystem, taking into account alignment.
    /// Returns the actual number of bytes reserved, which is >= |amount|.
    fn reserve(&self, amount: u64) -> Result<allocator::Reservation, Error>;
    /// Wraps a raw reserved-byte count in the RAII type.
    fn wrap_reservation(&self, amount: u64) -> allocator::Reservation;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
// There's an implicit state when there is no entry in the interval tree, which represents an
// absent range in the file (i.e. a hole which is not memory-resident).
enum CacheState {
    // The range is memory-resident and needs to be flushed.  The cache should have enough bytes
    // reserved to write this data range.  The range's data should not be discarded.
    Dirty,
    // The range is memory-resident and is being flushed.  The range's data should not be discarded
    // until it is Clean when the flush completes.
    Flushing,
}

#[derive(Clone, Debug)]
struct CachedRange {
    range: Range<u64>,
    state: CacheState,
}

impl CachedRange {
    fn is_flushing(&self) -> bool {
        self.state == CacheState::Flushing
    }
}

impl Interval<u64> for CachedRange {
    fn clone_with(&self, new_range: &Range<u64>) -> Self {
        Self { range: new_range.clone(), state: self.state.clone() }
    }
    fn merge(&self, other: &Self) -> Self {
        let state = self.state;
        Self { range: self.range.merge(&other.range), state }
    }
    fn has_mergeable_properties(&self, other: &Self) -> bool {
        self.state == other.state
    }
    fn overrides(&self, other: &Self) -> bool {
        // This follows the state-machine transitions that we expect to perform.
        match (&self.state, &other.state) {
            // Writing to a Flushing range results in it being Dirty.
            (CacheState::Dirty, CacheState::Flushing) => true,
            _ => false,
        }
    }
}

impl AsRef<Range<u64>> for CachedRange {
    fn as_ref(&self) -> &Range<u64> {
        &self.range
    }
}

struct Inner {
    intervals: IntervalTree<CachedRange, u64>,

    // Number of dirty bytes so far, excluding those which are in the midst of a flush.  Every dirty
    // byte must have a byte reserved for it to be written back, plus some extra reservation made
    // for transaction overhead.
    dirty_bytes: u64,

    creation_time: Option<Duration>,

    modification_time: Option<Duration>,
}

impl Inner {
    fn complete_flush(&mut self, data: FlushableData<'_, '_>, completed: bool) {
        let mut dirty_bytes = 0;
        for range in data.ranges {
            let removed =
                self.intervals.remove_matching_interval(&range, |i| i.is_flushing()).unwrap();
            if !completed {
                for mut interval in removed {
                    interval.state = CacheState::Dirty;
                    self.intervals.add_interval(&interval).unwrap();
                    dirty_bytes += interval.range.end - interval.range.start;
                }
            }
        }
        if dirty_bytes > 0 {
            // If we didn't complete the flush, take back whatever reservation we'll need for
            // another attempt.
            let reserved_before = data.reserver.reservation_needed(self.dirty_bytes);
            self.dirty_bytes += dirty_bytes;
            let needed_reservation = data.reserver.reservation_needed(self.dirty_bytes);
            let delta = needed_reservation.checked_sub(reserved_before).unwrap();
            data.reservation.forget_some(delta);
        }
    }

    fn take_metadata(&mut self, content_size: Option<u64>) -> Option<FlushableMetadata> {
        if content_size.is_some()
            || self.creation_time.is_some()
            || self.modification_time.is_some()
        {
            Some(FlushableMetadata {
                content_size,
                creation_time: self.creation_time.take(),
                modification_time: self.modification_time.take(),
            })
        } else {
            None
        }
    }
}

pub struct WritebackCache<B> {
    inner: Mutex<Inner>,
    data: B,
}

#[derive(Debug)]
pub struct FlushableMetadata {
    /// The size of the file at flush time, if it has changed.
    pub content_size: Option<u64>,
    /// Measured in time since the UNIX epoch in the UTC timezone.  Individual filesystems set their
    /// own granularities.
    pub creation_time: Option<Duration>,
    /// Measured in time since the UNIX epoch in the UTC timezone.  Individual filesystems set their
    /// own granularities.
    pub modification_time: Option<Duration>,
}

pub struct FlushableData<'a, 'b> {
    reservation: &'a allocator::Reservation,
    reserver: &'a dyn StorageReservation,
    pub ranges: Vec<Range<u64>>,
    pub buffer: Buffer<'b>,
}

impl std::fmt::Debug for FlushableData<'_, '_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("FlushableData")
            .field("reservation", &self.reservation)
            .field("ranges", &self.ranges)
            .finish()
    }
}

impl FlushableData<'_, '_> {
    #[cfg(test)]
    fn dirty_bytes(&self) -> u64 {
        self.ranges.iter().map(|r| r.end - r.start).sum()
    }
}

pub struct Flushable<'a, 'b, B: DataBuffer> {
    cache: &'a WritebackCache<B>,
    pub metadata: Option<FlushableMetadata>,
    pub data: Option<FlushableData<'a, 'b>>,
}

impl<B: DataBuffer> std::fmt::Debug for Flushable<'_, '_, B> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Flushable")
            .field("metadata", &self.metadata)
            .field("data", &self.data)
            .finish()
    }
}

impl<B: DataBuffer> Drop for Flushable<'_, '_, B> {
    fn drop(&mut self) {
        if self.metadata.is_none() && self.data.is_none() {
            return;
        }
        let mut inner = self.cache.inner.lock().unwrap();
        if let Some(metadata) = self.metadata.take() {
            if metadata.creation_time.is_some() && inner.creation_time.is_none() {
                inner.creation_time = metadata.creation_time;
            }
            if metadata.modification_time.is_some() && inner.modification_time.is_none() {
                inner.modification_time = metadata.modification_time;
            }
        }
        if let Some(data) = self.data.take() {
            inner.complete_flush(data, false);
        }
    }
}

#[derive(Debug)]
pub struct CachedMetadata {
    pub content_size: u64,
    pub dirty_bytes: u64,
    /// Measured in time since the UNIX epoch in the UTC timezone.  Individual filesystems set their
    /// own granularities.
    pub creation_time: Option<Duration>,
    /// Measured in time since the UNIX epoch in the UTC timezone.  Individual filesystems set their
    /// own granularities.
    pub modification_time: Option<Duration>,
}

impl<B> Drop for WritebackCache<B> {
    fn drop(&mut self) {
        let inner = self.inner.lock().unwrap();
        if inner.dirty_bytes > 0 {
            panic!("Dropping a WritebackCache without calling cleanup will leak reserved bytes");
        }
    }
}

impl<B: DataBuffer> WritebackCache<B> {
    pub fn new(data: B) -> Self {
        Self {
            inner: Mutex::new(Inner {
                intervals: IntervalTree::new(),
                dirty_bytes: 0,
                creation_time: None,
                modification_time: None,
            }),
            data,
        }
    }

    pub fn cleanup(&self, reserve: &dyn StorageReservation) {
        let mut inner = self.inner.lock().unwrap();
        if inner.dirty_bytes > 0 {
            // Let the RAII wrapper give back the reserved bytes.
            let _reservation =
                reserve.wrap_reservation(reserve.reservation_needed(inner.dirty_bytes));
            inner.dirty_bytes = 0;
        }
        inner.intervals.remove_interval(&(0..u64::MAX)).unwrap();
    }

    pub fn cached_metadata(&self) -> CachedMetadata {
        let inner = self.inner.lock().unwrap();
        CachedMetadata {
            content_size: self.data.size(),
            dirty_bytes: inner.dirty_bytes,
            creation_time: inner.creation_time.clone(),
            modification_time: inner.modification_time.clone(),
        }
    }

    pub fn content_size(&self) -> u64 {
        self.data.size()
    }

    pub fn dirty_bytes(&self) -> u64 {
        self.inner.lock().unwrap().dirty_bytes
    }

    /// This is not thread-safe; the caller is responsible for making sure that only one thread is
    /// mutating the cache at any point in time.
    pub async fn resize(
        &self,
        size: u64,
        block_size: u64,
        reserver: &dyn StorageReservation,
        source: &dyn ReadObjectHandle,
    ) -> Result<(), Error> {
        ensure!(size <= MAX_FILE_SIZE, FxfsError::TooBig);
        let old_size;
        let mut reservation_and_range = None;
        {
            let mut inner = self.inner.lock().unwrap();
            old_size = self.content_size();
            let aligned_size = round_up(size, block_size).unwrap();
            if size < old_size {
                let removed = inner.intervals.remove_interval(&(aligned_size..u64::MAX)).unwrap();
                let mut dirty_bytes = 0;
                for interval in removed {
                    if let CacheState::Dirty = interval.state {
                        dirty_bytes += interval.range.end - interval.range.start;
                    }
                }
                let before = reserver.reservation_needed(inner.dirty_bytes);
                inner.dirty_bytes = inner.dirty_bytes.checked_sub(dirty_bytes).unwrap();
                let needed = reserver.reservation_needed(inner.dirty_bytes);
                if needed < before {
                    let _ = reserver.wrap_reservation(before - needed);
                }
            } else if old_size % block_size != 0 {
                // If the tail isn't already dirty, we must bring that block in and mark it as dirty
                // to ensure that it gets properly zeroed.
                let aligned_range =
                    round_down(old_size, block_size)..round_up(old_size, block_size).unwrap();
                if inner.intervals.get_intervals(&aligned_range).unwrap().is_empty() {
                    reservation_and_range = Some((
                        reserver.reserve(
                            reserver.reservation_needed(inner.dirty_bytes + block_size)
                                - reserver.reservation_needed(inner.dirty_bytes),
                        )?,
                        aligned_range,
                    ));
                }
            }
        }

        // VmoDataBuffer will page in the tail page, but DataBuffer won't do that, and since we want
        // to mark it dirty we must make sure we read it in here.
        if reservation_and_range.is_some() {
            let mut buf = [0];
            self.data.read(old_size - 1, &mut buf, source).await?;
        }

        // Resize the buffer after making changes to |inner| to avoid a race when truncating (where
        // we could have intervals that reference nonexistent parts of the buffer).
        // Note that there's a similar race for extending when we do things in this order, but we
        // don't think that is problematic since there are no intervals (since we're expanding).
        // If that turns out to be problematic, we'll have to atomically update both the buffer and
        // the interval tree at once.
        self.data.resize(size).await;

        if let Some((reservation, range)) = reservation_and_range {
            let mut inner = self.inner.lock().unwrap();
            inner.intervals.add_interval(&CachedRange { range, state: CacheState::Dirty }).unwrap();
            inner.dirty_bytes += block_size;
            reservation.forget();
        }

        Ok(())
    }

    /// Read from the cache.
    pub async fn read(
        &self,
        offset: u64,
        buf: &mut [u8],
        source: &dyn ReadObjectHandle,
    ) -> Result<usize, Error> {
        self.data.read(offset, buf, source).await
    }

    /// Writes some new data into the cache, marking that data as Dirty.  If |offset| is None, the
    /// data is appended to the end of the existing data.  If |current_time| is set (as a duration
    /// since the UNIX epoch in UTC, with whatever granularity the filesystem supports), the cached
    /// mtime will be set to this value.  If the filesystem doesn't support timestamps, it may
    /// simply set this to None.  Returns the size after the write completes.  This is not
    /// thread-safe; the caller is responsible for making sure that only one thread is mutating the
    /// cache at any point in time.
    pub async fn write_or_append(
        &self,
        offset: Option<u64>,
        mut buf: &[u8],
        block_size: u64,
        reserver: &dyn StorageReservation,
        current_time: Option<Duration>,
        source: &dyn ReadObjectHandle,
    ) -> Result<(), Error> {
        let size = self.data.size();
        let offset = offset.unwrap_or(size);

        ensure!(offset < MAX_FILE_SIZE, FxfsError::TooBig);
        let max_len = MAX_FILE_SIZE - offset;
        if buf.len() as u64 > max_len {
            buf = &buf[..max_len as usize];
        }

        // |inner| shouldn't be modified until we're at a part of the function where nothing can
        // fail (either before an early-return, or at the end of the function).
        let mut dirtied_intervals = vec![];
        let mut dirtied_bytes = 0;
        let reservation;
        {
            let inner = self.inner.lock().unwrap();

            let end = offset + buf.len() as u64;
            let aligned_range = round_down(offset, block_size)..round_up(end, block_size).unwrap();

            let intervals = inner.intervals.get_intervals(&aligned_range).unwrap();
            // TODO(fxbug.dev/96146): This might be much simpler and more readable if we refactored
            // interval_tree to have an iterator interface.  See
            // https://fuchsia-review.googlesource.com/c/fuchsia/+/547024/comments/523de326_6e2b4766.
            let mut current_offset = aligned_range.start;
            let mut i = 0;
            while current_offset < end {
                let interval = intervals.get(i);
                if interval.is_none() {
                    // Passed the last known interval.
                    dirtied_bytes += aligned_range.end - current_offset;
                    dirtied_intervals.push(CachedRange {
                        range: current_offset..aligned_range.end,
                        state: CacheState::Dirty,
                    });
                    break;
                }

                let interval = interval.unwrap();
                assert!(interval.range.start % block_size == 0);
                assert!(interval.range.end % block_size == 0);
                if current_offset < interval.range.start {
                    // There's a hole before the next interval.
                    dirtied_bytes += interval.range.start - current_offset;
                    dirtied_intervals.push(CachedRange {
                        range: current_offset..interval.range.start,
                        state: CacheState::Dirty,
                    });
                    current_offset = interval.range.start;
                }

                // Writing over an existing interval.
                let next_end = std::cmp::min(interval.range.end, aligned_range.end);
                let overlap_range = current_offset..next_end;
                match &interval.state {
                    CacheState::Dirty => {
                        // The range is already dirty and has a reservation.  Nothing needs to be
                        // done.
                    }
                    CacheState::Flushing => {
                        // The range is flushing.  Since this is a new write, we need to reserve
                        // space for it, and mark the range as Dirty.
                        dirtied_bytes += overlap_range.end - overlap_range.start;
                        dirtied_intervals
                            .push(CachedRange { range: overlap_range, state: CacheState::Dirty })
                    }
                };
                current_offset = next_end;
                i += 1;
            }
            // This needs to be the worst-case amount to reserve, which means just using
            // dirtied_bytes here. We'll adjust this down if necessary later.
            let reservation_needed = reserver.reservation_needed(dirtied_bytes);
            reservation = if reservation_needed > 0 {
                Some(reserver.reserve(reservation_needed)?)
            } else {
                None
            };
        }

        // TODO(fxbug.dev/96074): This will need to change to support partial writes: when short of
        // free space it's possible that some of the write will succeed but not all.
        self.data.write(offset, buf, source).await?;

        // After this point, we're committing changes, so nothing should fail.
        let mut inner = self.inner.lock().unwrap();
        for interval in dirtied_intervals {
            assert!(interval.range.start % block_size == 0);
            assert!(interval.range.end % block_size == 0);
            inner.intervals.add_interval(&interval).unwrap();
        }
        if dirtied_bytes > 0 {
            let before = reserver.reservation_needed(inner.dirty_bytes);
            inner.dirty_bytes += dirtied_bytes;
            reservation
                .unwrap()
                .forget_some(reserver.reservation_needed(inner.dirty_bytes) - before);
        }
        inner.modification_time = current_time;
        Ok(())
    }

    /// Returns all data which can be flushed.  |allocate_buffer| is a callback which is used to
    /// allocate Buffer objects.  Each pending data region will be copied into a Buffer and returned
    /// to the caller in block-aligned ranges.  This is not thread-safe with respect to cache
    /// mutations; the caller must ensure that no changes can be made to the cache for the duration
    /// of this call.  The content size returned will only ever increase the size of the object.
    /// Truncation must be dealt with by calling take_flushable_metadata. |reserver| and
    /// |reservation| must both be storing reserved bytes under the same object owner, or it will
    /// corrupt the allocator accounting.
    pub fn take_flushable<'a, F>(
        &'a self,
        block_size: u64,
        last_known_size: u64,
        allocate_buffer: F,
        reserver: &'a dyn StorageReservation,
        reservation: &'a allocator::Reservation,
    ) -> Flushable<'_, 'a, B>
    where
        F: Fn(usize) -> Buffer<'a>,
    {
        let mut inner = self.inner.lock().unwrap();

        let size = self.data.size();
        let intervals = inner.intervals.remove_interval(&(0..u64::MAX)).unwrap();

        let mut bytes_to_flush = 0;
        let mut ranges = vec![];
        for mut interval in intervals {
            assert!(interval.range.start % block_size == 0);
            assert!(interval.range.end % block_size == 0);
            assert!(!interval.is_flushing(), "Unexpected interval {:?}", interval);

            interval.state = CacheState::Flushing;
            inner.intervals.add_interval(&interval).unwrap();
            bytes_to_flush += interval.range.end - interval.range.start;
            ranges.push(interval.range);
        }

        let content_size = if size > last_known_size { Some(size) } else { None };
        let metadata = inner.take_metadata(content_size);

        if bytes_to_flush == 0 {
            return Flushable { cache: self, metadata, data: None };
        }

        // Transfer reserved bytes into the supplied reservation. This is only safe if the caller
        // has been creating reservations in the reserver and the reservation under the same object
        // owner.
        reservation.add(reserver.reservation_needed(inner.dirty_bytes));
        inner.dirty_bytes = 0;

        let mut buffer = allocate_buffer(bytes_to_flush as usize);
        let mut slice = buffer.as_mut_slice();
        for r in &ranges {
            let (head, tail) = slice.split_at_mut((r.end - r.start) as usize);
            if r.end > size {
                let (head, tail) = head.split_at_mut((size - r.start) as usize);
                self.data.raw_read(r.start, head);
                tail.fill(0);
            } else {
                self.data.raw_read(r.start, head);
            }
            slice = tail;
        }

        Flushable {
            cache: self,
            metadata,
            data: Some(FlushableData { reservation: &reservation, reserver, ranges, buffer }),
        }
    }

    // Returns any cached metadata that needs to be flushed.  This will only capture changes in
    // content size that shrink the file from it's last recorded/uncached size; `take_flushable`
    // handles the case where the file has grown.
    pub fn take_flushable_metadata<'a>(&'a self, last_known_size: u64) -> Flushable<'_, 'a, B> {
        let mut inner = self.inner.lock().unwrap();
        let size = self.data.size();
        Flushable {
            cache: self,
            metadata: inner.take_metadata(if size < last_known_size { Some(size) } else { None }),
            data: None,
        }
    }

    /// Indicates that a flush was successful.
    pub fn complete_flush<'a>(&self, mut flushed: Flushable<'a, '_, B>) {
        flushed.metadata.take();
        if let Some(data) = flushed.data.take() {
            self.inner.lock().unwrap().complete_flush(data, true);
        }
    }

    /// Sets the cached timestamp values.  The filesystem should provide values which are truncated
    /// to the filesystem's maximum supported granularity.  This is not thread-safe; the caller is
    /// responsible for making sure that only one thread is mutating the cache at any point in time.
    pub fn update_timestamps(
        &self,
        creation_time: Option<Duration>,
        modification_time: Option<Duration>,
    ) {
        if creation_time.is_none() && modification_time.is_none() {
            return;
        }
        let mut inner = self.inner.lock().unwrap();
        inner.creation_time = creation_time.or(inner.creation_time);
        inner.modification_time = modification_time.or(inner.modification_time);
    }

    /// Returns the data buffer.
    pub fn data_buffer(&self) -> &B {
        &self.data
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{Flushable, FlushableData, StorageReservation, WritebackCache},
        crate::{
            data_buffer::MemDataBuffer,
            object_store::{
                allocator::{Allocator, AllocatorInfo, Reservation, ReservationOwner},
                transaction::Transaction,
            },
            round::{how_many, round_up},
            testing::fake_object::{FakeObject, FakeObjectHandle},
        },
        anyhow::{anyhow, Error},
        assert_matches::assert_matches,
        async_trait::async_trait,
        fuchsia_async as fasync,
        futures::{channel::oneshot::channel, join},
        std::{
            collections::BTreeMap,
            ops::Range,
            sync::{
                atomic::{AtomicU64, Ordering},
                Arc, Mutex,
            },
            time::Duration,
        },
        storage_device::buffer_allocator::{BufferAllocator, MemBufferSource},
    };

    struct FakeReserverInner {
        amount: Mutex<u64>,
        limit: u64,
    }

    impl Drop for FakeReserverInner {
        fn drop(&mut self) {
            assert_eq!(*self.amount.lock().unwrap(), self.limit);
        }
    }

    struct FakeReserver {
        inner: Arc<FakeReserverInner>,
        granularity: u64,
        sync_overhead: u64,
        flush_limit: u64,
    }
    impl FakeReserver {
        fn new(amount: u64, granularity: u64) -> Self {
            Self::new_with_sync_overhead(amount, granularity, 0, 0)
        }
        fn new_with_sync_overhead(
            amount: u64,
            granularity: u64,
            sync_overhead: u64,
            flush_limit: u64,
        ) -> Self {
            Self {
                inner: Arc::new(FakeReserverInner { amount: Mutex::new(amount), limit: amount }),
                granularity,
                sync_overhead,
                flush_limit,
            }
        }
    }

    impl StorageReservation for FakeReserver {
        fn reservation_needed(&self, mut amount: u64) -> u64 {
            amount = round_up(amount, self.granularity).unwrap();
            if self.sync_overhead > 0 && self.flush_limit > 0 {
                amount + how_many(amount, self.flush_limit) * self.sync_overhead
            } else {
                amount
            }
        }
        fn reserve(&self, amount: u64) -> Result<Reservation, Error> {
            self.inner
                .clone()
                .reserve(None, round_up(amount, self.granularity).unwrap())
                .ok_or(anyhow!("No Space"))
        }
        fn wrap_reservation(&self, amount: u64) -> Reservation {
            Reservation::new(self.inner.clone(), None, amount)
        }
    }

    // TODO(fxbug.dev/96148): It's crude to implement Allocator here, but we need to clean all of
    // this up anyways when we make Reservation a VFS-level construct.
    #[async_trait]
    impl Allocator for FakeReserverInner {
        fn object_id(&self) -> u64 {
            unreachable!();
        }

        fn info(&self) -> AllocatorInfo {
            unreachable!();
        }

        async fn allocate(
            &self,
            _transaction: &mut Transaction<'_>,
            _store_object_id: u64,
            _len: u64,
        ) -> Result<Range<u64>, Error> {
            unreachable!();
        }

        async fn deallocate(
            &self,
            _transaction: &mut Transaction<'_>,
            _object_id: u64,
            _device_range: Range<u64>,
        ) -> Result<u64, Error> {
            unreachable!();
        }

        async fn mark_allocated(
            &self,
            _transaction: &mut Transaction<'_>,
            _store_object_id: u64,
            _device_range: Range<u64>,
        ) -> Result<(), Error> {
            unreachable!();
        }

        async fn set_bytes_limit(
            &self,
            _transaction: &mut Transaction<'_>,
            _owner_object_id: u64,
            _bytes: u64,
        ) -> Result<(), Error> {
            unreachable!();
        }

        async fn mark_for_deletion(
            &self,
            _transaction: &mut Transaction<'_>,
            _owner_object_id: u64,
        ) {
            unimplemented!();
        }

        async fn did_flush_device(&self, _flush_log_offset: u64) {
            unreachable!();
        }

        fn reserve(
            self: Arc<Self>,
            owner_object_id: Option<u64>,
            amount: u64,
        ) -> Option<Reservation> {
            {
                let mut inner = self.amount.lock().unwrap();
                if *inner < amount {
                    return None;
                } else {
                    *inner -= amount;
                }
            }
            Some(Reservation::new(self, owner_object_id, amount))
        }

        fn reserve_at_most(
            self: Arc<Self>,
            _owner_object_id: Option<u64>,
            _amount: u64,
        ) -> Reservation {
            unreachable!();
        }

        fn get_allocated_bytes(&self) -> u64 {
            unreachable!();
        }

        fn get_owner_allocated_bytes(&self) -> BTreeMap<u64, i64> {
            unimplemented!();
        }

        fn get_used_bytes(&self) -> u64 {
            unreachable!();
        }
    }

    impl ReservationOwner for FakeReserverInner {
        fn release_reservation(&self, _owner_object_id: Option<u64>, amount: u64) {
            let mut inner = self.amount.lock().unwrap();
            *inner += amount;
            assert!(*inner <= self.limit);
        }
    }

    #[derive(Debug)]
    struct ExpectedRange(u64, Vec<u8>);
    fn check_data_matches(actual: &FlushableData<'_, '_>, expected: &[ExpectedRange]) {
        if actual.ranges.len() != expected.len() {
            panic!("Expected {} ranges, got {} ranges", expected.len(), actual.ranges.len());
        }
        let mut i = 0;
        let mut slice = actual.buffer.as_slice();
        while i < actual.ranges.len() {
            let expected = expected.get(i).unwrap();
            let actual = actual.ranges.get(i).unwrap();
            let (head, tail) = slice.split_at((actual.end - actual.start) as usize);
            if expected.0 != actual.start || &expected.1[..] != head {
                panic!("Expected {:?}, got {:?}, {:?}", expected, actual, slice);
            }
            slice = tail;
            i += 1;
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_read() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let mut buffer = vec![0u8; 8192];
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        buffer.fill(123u8);
        cache
            .write_or_append(Some(0), &buffer[..3000], 512, &reserver, None, &source)
            .await
            .expect("write failed");

        buffer.fill(0u8);
        assert_eq!(cache.read(0, &mut buffer[..4096], &source).await.expect("read failed"), 3000);
        assert_eq!(&buffer[..3000], vec![123u8; 3000 as usize]);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_append() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        let mut buffer = vec![0u8; 8192];

        buffer.fill(123u8);
        cache
            .write_or_append(None, &buffer[..3000], 512, &reserver, None, &source)
            .await
            .expect("write failed");
        buffer.fill(45u8);
        cache
            .write_or_append(None, &buffer[..3000], 512, &reserver, None, &source)
            .await
            .expect("write failed");

        buffer.fill(0u8);
        assert_eq!(cache.content_size(), 6000);
        assert_eq!(cache.read(0, &mut buffer[..6000], &source).await.expect("read failed"), 6000);
        assert_eq!(&buffer[..3000], vec![123u8; 3000]);
        assert_eq!(&buffer[3000..6000], vec![45u8; 3000]);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_reserving_bytes_fails() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(16384)));
        // We size the reserver so that only a one-block write can succeed.
        let reserver = FakeReserver::new(512, 512);
        let cache = WritebackCache::new(MemDataBuffer::new(8192));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        let buffer = vec![0u8; 8192];
        // Create a clean region in the middle of the cache so that we split the write into two
        // dirty ranges.
        cache
            .write_or_append(Some(512), &buffer[..512], 512, &reserver, None, &source)
            .await
            .expect("write failed");
        {
            let reservation = reserver.wrap_reservation(0);
            let flushable = cache.take_flushable(
                512,
                0,
                |size| allocator.allocate_buffer(size),
                &reserver,
                &reservation,
            );
            let data = flushable.data.as_ref().expect("no data");
            assert_eq!(data.dirty_bytes(), 512);
            assert_eq!(data.ranges.len(), 1);
            cache.complete_flush(flushable);
        }

        cache
            .write_or_append(Some(0), &buffer[..], 512, &reserver, None, &source)
            .await
            .expect_err("write succeeded");

        // Ensure we can still reserve bytes, i.e. no reservations are leaked by the failed write.
        assert_matches!(reserver.reserve(512), Ok(_));

        // Ensure neither regions were marked dirty, i.e. the tree state wasn't affected.
        let reservation = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
        );
        assert_matches!(flushable.data, None);
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resize_expand() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(16384)));
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        let mut buffer = vec![0u8; 8192];
        buffer.fill(123u8);
        cache
            .write_or_append(None, &buffer[..1], 512, &reserver, None, &source)
            .await
            .expect("write failed");
        cache.resize(1000, 512, &reserver, &source).await.expect("resize failed");
        assert_eq!(cache.content_size(), 1000);

        // The entire length of the file should be clean and contain the write, plus some zeroes.
        buffer.fill(0xaa);
        assert_eq!(cache.read(0, &mut buffer[..], &source).await.expect("read failed"), 1000);
        assert_eq!(&buffer[..1], vec![123u8]);
        assert_eq!(&buffer[1..1000], vec![0u8; 999]);

        // Only the first blocks should need a flush.
        let reservation = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
        );
        let data = flushable.data.as_ref().expect("no data");
        check_data_matches(
            &data,
            &[ExpectedRange(0, {
                let mut data = vec![0u8; 512];
                data[0] = 123u8;
                data
            })],
        );
        assert_eq!(flushable.metadata.as_ref().expect("no metadata").content_size, Some(1000));
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resize_shrink() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(16384)));
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        let mut buffer = vec![0u8; 8192];
        buffer.fill(123u8);
        cache
            .write_or_append(None, &buffer[..], 512, &reserver, None, &source)
            .await
            .expect("write failed");
        cache.resize(1000, 512, &reserver, &source).await.expect("resize failed");
        assert_eq!(cache.content_size(), 1000);

        // The resize should have truncated the pending writes.
        let reservation = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
        );
        let data = flushable.data.as_ref().expect("no data");
        assert_eq!(data.dirty_bytes(), 1024);
        check_data_matches(
            data,
            &[ExpectedRange(0, {
                let mut data = vec![0; 1024];
                data[..1000].fill(123);
                data
            })],
        );
        assert_eq!(flushable.metadata.as_ref().expect("no metadata").content_size, Some(1000));
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_no_data() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(8192)));
        let reserver = FakeReserver::new(1, 1);
        let cache = WritebackCache::new(MemDataBuffer::new(0));

        let reservation = reserver.wrap_reservation(0);
        assert_matches!(
            cache.take_flushable(
                512,
                0,
                |size| allocator.allocate_buffer(size),
                &reserver,
                &reservation,
            ),
            Flushable { data: None, metadata: None, .. }
        );
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_some_data() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(65536)));
        let reserver = FakeReserver::new(65536, 512);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        let mut buffer = vec![0u8; 8192];

        buffer.fill(123u8);
        cache
            .write_or_append(Some(0), &buffer[..2000], 512, &reserver, None, &source)
            .await
            .expect("write failed");
        buffer.fill(45u8);
        cache
            .write_or_append(Some(2048), &buffer[..1], 512, &reserver, None, &source)
            .await
            .expect("write failed");
        buffer.fill(67u8);
        cache
            .write_or_append(Some(4000), &buffer[..100], 512, &reserver, None, &source)
            .await
            .expect("write failed");
        buffer.fill(89u8);
        cache
            .write_or_append(Some(4000), &buffer[..50], 512, &reserver, None, &source)
            .await
            .expect("write failed");
        let reservation = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
        );
        let data = flushable.data.as_ref().expect("no data");
        assert_eq!(data.dirty_bytes(), 2048 + 512 + 1024);
        check_data_matches(
            &data,
            &[
                ExpectedRange(0, {
                    let mut data = vec![0u8; 2560];
                    data[..2000].fill(123u8);
                    data[2048..2049].fill(45u8);
                    data
                }),
                ExpectedRange(3584, {
                    let mut data = vec![0u8; 1024];
                    data[416..466].fill(89u8);
                    data[466..516].fill(67u8);
                    data
                }),
            ],
        );
        assert_eq!(flushable.metadata.as_ref().expect("no metadata").content_size, Some(4100));
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_returns_reservation_on_abort() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(65536)));
        // Enough room for 2 flushes of 512 bytes each
        let reserver = FakeReserver::new_with_sync_overhead(2048, 512, 512, 1024);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        let buffer = [0u8; 1];
        cache
            .write_or_append(Some(0), &buffer, 512, &reserver, None, &source)
            .await
            .expect("write failed");
        let reservation = reserver.wrap_reservation(0);
        {
            let flushable = cache.take_flushable(
                512,
                0,
                |size| allocator.allocate_buffer(size),
                &reserver,
                &reservation,
            );
            let data = flushable.data.as_ref().expect("no data");
            assert_eq!(data.dirty_bytes(), 512);
            assert_eq!(reservation.amount(), 1024);

            // This write will claim another 512 + 512 bytes of reservation, taking the rest of the
            // pool.
            cache
                .write_or_append(Some(512), &buffer, 512, &reserver, None, &source)
                .await
                .expect("write failed");
            reserver.reserve(1).expect_err("Reservation should be full");
        }
        // Dropping |data| should have given 512 bytes back to the |reservation| (and thus the
        // pool) and kept a total of 1536 bytes, since we only need 1536 bytes to flush the
        // remaining 1024 bytes of data (v.s. the 2048 bytes we needed when we interleaved
        // writing/syncing).
        assert_eq!(reservation.amount(), 512);
        reserver.reserve(1).expect_err("Reservation should be full");

        let reservation2 = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation2,
        );
        let data = flushable.data.as_ref().expect("no data");
        assert_eq!(data.dirty_bytes(), 1024);
        assert_eq!(reservation2.amount(), 1536);

        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_most_recent_write_timestamp() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(65536)));
        let reserver = FakeReserver::new(65536, 4096);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));
        let secs = AtomicU64::new(1);
        let current_time = || Some(Duration::new(secs.fetch_add(1, Ordering::SeqCst), 0));

        let mut buffer = vec![0u8; 8192];

        buffer.fill(123u8);
        cache
            .write_or_append(Some(0), &buffer[..1], 512, &reserver, current_time(), &source)
            .await
            .expect("write failed");
        buffer.fill(45u8);
        cache
            .write_or_append(Some(0), &buffer[..1], 512, &reserver, current_time(), &source)
            .await
            .expect("write failed");
        let reservation = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
        );
        assert_eq!(
            flushable.metadata.as_ref().expect("no metadata").modification_time,
            Some(Duration::new(2, 0))
        );
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_explicit_timestamps() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(65536)));
        let reserver = FakeReserver::new(65536, 4096);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        let buffer = vec![0u8; 8192];
        cache
            .write_or_append(Some(0), &buffer[..1], 512, &reserver, Some(Duration::ZERO), &source)
            .await
            .expect("write failed");
        cache.update_timestamps(Some(Duration::new(1, 0)), Some(Duration::new(2, 0)));

        let reservation = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
        );
        let metadata = flushable.metadata.as_ref().expect("no metadata");
        assert_eq!(metadata.creation_time, Some(Duration::new(1, 0)));
        assert_eq!(metadata.modification_time, Some(Duration::new(2, 0)));
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resize_while_flushing() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(8192)));
        let reserver = FakeReserver::new(65536, 512);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        let mut buffer = vec![0u8; 512];
        buffer.fill(123u8);
        cache
            .write_or_append(Some(0), &buffer[..], 512, &reserver, None, &source)
            .await
            .expect("write failed");

        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        join!(
            async {
                let reservation = reserver.wrap_reservation(0);
                let flushable = cache.take_flushable(
                    512,
                    0,
                    |size| allocator.allocate_buffer(size),
                    &reserver,
                    &reservation,
                );
                assert_eq!(
                    flushable.metadata.as_ref().expect("no metadata").content_size,
                    Some(512)
                );
                check_data_matches(
                    flushable.data.as_ref().expect("no data"),
                    &[ExpectedRange(0, vec![123u8; 512])],
                );
                send1.send(()).unwrap();
                recv2.await.unwrap();
            },
            async {
                recv1.await.unwrap();
                cache.resize(511, 512, &reserver, &source).await.expect("resize failed");
                send2.send(()).unwrap();
            },
        );
        let reservation = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
        );
        assert_eq!(flushable.metadata.as_ref().expect("no metadata").content_size, Some(511));
        let mut expected = vec![123u8; 511];
        expected.append(&mut vec![0]);
        let data = flushable.data.as_ref().expect("no data");
        check_data_matches(&data, &[ExpectedRange(0, expected)]);
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }
}
