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

    // We keep track of the changed content size separately from `data` because the size managed by
    // `data` is modified under different locks.  When flushing, we need to capture a size that is
    // consistent with `intervals`.
    content_size: Option<u64>,

    // Number of dirty bytes so far, excluding those which are in the midst of a flush.  Every dirty
    // byte must have a byte reserved for it to be written back, plus some extra reservation made
    // for transaction overhead.
    dirty_bytes: u64,

    creation_time: Option<Duration>,

    modification_time: Option<Duration>,

    // Holds the minimum size the object has been since the last flush.
    min_size: u64,

    // If set, the minimum size as it was when take_flushable_data was called.
    flushing_min_size: Option<u64>,
}

impl Inner {
    fn complete_flush<'a>(
        &mut self,
        ranges: Vec<FlushableRange<'a>>,
        reservation: &allocator::Reservation,
        reserver: &dyn StorageReservation,
        completed: bool,
    ) {
        let dirty_bytes_before = self.dirty_bytes;
        for range in ranges {
            let removed =
                self.intervals.remove_matching_interval(&range.range, |i| i.is_flushing()).unwrap();
            for mut interval in removed {
                if !completed {
                    interval.state = CacheState::Dirty;
                    self.intervals.add_interval(&interval).unwrap();
                    self.dirty_bytes += interval.range.end - interval.range.start;
                }
            }
        }
        let flushing_min_size = self.flushing_min_size.take().unwrap();
        if !completed {
            if flushing_min_size < self.min_size {
                // TODO(csuter): Add test for this case.
                self.min_size = flushing_min_size;
            }
            // If we didn't complete the flush, take back whatever reservation we'll need for
            // another attempt.
            let reserved_before = reserver.reservation_needed(dirty_bytes_before);
            let needed_reservation = reserver.reservation_needed(self.dirty_bytes);
            let delta = needed_reservation.checked_sub(reserved_before).unwrap();
            assert_eq!(reservation.take_some(delta), delta);
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
    reservation: &'b allocator::Reservation,
    reserver: &'b dyn StorageReservation,
    ranges: Vec<FlushableRange<'a>>,
    flush_progress_offset: Option<u64>,
    cache: Option<&'b WritebackCache<B>>,
}

impl<B: DataBuffer> std::fmt::Debug for FlushableData<'_, '_, B> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("FlushableData")
            .field("metadata", &self.metadata)
            .field("reservation", &self.reservation)
            .field("ranges", &self.ranges)
            .finish()
    }
}

impl<'a, 'b, B: DataBuffer> FlushableData<'a, 'b, B> {
    pub fn ranges(&self) -> std::slice::Iter<'_, FlushableRange<'a>> {
        self.ranges.iter()
    }
    pub fn has_data(&self) -> bool {
        !self.ranges.is_empty()
    }
    pub fn has_metadata(&self) -> bool {
        self.metadata.has_updates()
    }
    /// Returns the progress offset for flushing.  If this is Some, there's more data to flush, and
    /// the caller should attempt to flush that data on a new iteration (passing in the offset).
    pub fn flush_progress_offset(&self) -> &Option<u64> {
        &self.flush_progress_offset
    }
    #[cfg(test)]
    fn dirty_bytes(&self) -> u64 {
        self.ranges.iter().map(|r| r.range.end - r.range.start).sum()
    }
}

