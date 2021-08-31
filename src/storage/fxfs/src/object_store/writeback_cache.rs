// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        object_handle::ReadObjectHandle,
        object_store::{
            allocator::{self},
            data_buffer::DataBuffer,
        },
        round::{round_down, round_up},
    },
    anyhow::Error,
    async_utils::event::{Event, EventWaitResult},
    interval_tree::{interval::Interval, interval_tree::IntervalTree, utils::RangeOps},
    std::ops::Range,
    std::sync::Mutex,
    std::time::Duration,
    storage_device::buffer::{Buffer, BufferRef},
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
// # Truncating
//
// Truncates which extend the cache will result in all of the new pages being Clean and immediately
// cache-readable (the cache may, in fact, be sparse).
//
// Truncates which shrink the cache will clear any of the affected ranges (even those which are
// Dirty or Flushing). If an in-progress flush is affected by the truncate, the flush will still
// write out the truncated ranges, and they will be deleted on the next flush.
//
// # Flushing
//
// Flushing is separated into two parts: the first copies out all flushable data/metadata, and the
// second marks that flush as complete (or aborts the flush).  At most one task can be flushing at a
// time, and flushing does not block any other cache operations.

// When reading into the cache, this is the minimum granularity that we load data at.
pub const CACHE_READ_AHEAD_SIZE: u64 = 32_768;

/// StorageReservation should be implemented by filesystems to provide in-memory reservation of
/// blocks for pending writes.
pub trait StorageReservation: Send + Sync {
    /// Reserves as many bytes as the filesystem needs to perform a sync (not including bytes
    /// reserved for the actual data for the sync itself).  This might include bytes for any journal
    /// transactionscache..
    fn reserve_for_sync(&self) -> Result<allocator::Reservation, Error>;
    /// Reserves enough bytes to write the given |range| in a file, taking into account alignment
    /// and write semantics (e.g. whether writes are COW).
    /// Returns the actual number of bytes reserved, which is >= |range.end - range.start|.
    fn reserve(&self, range: Range<u64>) -> Result<allocator::Reservation, Error>;
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
    pub range: Range<u64>,
    pub state: CacheState,
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

    // Bytes reserved for flushing data.
    bytes_reserved: u64,

    // Bytes reserved for the actual flush operation.
    sync_bytes_reserved: u64,

    // We keep track of the changed content size separately from `data` because the size managed by
    // `data` is modified under different locks.  When flushing, we need to capture a size that is
    // consistent with `intervals`.
    content_size: Option<u64>,

    creation_time: Option<Duration>,
    modification_time: Option<Duration>,
    flush_event: Option<Event>,

    // Holds the minimum size the object has been since the last flush.
    min_size: u64,

    // If set, the minimum size as it was when take_flushable_data was called.
    flushing_min_size: Option<u64>,
}

pub struct WritebackCache<B> {
    inner: Mutex<Inner>,
    data: B,
}

#[derive(Debug)]
pub struct FlushableMetadata {
    pub content_size: Option<u64>,
    /// Measured in time since the UNIX epoch in the UTC timezone.  Individual filesystems set their
    /// own granularities.
    pub creation_time: Option<Duration>,
    /// Measured in time since the UNIX epoch in the UTC timezone.  Individual filesystems set their
    /// own granularities.
    pub modification_time: Option<Duration>,
}

impl FlushableMetadata {
    pub fn has_updates(&self) -> bool {
        self.content_size.is_some()
            || self.creation_time.is_some()
            || self.modification_time.is_some()
    }
}

/// A block-aligned range that can be written out to disk.
pub struct FlushableRange<'a> {
    range: Range<u64>,
    data: Buffer<'a>,
}

impl<'a> FlushableRange<'a> {
    pub fn offset(&self) -> u64 {
        self.range.start
    }
    pub fn data(&self) -> BufferRef<'_> {
        self.data.as_ref()
    }
}

impl std::fmt::Debug for FlushableRange<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("FlushableRange")
            .field("range", &self.range)
            .field("data_len", &self.data.len())
            .finish()
    }
}

pub struct FlushableData<'a, 'b, B: DataBuffer> {
    pub metadata: FlushableMetadata,
    // TODO(jfsulliv): Create a VFS version of this so that we don't need to depend on fxfs code.
    // (That will require porting fxfs to use the VFS version.)
    reservation: allocator::Reservation,
    // Keep track of how many bytes |reservation| started with, in case the flush fails.
    bytes_reserved: u64,
    sync_bytes_reserved: u64,
    ranges: Vec<FlushableRange<'a>>,
    cache: Option<&'b WritebackCache<B>>,
}

