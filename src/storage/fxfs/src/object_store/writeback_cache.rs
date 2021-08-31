// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{
        allocator::{self},
        data_buffer::{DataBuffer, NativeDataBuffer},
        store_object_handle::{round_down, round_up},
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
// - FlushingAndDirty (There is a pending write of the chunk which is being flushed by some task,
//   but there was also a new write in the meantime).
//
// For either of the Dirty states, the cache will have a backing storage reservation for that chunk.
// (While Flushing or FlushingAndDirty, there's also a reservation held by the flush task, so in
// FlushingAndDirty we will have two reservations).
//
// # Reading
//
// Reads from the cache return one of three states (see ReadResult):
// - A complete read, which means the entire range was in the cache,
// - A set of missing ranges, which need to be loaded from disk, or
// - An event to wait on, indicating some other reader is already loading a requested range.
//
// The caller is expected to loop until the read call returns ReadResult::Done.
//
// Reads are expanded to RAS-alignment.
//
// # Writing
//
// Aligned writes to the cache always complete immediately, and mark the affected ranges as dirty.
// Unaligned writes which are not in the cache need the head and tail to be read-modify-written; a
// similar protocol is used for loading the head and tail block as is used for reading
// (WriteResult::MissingRanges and WriteResult::Contended).
//
// Writes can go over ranges which are pending a read, in which case the overlapping part of the
// loaded result is simply discarded when the read completes.  Writes can also go over a range which
// is being flushed (see FlushingAndDirty).
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

#[derive(Clone, Debug)]
// There's an implicit state when there is no entry in the interval tree, which represents an
// absent range in the file (i.e. a hole which is not memory-resident).
enum CacheState {
    // The range is not memory-resident yet and some task is about to load it in.
    PendingRead(Event),
    // The range is memory-resident and has no writes to flush.  The range's data can be discarded
    // to free memory from the cache.
    Clean,
    // The range is memory-resident and needs to be flushed.  The cache should have enough bytes
    // reserved to write this data range.  The range's data should not be discarded.
    Dirty,
    // The range is memory-resident and is being flushed.  The range's data should not be discarded
    // until it is Clean when the flush completes.
    Flushing,
    // Like Flushing, but the range's data has also been written to during the flush, so once the
    // flush is complete, the range should become Dirty.  There will be two reservations for the
    // range, one in the WritebackCache and one which is passed out to whatever task is doing the
    // flush.
    FlushingAndDirty,
}

#[derive(Clone, Debug)]
struct CachedRange {
    pub range: Range<u64>,
    pub state: CacheState,
}

impl CachedRange {
    fn is_flushing(&self) -> bool {
        match &self.state {
            CacheState::Flushing | CacheState::FlushingAndDirty => true,
            _ => false,
        }
    }
    fn is_dirty(&self) -> bool {
        match &self.state {
            CacheState::Dirty | CacheState::Flushing | &CacheState::FlushingAndDirty => true,
            _ => false,
        }
    }
}

impl Interval<u64> for CachedRange {
    fn clone_with(&self, new_range: &Range<u64>) -> Self {
        Self { range: new_range.clone(), state: self.state.clone() }
    }
    fn merge(&self, other: &Self) -> Self {
        let state = match (&self.state, &other.state) {
            (CacheState::Clean, CacheState::Clean) => CacheState::Clean,
            (CacheState::Dirty, CacheState::Dirty) => CacheState::Dirty,
            (CacheState::Flushing, CacheState::Flushing) => CacheState::Flushing,
            (CacheState::FlushingAndDirty, CacheState::FlushingAndDirty) => {
                CacheState::FlushingAndDirty
            }
            _ => unreachable!(),
        };
        Self { range: self.range.merge(&other.range), state }
    }
    fn has_mergeable_properties(&self, other: &Self) -> bool {
        match (&self.state, &other.state) {
            (CacheState::Clean, CacheState::Clean) => true,
            (CacheState::Dirty, CacheState::Dirty) => true,
            (CacheState::Flushing, CacheState::Flushing) => true,
            (CacheState::FlushingAndDirty, CacheState::FlushingAndDirty) => true,
            // We can't merge two PendingReads since they might be occurring on two different tasks.
            _ => false,
        }
    }
    fn overrides(&self, other: &Self) -> bool {
        // This follows the state-machine transitions that we expect to perform.
        match (&self.state, &other.state) {
            // When we mark a range as dirty on write, we overwrite an existing range.
            (CacheState::Dirty, CacheState::Clean) => true,
            // Dirty writes should go over-top of a pending read, making the read a NOP.
            (CacheState::Dirty, CacheState::PendingRead(_)) => true,
            // Writing to a Flushing range results in it being FlushingAndDirty.
            (CacheState::FlushingAndDirty, CacheState::Flushing) => true,
            // When we mark a range as clean after flushing, we overwrite a flushing range.
            (CacheState::Clean, CacheState::Flushing) => true,
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
    size_changed: bool,
    creation_time: Option<Duration>,
    modification_time: Option<Duration>,
    flush_event: Option<Event>,
}

pub struct WritebackCache {
    inner: Mutex<Inner>,
    data: NativeDataBuffer,
}

pub struct MissingRange<'a> {
    range: Range<u64>,
    cache: Option<&'a WritebackCache>,
}

impl<'a> MissingRange<'a> {
    pub fn populate(mut self, buf: &[u8]) {
        let cache = std::mem::take(&mut self.cache);
        cache.unwrap().complete_read(self.range.clone(), Some(buf));
    }
    pub fn range(&self) -> &Range<u64> {
        &self.range
    }
}

impl<'a> Drop for MissingRange<'a> {
    fn drop(&mut self) {
        if let Some(cache) = self.cache {
            cache.complete_read(self.range.clone(), None);
        }
    }
}

impl std::fmt::Debug for MissingRange<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.range)
    }
}

