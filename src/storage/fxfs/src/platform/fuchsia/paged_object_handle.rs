// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        data_buffer::DataBuffer,
        errors::FxfsError,
        filesystem::MAX_FILE_SIZE,
        log::*,
        object_handle::{GetProperties, ObjectHandle, ObjectProperties, ReadObjectHandle},
        object_store::{
            allocator::{Allocator, Reservation, ReservationOwner, SimpleAllocator},
            transaction::{
                AssocObj, Mutation, Options, Transaction, TRANSACTION_METADATA_MAX_AMOUNT,
            },
            AttributeKey, HandleOwner, ObjectKey, ObjectStore, ObjectValue, StoreObjectHandle,
            Timestamp,
        },
        platform::fuchsia::{
            file::FxFile,
            pager::Pager,
            pager::{PagerVmoStatsOptions, VmoDirtyRange},
            vmo_data_buffer::VmoDataBuffer,
            volume::FxVolume,
        },
        round::{how_many, round_up},
    },
    anyhow::{ensure, Context, Error},
    fuchsia_zircon as zx,
    scopeguard::ScopeGuard,
    std::{
        ops::{FnOnce, Range},
        sync::{Arc, Mutex},
    },
    storage_device::buffer::Buffer,
};

/// How much data each sync transaction in a given flush will cover.
const FLUSH_BATCH_SIZE: u64 = 524_288;

/// An expanding write will: mark a page as dirty, write to the page, and then update the content
/// size. If a flush is triggered during an expanding write then query_dirty_ranges may return pages
/// that have been marked dirty but are beyond the content size. Those extra pages can't be cleaned
/// during the flush and will have to be cleaned in a later flush. The initial flush will consume
/// the transaction metadata space that the extra pages were supposed to be part of leaving no
/// transaction metadata space for the extra pages in the next flush if no additional pages are
/// dirtied. `SPARE_SIZE` is extra metadata space that gets reserved be able to flush the extra
/// pages if this situation occurs.
const SPARE_SIZE: u64 = TRANSACTION_METADATA_MAX_AMOUNT;

pub struct PagedObjectHandle {
    inner: Mutex<Inner>,
    buffer: VmoDataBuffer,
    handle: StoreObjectHandle<FxVolume>,

    // TODO(fxbug.dev/102659): Use a LockKey and the LockManager instead.
    truncate_lock: futures::lock::Mutex<()>,
}

struct Inner {
    dirty_crtime: Option<Timestamp>,
    dirty_mtime: Option<Timestamp>,

    /// The number of pages that have been marked dirty by the kernel and need to be cleaned.
    dirty_page_count: u64,

    /// The amount of extra space currently reserved. See `SPARE_SIZE`.
    spare: u64,
}

/// Returns the amount of space that should be reserved to be able to flush `page_count` pages.
fn reservation_needed(page_count: u64) -> u64 {
    let page_size = zx::system_get_page_size() as u64;
    let pages_per_transaction = FLUSH_BATCH_SIZE / page_size;
    let transaction_count = how_many(page_count, pages_per_transaction);
    transaction_count * TRANSACTION_METADATA_MAX_AMOUNT + page_count * page_size
}

/// Splits the half-open range `[range.start, range.end)` into the ranges `[range.start,
/// split_point)` and `[split_point, range.end)`. If either of the new ranges would be empty then
/// `None` is returned in its place and `Some(range)` is returned for the other. `range` must not be
/// empty.
fn split_range(range: Range<u64>, split_point: u64) -> (Option<Range<u64>>, Option<Range<u64>>) {
    debug_assert!(!range.is_empty());
    if split_point <= range.start {
        (None, Some(range))
    } else if split_point >= range.end {
        (Some(range), None)
    } else {
        (Some(range.start..split_point), Some(split_point..range.end))
    }
}

/// Returns the number of pages spanned by `range`. `range` must be page aligned.
fn page_count(range: Range<u64>) -> u64 {
    let page_size = zx::system_get_page_size() as u64;
    debug_assert!(range.start <= range.end);
    debug_assert!(range.start % page_size == 0);
    debug_assert!(range.end % page_size == 0);
    (range.end - range.start) / page_size
}

/// Drops `guard` without running the callback.
fn dismiss_scopeguard<T, U: std::ops::FnOnce(T), S: scopeguard::Strategy>(
    guard: ScopeGuard<T, U, S>,
) {
    ScopeGuard::into_inner(guard);
}

impl Inner {
    fn reservation(&self) -> u64 {
        reservation_needed(self.dirty_page_count) + self.spare
    }

    /// Takes all the dirty pages and returns a (<count of dirty pages>, <reservation>).
    fn take(
        &mut self,
        allocator: Arc<SimpleAllocator>,
        store_object_id: u64,
    ) -> (u64, Reservation) {
        let reservation = allocator.reserve_at_most(Some(store_object_id), 0);
        reservation.add(self.reservation());
        self.spare = 0;
        (std::mem::take(&mut self.dirty_page_count), reservation)
    }

    /// Takes all the dirty pages and adds to the reservation.  Returns the number of dirty pages.
    fn move_to(&mut self, reservation: &Reservation) -> u64 {
        reservation.add(self.reservation());
        self.spare = 0;
        std::mem::take(&mut self.dirty_page_count)
    }

    // Put back some dirty pages taking from reservation as required.
    fn put_back(&mut self, count: u64, reservation: &Reservation) {
        if count > 0 {
            let before = self.reservation();
            self.dirty_page_count += count;
            let needed = reservation_needed(self.dirty_page_count);
            self.spare = std::cmp::min(reservation.amount() + before - needed, SPARE_SIZE);
            reservation.forget_some(needed + self.spare - before);
        }
    }
}

impl PagedObjectHandle {
    pub fn new(handle: StoreObjectHandle<FxVolume>) -> Self {
        let size = handle.get_size();
        Self {
            buffer: handle.owner().create_data_buffer(handle.object_id(), size),
            handle,
            inner: Mutex::new(Inner {
                dirty_crtime: None,
                dirty_mtime: None,
                dirty_page_count: 0,
                spare: 0,
            }),
            truncate_lock: futures::lock::Mutex::default(),
        }
    }