impl<'a, 'b, B: DataBuffer> Drop for FlushableData<'a, 'b, B> {
    fn drop(&mut self) {
        if let Some(cache) = self.cache {
            {
                let mut inner = cache.inner.lock().unwrap();
                if self.metadata.creation_time.is_some() && inner.creation_time.is_none() {
                    inner.creation_time = self.metadata.creation_time;
                }
                if self.metadata.modification_time.is_some() && inner.modification_time.is_none() {
                    inner.modification_time = self.metadata.modification_time;
                }
            }
            let mut inner = cache.inner.lock().unwrap();
            inner.complete_flush(
                std::mem::take(&mut self.ranges),
                self.reservation,
                self.reserver,
                false,
            );
            if inner.content_size.is_none() {
                inner.content_size = self.metadata.content_size;
            }
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
                content_size: None,
                dirty_bytes: 0,
                creation_time: None,
                modification_time: None,
                min_size: data.size(),
                flushing_min_size: None,
            }),
            data,
        }
    }

    pub fn cleanup(&self, reserve: &dyn StorageReservation) {
        let mut inner = self.inner.lock().unwrap();
        let reserved = reserve.reservation_needed(inner.dirty_bytes);
        if reserved > 0 {
            // Let the RAII wrapper give back the reserved bytes.
            let _reservation = reserve.wrap_reservation(reserved);
        }
        inner.dirty_bytes = 0;
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

    pub fn min_size(&self) -> u64 {
        let inner = self.inner.lock().unwrap();
        inner.flushing_min_size.map(|s| std::cmp::min(s, inner.min_size)).unwrap_or(inner.min_size)
    }

    /// This is not thread-safe; the caller is responsible for making sure that only one thread is
    /// mutating the cache at any point in time.
    pub async fn resize(
        &self,
        size: u64,
        block_size: u64,
        reserver: &dyn StorageReservation,
    ) -> Result<(), Error> {
        {
            let mut inner = self.inner.lock().unwrap();
            let old_size = *inner.content_size.as_ref().unwrap_or(&(self.data.size() as u64));
            let aligned_size = round_up(size, block_size).unwrap();
            if size < old_size {
                let removed = inner.intervals.remove_interval(&(aligned_size..u64::MAX)).unwrap();
                let reserved_before = reserver.reservation_needed(inner.dirty_bytes);
                for interval in removed {
                    if let CacheState::Dirty = interval.state {
                        inner.dirty_bytes = inner
                            .dirty_bytes
                            .checked_sub(interval.range.end - interval.range.start)
                            .unwrap();
                    }
                }
                let reserved_after = reserver.reservation_needed(inner.dirty_bytes);
                if reserved_after < reserved_before {
                    // Give back whatever reservation we no longer need.
                    let _ = reserver.wrap_reservation(reserved_before - reserved_after);
                }
            }
            if size != old_size {
                inner.content_size = Some(size);
            }
            if size < inner.min_size {
                inner.min_size = size;
            }
        }
        // Resize the buffer after making changes to |inner| to avoid a race when truncating (where
        // we could have intervals that reference nonexistent parts of the buffer).
        // Note that there's a similar race for extending when we do things in this order, but we
        // don't think that is problematic since there are no intervals (since we're expanding).
        // If that turns out to be problematic, we'll have to atomically update both the buffer and
        // the interval tree at once.
        self.data.resize(size).await;
        Ok(())
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
        reserver: &dyn StorageReservation,
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
        let mut dirtied_bytes = 0;
        let reservation;
        {
            let inner = self.inner.lock().unwrap();

            let end = offset + buf.len() as u64;
            let aligned_range = round_down(offset, block_size)..round_up(end, block_size).unwrap();

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
            let reservation_needed = reserver.reservation_needed(inner.dirty_bytes + dirtied_bytes)
                - reserver.reservation_needed(inner.dirty_bytes);
            reservation = if reservation_needed > 0 {
                Some(reserver.reserve(reservation_needed)?)
            } else {
                None
            };
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
        if let Some(reservation) = reservation {
            // The cache implicitly owns the reservation in dirty_bytes.
            reservation.take();
        }
        inner.dirty_bytes += dirtied_bytes;
        inner.modification_time = current_time;
        Ok(())
    }

    /// Returns all data which can be flushed.  |allocate_buffer| is a callback which is used to
    /// allocate Buffer objects.  Each pending data region will be copied into a Buffer and returned
    /// to the caller in block-aligned ranges.  If there's already an ongoing flush, the caller
    /// receives an Event that resolves when the other flush is complete.  This is not thread-safe
    /// with respect to cache mutations; the caller must ensure that no changes can be made to the
    /// cache for the duration of this call.
    pub fn take_flushable_data<'a, 'b, F>(
        &'b self,
        block_size: u64,
        last_known_size: u64,
        allocate_buffer: F,
        reserver: &'b dyn StorageReservation,
        reservation: &'b allocator::Reservation,
        progress_offset: u64,
        limit: u64,
    ) -> FlushableData<'a, 'b, B>
    where
        F: Fn(usize) -> Buffer<'a>,
    {
        // TODO(jfsulliv): Support using a single shared transfer buffer for all pending data.
        let mut inner = self.inner.lock().unwrap();
        assert!(inner.flushing_min_size.is_none()); // Ensure a flush isn't currently in-progress.

        let size = *inner.content_size.as_ref().unwrap_or(&(self.data.size() as u64));
        let intervals = inner.intervals.remove_interval(&(0..u64::MAX)).unwrap();

        let mut bytes_to_flush = 0;
        let mut flush_progress_offset =
            if progress_offset > 0 { Some(progress_offset) } else { None };
        let mut ranges = vec![];
        let mut i = 0;
        while let Some(mut interval) = intervals.get(i).cloned() {
            assert!(interval.range.start % block_size == 0);
            assert!(interval.range.end % block_size == 0);
            if let CacheState::Dirty = &interval.state {
            } else {
                panic!("Unexpected interval {:?}", interval);
            }

            let interval_end = std::cmp::min(size, interval.range.end);
            let end = std::cmp::min(interval_end, interval.range.start + limit - bytes_to_flush);
            let len = end - interval.range.start;
            let mut buffer = allocate_buffer(len as usize);
            self.data.raw_read(interval.range.start, buffer.as_mut_slice());

            if end < interval_end {
                // If we only flushed part of the interval due to limit being hit,
                // re-insert the tail of the interval.
                let mut interval_tail = interval.clone();
                interval_tail.range.start = round_down(end, block_size);
                inner.intervals.add_interval(&interval_tail).unwrap();

                interval.state = CacheState::Flushing;
                interval.range.end = end;
                inner.intervals.add_interval(&interval).unwrap();
            } else {
                interval.state = CacheState::Flushing;
                inner.intervals.add_interval(&interval).unwrap();
            }
            ranges.push(FlushableRange { range: interval.range.clone(), data: buffer });

            flush_progress_offset =
                if i == intervals.len() - 1 && end == interval_end { None } else { Some(end) };
            i += 1;

            bytes_to_flush += interval.range.end - interval.range.start;
            if bytes_to_flush >= limit {
                break;
            }
        }
        for interval in &intervals[i..] {
            // Add back any intervals we skipped flushing.
            inner.intervals.add_interval(interval).unwrap();
        }
        inner.dirty_bytes = inner.dirty_bytes.checked_sub(bytes_to_flush).unwrap();

        let content_size = if let Some(offset) = flush_progress_offset {
            // If this is a partial flush, we can only update the content size as far as the data
            // we've flushed, and only update it if that size was an increase (otherwise this would
            // appear to be a false truncation).
            if offset > last_known_size {
                Some(offset)
            } else {
                None
            }
        } else {
            inner.content_size.take()
        };
        let metadata = FlushableMetadata {
            content_size,
            creation_time: std::mem::take(&mut inner.creation_time),
            modification_time: std::mem::take(&mut inner.modification_time),
        };

        inner.flushing_min_size = Some(inner.min_size);
        if let Some(s) = inner.content_size {
            inner.min_size = s;
        }

        let bytes_reserved = reserver.reservation_needed(bytes_to_flush);
        reservation.add(bytes_reserved);
        FlushableData {
            metadata,
            reservation: &reservation,
            reserver,
            ranges,
            flush_progress_offset,
            cache: Some(self),
        }
    }

    /// Indicates that a flush was successful.
    pub fn complete_flush<'a>(&self, mut flushed: FlushableData<'a, '_, B>) {
        flushed.cache = None; // We don't need to clean up on drop any more.
        self.inner.lock().unwrap().complete_flush(
            std::mem::take(&mut flushed.ranges),
            flushed.reservation,
            flushed.reserver,
            true,
        );
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
                caching_object_handle::FLUSH_BATCH_SIZE,
                data_buffer::MemDataBuffer,
                filesystem::Mutations,
                journal::checksum_list::ChecksumList,
                transaction::{Mutation, Transaction},
            },
            round::round_up,
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
                inner: Arc::new(FakeReserverInner(Mutex::new(amount))),
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
                amount
                    + round_up(amount, self.flush_limit).unwrap() / self.flush_limit
                        * self.sync_overhead
            } else {
                amount
            }
        }
        fn reserve(&self, amount: u64) -> Result<Reservation, Error> {
            self.inner
                .clone()
                .reserve(round_up(amount, self.granularity).unwrap())
                .ok_or(anyhow!("No Space"))
        }
        fn wrap_reservation(&self, amount: u64) -> Reservation {
            Reservation::new(self.inner.clone(), amount)
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
            panic!("Expected {} ranges, got {} ranges", expected.len(), actual.ranges.len());
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
            let reservation = reserver.wrap_reservation(0);
            let flushable = cache.take_flushable_data(
                512,
                0,
                |size| allocator.allocate_buffer(size),
                &reserver,
                &reservation,
                0,
                FLUSH_BATCH_SIZE,
            );
            assert_eq!(flushable.dirty_bytes(), 512);
            assert_eq!(flushable.ranges.len(), 1);
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
        let flushable = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            0,
            FLUSH_BATCH_SIZE,
        );
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
        cache.resize(1000, 512, &reserver).await.expect("resize failed");
        assert_eq!(cache.content_size(), 1000);

        // The entire length of the file should be clean and contain the write, plus some zeroes.
        buffer.fill(0xaa);
        assert_eq!(cache.read(0, &mut buffer[..], &source).await.expect("read failed"), 1000);
        assert_eq!(&buffer[..1], vec![123u8]);
        assert_eq!(&buffer[1..1000], vec![0u8; 999]);

        // Only the first blocks should need a flush.
        let reservation = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            0,
            FLUSH_BATCH_SIZE,
        );
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
        cache.resize(1000, 512, &reserver).await.expect("resize failed");
        assert_eq!(cache.content_size(), 1000);

        // The resize should have truncated the pending writes.
        let reservation = reserver.wrap_reservation(0);
        let flushable = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            0,
            FLUSH_BATCH_SIZE,
        );
        assert_eq!(flushable.dirty_bytes(), 1024);
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

        let reservation = reserver.wrap_reservation(0);
        let data = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            0,
            FLUSH_BATCH_SIZE,
        );
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
        let reservation = reserver.wrap_reservation(0);
        let data = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            0,
            FLUSH_BATCH_SIZE,
        );
        assert_eq!(data.dirty_bytes(), 2048 + 512 + 1024);
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
            let data = cache.take_flushable_data(
                512,
                0,
                |size| allocator.allocate_buffer(size),
                &reserver,
                &reservation,
                0,
                512,
            );
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
        let data = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation2,
            0,
            1024,
        );
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
        let data = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            0,
            FLUSH_BATCH_SIZE,
        );
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

        let reservation = reserver.wrap_reservation(0);
        let data = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            0,
            FLUSH_BATCH_SIZE,
        );
        assert!(data.metadata.has_updates());
        assert_eq!(data.metadata.creation_time, Some(Duration::new(1, 0)));
        assert_eq!(data.metadata.modification_time, Some(Duration::new(2, 0)));
        cache.complete_flush(data);
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
                let data = cache.take_flushable_data(
                    512,
                    0,
                    |size| allocator.allocate_buffer(size),
                    &reserver,
                    &reservation,
                    0,
                    FLUSH_BATCH_SIZE,
                );
                assert!(data.metadata.has_updates());
                assert_eq!(data.metadata.content_size, Some(512));
                check_data_matches(&data, &[ExpectedRange(0, vec![123u8; 512])]);
                send1.send(()).unwrap();
                recv2.await.unwrap();
            },
            async {
                recv1.await.unwrap();
                cache.resize(511, 512, &reserver).await.expect("resize failed");
                send2.send(()).unwrap();
            },
        );
        let reservation = reserver.wrap_reservation(0);
        let data = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            0,
            FLUSH_BATCH_SIZE,
        );
        assert!(data.metadata.has_updates());
        assert_eq!(data.metadata.content_size, Some(511));
        check_data_matches(&data, &[ExpectedRange(0, vec![123u8; 511])]);
        cache.complete_flush(data);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_partial_flushes() {
        const FLUSH_LIMIT: u64 = 65_536;
        const SYNC_OVERHEAD: u64 = 32_768;
        let allocator =
            BufferAllocator::new(512, Box::new(MemBufferSource::new(FLUSH_LIMIT as usize)));
        // The values here are carefully selected so that we will have just enough room in the
        // reservation pool to flush FLUSH_BATCH_SIZE + SYNC_OVERHEAD bytes of data (with room for two syncs).
        let reserver = FakeReserver::new_with_sync_overhead(
            FLUSH_LIMIT + 3 * SYNC_OVERHEAD,
            512,
            SYNC_OVERHEAD,
            FLUSH_LIMIT,
        );
        let cache = WritebackCache::new(MemDataBuffer::new(0));
        let source = FakeObjectHandle::new(Arc::new(FakeObject::new()));

        let buffer = vec![123u8; SYNC_OVERHEAD as usize];
        let mut dirty = 0;
        while dirty < FLUSH_LIMIT + SYNC_OVERHEAD {
            cache
                .write_or_append(
                    None,
                    &buffer[..],
                    512,
                    &reserver,
                    Some(Duration::new(1, 0)),
                    &source,
                )
                .await
                .expect("Write failed");
            dirty += buffer.len() as u64;
        }

        let reservation = reserver.wrap_reservation(0);
        {
            // If the partially flushed data is <= last_known_size, no new size should be flushed.
            let data = cache.take_flushable_data(
                512,
                FLUSH_LIMIT,
                |size| allocator.allocate_buffer(size),
                &reserver,
                &reservation,
                0,
                FLUSH_LIMIT,
            );
            assert_eq!(reservation.amount(), FLUSH_LIMIT + SYNC_OVERHEAD);
            assert_eq!(data.metadata.content_size, None);
            assert!(data.metadata.modification_time.is_some());
        }
        std::mem::drop(reservation);

        let reservation = reserver.wrap_reservation(0);
        let data = cache.take_flushable_data(
            512,
            0,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            0,
            FLUSH_LIMIT,
        );
        check_data_matches(&data, &[ExpectedRange(0, vec![123u8; FLUSH_LIMIT as usize])]);
        assert_eq!(data.metadata.content_size, Some(FLUSH_LIMIT));
        assert!(data.metadata.modification_time.is_some());
        assert_eq!(data.reservation.amount(), FLUSH_LIMIT + SYNC_OVERHEAD);
        cache.complete_flush(data);
        std::mem::drop(reservation);

        // Even if last_known_size is arbitrarily high, we should always flush the content size for
        // the last bit of data.
        let reservation = reserver.wrap_reservation(0);
        let data = cache.take_flushable_data(
            512,
            u64::MAX,
            |size| allocator.allocate_buffer(size),
            &reserver,
            &reservation,
            FLUSH_LIMIT,
            FLUSH_LIMIT,
        );
        check_data_matches(
            &data,
            &[ExpectedRange(FLUSH_LIMIT, vec![123u8; SYNC_OVERHEAD as usize])],
        );
        assert_eq!(data.metadata.content_size, Some(FLUSH_LIMIT + SYNC_OVERHEAD));
        assert_eq!(data.reservation.amount(), SYNC_OVERHEAD * 2);
        cache.complete_flush(data);

        // Now that the flushes are complete, we should be able to reserve the whole pool.
        std::mem::drop(reservation);
        reserver.reserve(FLUSH_LIMIT + 3 * SYNC_OVERHEAD).expect("Can't reserve the pool");

        cache.cleanup(&reserver);
    }
}