impl<B: DataBuffer> std::fmt::Debug for FlushableData<'_, '_, B> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("FlushableData")
            .field("metadata", &self.metadata)
            .field("reservation", &self.reservation)
            .field("bytes_reserved", &self.bytes_reserved)
            .field("sync_bytes_reserved", &self.sync_bytes_reserved)
            .field("ranges", &self.ranges)
            .finish()
    }
}

impl<'a, 'b, B: DataBuffer> FlushableData<'a, 'b, B> {
    /// Returns a reference to the reservation made for the flush.
    /// If the flush fails, this must be *completely* refilled to its original value.
    pub fn reservation(&self) -> &allocator::Reservation {
        &self.reservation
    }
    pub fn ranges(&self) -> std::slice::Iter<'_, FlushableRange<'a>> {
        self.ranges.iter()
    }
    pub fn has_data(&self) -> bool {
        !self.ranges.is_empty()
    }
    pub fn has_metadata(&self) -> bool {
        self.metadata.has_updates()
    }
}

impl<'a, 'b, B: DataBuffer> Drop for FlushableData<'a, 'b, B> {
    fn drop(&mut self) {
        if let Some(cache) = self.cache {
            let reserved = self.reservation.take();
            if reserved != self.bytes_reserved + self.sync_bytes_reserved {
                panic!("Flush aborted without returning all reserved bytes to the cache");
            }
            cache.complete_flush_inner(
                std::mem::take(&mut self.ranges),
                false,
                self.bytes_reserved,
                self.sync_bytes_reserved,
            );
        }
    }
}

#[derive(Debug)]
pub struct CachedMetadata {
    pub content_size: u64,
    pub bytes_reserved: u64,
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
        if inner.bytes_reserved > 0 || inner.sync_bytes_reserved > 0 {
            panic!("Dropping a WritebackCache without calling cleanup will leak reserved bytes");
        }
    }
}

impl<B: DataBuffer> WritebackCache<B> {
    pub fn new(data: B) -> Self {
        Self {
            inner: Mutex::new(Inner {
                intervals: IntervalTree::new(),
                bytes_reserved: 0,
                sync_bytes_reserved: 0,
                content_size: None,
                creation_time: None,
                modification_time: None,
                flush_event: None,
                min_size: data.size(),
                flushing_min_size: None,
            }),
            data,
        }
    }

    pub fn cleanup(&self, reserve: &dyn StorageReservation) {
        let mut inner = self.inner.lock().unwrap();
        let reserved = std::mem::take(&mut inner.bytes_reserved)
            + std::mem::take(&mut inner.sync_bytes_reserved);
        if reserved > 0 {
            // Let the RAII wrapper give back the reserved bytes.
            let _reservation = reserve.wrap_reservation(reserved);
        }
    }

    pub fn cached_metadata(&self) -> CachedMetadata {
        let inner = self.inner.lock().unwrap();
        CachedMetadata {
            content_size: self.data.size(),
            bytes_reserved: inner.bytes_reserved,
            creation_time: inner.creation_time.clone(),
            modification_time: inner.modification_time.clone(),
        }
    }

    pub fn content_size(&self) -> u64 {
        self.data.size()
    }

    pub fn min_size(&self) -> u64 {
        let inner = self.inner.lock().unwrap();
        inner.flushing_min_size.map(|s| std::cmp::min(s, inner.min_size)).unwrap_or(inner.min_size)
    }

    /// This is not thread-safe; the caller is responsible for making sure that only one thread is
    /// mutating the cache at any point in time.
    pub async fn resize(&self, size: u64, block_size: u64) {
        let old_size = self.data.size();
        self.data.resize(size).await;
        let mut inner = self.inner.lock().unwrap();
        let aligned_size = round_up(size, block_size).unwrap();
        if size < old_size {
            inner.intervals.remove_interval(&(aligned_size..u64::MAX)).unwrap();
        }
        if size != old_size {
            inner.content_size = Some(size);
        }
        if size < inner.min_size {
            inner.min_size = size;
        }
    }