    pub fn owner(&self) -> &Arc<FxVolume> {
        self.handle.owner()
    }

    pub fn store(&self) -> &ObjectStore {
        self.handle.store()
    }

    pub fn vmo(&self) -> &zx::Vmo {
        self.buffer.vmo()
    }

    pub fn pager(&self) -> &Pager<FxFile> {
        self.owner().pager()
    }

    async fn new_transaction<'a>(
        &self,
        reservation: Option<&'a Reservation>,
    ) -> Result<Transaction<'a>, Error> {
        self.store()
            .filesystem()
            .new_transaction(
                &[],
                Options {
                    skip_journal_checks: false,
                    borrow_metadata_space: reservation.is_none(),
                    allocator_reservation: reservation,
                    ..Default::default()
                },
            )
            .await
    }

    fn allocator(&self) -> Arc<SimpleAllocator> {
        self.store().filesystem().allocator()
    }

    pub fn uncached_handle(&self) -> &StoreObjectHandle<FxVolume> {
        &self.handle
    }

    pub fn uncached_size(&self) -> u64 {
        self.handle.get_size()
    }

    pub async fn read_uncached(&self, range: std::ops::Range<u64>) -> Result<Buffer<'_>, Error> {
        let mut buffer = self.handle.allocate_buffer((range.end - range.start) as usize);
        let read = self.handle.read(range.start, buffer.as_mut()).await?;
        buffer.as_mut_slice()[read..].fill(0);
        Ok(buffer)
    }

    pub async fn mark_dirty(&self, page_range: Range<u64>) {
        let vmo = self.vmo();
        let (valid_pages, invalid_pages) = split_range(page_range, MAX_FILE_SIZE);
        if let Some(invalid_pages) = invalid_pages {
            self.pager().report_failure(vmo, invalid_pages, zx::Status::FILE_BIG);
        }
        let page_range = match valid_pages {
            Some(page_range) => page_range,
            None => return,
        };

        let mut inner = self.inner.lock().unwrap();
        let new_inner = Inner {
            dirty_page_count: inner.dirty_page_count + page_count(page_range.clone()),
            spare: SPARE_SIZE,
            ..*inner
        };
        let previous_reservation = inner.reservation();
        let new_reservation = new_inner.reservation();
        let reservation_delta = new_reservation - previous_reservation;
        // The reserved amount will never decrease but might the same.
        if reservation_delta > 0 {
            match self.allocator().reserve(Some(self.store().store_object_id()), reservation_delta)
            {
                Some(reservation) => {
                    // `PagedObjectHandle` doesn't hold onto a `Reservation` object for tracking
                    // reservations. The amount of space reserved by a `PagedObjectHandle` should
                    // always be derivable from `Inner`.
                    reservation.forget();
                }
                None => {
                    self.pager().report_failure(vmo, page_range, zx::Status::NO_SPACE);
                    return;
                }
            }
        }
        *inner = new_inner;
        self.pager().dirty_pages(vmo, page_range);
    }

    /// Queries the VMO to see if it was modified since the last time this function was called.
    fn was_file_modified_since_last_call(&self) -> Result<bool, zx::Status> {
        let stats =
            self.pager().query_vmo_stats(self.vmo(), PagerVmoStatsOptions::RESET_VMO_STATS)?;
        Ok(stats.was_vmo_modified())
    }

    /// Calls `query_dirty_ranges` to collect the ranges of the VMO that need to be flushed.
    fn collect_modified_ranges(&self) -> Result<Vec<VmoDirtyRange>, Error> {
        let mut modified_ranges: Vec<VmoDirtyRange> = Vec::new();
        let vmo = self.vmo();
        let pager = self.pager();

        // Whilst it's tempting to only collect ranges within 0..content_size, we need to collect
        // all the ranges so we can count up how many pages we're not going to flush, and then
        // make sure we return them so that we keep sufficient space reserved.
        let vmo_size = vmo.get_size()?;

        // `query_dirty_ranges` includes both dirty ranges and zero ranges. If there are no zero
        // pages and all of the dirty pages are consecutive then we'll receive only one range back
        // for all of the dirty pages. On the other end, there could be alternating zero and dirty
        // pages resulting in two times the number dirty pages in ranges. Also, since flushing
        // doesn't block mark_dirty, the number of ranges may change as they are being queried. 16
        // ranges was chosen as the initial buffer size to avoid wastefully using memory while also
        // being sufficient for common file usage patterns.
        let mut remaining = 16;
        let mut offset = 0;
        let mut total_received = 0;
        loop {
            modified_ranges.resize(total_received + remaining, VmoDirtyRange::default());
            let actual;
            (actual, remaining) = pager
                .query_dirty_ranges(vmo, offset..vmo_size, &mut modified_ranges[total_received..])
                .context("query_dirty_ranges failed")?;
            total_received += actual;
            // If fewer ranges were received than asked for then drop the extra allocated ranges.
            modified_ranges.resize(total_received, VmoDirtyRange::default());
            if actual == 0 {
                break;
            }
            let last = modified_ranges.last().unwrap();
            offset = last.range().end;
            if remaining == 0 {
                break;
            }
        }
        Ok(modified_ranges)
    }

    /// Queries for the ranges that need to be flushed and splits the ranges into batches that will
    /// each fit into a single transaction.
    fn collect_flush_batches(&self, content_size: u64) -> Result<FlushBatches, Error> {
        let page_aligned_content_size = round_up(content_size, zx::system_get_page_size()).unwrap();
        let modified_ranges =
            self.collect_modified_ranges().context("collect_modified_ranges failed")?;

        let mut flush_batches = FlushBatches::default();
        for modified_range in modified_ranges {
            // Skip ranges entirely past the content size.  It might be tempting to consider
            // flushing the range anyway and making up some value for content size, but that's not
            // safe because the pages will be zeroed before they are written to and it would be
            // wrong to write zeroed data.
            let (range, past_content_size_page_range) =
                split_range(modified_range.range(), page_aligned_content_size);

            if let Some(past_content_size_page_range) = past_content_size_page_range {
                if !modified_range.is_zero_range() {
                    // If the range is not zero then space should have been reserved for it that
                    // should continue to be reserved after this flush.
                    flush_batches.skip_range(past_content_size_page_range);
                }
            }

            if let Some(range) = range {
                flush_batches
                    .add_range(FlushRange { range, is_zero_range: modified_range.is_zero_range() });
            }
        }

        Ok(flush_batches)
    }

    async fn add_metadata_to_transaction<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        content_size: u64,
        previous_content_size: u64,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        if previous_content_size != content_size {
            transaction.add_with_object(
                self.store().store_object_id(),
                Mutation::replace_or_insert_object(
                    ObjectKey::attribute(
                        self.handle.object_id(),
                        self.handle.attribute_id(),
                        AttributeKey::Size,
                    ),
                    ObjectValue::attribute(content_size),
                ),
                AssocObj::Borrowed(&self.handle),
            );
        }
        self.handle
            .write_timestamps(transaction, crtime, mtime)
            .await
            .context("write_timestamps failed")?;
        Ok(())
    }

    /// Flushes only the metadata of the file by borrowing metadata space. If set, `flush_batch`
    /// must only contain ranges that need to be zeroed.
    async fn flush_metadata(
        &self,
        content_size: u64,
        previous_content_size: u64,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
        flush_batch: Option<&FlushBatch>,
    ) -> Result<(), Error> {
        let mut transaction = self.new_transaction(None).await?;
        self.add_metadata_to_transaction(
            &mut transaction,
            content_size,
            previous_content_size,
            crtime,
            mtime,
        )
        .await?;

        if let Some(batch) = flush_batch {
            assert!(batch.dirty_byte_count == 0);
            batch.writeback_begin(self.vmo(), self.pager());
            batch.add_to_transaction(&mut transaction, &self.buffer, &self.handle).await?;
        }
        transaction.commit().await.context("Failed to commit transaction")?;
        if let Some(batch) = flush_batch {
            batch.writeback_end(self.vmo(), self.pager());
        }
        Ok(())
    }

    async fn flush_data<T: FnOnce(u64), U: FnOnce(())>(
        &self,
        reservation: &Reservation,
        mut reservation_guard: ScopeGuard<u64, T>,
        timestamp_guard: ScopeGuard<(), U>,
        content_size: u64,
        previous_content_size: u64,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
        flush_batches: Vec<FlushBatch>,
    ) -> Result<(), Error> {
        let pager = self.pager();
        let vmo = self.vmo();

        let mut timestamp_guard = Some(timestamp_guard);
        let mut is_first_transaction = true;
        for batch in flush_batches {
            let mut transaction = self.new_transaction(Some(&reservation)).await?;
            if is_first_transaction {
                // TODO(fxbug.dev/102659): Advance the content size incrementally with each
                // transaction. Advancing the content size all the way to the end in the first
                // transactions has the potential to expose zeros at the end of the file if one of
                // the subsequent transactions fails.
                self.add_metadata_to_transaction(
                    &mut transaction,
                    content_size,
                    previous_content_size,
                    crtime,
                    mtime,
                )
                .await?;
            }
            batch.writeback_begin(vmo, pager);
            batch
                .add_to_transaction(&mut transaction, &self.buffer, &self.handle)
                .await
                .context("batch add_to_transaction failed")?;
            transaction.commit().await.context("Failed to commit transaction")?;
            *reservation_guard -= batch.page_count();
            if is_first_transaction {
                dismiss_scopeguard(timestamp_guard.take().unwrap());
            }
            batch.writeback_end(vmo, pager);
            is_first_transaction = false;
        }
        dismiss_scopeguard(reservation_guard);
        Ok(())
    }

    async fn flush_impl(&self) -> Result<(), Error> {
        // If the VMO is shrunk between getting the VMO's size and calling query_dirty_ranges or
        // reading the cached data then the flush could fail. This lock is held to prevent the file
        // from shrinking while it's being flushed. Technically, a client could call
        // GetBackingMemory to get a handle to the VMO and directly resize it to cause problems.
        //
        // TODO(fxbug.dev/96836): Update this comment when fxfs only hands out non-resizable
        // reference child VMOs.
        let _truncate_guard = self.truncate_lock.lock().await;

        // If the file had several dirty pages and then was truncated to before those dirty pages
        // then we'll still have space reserved that is no longer needed and should be released as
        // part of this flush.
        //
        // If `reservation` and `dirty_pages` were pulled out of `inner` after calling
        // `query_dirty_ranges` then we wouldn't be able to tell the difference between pages there
        // dirtied between those 2 operations and dirty pages that were made irrelevant by the
        // truncate.
        let (mut mtime, crtime, (dirty_pages, reservation)) = {
            let mut inner = self.inner.lock().unwrap();
            (
                inner.dirty_mtime.take(),
                inner.dirty_crtime.take(),
                inner.take(self.allocator(), self.store().store_object_id()),
            )
        };

        let mut reservation_guard = scopeguard::guard(dirty_pages, |dirty_pages| {
            self.inner.lock().unwrap().put_back(dirty_pages, &reservation);
        });

        let content_size = self.vmo().get_content_size().context("get_content_size failed")?;
        let previous_content_size = self.handle.get_size();
        let FlushBatches {
            batches: flush_batches,
            dirty_page_count: pages_to_flush,
            skipped_dirty_page_count: mut pages_not_flushed,
        } = self.collect_flush_batches(content_size)?;

        if self.was_file_modified_since_last_call()? {
            mtime = Some(Timestamp::now());
        }
        let timestamp_guard = scopeguard::guard((), |_| {
            let mut inner = self.inner.lock().unwrap();
            if inner.dirty_crtime.is_none() {
                inner.dirty_crtime = crtime;
            }
            if inner.dirty_mtime.is_none() {
                inner.dirty_mtime = mtime;
            }
        });

        // If pages were dirtied between getting the reservation and collecting the dirty ranges
        // then we might need to update the reservation.
        if pages_to_flush > dirty_pages {
            // This potentially takes more reservation than might be necessary.  We could perhaps
            // optimize this to take only what might be required.
            let new_dirty_pages = self.inner.lock().unwrap().move_to(&reservation);

            // Make sure we account for pages we might not flush to ensure we keep them reserved.
            pages_not_flushed = dirty_pages + new_dirty_pages - pages_to_flush;

            // Make sure we return the new dirty pages on failure.
            *reservation_guard += new_dirty_pages;

            assert!(
                reservation.amount() >= reservation_needed(pages_to_flush),
                "reservation: {}, needed: {}, dirty_pages: {}, pages_to_flush: {}",
                reservation.amount(),
                reservation_needed(pages_to_flush),
                dirty_pages,
                pages_to_flush
            );
        } else {
            // The reservation we have is sufficient for pages_to_flush, but it might not be enough
            // for pages_not_flushed as well.
            pages_not_flushed = std::cmp::min(dirty_pages - pages_to_flush, pages_not_flushed);
        }

        if pages_to_flush == 0 {
            self.flush_metadata(
                content_size,
                previous_content_size,
                crtime,
                mtime,
                flush_batches.first(),
            )
            .await?;
            dismiss_scopeguard(reservation_guard);
            dismiss_scopeguard(timestamp_guard);
        } else {
            self.flush_data(
                &reservation,
                reservation_guard,
                timestamp_guard,
                content_size,
                previous_content_size,
                crtime,
                mtime,
                flush_batches,
            )
            .await?
        }

        let mut inner = self.inner.lock().unwrap();
        inner.put_back(pages_not_flushed, &reservation);

        Ok(())
    }

    pub async fn flush(&self) -> Result<(), Error> {
        match self.flush_impl().await {
            Ok(()) => Ok(()),
            Err(error) => {
                error!(?error, "Failed to flush");
                Err(error)
            }
        }
    }

    async fn shrink_file(&self, new_size: u64) -> Result<(), Error> {
        let mut transaction = self.new_transaction(None).await?;

        let needs_trim = self.handle.shrink(&mut transaction, new_size).await?.0;

        // Truncating a file updates the file's mtime. `was_file_modified_since_last_call` is
        // called to clear the VMO modified stats so if the VMO was modified and there are no
        // more modifications between this truncate and the next flush then the next flush won't
        // also update the mtime.
        //
        // If the truncate transaction fails then a value needs to be picked to restore the
        // mtime to. If the VMO was not in the modified state then the mtime should be restored
        // to the previously cached mtime. If the VMO was in the modified state then we can't
        // restore that bit but it indicates that the mtime should be updated anyways so the
        // stored mtime is set to the current time.
        let (old_mtime, crtime) = {
            let mut inner = self.inner.lock().unwrap();
            (inner.dirty_mtime.take(), inner.dirty_crtime.take())
        };
        let was_file_modified = self.was_file_modified_since_last_call()?;
        let now = Some(Timestamp::now());
        let restore_mtime = if was_file_modified { now } else { old_mtime };
        let timestamp_guard = scopeguard::guard((), |_| {
            let mut inner = self.inner.lock().unwrap();
            if inner.dirty_crtime.is_none() {
                inner.dirty_crtime = crtime;
            }
            if inner.dirty_mtime.is_none() {
                inner.dirty_mtime = restore_mtime;
            }
        });

        self.handle
            .write_timestamps(&mut transaction, crtime, now)
            .await
            .context("write_timestamps failed")?;
        transaction.commit().await.context("Failed to commit transaction")?;
        dismiss_scopeguard(timestamp_guard);

        if needs_trim {
            self.store().trim(self.object_id()).await?;
        }

        Ok(())
    }

    pub async fn truncate(&self, new_size: u64) -> Result<(), Error> {
        ensure!(new_size <= MAX_FILE_SIZE, FxfsError::InvalidArgs);
        let _truncate_guard = self.truncate_lock.lock().await;

        self.buffer.resize(new_size).await;

        // Shrinking a large fragmented file can use lots of metadata space which isn't accounted
        // for in the dirty page reservations. To avoid running out of metadata space during a
        // flush, blocks are freed as part of the truncate. The entire truncate transaction can
        // borrow metadata space because it will result in more free space after a compaction.
        let block_size = self.handle.block_size();
        let new_block_aligned_size = round_up(new_size, block_size).unwrap();
        let previous_block_aligned_size = round_up(self.handle.get_size(), block_size).unwrap();
        if new_block_aligned_size < previous_block_aligned_size {
            if let Err(error) = self.shrink_file(new_size).await {
                warn!(?error, "Failed to shrink the file");
            }
        }

        // There may be reservations for dirty pages that are no longer relevant but the locations
        // of the pages is not tracked so they are assumed to still be dirty. This will get
        // rectified on the next flush.
        Ok(())
    }

    pub async fn write_timestamps<'a>(
        &'a self,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        if crtime.is_none() && mtime.is_none() {
            return Ok(());
        }
        let mut inner = self.inner.lock().unwrap();
        inner.dirty_crtime = crtime.or(inner.dirty_crtime);
        if let Some(mtime) = mtime {
            // Reset the VMO stats so modifications to the contents of the file between now and the
            // next flush can be detected. The next flush should contain the explicitly set mtime
            // unless the contents of the file are modified between now and the next flush.
            let _ = self.was_file_modified_since_last_call()?;
            inner.dirty_mtime = Some(mtime);
        }
        Ok(())
    }

    pub async fn get_properties(&self) -> Result<ObjectProperties, Error> {
        let mut props = self.handle.get_properties().await?;
        let mut inner = self.inner.lock().unwrap();
        if self.was_file_modified_since_last_call()? {
            inner.dirty_mtime = Some(Timestamp::now());
        }
        props.allocated_size += inner.dirty_page_count * zx::system_get_page_size() as u64;
        props.data_attribute_size = self.buffer.size();
        props.creation_time = inner.dirty_crtime.as_ref().unwrap_or(&props.creation_time).clone();
        props.modification_time =
            inner.dirty_mtime.as_ref().unwrap_or(&props.modification_time).clone();
        Ok(props)
    }
}