pub enum ReadResult<'a> {
    // The read is complete and the data has been loaded into the provided buffer.
    // The number of actual content bytes read is returned.
    Done(u64),
    // The read did not complete because some of the intervals need to be loaded from disk.
    // Each range will span no more than CACHE_CHUNK_SIZE (i.e. if the start of the range is
    // unaligned, the size is strictly less than CACHE_CHUNK_SIZE).
    // The caller should load all of the ranges and install them with MissingRange::populate().
    MissingRanges(Vec<MissingRange<'a>>),
    // The read did not complete, and another reader is already working on one of the requested
    // intervals. The caller should wait until the EventWaitResult is completed (the status is not
    // relevant) before trying again.
    // TODO(jfsulliv): It should be possible for readers to make partial progress, so the return
    // value should yield a mix of ranges that can be worked on (MissingRanges) and ranges that
    // are contended (Contended).  Do the same for WriteResult.
    Contended(EventWaitResult),
}

impl std::fmt::Debug for ReadResult<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ReadResult::Done(bytes) => write!(f, "ReadResult::Done({:?})", bytes),
            ReadResult::MissingRanges(ranges) => {
                write!(f, "ReadResult::MissingRanges({:?})", ranges)
            }
            ReadResult::Contended(_) => write!(f, "ReadResult::Contended"),
        }
    }
}

pub enum WriteResult<'a> {
    // The write is complete.
    // The file's size after the write completed is returned.
    Done(u64),
    // The write did not complete because it was unaligned and the unaligned blocks were not present
    // in the cache.  See ReadResult::MissingRanges.
    MissingRanges(Vec<MissingRange<'a>>),
    // The write did not complete because it was unaligned and another reader is already working on
    // one of the intervals that needs to be read. The caller should wait until the EventWaitResult
    // is completed (the status is not relevant) before trying again.
    Contended(EventWaitResult),
}

impl std::fmt::Debug for WriteResult<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            WriteResult::Done(bytes) => write!(f, "WriteResult::Done({:?})", bytes),
            WriteResult::MissingRanges(ranges) => {
                write!(f, "WriteResult::MissingRanges({:?})", ranges)
            }
            WriteResult::Contended(_) => write!(f, "WriteResult::Contended"),
        }
    }
}

#[derive(Debug)]
pub struct FlushableMetadata {
    pub content_size: u64,
    pub content_size_changed: bool,
    /// Measured in time since the UNIX epoch in the UTC timezone.  Individual filesystems set their
    /// own granularities.
    pub creation_time: Option<Duration>,
    /// Measured in time since the UNIX epoch in the UTC timezone.  Individual filesystems set their
    /// own granularities.
    pub modification_time: Option<Duration>,
}