    /// Read from the cache.
    pub async fn read(
        &self,
        offset: u64,
        buf: &mut [u8],
        source: &dyn ReadObjectHandle,
    ) -> Result<usize, Error> {
        // TODO(csuter): There are some races here that need to be addressed.  If the read requires
        // a page-in, but whilst we are doing that, the object is truncated and then flushed, the
        // read can then fail with a short read, but that probably isn't handled correctly right
        // now: it might return an error or it might return zeroes where there should be none.
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
        buf: &[u8],
        block_size: u64,
        reserve: &dyn StorageReservation,
        current_time: Option<Duration>,
        source: &dyn ReadObjectHandle,
    ) -> Result<(), Error> {
        let size = self.data.size();
        let offset = offset.unwrap_or(size);

        // |inner| shouldn't be modified until we're at a part of the function where nothing can
        // fail (either before an early-return, or at the end of the function).
        // TODO(jfsulliv): Consider splitting this up into a prepare and commit function (the
        // prepare function would need to return a lock).  That would make this requirement
        // more explicit, but might also have other advantages, such as not needing the
        // StorageReservation trait nor the Reservation wrapper any more.  However, it involves
        // other complexity, mostly around the lock.
        let mut dirtied_intervals = vec![];
        let mut reservations = vec![];
        let sync_reservation;
        {
            let inner = self.inner.lock().unwrap();

            let end = offset + buf.len() as u64;
            let aligned_range = round_down(offset, block_size)..round_up(end, block_size).unwrap();
            sync_reservation = if inner.sync_bytes_reserved == 0 {
                Some(reserve.reserve_for_sync()?)
            } else {
                None
            };

            let intervals = inner.intervals.get_intervals(&aligned_range).unwrap();
            // TODO(jfsulliv): This might be much simpler and more readable if we refactored
            // interval_tree to have an iterator interface.  See
            // https://fuchsia-review.googlesource.com/c/fuchsia/+/547024/comments/523de326_6e2b4766.
            let mut current_offset = aligned_range.start;
            let mut i = 0;
            while current_offset < end {
                let interval = intervals.get(i);
                if interval.is_none() {
                    // Passed the last known interval.
                    reservations.push(reserve.reserve(current_offset..aligned_range.end)?);
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
                    reservations.push(reserve.reserve(current_offset..interval.range.start)?);
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
                        reservations.push(reserve.reserve(overlap_range.clone())?);
                        dirtied_intervals
                            .push(CachedRange { range: overlap_range, state: CacheState::Dirty })
                    }
                };
                current_offset = next_end;
                i += 1;
            }
        }

        // TODO(csuter): This will need to change to support partial writes: when short of free
        // space it's possible that some of the write will succeed but not all.
        self.data.write(offset, buf, source).await?;