impl Drop for PagedObjectHandle {
    fn drop(&mut self) {
        let inner = self.inner.lock().unwrap();
        let reservation = inner.reservation();
        if reservation > 0 {
            self.allocator().release_reservation(Some(self.store().store_object_id()), reservation);
        }
    }
}

impl ObjectHandle for PagedObjectHandle {
    fn set_trace(&self, v: bool) {
        self.handle.set_trace(v);
    }
    fn object_id(&self) -> u64 {
        self.handle.object_id()
    }
    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.handle.allocate_buffer(size)
    }
    fn block_size(&self) -> u64 {
        self.handle.block_size()
    }
    fn get_size(&self) -> u64 {
        self.buffer.size()
    }
}

#[derive(Default, Debug)]
struct FlushBatches {
    batches: Vec<FlushBatch>,

    /// The number of dirty pages spanned by `batches`, excluding zero ranges.
    dirty_page_count: u64,

    /// The number of pages that were marked dirty but are not included in `batches` because they
    /// don't need to be flushed. These are pages that were beyond the VMO's content size.
    skipped_dirty_page_count: u64,
}

impl FlushBatches {
    fn add_range(&mut self, range: FlushRange) {
        if self.batches.is_empty() {
            self.batches.push(FlushBatch::default());
        }
        if !range.is_zero_range {
            self.dirty_page_count += range.page_count();
        }
        let mut remaining = self.batches.last_mut().unwrap().add_range(range);
        while let Some(range) = remaining {
            let mut batch = FlushBatch::default();
            remaining = batch.add_range(range);
            self.batches.push(batch);
        }
    }