impl FlushableMetadata {
    pub fn has_updates(&self) -> bool {
        self.content_size_changed
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

pub struct FlushableData<'a, 'b> {
    pub metadata: FlushableMetadata,
    // TODO(jfsulliv): Create a VFS version of this so that we don't need to depend on fxfs code.
    // (That will require porting fxfs to use the VFS version.)
    reservation: allocator::Reservation,
    // Keep track of how many bytes |reservation| started with, in case the flush fails.
    bytes_reserved: u64,
    sync_bytes_reserved: u64,
    ranges: Vec<FlushableRange<'a>>,
    cache: Option<&'b WritebackCache>,
}

impl std::fmt::Debug for FlushableData<'_, '_> {
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

impl<'a, 'b> FlushableData<'a, 'b> {
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

impl<'a, 'b> Drop for FlushableData<'a, 'b> {
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

impl Inner {
    // TODO(jfsulliv): We should be able to give back some reserved bytes immediately after a
    // truncate.
    fn resize(&mut self, size: u64, block_size: u64, data: &NativeDataBuffer) {
        let old_size = data.size();
        let aligned_size = round_up(size, block_size).unwrap();
        if size < old_size {
            self.intervals.remove_interval(&(aligned_size..u64::MAX)).unwrap();
        } else if size > old_size {
            let aligned_old_size = round_up(old_size, block_size).unwrap();
            if aligned_old_size < aligned_size {
                // The rest of the newly extended hole is clean, since it's just zeroes.
                self.intervals
                    .add_interval(&CachedRange {
                        range: aligned_old_size..aligned_size,
                        state: CacheState::Clean,
                    })
                    .unwrap();
            }
        }
        if size != old_size {
            self.size_changed = true;
            data.resize(size);
        }
    }
}

impl Drop for WritebackCache {
    fn drop(&mut self) {
        let inner = self.inner.lock().unwrap();
        if inner.bytes_reserved > 0 || inner.sync_bytes_reserved > 0 {
            panic!("Dropping a WritebackCache without calling cleanup will leak reserved bytes");
        }
    }
}

impl WritebackCache {
    pub fn new(data: NativeDataBuffer) -> Self {
        Self {
            inner: Mutex::new(Inner {
                intervals: IntervalTree::new(),
                bytes_reserved: 0,
                sync_bytes_reserved: 0,
                size_changed: false,
                creation_time: None,
                modification_time: None,
                flush_event: None,
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

    pub fn resize(&self, size: u64, block_size: u64) {
        self.inner.lock().unwrap().resize(size, block_size, &self.data);
    }

    /// Read from the cache.  See ReadResult for details.
    /// TODO(jfsulliv): We should make reads non-atomic, i.e. make ReadResult return a progress
    /// offset rather than doing the entire copy in one go.
    pub fn read(&self, offset: u64, buf: &mut [u8], block_size: u64) -> ReadResult<'_> {
        // TODO(jfsulliv): We need to be careful about pending reads interacting with discardable
        // ranges, when we get to that. Discarding needs to only touch pages that are Clean. This
        // sequence in particular might cause problems:
        // 1. Read Start
        // 2. Write
        // 3. Flush
        // 4. At this point the page is not discardable; the read hasn't finished so the page can't
        //    be discarded until the read has finished.
        let mut inner = self.inner.lock().unwrap();
        let target_end = offset + buf.len() as u64;
        let end = std::cmp::min(target_end, self.data.size());
        let buf = if end < target_end {
            let len = end.saturating_sub(offset) as usize;
            &mut buf[..len]
        } else {
            buf
        };
        if buf.len() == 0 {
            return ReadResult::Done(0);
        }

        let aligned_end = round_up(end, block_size).unwrap();
        let readahead_range = round_down(offset, CACHE_READ_AHEAD_SIZE)
            ..std::cmp::min(round_up(aligned_end, CACHE_READ_AHEAD_SIZE).unwrap(), aligned_end);
        let intervals = inner.intervals.get_intervals(&readahead_range).unwrap();

        let mut current_offset = readahead_range.start;
        let mut missing_ranges: Vec<MissingRange<'_>> = vec![];
        let mut i = 0;
        while current_offset < readahead_range.end {
            let interval = intervals.get(i);
            let next_interval_start = if let Some(interval) = interval {
                interval.range.start
            } else {
                readahead_range.end
            };
            while current_offset < next_interval_start {
                // Read in any holes up to the next known range (or the end of the file).
                let chunk =
                    std::cmp::min(next_interval_start - current_offset, CACHE_READ_AHEAD_SIZE);
                let range = current_offset..current_offset + chunk;
                missing_ranges.push(MissingRange { range: range.clone(), cache: Some(self) });
                inner
                    .intervals
                    .add_interval(&CachedRange {
                        range,
                        state: CacheState::PendingRead(Event::new()),
                    })
                    .unwrap();
                current_offset += chunk;
            }
            if current_offset == readahead_range.end {
                break;
            }

            // There's an interval where we want to read from, but we need to check if it's a
            // pending read.
            let interval = interval.unwrap();
            assert!(interval.range.start % block_size == 0);
            assert!(interval.range.end % block_size == 0);
            if let CacheState::PendingRead(event) = &interval.state {
                // TODO(jfsulliv): It might be more efficient to block on all affected ranges,
                // rather than the first we find.
                return ReadResult::Contended(event.wait_or_dropped());
            }
            current_offset = std::cmp::min(interval.range.end, readahead_range.end);
            i += 1;
        }

        if !missing_ranges.is_empty() {
            ReadResult::MissingRanges(missing_ranges)
        } else {
            let len = buf.len() as u64;
            self.data.read(offset, buf);
            ReadResult::Done(len)
        }
    }

    // Populates a range that had to be loaded from disk.
    // If |data| is none, the read was aborted, so the range returns to being unmapped.
    fn complete_read(&self, range: Range<u64>, data: Option<&[u8]>) {
        let mut inner = self.inner.lock().unwrap();
        let ranges = inner
            .intervals
            .remove_matching_interval(&range, |r| {
                if let CacheState::PendingRead(_) = &r.state {
                    true
                } else {
                    false
                }
            })
            .unwrap();
        if let Some(buf) = data {
            // Only populate the parts that actually have a pending read (a writer may have come in
            // the meantime).
            for mut cached_range in ranges {
                let buf_offset = (cached_range.range.start - range.start) as usize;
                let len = std::cmp::min(
                    (cached_range.range.end - cached_range.range.start) as usize,
                    buf.len() - buf_offset,
                );
                self.data.write(cached_range.range.start, &buf[buf_offset..buf_offset + len]);
                cached_range.state = CacheState::Clean;
                inner.intervals.add_interval(&cached_range).unwrap();
            }
        }
    }

    /// Writes some new data into the cache, marking that data as Dirty.
    /// If |offset| is None, the data is appended to the end of the existing data.
    /// For unaligned writes, the unaligned head and tail block must already be in the cache. If
    /// they aren't, then the caller will receive a WriteResult::MissingRanges (or Contended) and
    /// must first load those blocks into the cache.
    /// If |current_time| is set (as a duration since the UNIX epoch in UTC, with whatever
    /// granularity the filesystem supports), the cached mtime will be set to this value.  If the
    /// filesystem doesn't support timestamps, it may simply set this to None.
    /// Returns the size after the write completes.
    pub fn write_or_append(
        &self,
        offset: Option<u64>,
        buf: &[u8],
        block_size: u64,
        reserve: &dyn StorageReservation,
        current_time: Option<Duration>,
    ) -> Result<WriteResult<'_>, Error> {
        // |inner| shouldn't be modified until we're at a part of the function where nothing can
        // fail (either before an early-return, or at the end of the function).
        // TODO(jfsulliv): Consider splitting this up into a prepare and commit function (the
        // prepare function would need to return a lock).  That would make this requirement
        // more explicit, but might also have other advantages, such as not needing the
        // StorageReservation trait nor the Reservation wrapper any more.  However, it involves
        // other complexity, mostly around the lock.
        let mut inner = self.inner.lock().unwrap();

        let offset = offset.unwrap_or(self.data.size() as u64);
        let end = offset + buf.len() as u64;
        let aligned_range = round_down(offset, block_size)..round_up(end, block_size).unwrap();

        let intervals = inner.intervals.get_intervals(&aligned_range).unwrap();

        // Determine if we need to request a read due to missing unaligned parts.
        let head_align = offset % block_size;
        let tail_align = if end < self.data.size() as u64 { end % block_size } else { 0 };
        let first_block_start = offset - head_align;
        let last_block_start = end - tail_align;
        let mut missing_ranges = vec![];
        loop {
            if head_align == 0 {
                break;
            }
            if first_block_start >= self.data.size() as u64 {
                break;
            }
            if let Some(interval) = intervals.get(0) {
                if interval.range.contains(&first_block_start) {
                    if let CacheState::PendingRead(event) = &interval.state {
                        return Ok(WriteResult::Contended(event.wait_or_dropped()));
                    }
                    break;
                }
            }
            missing_ranges.push(MissingRange {
                range: first_block_start..first_block_start + block_size,
                cache: Some(self),
            });
            break;
        }
        loop {
            if !missing_ranges.is_empty() && first_block_start == last_block_start {
                break;
            }
            if tail_align == 0 {
                break;
            }
            if last_block_start >= self.data.size() as u64 {
                break;
            }
            if let Some(interval) = intervals.get(intervals.len().saturating_sub(1)) {
                if interval.range.contains(&last_block_start) {
                    if let CacheState::PendingRead(event) = &interval.state {
                        return Ok(WriteResult::Contended(event.wait_or_dropped()));
                    }
                    break;
                }
            }
            missing_ranges.push(MissingRange {
                range: last_block_start..last_block_start + block_size,
                cache: Some(self),
            });
            break;
        }
        if !missing_ranges.is_empty() {
            for range in missing_ranges.iter() {
                inner
                    .intervals
                    .add_interval(&CachedRange {
                        range: range.range.clone(),
                        state: CacheState::PendingRead(Event::new()),
                    })
                    .unwrap();
            }
            return Ok(WriteResult::MissingRanges(missing_ranges));
        }

        let sync_reservation =
            if inner.sync_bytes_reserved == 0 { Some(reserve.reserve_for_sync()?) } else { None };

        // TODO(jfsulliv): This might be much simpler and more readable if we refactored
        // interval_tree to have an iterator interface.  See
        // https://fuchsia-review.googlesource.com/c/fuchsia/+/547024/comments/523de326_6e2b4766.
        let mut current_offset = aligned_range.start;
        let mut i = 0;
        let mut dirtied_intervals = vec![];
        let mut reservations = vec![];
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
                CacheState::Dirty | CacheState::FlushingAndDirty => {
                    // The range is already dirty and has a reservation.  Nothing needs to be done.
                }
                CacheState::Flushing => {
                    // The range is flushing.  Since this is a new write, we need to reserve space
                    // for it, and mark the range as FlushingAndDirty.
                    reservations.push(reserve.reserve(overlap_range.clone())?);
                    dirtied_intervals.push(CachedRange {
                        range: overlap_range,
                        state: CacheState::FlushingAndDirty,
                    })
                }
                CacheState::Clean | CacheState::PendingRead(_) => {
                    // The range is clean.  Reserve space for it, and mark the range as Dirty.
                    // Note that for a PendingRead, the reader will still read from disk and attempt
                    // to fill the range in, but complete_read won't overwrite the Dirty data, so
                    // that read is wasted. We could potentially signal the reader that they can
                    // abort the read here, although this is a small optimization.
                    reservations.push(reserve.reserve(overlap_range.clone())?);
                    dirtied_intervals
                        .push(CachedRange { range: overlap_range, state: CacheState::Dirty })
                }
            };
            current_offset = next_end;
            i += 1;
        }

        // After this point, we're committing changes, so nothing should fail.
        if offset + buf.len() as u64 > self.data.size() {
            inner.resize(offset + buf.len() as u64, block_size, &self.data);
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
        self.data.write(offset, buf);
        Ok(WriteResult::Done(self.data.size() as u64))
    }

    /// Returns all data which can be flushed.
    /// |allocate_buffer| is a callback which is used to allocate Buffer objects.  Each pending data
    /// region will be copied into a Buffer and returned to the caller in block-aligned ranges.
    /// If there's already an ongoing flush, the caller receives an Event that resolves when the
    /// other flush is complete.
    pub fn take_flushable_data<'a, F>(
        &self,
        block_size: u64,
        allocate_buffer: F,
        reserve: &dyn StorageReservation,
    ) -> Result<FlushableData<'a, '_>, EventWaitResult>
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

        let intervals =
            inner.intervals.remove_matching_interval(&(0..u64::MAX), |i| i.is_dirty()).unwrap();

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
            self.data.read(interval.range.start, buffer.as_mut_slice());
            ranges.push(FlushableRange { range: interval.range.clone(), data: buffer });

            interval.state = CacheState::Flushing;
            inner.intervals.add_interval(&interval).unwrap();
        }

        let bytes_reserved = inner.bytes_reserved;
        let sync_bytes_reserved = inner.sync_bytes_reserved;
        Ok(FlushableData {
            metadata: FlushableMetadata {
                content_size: self.data.size() as u64,
                content_size_changed: std::mem::take(&mut inner.size_changed),
                creation_time: std::mem::take(&mut inner.creation_time),
                modification_time: std::mem::take(&mut inner.modification_time),
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
    pub fn complete_flush<'a>(&self, mut flushed: FlushableData<'a, '_>) {
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
            for mut interval in removed {
                interval.state = loop {
                    if let CacheState::Flushing = interval.state {
                        if completed {
                            break CacheState::Clean;
                        }
                    }
                    break CacheState::Dirty;
                };
                inner.intervals.add_interval(&interval).unwrap();
            }
        }
        inner.bytes_reserved += returned_bytes;
        inner.sync_bytes_reserved += returned_sync_bytes;
        inner.flush_event = None;
    }

    /// Sets the cached timestamp values.  The filesystem should provide values which are truncated
    /// to the filesystem's maximum supported granularity.
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
    pub fn data_buffer(&self) -> &NativeDataBuffer {
        &self.data
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{FlushableData, ReadResult, StorageReservation, WriteResult, WritebackCache},
        crate::object_store::{
            allocator::{Allocator, Reservation},
            data_buffer::NativeDataBuffer,
            filesystem::Mutations,
            journal::checksum_list::ChecksumList,
            store_object_handle::{round_down, round_up},
            transaction::{Mutation, Transaction},
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
    fn check_data_matches(actual: &FlushableData<'_, '_>, expected: &[ExpectedRange]) {
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
    async fn test_read_absent() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(3000));
        let mut buffer = vec![0u8; 8192];

        let result = cache.read(0, &mut buffer[..4096], 512);
        match result {
            ReadResult::MissingRanges(mut ranges) => {
                assert_eq!(ranges.len(), 1);
                let range = ranges.pop().unwrap();
                assert_eq!(range.range(), &(0..3072));
                buffer.fill(123u8);
                range.populate(&buffer[0..3000]);
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        buffer.fill(0u8);
        let result = cache.read(0, &mut buffer[..4096], 512);
        match result {
            ReadResult::Done(bytes) => {
                assert_eq!(bytes, 3000);
                assert_eq!(buffer.as_slice()[..bytes as usize], vec![123u8; bytes as usize]);
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_past_eof() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(3000));
        let mut buffer = vec![0u8; 8192];

        let result = cache.read(3001, &mut buffer[..4096], 512);
        if let ReadResult::Done(bytes) = result {
            assert_eq!(bytes, 0);
        } else {
            panic!("Unexpected result {:?}", result);
        }
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reads_block() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(8192));
        let mut buffer1 = vec![0u8; 8192];
        let mut buffer2 = vec![0u8; 8192];
        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        join!(
            async {
                let result = cache.read(3000, &mut buffer1[..1000], 512);
                match result {
                    ReadResult::MissingRanges(mut ranges) => {
                        send1.send(()).unwrap();
                        recv2.await.unwrap(); // Wait until the other task has attempted to read.
                        assert_eq!(ranges.len(), 1);
                        let range = ranges.pop().unwrap();
                        assert_eq!(range.range(), &(0..4096));
                        buffer1.fill(123u8);
                        range.populate(&buffer1[..8192]);
                    }
                    _ => panic!("Unexpected result {:?}", result),
                }
                let result = cache.read(3000, &mut buffer1[..1000], 512);
                match result {
                    ReadResult::Done(bytes) => {
                        assert_eq!(bytes, 1000);
                    }
                    _ => panic!("Unexpected result {:?}", result),
                }
            },
            async {
                recv1.await.unwrap(); // Wait until the other task has claimed the range.
                let result = cache.read(0, &mut buffer2[..4096], 512);
                match result {
                    ReadResult::Contended(event) => {
                        send2.send(()).unwrap();
                        let _ = event.await;
                    }
                    _ => panic!("Unexpected result {:?}", result),
                }
                let result = cache.read(0, &mut buffer2[..4096], 512);
                match result {
                    ReadResult::Done(bytes) => {
                        assert_eq!(bytes, 4096);
                    }
                    _ => panic!("Unexpected result {:?}", result),
                }
            },
        );
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reads_block_unaligned_writes() {
        let reserver = FakeReserver::new(65536, 512);
        let cache = WritebackCache::new(NativeDataBuffer::new(8192));
        let mut buffer1 = vec![0u8; 8192];
        let buffer2 = vec![0u8; 8192];
        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        join!(
            async {
                let result = cache.read(0, &mut buffer1[..], 512);
                match result {
                    ReadResult::MissingRanges(mut ranges) => {
                        send1.send(()).unwrap();
                        recv2.await.unwrap(); // Wait until the other task has attempted to write.
                        assert_eq!(ranges.len(), 1);
                        let range = ranges.pop().unwrap();
                        buffer1.fill(123u8);
                        range.populate(&buffer1[..]);
                    }
                    _ => panic!("Unexpected result {:?}", result),
                }
                let result = cache.read(0, &mut buffer1[..], 512);
                match result {
                    ReadResult::Done(bytes) => {
                        assert_eq!(bytes, 8192);
                    }
                    _ => panic!("Unexpected result {:?}", result),
                }
            },
            async {
                // Wait until the other task has claimed the range.
                recv1.await.unwrap();
                // Aligned writes can go through.
                let result = cache
                    .write_or_append(Some(512), &buffer2[..512], 512, &reserver, None)
                    .expect("write failed");
                if let WriteResult::Done(bytes) = result {
                    assert_eq!(bytes, 8192);
                } else {
                    panic!("Unexpected result {:?}", result)
                };
                // Writes that extend the file can go through.
                let result = cache
                    .write_or_append(Some(8192), &buffer2[..1], 512, &reserver, None)
                    .expect("write failed");
                if let WriteResult::Done(bytes) = result {
                    assert_eq!(bytes, 8193);
                } else {
                    panic!("Unexpected result {:?}", result)
                };
                let result = cache
                    .write_or_append(None, &buffer2[..1], 512, &reserver, None)
                    .expect("write failed");
                if let WriteResult::Done(bytes) = result {
                    assert_eq!(bytes, 8194);
                } else {
                    panic!("Unexpected result {:?}", result)
                };
                // Writes that are unaligned at the head or tail should block.
                let mut result1 = cache
                    .write_or_append(Some(500), &buffer2[..524], 512, &reserver, None)
                    .expect("write failed");
                let mut result2 = cache
                    .write_or_append(Some(4096), &buffer2[..1], 512, &reserver, None)
                    .expect("write failed");
                match (&mut result1, &mut result2) {
                    (WriteResult::Contended(event1), WriteResult::Contended(event2)) => {
                        send2.send(()).unwrap();
                        let _ = event1.await;
                        let _ = event2.await;
                    }
                    _ => panic!("Unexpected results {:?} {:?}", result1, result2),
                }

                let result1 = cache
                    .write_or_append(Some(500), &buffer2[..524], 512, &reserver, None)
                    .expect("write failed");
                let result2 = cache
                    .write_or_append(Some(4096), &buffer2[..1], 512, &reserver, None)
                    .expect("write failed");
                match (&result1, &result2) {
                    (WriteResult::Done(_), WriteResult::Done(_)) => {}
                    _ => panic!("Unexpected results {:?} {:?}", result1, result2),
                }
            },
        );
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_aborted() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(3000));
        let mut buffer = vec![0u8; 8192];

        let result = cache.read(0, &mut buffer[..4096], 512);
        match result {
            ReadResult::MissingRanges(ranges) => {
                assert_eq!(ranges.len(), 1);
                // Drop ranges without populating the result.
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        // We should get the same result on the next call.
        let result = cache.read(0, &mut buffer[..4096], 512);
        match result {
            ReadResult::MissingRanges(ranges) => {
                assert_eq!(ranges.len(), 1);
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_read() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(0));
        let mut buffer = vec![0u8; 8192];

        buffer.fill(123u8);
        let result = cache
            .write_or_append(Some(0), &buffer[..3000], 512, &reserver, None)
            .expect("write failed");
        match result {
            WriteResult::Done(size) => {
                assert_eq!(size, 3000);
            }
            _ => panic!("Unexpected result {:?}", result),
        }

        buffer.fill(0u8);
        let result = cache.read(0, &mut buffer[..4096], 512);
        match result {
            ReadResult::Done(bytes) => {
                assert_eq!(bytes, 3000);
                assert_eq!(buffer.as_slice()[..bytes as usize], vec![123u8; bytes as usize]);
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_over_pending_read() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(16384)));
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(8192));
        let mut buffer = vec![0u8; 8192];

        let result = cache.read(0, &mut buffer[..], 512);
        match result {
            ReadResult::MissingRanges(mut ranges) => {
                assert_eq!(ranges.len(), 1);
                let range = ranges.pop().unwrap();
                assert_eq!(range.range(), &(0..8192));
                buffer.fill(45u8);
                let result2 = cache
                    .write_or_append(Some(0), &buffer[..4096], 512, &reserver, None)
                    .expect("Write failed");
                if let WriteResult::Done(size) = result2 {
                    assert_eq!(size, 8192);
                } else {
                    panic!("Unexpected result {:?}", result2)
                }
                buffer.fill(123u8);
                range.populate(&buffer[..]);
            }
            _ => panic!("Unexpected result {:?}", result),
        }

        // The cache should reflect the state after the write, discarding the pending read result
        // where they overlap.
        let result = cache.read(0, &mut buffer[..], 512);
        if let ReadResult::Done(bytes) = result {
            assert_eq!(bytes, 8192);
            assert_eq!(buffer.as_slice()[..4096], [45u8; 4096]);
            assert_eq!(buffer.as_slice()[4096..8192], [123u8; 4096]);
        }

        let data = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        check_data_matches(&data, &[ExpectedRange(0, vec![45u8; 4096])]);
        cache.complete_flush(data);

        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_append() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(0));
        let mut buffer = vec![0u8; 8192];

        buffer.fill(123u8);
        let result = cache
            .write_or_append(None, &buffer[..3000], 512, &reserver, None)
            .expect("write failed");
        match result {
            WriteResult::Done(size) => {
                assert_eq!(size, 3000);
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        buffer.fill(45u8);
        let result = cache
            .write_or_append(None, &buffer[..3000], 512, &reserver, None)
            .expect("write failed");
        match result {
            WriteResult::Done(size) => {
                assert_eq!(size, 6000);
            }
            _ => panic!("Unexpected result {:?}", result),
        }

        buffer.fill(0u8);
        assert_eq!(cache.content_size(), 6000);
        let result = cache.read(0, &mut buffer[..6000], 512);
        match result {
            ReadResult::Done(bytes) => {
                assert_eq!(bytes, 6000);
                assert_eq!(buffer.as_slice()[..3000], vec![123u8; 3000]);
                assert_eq!(buffer.as_slice()[3000..6000], vec![45u8; 3000]);
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_read_partially_paged_in() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(4096));
        let mut buffer = vec![0u8; 8192];

        buffer.fill(123u8);
        let result = cache
            .write_or_append(Some(4096), &buffer[..3000], 512, &reserver, None)
            .expect("write failed");
        match result {
            WriteResult::Done(size) => {
                assert_eq!(size, 7096);
            }
            _ => panic!("Unexpected result {:?}", result),
        }

        buffer.fill(0u8);
        let result = cache.read(0, &mut buffer[..], 512);
        match result {
            ReadResult::MissingRanges(mut ranges) => {
                assert_eq!(ranges.len(), 1);
                let missing_range = ranges.pop().unwrap();
                assert_eq!(missing_range.range(), &(0..4096));
                buffer.fill(45u8);
                let data = &buffer
                    [missing_range.range().start as usize..missing_range.range().end as usize];
                missing_range.populate(data);
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        let result = cache.read(0, &mut buffer[..], 512);
        match result {
            ReadResult::Done(bytes) => {
                assert_eq!(bytes, 7096);
                assert_eq!(buffer.as_slice()[..4096], vec![45u8; 4096]);
                assert_eq!(buffer.as_slice()[4096..7096], vec![123u8; 3000]);
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_read_sparse() {
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(0));
        let mut buffer = vec![0u8; 8192];

        buffer.fill(123u8);
        let result = cache
            .write_or_append(Some(4096), &buffer[..4096], 512, &reserver, None)
            .expect("write failed");
        match result {
            WriteResult::Done(size) => {
                assert_eq!(size, 8192);
            }
            _ => panic!("Unexpected result {:?}", result),
        }

        buffer.fill(45u8);
        let result = cache.read(0, &mut buffer[..4096], 512);
        match result {
            ReadResult::Done(bytes) => {
                assert_eq!(bytes, 4096);
            }
            _ => panic!("Unexpected result {:?}", result),
        }
        assert_eq!(buffer.as_slice()[..4096], [0u8; 4096]);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_reserving_bytes_fails() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(16384)));
        // We size the reserver so that only a one-block write can succeed.
        let reserver = FakeReserver::new(512, 512);
        let cache = WritebackCache::new(NativeDataBuffer::new(8192));

        let buffer = vec![0u8; 8192];
        // Create a clean region in the middle of the cache so that we split the write into two
        // dirty ranges.
        let result = cache
            .write_or_append(Some(512), &buffer[..512], 512, &reserver, None)
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
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
            .write_or_append(Some(0), &buffer[..], 512, &reserver, None)
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
        let cache = WritebackCache::new(NativeDataBuffer::new(0));

        let mut buffer = vec![0u8; 8192];
        buffer.fill(123u8);
        let result =
            cache.write_or_append(None, &buffer[..1], 512, &reserver, None).expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
        cache.resize(1000, 512);
        assert_eq!(cache.content_size(), 1000);

        // The entire length of the file should be clean and contain the write, plus some zeroes.
        buffer.fill(0xaa);
        let result = cache.read(0, &mut buffer[..], 512);
        if let ReadResult::Done(size) = result {
            assert_eq!(size, 1000);
            assert_eq!(buffer.as_slice()[..1], vec![123u8]);
            assert_eq!(buffer.as_slice()[1..1000], vec![0u8; 999]);
        } else {
            panic!("Unexpected result {:?}", result);
        }

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
        assert!(flushable.metadata.content_size_changed);
        assert_eq!(flushable.metadata.content_size, 1000);
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resize_shrink() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(16384)));
        let reserver = FakeReserver::new(8192, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(0));

        let mut buffer = vec![0u8; 8192];
        buffer.fill(123u8);
        let result =
            cache.write_or_append(None, &buffer[..], 512, &reserver, None).expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
        cache.resize(1000, 512);
        assert_eq!(cache.content_size(), 1000);

        // The resize should have truncated the pending writes.
        let flushable = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        assert_eq!(flushable.bytes_reserved, 8192);
        check_data_matches(&flushable, &[ExpectedRange(0, vec![123u8; 1000])]);
        assert!(flushable.metadata.content_size_changed);
        assert_eq!(flushable.metadata.content_size, 1000);
        cache.complete_flush(flushable);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_no_data() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(8192)));
        let reserver = FakeReserver::new(1, 1);
        let cache = WritebackCache::new(NativeDataBuffer::new(0));

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
        let cache = WritebackCache::new(NativeDataBuffer::new(0));

        let mut buffer = vec![0u8; 8192];

        buffer.fill(123u8);
        let result = cache
            .write_or_append(Some(0), &buffer[..2000], 512, &reserver, None)
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
        buffer.fill(45u8);
        let result = cache
            .write_or_append(Some(2048), &buffer[..1], 512, &reserver, None)
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
        buffer.fill(67u8);
        let result = cache
            .write_or_append(Some(4000), &buffer[..100], 512, &reserver, None)
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
        buffer.fill(89u8);
        let result = cache
            .write_or_append(Some(4000), &buffer[..50], 512, &reserver, None)
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
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
        assert_eq!(data.metadata.content_size, 4100);
        assert!(data.metadata.content_size_changed);
        cache.complete_flush(data);
        cache.cleanup(&reserver);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_flush_returns_reservation_on_abort() {
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(65536)));
        let reserver = FakeReserver::new(512, 512);
        let cache = WritebackCache::new(NativeDataBuffer::new(0));

        let buffer = vec![0u8; 1];
        let result = cache
            .write_or_append(Some(0), &buffer[..], 512, &reserver, None)
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
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
        let cache = WritebackCache::new(NativeDataBuffer::new(0));
        let secs = AtomicU64::new(1);
        let current_time = || Some(Duration::new(secs.fetch_add(1, Ordering::SeqCst), 0));

        let mut buffer = vec![0u8; 8192];

        buffer.fill(123u8);
        let result = cache
            .write_or_append(Some(0), &buffer[..1], 512, &reserver, current_time())
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
        buffer.fill(45u8);
        let result = cache
            .write_or_append(Some(0), &buffer[..1], 512, &reserver, current_time())
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
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
        let cache = WritebackCache::new(NativeDataBuffer::new(0));

        let buffer = vec![0u8; 8192];
        let result = cache
            .write_or_append(Some(0), &buffer[..1], 512, &reserver, Some(Duration::ZERO))
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }
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
        let cache = WritebackCache::new(NativeDataBuffer::new(0));

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
        let cache = WritebackCache::new(NativeDataBuffer::new(0));

        let mut buffer = vec![0u8; 512];
        buffer.fill(123u8);
        let result = cache
            .write_or_append(Some(0), &buffer[..], 512, &reserver, None)
            .expect("write failed");
        if let WriteResult::Done(_) = result {
        } else {
            panic!("Unexpected result {:?}", result);
        }

        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        join!(
            async {
                let data = cache
                    .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
                    .ok()
                    .unwrap();
                assert!(data.metadata.has_updates());
                assert_eq!(data.metadata.content_size, 512);
                check_data_matches(&data, &[ExpectedRange(0, vec![123u8; 512])]);
                send1.send(()).unwrap();
                recv2.await.unwrap();
            },
            async {
                recv1.await.unwrap();
                cache.resize(511, 512);
                send2.send(()).unwrap();
            },
        );
        let data = cache
            .take_flushable_data(512, |size| allocator.allocate_buffer(size), &reserver)
            .ok()
            .unwrap();
        assert!(data.metadata.has_updates());
        assert_eq!(data.metadata.content_size, 511);
        check_data_matches(&data, &[ExpectedRange(0, vec![123u8; 511])]);
        cache.complete_flush(data);
        cache.cleanup(&reserver);
    }
}