        // After this point, we're committing changes, so nothing should fail.
        let mut inner = self.inner.lock().unwrap();
        let end = offset + buf.len() as u64;
        if end > size {
            inner.content_size = Some(end);
        }
        for interval in dirtied_intervals {
            assert!(interval.range.start % block_size == 0);
            assert!(interval.range.end % block_size == 0);
            inner.intervals.add_interval(&interval).unwrap();
        }
        for reservation in reservations {
            let amount = reservation.take();
            inner.bytes_reserved += amount;
        }
        if let Some(sync_reservation) = sync_reservation {
            inner.sync_bytes_reserved += sync_reservation.take();
        }
        inner.modification_time = current_time;
        Ok(())
    }

    /// Returns all data which can be flushed.
    /// |allocate_buffer| is a callback which is used to allocate Buffer objects.  Each pending data
    /// region will be copied into a Buffer and returned to the caller in block-aligned ranges.
    /// If there's already an ongoing flush, the caller receives an Event that resolves when the
    /// other flush is complete.  This is considered thread-safe with respect to cache mutations.
    pub fn take_flushable_data<'a, F>(
        &self,
        block_size: u64,
        allocate_buffer: F,
        reserve: &dyn StorageReservation,
    ) -> Result<FlushableData<'a, '_, B>, EventWaitResult>
    where
        F: Fn(usize) -> Buffer<'a>,
    {
        // TODO(jfsulliv): Support reading out batches of flushable data.
        // TODO(jfsulliv): Support using a single shared transfer buffer for all pending data.
        let mut inner = self.inner.lock().unwrap();
        let size = self.data.size() as u64;

        if let Some(event) = inner.flush_event.as_ref() {
            return Err(event.wait_or_dropped());
        }
        inner.flush_event = Some(Event::new());

        let intervals = inner.intervals.remove_interval(&(0..u64::MAX)).unwrap();

        let mut ranges = vec![];
        for mut interval in intervals {
            assert!(interval.range.start % block_size == 0);
            assert!(interval.range.end % block_size == 0);
            if let CacheState::Dirty = &interval.state {
            } else {
                panic!("Unexpected interval {:?}", interval);
            }

            let len = std::cmp::min(size, interval.range.end) - interval.range.start;
            let mut buffer = allocate_buffer(len as usize);
            self.data.raw_read(interval.range.start, buffer.as_mut_slice());
            ranges.push(FlushableRange { range: interval.range.clone(), data: buffer });

            interval.state = CacheState::Flushing;
            inner.intervals.add_interval(&interval).unwrap();
        }

        let bytes_reserved = inner.bytes_reserved;
        let sync_bytes_reserved = inner.sync_bytes_reserved;

        assert!(inner.flushing_min_size.is_none());
        inner.flushing_min_size = Some(inner.min_size);
        if let Some(s) = inner.content_size {
            inner.min_size = s;
        }

        Ok(FlushableData {
            metadata: FlushableMetadata {
                content_size: inner.content_size.take(),
                creation_time: inner.creation_time.take(),
                modification_time: inner.modification_time.take(),
            },
            reservation: reserve.wrap_reservation(
                std::mem::take(&mut inner.bytes_reserved)
                    + std::mem::take(&mut inner.sync_bytes_reserved),
            ),
            bytes_reserved,
            sync_bytes_reserved,
            ranges,
            cache: Some(self),
        })
    }

    /// Indicates that a flush was successful.
    pub fn complete_flush<'a>(&self, mut flushed: FlushableData<'a, '_, B>) {
        flushed.cache = None; // We don't need to clean up on drop any more.
        self.complete_flush_inner(std::mem::take(&mut flushed.ranges), true, 0, 0);
    }

    fn complete_flush_inner<'a>(
        &self,
        ranges: Vec<FlushableRange<'a>>,
        completed: bool,
        returned_bytes: u64,
        returned_sync_bytes: u64,
    ) {
        let mut inner = self.inner.lock().unwrap();
        for range in ranges {
            let removed = inner
                .intervals
                .remove_matching_interval(&range.range, |i| i.is_flushing())
                .unwrap();
            if !completed {
                for mut interval in removed {
                    if let CacheState::Flushing = interval.state {
                        interval.state = CacheState::Dirty;
                        inner.intervals.add_interval(&interval).unwrap();
                    }
                }
            }
        }
        inner.bytes_reserved += returned_bytes;
        inner.sync_bytes_reserved += returned_sync_bytes;
        inner.flush_event = None;
        let flushing_min_size = inner.flushing_min_size.take().unwrap();
        // TODO(csuter): Add test for this case.
        if !completed && flushing_min_size < inner.min_size {
            inner.min_size = flushing_min_size;
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
        if let (None, None) = (creation_time.as_ref(), modification_time.as_ref()) {
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
        super::{FlushableData, StorageReservation, WritebackCache},
        crate::{
            object_store::{
                allocator::{Allocator, Reservation},
                data_buffer::MemDataBuffer,
                filesystem::Mutations,
                journal::checksum_list::ChecksumList,
                transaction::{Mutation, Transaction},
            },
            round::{round_down, round_up},
            testing::fake_object::{FakeObject, FakeObjectHandle},
        },
        anyhow::{anyhow, Error},
        async_trait::async_trait,
        fuchsia_async as fasync,
        futures::{channel::oneshot::channel, join},
        matches::assert_matches,
        std::{
            any::Any,
            ops::Range,
            sync::{
                atomic::{AtomicU64, Ordering},
                Arc, Mutex,
            },
            time::Duration,
        },
        storage_device::buffer_allocator::{BufferAllocator, MemBufferSource},
    };

    struct FakeReserverInner(Mutex<u64>);
    struct FakeReserver(Arc<FakeReserverInner>, u64);
    impl FakeReserver {
        fn new(amount: u64, granularity: u64) -> Self {
            Self(Arc::new(FakeReserverInner(Mutex::new(amount))), granularity)
        }
    }
    impl StorageReservation for FakeReserver {
        fn reserve_for_sync(&self) -> Result<Reservation, Error> {
            Ok(self.0.clone().reserve(0).unwrap())
        }
        fn reserve(&self, range: Range<u64>) -> Result<Reservation, Error> {
            let aligned_range =
                round_down(range.start, self.1)..round_up(range.end, self.1).unwrap();
            self.0
                .clone()
                .reserve(aligned_range.end - aligned_range.start)
                .ok_or(anyhow!("No Space"))
        }
        fn wrap_reservation(&self, amount: u64) -> Reservation {
            Reservation::new(self.0.clone(), amount)
        }
    }

    // TODO(jfsulliv): It's crude to implement Allocator here, but we need to clean all of this up
    // anyways when we make Reservation a VFS-level construct.
    #[async_trait]
    impl Allocator for FakeReserverInner {
        fn object_id(&self) -> u64 {
            unreachable!();
        }

        async fn allocate(
            &self,
            _transaction: &mut Transaction<'_>,
            _len: u64,
        ) -> Result<Range<u64>, Error> {
            unreachable!();
        }

        fn add_ref(&self, _transaction: &mut Transaction<'_>, _device_range: Range<u64>) {
            unreachable!();
        }

        async fn deallocate(
            &self,
            _transaction: &mut Transaction<'_>,
            _device_range: Range<u64>,
        ) -> Result<u64, Error> {
            unreachable!();
        }

        async fn mark_allocated(
            &self,
            _transaction: &mut Transaction<'_>,
            _device_range: Range<u64>,
        ) -> Result<(), Error> {
            unreachable!();
        }

        fn as_mutations(self: Arc<Self>) -> Arc<dyn Mutations> {
            unreachable!();
        }

        fn as_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync> {
            unreachable!();
        }

        async fn did_flush_device(&self, _flush_log_offset: u64) {
            unreachable!();
        }

        fn reserve(self: Arc<Self>, amount: u64) -> Option<Reservation> {
            {
                let mut inner = self.0.lock().unwrap();
                if *inner < amount {
                    return None;
                } else {
                    *inner -= amount;
                }
            }
            Some(Reservation::new(self, amount))
        }

        fn reserve_at_most(self: Arc<Self>, _amount: u64) -> Reservation {
            unreachable!();
        }

        fn release_reservation(&self, amount: u64) {
            let mut inner = self.0.lock().unwrap();
            *inner += amount;
        }

        fn get_allocated_bytes(&self) -> u64 {
            unreachable!();
        }

        fn get_used_bytes(&self) -> u64 {
            unreachable!();
        }

        async fn validate_mutation(
            &self,
            _journal_offset: u64,
            _mutation: &Mutation,
            _checksum_list: &mut ChecksumList,
        ) -> Result<bool, Error> {
            unreachable!()
        }
    }

    // Matcher for FlushableRange.
    #[derive(Debug)]
    struct ExpectedRange(u64, Vec<u8>);
    fn check_data_matches(
        actual: &FlushableData<'_, '_, MemDataBuffer>,
        expected: &[ExpectedRange],
    ) {
        if actual.ranges.len() != expected.len() {
            panic!("Expected {:?}, got {:?}", expected, actual);
        }
        let mut i = 0;
        while i < actual.ranges.len() {
            let expected = expected.get(i).unwrap();
            let actual = actual.ranges.get(i).unwrap();
            if expected.0 != actual.offset() || &expected.1[..] != actual.data.as_slice() {
                panic!("Expected {:?}, got {:?}", expected, actual);
            }
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
            let flushable = cache
                .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
                .ok()
                .unwrap();
            assert_eq!(flushable.bytes_reserved, 512);
            assert_eq!(flushable.ranges.len(), 1);
            cache.complete_flush(flushable);
        }

        cache
            .write_or_append(Some(0), &buffer[..], 512, &reserver, None, &source)
            .await
            .expect_err("write succeeded");

        // Ensure we can still reserve bytes, i.e. no reservations are leaked by the failed write.
        assert_matches!(reserver.reserve(0..512), Ok(_));

        // Ensure neither regions were marked dirty, i.e. the tree state wasn't affected.
        let flushable = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        check_data_matches(&flushable, &[]);
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
        cache.resize(1000, 512).await;
        assert_eq!(cache.content_size(), 1000);

        // The entire length of the file should be clean and contain the write, plus some zeroes.
        buffer.fill(0xaa);
        assert_eq!(cache.read(0, &mut buffer[..], &source).await.expect("read failed"), 1000);
        assert_eq!(&buffer[..1], vec![123u8]);
        assert_eq!(&buffer[1..1000], vec![0u8; 999]);

        // Only the first blocks should need a flush.
        let flushable = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        check_data_matches(
            &flushable,
            &[ExpectedRange(
                0,
                (|| {
                    let mut data = vec![0u8; 512];
                    data[0] = 123u8;
                    data
                })(),
            )],
        );
        assert_eq!(flushable.metadata.content_size, Some(1000));
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
        cache.resize(1000, 512).await;
        assert_eq!(cache.content_size(), 1000);

        // The resize should have truncated the pending writes.
        let flushable = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        assert_eq!(flushable.bytes_reserved, 8192);
        check_data_matches(&flushable, &[ExpectedRange(0, vec![123u8; 1000])]);
        assert_eq!(flushable.metadata.content_size, Some(1000));
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_no_data() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(8192)));
        let reserver = FakeReserver::new(1, 1);
        let cache = WritebackCache::new(MemDataBuffer::new(0));

        let data = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        assert_eq!(data.ranges.len(), 0);
        assert!(!data.metadata.has_updates());
        cache.complete_flush(data);
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
        let data = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        assert_eq!(data.bytes_reserved, 2048 + 512 + 1024);
        check_data_matches(
            &data,
            &[
                ExpectedRange(
                    0,
                    (|| {
                        let mut data = vec![0u8; 2560];
                        data[..2000].fill(123u8);
                        data[2048..2049].fill(45u8);
                        data
                    })(),
                ),
                ExpectedRange(
                    3584,
                    (|| {
                        let mut data = vec![0u8; 516];
                        data[416..466].fill(89u8);
                        data[466..516].fill(67u8);
                        data
                    })(),
                ),
            ],
        );
        assert!(data.metadata.has_updates());
        assert_eq!(data.metadata.content_size, Some(4100));
        cache.complete_flush(data);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_returns_reservation_on_abort() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(65536)));
        let reserver = FakeReserver::new(512, 512);
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        let buffer = vec![0u8; 1];
        cache
            .write_or_append(Some(0), &buffer[..], 512, &reserver, None, &source)
            .await
            .expect("write failed");
        {
            let data = cache
                .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
                .ok()
                .unwrap();
            assert_eq!(data.bytes_reserved, 512);
            // The reservation is held by |data|.
            reserver.reserve(0..1).expect_err("Reservation should be full");
        }
        // Dropping |data| should have given the reservation back to |cache|.  Taking the flushable
        // data again should still yield the reserved byte.
        let data = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        assert_eq!(data.bytes_reserved, 512);
        cache.complete_flush(data);
        // Now that the flush is complete, the underlying reservation should be free.
        reserver.reserve(0..1).expect("Reservation failed");
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
        let data = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        assert!(data.metadata.has_updates());
        assert_eq!(data.metadata.modification_time, Some(Duration::new(2, 0)));
        cache.complete_flush(data);
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

        let data = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        assert!(data.metadata.has_updates());
        assert_eq!(data.metadata.creation_time, Some(Duration::new(1, 0)));
        assert_eq!(data.metadata.modification_time, Some(Duration::new(2, 0)));
        cache.complete_flush(data);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_blocks_other_flush() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(8192)));
        let reserver = FakeReserver::new(65536, 512);
        let cache = WritebackCache::new(MemDataBuffer::new(0));

        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        let (send3, recv3) = channel();
        join!(
            async {
                let data = cache
                    .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
                    .ok()
                    .unwrap();
                send1.send(()).unwrap();
                recv2.await.unwrap();
                cache.complete_flush(data);
                send3.send(()).unwrap();
            },
            async {
                recv1.await.unwrap();
                let res = cache.take_flushable_data(
                    512,
                    |size| allocator.allocate_buffer(size),
                    &reserver,
                );
                send2.send(()).unwrap();
                if let Err(event) = res {
                    let _ = event.await;
                } else {
                    panic!("Should block");
                }
                recv3.await.unwrap();
                let res = cache.take_flushable_data(
                    512,
                    |size| allocator.allocate_buffer(size),
                    &reserver,
                );
                if let Ok(_) = res {
                } else {
                    panic!("Shoudn't block");
                }
            }
        );
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
                let data = cache
                    .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
                    .ok()
                    .unwrap();
                assert!(data.metadata.has_updates());
                assert_eq!(data.metadata.content_size, Some(512));
                check_data_matches(&data, &[ExpectedRange(0, vec![123u8; 512])]);
                send1.send(()).unwrap();
                recv2.await.unwrap();
            },
            async {
                recv1.await.unwrap();
                cache.resize(511, 512).await;
                send2.send(()).unwrap();
            },
        );
        let data = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        assert!(data.metadata.has_updates());
        assert_eq!(data.metadata.content_size, Some(511));
        check_data_matches(&data, &[ExpectedRange(0, vec![123u8; 511])]);
        cache.complete_flush(data);
        cache.cleanup(&reserver);
    }
}