    fn skip_range(&mut self, range: Range<u64>) {
        self.skipped_dirty_page_count += page_count(range);
    }
}

#[derive(Default, Debug, PartialEq)]
struct FlushBatch {
    /// The ranges to be flushed in this batch.
    ranges: Vec<FlushRange>,

    /// The number of bytes spanned by `ranges`, excluding zero ranges.
    dirty_byte_count: u64,
}

impl FlushBatch {
    /// Adds `range` to this batch. If `range` doesn't entirely fit into this batch then the
    /// remaining part of the range is returned.
    fn add_range(&mut self, range: FlushRange) -> Option<FlushRange> {
        debug_assert!(range.range.start >= self.ranges.last().map_or(0, |r| r.range.end));
        if range.is_zero_range {
            self.ranges.push(range);
            return None;
        }

        let split_point = range.range.start + (FLUSH_BATCH_SIZE - self.dirty_byte_count);
        let (range, remaining) = split_range(range.range, split_point);

        if let Some(range) = range {
            let range = FlushRange { range, is_zero_range: false };
            self.dirty_byte_count += range.len();
            self.ranges.push(range);
        }

        remaining.map(|range| FlushRange { range, is_zero_range: false })
    }

    fn page_count(&self) -> u64 {
        how_many(self.dirty_byte_count, zx::system_get_page_size())
    }

    fn writeback_begin(&self, vmo: &zx::Vmo, pager: &Pager<FxFile>) {
        for range in &self.ranges {
            pager.writeback_begin(vmo, range.range.clone());
        }
    }

    fn writeback_end(&self, vmo: &zx::Vmo, pager: &Pager<FxFile>) {
        for range in &self.ranges {
            pager.writeback_end(vmo, range.range.clone());
        }
    }

    async fn add_to_transaction<'a>(
        &self,
        transaction: &mut Transaction<'a>,
        vmo_data_buffer: &VmoDataBuffer,
        handle: &'a StoreObjectHandle<FxVolume>,
    ) -> Result<(), Error> {
        for range in &self.ranges {
            if range.is_zero_range {
                handle
                    .zero(transaction, range.range.clone())
                    .await
                    .context("zeroing a range failed")?;
            }
        }

        if self.dirty_byte_count > 0 {
            let mut buffer = handle.allocate_buffer(self.dirty_byte_count.try_into().unwrap());
            let mut slice = buffer.as_mut_slice();

            let mut dirty_ranges = Vec::new();
            for range in &self.ranges {
                if range.is_zero_range {
                    continue;
                }
                let range = range.range.clone();
                let (head, tail) =
                    slice.split_at_mut((range.end - range.start).try_into().unwrap());
                // TODO(fxb/88676): Zero out the portion of the block beyond the content size.
                vmo_data_buffer.raw_read(range.start, head);
                slice = tail;
                dirty_ranges.push(range);
            }
            handle
                .multi_write(transaction, &dirty_ranges, buffer.as_mut())
                .await
                .context("multi_write failed")?;
        }

        Ok(())
    }
}

#[derive(Debug, PartialEq, Clone)]
struct FlushRange {
    range: Range<u64>,
    is_zero_range: bool,
}

impl FlushRange {
    fn len(&self) -> u64 {
        self.range.end - self.range.start
    }

    fn page_count(&self) -> u64 {
        how_many(self.range.end - self.range.start, zx::system_get_page_size())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::platform::fuchsia::testing::{close_file_checked, open_file_checked, TestFixture},
        fidl_fuchsia_io as fio, fuchsia_async as fasync,
        fuchsia_fs::file,
        fuchsia_zircon as zx,
        std::{sync::atomic::Ordering, time::Duration},
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const BLOCK_SIZE: u32 = 512;
    const BLOCK_COUNT: u64 = 16384;
    const FILE_NAME: &str = "file";
    const ONE_DAY: u64 = Duration::from_secs(60 * 60 * 24).as_nanos() as u64;

    async fn get_attrs_checked(file: &fio::FileProxy) -> fio::NodeAttributes {
        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        zx::Status::ok(status).expect("get_attr failed");
        attrs
    }

    async fn set_attrs_checked(file: &fio::FileProxy, crtime: Option<u64>, mtime: Option<u64>) {
        let mut attributes = fio::NodeAttributes {
            mode: 0,
            id: 0,
            content_size: 0,
            storage_size: 0,
            link_count: 0,
            creation_time: crtime.unwrap_or(0),
            modification_time: mtime.unwrap_or(0),
        };

        let mut mask = fio::NodeAttributeFlags::empty();
        if crtime.is_some() {
            mask |= fio::NodeAttributeFlags::CREATION_TIME;
        }
        if mtime.is_some() {
            mask |= fio::NodeAttributeFlags::MODIFICATION_TIME;
        }

        let status = file.set_attr(mask, &mut attributes).await.expect("FIDL call failed");
        zx::Status::ok(status).expect("set_attr failed");
    }

    #[fasync::run(10, test)]
    async fn test_large_flush_requiring_multiple_transactions() {
        let device = FakeDevice::new(BLOCK_COUNT, BLOCK_SIZE);
        let fixture = TestFixture::open(DeviceHolder::new(device), true, false).await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            FILE_NAME,
        )
        .await;
        let info = file.describe().await.unwrap();
        let stream: zx::Stream = info.stream.unwrap();

        // Touch enough pages that 3 transaction will be required.
        let page_size = zx::system_get_page_size() as u64;
        let write_count: u64 = (FLUSH_BATCH_SIZE / page_size) * 2 + 10;
        for i in 0..write_count {
            stream
                .writev_at(zx::StreamWriteOptions::empty(), i * page_size, &[&[0, 1, 2, 3, 4]])
                .expect("write should succeed");
        }

        file.sync().await.unwrap().unwrap();

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_multi_transaction_flush_with_failing_middle_transaction() {
        let mut device = FakeDevice::new(BLOCK_COUNT, BLOCK_SIZE);
        let fail_device_after = Arc::new(std::sync::atomic::AtomicI64::new(i64::MAX));
        device.set_op_callback({
            let fail_device_after = fail_device_after.clone();
            move |_| {
                if fail_device_after.fetch_sub(1, Ordering::Relaxed) < 1 {
                    Err(zx::Status::IO.into())
                } else {
                    Ok(())
                }
            }
        });
        let fixture = TestFixture::open(DeviceHolder::new(device), true, false).await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            FILE_NAME,
        )
        .await;

        let info = file.describe().await.unwrap();
        let stream: zx::Stream = info.stream.unwrap();
        // Touch enough pages that 3 transaction will be required.
        let page_size = zx::system_get_page_size() as u64;
        let write_count: u64 = (FLUSH_BATCH_SIZE / page_size) * 2 + 10;
        for i in 0..write_count {
            stream
                .writev_at(zx::StreamWriteOptions::empty(), i * page_size, &[&i.to_le_bytes()])
                .expect("write should succeed");
        }

        // Succeed the multi_write call from the first transaction and fail the multi_write call
        // from the second transaction. The metadata from all of the transactions doesn't get
        // written to disk until the journal is synced which happens in FxFile::sync after all of
        // the multi_writes.
        fail_device_after.store(1, Ordering::Relaxed);
        file.sync().await.unwrap().expect_err("sync should fail");
        fail_device_after.store(i64::MAX, Ordering::Relaxed);

        // This sync will panic if the allocator reservations intended for the second or third
        // transactions weren't retained or the pages in the first transaction weren't properly
        // cleaned.
        file.sync().await.unwrap().expect("sync should succeed");

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_writeback_begin_and_end_are_called_correctly() {
        let device = FakeDevice::new(BLOCK_COUNT, BLOCK_SIZE);
        let fixture = TestFixture::open(DeviceHolder::new(device), true, false).await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            FILE_NAME,
        )
        .await;
        let info = file.describe().await.expect("describe failed");
        let stream: zx::Stream = info.stream.unwrap();

        let page_size = zx::system_get_page_size() as u64;
        // Dirty lots of pages so multiple transactions are required.
        let write_count: u64 = (FLUSH_BATCH_SIZE / page_size) * 2 + 10;
        for i in 0..(write_count * 2) {
            stream
                .writev_at(zx::StreamWriteOptions::empty(), i * page_size, &[&[0, 1, 2, 3, 4]])
                .unwrap();
        }
        // Sync the file to mark all of pages as clean.
        file.sync().await.unwrap().unwrap();
        // Set the file size to 0 to mark all of the cleaned pages as zero pages.
        file.resize(0).await.unwrap().unwrap();
        // Write to every other page to force alternating zero and dirty pages.
        for i in 0..write_count {
            stream
                .writev_at(zx::StreamWriteOptions::empty(), i * page_size * 2, &[&[0, 1, 2, 3, 4]])
                .unwrap();
        }
        // Sync to mark everything as clean again.
        file.sync().await.unwrap().unwrap();

        // Touch a single page so another flush is required.
        stream.writev_at(zx::StreamWriteOptions::empty(), 0, &[&[0, 1, 2, 3, 4]]).unwrap();

        // If writeback_begin and writeback_end weren't called in the correct order in the previous
        // sync then not all of the pages will have been marked clean. If not all of the pages were
        // cleaned then this sync will panic because there won't be enough reserved space to clean
        // the pages that weren't properly cleaned in the previous sync.
        file.sync().await.unwrap().unwrap();

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_writing_overrides_set_mtime() {
        let device = FakeDevice::new(8192, 512);
        let fixture = TestFixture::open(DeviceHolder::new(device), true, false).await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            FILE_NAME,
        )
        .await;

        let initial_time = get_attrs_checked(&file).await.modification_time;
        // Advance the mtime by a large amount that should be reachable by the test.
        set_attrs_checked(&file, None, Some(initial_time + ONE_DAY)).await;

        let updated_time = get_attrs_checked(&file).await.modification_time;
        assert!(updated_time > initial_time);

        file::write(&file, &[1, 2, 3, 4]).await.expect("write failed");

        // Writing to the file after advancing the mtime will bring the mtime back to the current
        // time.
        let current_mtime = get_attrs_checked(&file).await.modification_time;
        assert!(current_mtime < updated_time);

        file.sync().await.unwrap().unwrap();
        let synced_mtime = get_attrs_checked(&file).await.modification_time;
        assert_eq!(synced_mtime, current_mtime);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_flushing_after_get_attr_does_not_change_mtime() {
        let device = FakeDevice::new(8192, 512);
        let fixture = TestFixture::open(DeviceHolder::new(device), true, false).await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            FILE_NAME,
        )
        .await;

        file.write(&[1, 2, 3, 4])
            .await
            .expect("FIDL call failed")
            .map_err(zx::Status::from_raw)
            .expect("write failed");

        let first_mtime = get_attrs_checked(&file).await.modification_time;

        // The contents of the file haven't changed since get_attr was called so the flushed mtime
        // should be the same as the mtime returned from the get_attr call.
        file.sync().await.unwrap().unwrap();
        let flushed_mtime = get_attrs_checked(&file).await.modification_time;
        assert_eq!(flushed_mtime, first_mtime);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_timestamps_are_preserved_across_flush_failures() {
        let mut device = FakeDevice::new(BLOCK_COUNT, BLOCK_SIZE);
        let fail_device = Arc::new(std::sync::atomic::AtomicBool::new(false));
        device.set_op_callback({
            let fail_device = fail_device.clone();
            move |_| {
                if fail_device.load(Ordering::Relaxed) {
                    Err(zx::Status::IO.into())
                } else {
                    Ok(())
                }
            }
        });
        let fixture = TestFixture::open(DeviceHolder::new(device), true, false).await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            FILE_NAME,
        )
        .await;
        file::write(&file, [1, 2, 3, 4]).await.unwrap();

        let attrs = get_attrs_checked(&file).await;
        let future = attrs.creation_time + ONE_DAY;
        set_attrs_checked(&file, Some(future), Some(future)).await;

        fail_device.store(true, Ordering::Relaxed);
        file.sync().await.unwrap().expect_err("sync should fail");
        fail_device.store(false, Ordering::Relaxed);

        let attrs = get_attrs_checked(&file).await;
        assert_eq!(attrs.creation_time, future);
        assert_eq!(attrs.modification_time, future);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_max_file_size() {
        let device = FakeDevice::new(BLOCK_COUNT, BLOCK_SIZE);
        let fixture = TestFixture::open(DeviceHolder::new(device), true, false).await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            FILE_NAME,
        )
        .await;
        let info = file.describe().await.unwrap();
        let stream: zx::Stream = info.stream.unwrap();

        stream
            .writev_at(zx::StreamWriteOptions::empty(), MAX_FILE_SIZE - 1, &[&[1]])
            .expect("write should succeed");
        stream
            .writev_at(zx::StreamWriteOptions::empty(), MAX_FILE_SIZE, &[&[1]])
            .expect_err("write should fail");
        assert_eq!(get_attrs_checked(&file).await.content_size, MAX_FILE_SIZE);

        file.resize(MAX_FILE_SIZE).await.unwrap().expect("resize should succeed");
        file.resize(MAX_FILE_SIZE + 1).await.unwrap().expect_err("resize should fail");

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[test]
    fn test_split_range() {
        assert_eq!(split_range(10..20, 0), (None, Some(10..20)));
        assert_eq!(split_range(10..20, 9), (None, Some(10..20)));
        assert_eq!(split_range(10..20, 10), (None, Some(10..20)));
        assert_eq!(split_range(10..20, 11), (Some(10..11), Some(11..20)));
        assert_eq!(split_range(10..20, 15), (Some(10..15), Some(15..20)));
        assert_eq!(split_range(10..20, 19), (Some(10..19), Some(19..20)));
        assert_eq!(split_range(10..20, 20), (Some(10..20), None));
        assert_eq!(split_range(10..20, 25), (Some(10..20), None));
    }

    #[test]
    fn test_reservation_needed() {
        let page_size = zx::system_get_page_size() as u64;
        assert_eq!(FLUSH_BATCH_SIZE / page_size, 128);

        assert_eq!(reservation_needed(0), 0);

        assert_eq!(reservation_needed(1), TRANSACTION_METADATA_MAX_AMOUNT + 1 * page_size);
        assert_eq!(reservation_needed(10), TRANSACTION_METADATA_MAX_AMOUNT + 10 * page_size);
        assert_eq!(reservation_needed(128), TRANSACTION_METADATA_MAX_AMOUNT + 128 * page_size);

        assert_eq!(reservation_needed(129), 2 * TRANSACTION_METADATA_MAX_AMOUNT + 129 * page_size);
        assert_eq!(reservation_needed(256), 2 * TRANSACTION_METADATA_MAX_AMOUNT + 256 * page_size);

        assert_eq!(
            reservation_needed(1500),
            12 * TRANSACTION_METADATA_MAX_AMOUNT + 1500 * page_size
        );
    }

    #[test]
    fn test_flush_range() {
        let range = FlushRange { range: 0..4096, is_zero_range: false };
        assert_eq!(range.len(), 4096);
        assert_eq!(range.page_count(), 1);

        let range = FlushRange { range: 4096..8192, is_zero_range: false };
        assert_eq!(range.len(), 4096);
        assert_eq!(range.page_count(), 1);

        let range = FlushRange { range: 4096..4608, is_zero_range: false };
        assert_eq!(range.len(), 512);
        assert_eq!(range.page_count(), 1);
    }

    #[test]
    fn test_flush_batch_zero_ranges_do_not_count_towards_dirty_bytes() {
        let mut flush_batch = FlushBatch::default();

        assert_eq!(flush_batch.add_range(FlushRange { range: 0..4096, is_zero_range: true }), None);
        assert_eq!(flush_batch.dirty_byte_count, 0);

        let remaining = flush_batch
            .add_range(FlushRange { range: 4096..FLUSH_BATCH_SIZE * 2, is_zero_range: false });
        // The batch was filled up and the amount that couldn't fit was returned.
        assert!(remaining.is_some());
        assert_eq!(flush_batch.dirty_byte_count, FLUSH_BATCH_SIZE);

        // Switching the extra amount to a zero range will cause it to  fit because it doesn't count
        // towards the dirty bytes.
        let mut remaining = remaining.unwrap();
        remaining.is_zero_range = true;
        assert_eq!(flush_batch.add_range(remaining), None);
    }

    #[test]
    fn test_flush_batch_page_count() {
        let mut flush_batch = FlushBatch::default();
        assert_eq!(flush_batch.page_count(), 0);

        flush_batch.add_range(FlushRange { range: 0..4096, is_zero_range: true });
        // Zero ranges don't count towards the page count.
        assert_eq!(flush_batch.page_count(), 0);

        flush_batch.add_range(FlushRange { range: 4096..8192, is_zero_range: false });
        assert_eq!(flush_batch.page_count(), 1);

        // Adding a partial page rounds up to the next page. Only the page containing the content
        // size should be a partial page so handling multiple partial pages isn't necessary.
        flush_batch.add_range(FlushRange { range: 8192..8704, is_zero_range: false });
        assert_eq!(flush_batch.page_count(), 2);
    }

    #[test]
    fn test_flush_batch_add_range_splits_range() {
        let mut flush_batch = FlushBatch::default();

        let remaining = flush_batch
            .add_range(FlushRange { range: 0..(FLUSH_BATCH_SIZE + 4096), is_zero_range: false });
        let remaining = remaining.expect("The batch should have run out of space");
        assert_eq!(remaining.range, FLUSH_BATCH_SIZE..(FLUSH_BATCH_SIZE + 4096));
        assert_eq!(remaining.is_zero_range, false);

        let range = FlushRange {
            range: (FLUSH_BATCH_SIZE + 4096)..(FLUSH_BATCH_SIZE + 8192),
            is_zero_range: false,
        };
        assert_eq!(flush_batch.add_range(range.clone()), Some(range));
    }

    #[test]
    fn test_flush_batches_add_range_huge_range() {
        let mut batches = FlushBatches::default();
        batches.add_range(FlushRange {
            range: 0..(FLUSH_BATCH_SIZE * 2 + 8192),
            is_zero_range: false,
        });
        assert_eq!(batches.dirty_page_count, 258);
        assert_eq!(
            batches.batches,
            vec![
                FlushBatch {
                    ranges: vec![FlushRange { range: 0..FLUSH_BATCH_SIZE, is_zero_range: false }],
                    dirty_byte_count: FLUSH_BATCH_SIZE,
                },
                FlushBatch {
                    ranges: vec![FlushRange {
                        range: FLUSH_BATCH_SIZE..(FLUSH_BATCH_SIZE * 2),
                        is_zero_range: false
                    }],
                    dirty_byte_count: FLUSH_BATCH_SIZE,
                },
                FlushBatch {
                    ranges: vec![FlushRange {
                        range: (FLUSH_BATCH_SIZE * 2)..(FLUSH_BATCH_SIZE * 2 + 8192),
                        is_zero_range: false
                    }],
                    dirty_byte_count: 8192,
                }
            ]
        );
    }

    #[test]
    fn test_flush_batches_add_range_multiple_ranges() {
        let page_size = zx::system_get_page_size() as u64;
        let mut batches = FlushBatches::default();
        batches.add_range(FlushRange { range: 0..page_size, is_zero_range: false });
        batches.add_range(FlushRange { range: page_size..(page_size * 3), is_zero_range: true });
        batches.add_range(FlushRange {
            range: (page_size * 7)..(page_size * 150),
            is_zero_range: false,
        });
        batches.add_range(FlushRange {
            range: (page_size * 200)..(page_size * 500),
            is_zero_range: true,
        });
        batches.add_range(FlushRange {
            range: (page_size * 500)..(page_size * 650),
            is_zero_range: false,
        });

        assert_eq!(batches.dirty_page_count, 294);
        assert_eq!(
            batches.batches,
            vec![
                FlushBatch {
                    ranges: vec![
                        FlushRange { range: 0..page_size, is_zero_range: false },
                        FlushRange { range: page_size..(page_size * 3), is_zero_range: true },
                        FlushRange {
                            range: (page_size * 7)..(page_size * 134),
                            is_zero_range: false
                        },
                    ],
                    dirty_byte_count: FLUSH_BATCH_SIZE,
                },
                FlushBatch {
                    ranges: vec![
                        FlushRange {
                            range: (page_size * 134)..(page_size * 150),
                            is_zero_range: false
                        },
                        FlushRange {
                            range: (page_size * 200)..(page_size * 500),
                            is_zero_range: true,
                        },
                        FlushRange {
                            range: (page_size * 500)..(page_size * 612),
                            is_zero_range: false
                        },
                    ],
                    dirty_byte_count: FLUSH_BATCH_SIZE,
                },
                FlushBatch {
                    ranges: vec![FlushRange {
                        range: (page_size * 612)..(page_size * 650),
                        is_zero_range: false
                    }],
                    dirty_byte_count: 38 * page_size,
                }
            ]
        );
    }

    #[test]
    fn test_flush_batches_skip_range() {
        let mut batches = FlushBatches::default();
        batches.skip_range(0..8192);
        assert_eq!(batches.dirty_page_count, 0);
        assert!(batches.batches.is_empty());
        assert_eq!(batches.skipped_dirty_page_count, 2);
    }
}
