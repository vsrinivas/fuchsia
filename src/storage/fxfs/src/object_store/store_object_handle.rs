// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        lsm_tree::types::{ItemRef, LayerIterator},
        object_handle::{ObjectHandle, ObjectProperties},
        object_store::{
            crypt::UnwrappedKeys,
            journal::fletcher64,
            record::{
                Checksums, ExtentKey, ExtentValue, ObjectAttributes, ObjectItem, ObjectKey,
                ObjectKind, ObjectValue, Timestamp,
            },
            transaction::{
                AssocObj, AssociatedObject, LockKey, Mutation, ObjectStoreMutation, Options,
                Transaction,
            },
            HandleOptions, ObjectStore,
        },
    },
    anyhow::{bail, Context, Error},
    async_trait::async_trait,
    futures::{stream::FuturesUnordered, try_join, TryStreamExt},
    interval_tree::utils::RangeOps,
    std::{
        cmp::min,
        ops::{Bound, Range},
        sync::{
            atomic::{self, AtomicBool},
            Arc, Mutex,
        },
    },
    storage_device::buffer::{Buffer, BufferRef, MutableBufferRef},
};

// Property updates which are pending a flush.  These can be set by update_timestamps and are
// flushed along with any other updates on write.  While set, the object's properties
// (get_properties) will reflect the pending values.  This is useful for performance, e.g. to permit
// updating properties on a buffered write but deferring the flush until later.
// TODO(jfsulliv): We should flush these when we close the handle.
#[derive(Clone, Debug, Default)]
struct PendingPropertyUpdates {
    creation_time: Option<Timestamp>,
    modification_time: Option<Timestamp>,
}

// TODO(csuter): We should probably be a little more frugal about what we store here since there
// could be a lot of these structures. We could change the size to be atomic.
pub struct StoreObjectHandle<S> {
    owner: Arc<S>,
    object_id: u64,
    attribute_id: u64,
    keys: Option<UnwrappedKeys>,
    size: Mutex<u64>,
    options: HandleOptions,
    pending_properties: Mutex<PendingPropertyUpdates>,
    trace: AtomicBool,
}

impl<S: AsRef<ObjectStore>> StoreObjectHandle<S> {
    pub fn new(
        owner: Arc<S>,
        object_id: u64,
        keys: Option<UnwrappedKeys>,
        attribute_id: u64,
        size: u64,
        options: HandleOptions,
        trace: bool,
    ) -> Self {
        Self {
            owner,
            object_id,
            keys,
            attribute_id,
            size: Mutex::new(size),
            options,
            pending_properties: Mutex::new(PendingPropertyUpdates::default()),
            trace: AtomicBool::new(trace),
        }
    }
}

impl<S: AsRef<ObjectStore> + Send + Sync + 'static> StoreObjectHandle<S> {
    pub fn owner(&self) -> &Arc<S> {
        &self.owner
    }

    pub fn store(&self) -> &ObjectStore {
        self.owner.as_ref().as_ref()
    }

    async fn write_timestamps<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        if let (None, None) = (crtime.as_ref(), mtime.as_ref()) {
            return Ok(());
        }
        let mut item = self.txn_get_object(transaction).await?;
        if let ObjectValue::Object { ref mut attributes, .. } = item.value {
            if let Some(time) = crtime {
                attributes.creation_time = time;
            }
            if let Some(time) = mtime {
                attributes.modification_time = time;
            }
        } else {
            bail!(FxfsError::Inconsistent);
        };
        transaction.add(
            self.store().store_object_id(),
            Mutation::replace_or_insert_object(item.key, item.value),
        );
        Ok(())
    }

    async fn apply_pending_properties<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
    ) -> Result<(), Error> {
        let pending = std::mem::take(&mut *self.pending_properties.lock().unwrap());
        self.write_timestamps(transaction, pending.creation_time, pending.modification_time).await
    }

    /// Extend the file with the given extent.  The only use case for this right now is for files
    /// that must exist at certain offsets on the device, such as super-blocks.
    pub async fn extend<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        device_range: Range<u64>,
    ) -> Result<(), Error> {
        let old_end =
            round_up(self.txn_get_size(transaction), self.block_size()).ok_or(FxfsError::TooBig)?;
        let new_size = old_end + device_range.end - device_range.start;
        self.store().allocator().mark_allocated(transaction, device_range.clone()).await?;
        transaction.add_with_object(
            self.store().store_object_id,
            Mutation::replace_or_insert_object(
                ObjectKey::attribute(self.object_id, self.attribute_id),
                ObjectValue::attribute(new_size),
            ),
            AssocObj::Borrowed(self),
        );
        transaction.add(
            self.store().store_object_id,
            Mutation::extent(
                ExtentKey::new(self.object_id, self.attribute_id, old_end..new_size),
                ExtentValue::new(device_range.start),
            ),
        );
        self.update_allocated_size(transaction, device_range.end - device_range.start, 0).await
    }

    async fn write_at(
        &self,
        offset: u64,
        buf: BufferRef<'_>,
        mut device_offset: u64,
        compute_checksum: bool,
    ) -> Result<Checksums, Error> {
        let block_size = u64::from(self.block_size());
        let start_align = offset % block_size;
        let start_offset = offset - start_align;
        let end = offset + buf.len() as u64;
        let trace = self.trace.load(atomic::Ordering::Relaxed);
        let mut checksums = Vec::new();
        let mut transfer_buf = self
            .store()
            .device
            .allocate_buffer(round_up(end - start_offset, block_size).unwrap() as usize);
        let mut end_align = end % block_size;

        // Deal with head alignment.
        if start_align > 0 {
            let mut head_block = transfer_buf.subslice_mut(..block_size as usize);
            let read = self.read(start_offset, head_block.reborrow()).await?;
            head_block.as_mut_slice()[read..].fill(0);
            device_offset -= start_align;
            if end - end_align == start_offset {
                end_align = 0;
            }
        };

        // Deal with tail alignment.
        if end_align > 0 {
            let mut tail_block =
                transfer_buf.subslice_mut(transfer_buf.len() - block_size as usize..);
            let read = self.read(end - end_align, tail_block.reborrow()).await?;
            tail_block.as_mut_slice()[read..].fill(0);
        }

        transfer_buf.as_mut_slice()[start_align as usize..start_align as usize + buf.len()]
            .copy_from_slice(buf.as_slice());
        if trace {
            log::info!(
                "{}.{} W {:?}",
                self.store().store_object_id(),
                self.object_id,
                device_offset..device_offset + transfer_buf.len() as u64
            );
        }
        if let Some(keys) = &self.keys {
            keys.encrypt(start_offset, transfer_buf.as_mut_slice());
        }
        try_join!(self.store().device.write(device_offset, transfer_buf.as_ref()), async {
            if compute_checksum {
                for chunk in transfer_buf.as_slice().chunks_exact(block_size as usize) {
                    checksums.push(fletcher64(chunk, 0));
                }
            }
            Ok(())
        })?;
        Ok(if compute_checksum { Checksums::Fletcher(checksums) } else { Checksums::None })
    }

    // Returns the amount deallocated.
    async fn deallocate_old_extents(
        &self,
        transaction: &mut Transaction<'_>,
        range: Range<u64>,
    ) -> Result<u64, Error> {
        let block_size = u64::from(self.block_size());
        assert_eq!(range.start % block_size, 0);
        assert_eq!(range.end % block_size, 0);
        if range.start == range.end {
            return Ok(0);
        }
        let tree = &self.store().extent_tree;
        let layer_set = tree.layer_set();
        let key = ExtentKey::new(self.object_id, self.attribute_id, range);
        let lower_bound = key.search_key();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Included(&lower_bound)).await?;
        let allocator = self.store().allocator();
        let mut deallocated = 0;
        let trace = self.trace.load(atomic::Ordering::Relaxed);
        while let Some(ItemRef { key: extent_key, value: extent_value, .. }) = iter.get() {
            if extent_key.object_id != self.object_id
                || extent_key.attribute_id != self.attribute_id
            {
                break;
            }
            if let ExtentValue::Some { device_offset, .. } = extent_value {
                if let Some(overlap) = key.overlap(extent_key) {
                    let range = device_offset + overlap.start - extent_key.range.start
                        ..device_offset + overlap.end - extent_key.range.start;
                    if trace {
                        log::info!(
                            "{}.{} D {:?}",
                            self.store().store_object_id(),
                            self.object_id,
                            range
                        );
                    }
                    allocator.deallocate(transaction, range).await?;
                    deallocated += overlap.end - overlap.start;
                } else {
                    break;
                }
            }
            iter.advance().await?;
        }
        Ok(deallocated)
    }

    /// Zeroes the given range.  The range must be aligned.  Returns the amount of data deallocated.
    pub async fn zero(
        &self,
        transaction: &mut Transaction<'_>,
        range: Range<u64>,
    ) -> Result<(), Error> {
        let deallocated = self.deallocate_old_extents(transaction, range.clone()).await?;
        if deallocated > 0 {
            self.update_allocated_size(transaction, 0, deallocated).await?;
            transaction.add(
                self.store().store_object_id,
                Mutation::extent(
                    ExtentKey::new(self.object_id, self.attribute_id, range),
                    ExtentValue::deleted_extent(),
                ),
            );
        }
        Ok(())
    }

    // If |transaction| has an impending mutation for the underlying object, returns that.
    // Otherwise, looks up the object from the tree.
    async fn txn_get_object(&self, transaction: &Transaction<'_>) -> Result<ObjectItem, Error> {
        self.store().txn_get_object(transaction, self.object_id).await
    }

    // Within a transaction, the size of the object might have changed, so get the size from there
    // if it exists, otherwise, fall back on the cached size.
    fn txn_get_size(&self, transaction: &Transaction<'_>) -> u64 {
        transaction
            .get_object_mutation(
                self.store().store_object_id,
                ObjectKey::attribute(self.object_id, self.attribute_id),
            )
            .and_then(|m| {
                if let ObjectItem { value: ObjectValue::Attribute { size }, .. } = m.item {
                    Some(size)
                } else {
                    None
                }
            })
            .unwrap_or_else(|| self.get_size())
    }

    // TODO(csuter): make this used
    #[cfg(test)]
    async fn get_allocated_size(&self) -> Result<u64, Error> {
        self.store().ensure_open().await?;
        if let ObjectItem {
            value: ObjectValue::Object { kind: ObjectKind::File { allocated_size, .. }, .. },
            ..
        } = self
            .store()
            .tree
            .find(&ObjectKey::object(self.object_id))
            .await?
            .expect("Unable to find object record")
        {
            Ok(allocated_size)
        } else {
            panic!("Unexpected object value");
        }
    }

    async fn update_allocated_size(
        &self,
        transaction: &mut Transaction<'_>,
        allocated: u64,
        deallocated: u64,
    ) -> Result<(), Error> {
        if allocated == deallocated {
            return Ok(());
        }
        let mut item = self.txn_get_object(transaction).await?;
        if let ObjectItem {
            value: ObjectValue::Object { kind: ObjectKind::File { ref mut allocated_size, .. }, .. },
            ..
        } = item
        {
            // The only way for these to fail are if the volume is inconsistent.
            *allocated_size = allocated_size
                .checked_add(allocated)
                .ok_or(FxfsError::Inconsistent)?
                .checked_sub(deallocated)
                .ok_or(FxfsError::Inconsistent)?;
        } else {
            panic!("Unexpceted object value");
        }
        transaction.add(
            self.store().store_object_id,
            Mutation::replace_or_insert_object(item.key, item.value),
        );
        Ok(())
    }
}

impl<S: Send + Sync + 'static> AssociatedObject for StoreObjectHandle<S> {
    fn will_apply_mutation(&self, mutation: &Mutation) {
        match mutation {
            Mutation::ObjectStore(ObjectStoreMutation {
                item: ObjectItem { value: ObjectValue::Attribute { size }, .. },
                ..
            }) => *self.size.lock().unwrap() = *size,
            _ => {}
        }
    }
}

// TODO(jfsulliv): Move into utils module or something else.
pub fn round_down<T: Into<u64>>(offset: u64, block_size: T) -> u64 {
    offset - offset % block_size.into()
}

pub fn round_up<T: Into<u64>>(offset: u64, block_size: T) -> Option<u64> {
    let block_size = block_size.into();
    Some(round_down(offset.checked_add(block_size - 1)?, block_size))
}

#[async_trait]
impl<S: AsRef<ObjectStore> + Send + Sync + 'static> ObjectHandle for StoreObjectHandle<S> {
    fn set_trace(&self, v: bool) {
        log::info!("{}.{} tracing: {}", self.store().store_object_id, self.object_id(), v);
        self.trace.store(v, atomic::Ordering::Relaxed);
    }

    fn object_id(&self) -> u64 {
        return self.object_id;
    }

    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.store().device.allocate_buffer(size)
    }

    fn block_size(&self) -> u32 {
        self.store().block_size()
    }

    async fn read(&self, mut offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        if buf.len() == 0 {
            return Ok(0);
        }
        // Whilst the read offset must be aligned to the filesystem block size, the buffer need only
        // be aligned to the device's block size.
        let block_size = self.block_size() as u64;
        let device_block_size = self.store().device.block_size() as u64;
        assert_eq!(offset % block_size, 0);
        assert_eq!(buf.range().start as u64 % device_block_size, 0);
        let fs = self.store().filesystem();
        let _guard = fs
            .read_lock(&[LockKey::object_attribute(
                self.store().store_object_id,
                self.object_id,
                self.attribute_id,
            )])
            .await;
        let size = self.get_size();
        if offset >= size {
            return Ok(0);
        }
        let tree = &self.store().extent_tree;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger
            .seek(Bound::Included(&ExtentKey::new(
                self.object_id,
                self.attribute_id,
                offset..offset + 1,
            )))
            .await?;
        let to_do = min(buf.len() as u64, size - offset) as usize;
        buf = buf.subslice_mut(0..to_do);
        let end_align = ((offset + to_do as u64) % block_size) as usize;
        let trace = self.trace.load(atomic::Ordering::Relaxed);
        while let Some(ItemRef { key: extent_key, value: extent_value, .. }) = iter.get() {
            if extent_key.object_id != self.object_id
                || extent_key.attribute_id != self.attribute_id
            {
                break;
            }
            if extent_key.range.start > offset {
                // Zero everything up to the start of the extent.
                let to_zero = min(extent_key.range.start - offset, buf.len() as u64) as usize;
                for i in &mut buf.as_mut_slice()[..to_zero] {
                    *i = 0;
                }
                buf = buf.subslice_mut(to_zero..);
                if buf.is_empty() {
                    break;
                }
                offset += to_zero as u64;
            }

            if let ExtentValue::Some { device_offset, key_id, .. } = extent_value {
                let mut device_offset = device_offset + (offset - extent_key.range.start);

                let to_copy = min(buf.len() - end_align, (extent_key.range.end - offset) as usize);
                if to_copy > 0 {
                    if trace {
                        log::info!(
                            "{}.{} R {:?}",
                            self.store().store_object_id(),
                            self.object_id,
                            device_offset..device_offset + to_copy as u64
                        );
                    }
                    let mut subslice = buf.reborrow().subslice_mut(..to_copy);
                    self.store().device.read(device_offset, subslice.reborrow()).await?;
                    if let Some(keys) = &self.keys {
                        keys.decrypt(offset, *key_id, subslice.as_mut_slice())?;
                    }
                    buf = buf.subslice_mut(to_copy..);
                    if buf.is_empty() {
                        break;
                    }
                    offset += to_copy as u64;
                    device_offset += to_copy as u64;
                }

                // Deal with end alignment, again by reading the exsting contents into an alignment
                // buffer.
                if offset < extent_key.range.end && end_align > 0 {
                    let mut align_buf = self.store().device.allocate_buffer(block_size as usize);
                    if trace {
                        log::info!(
                            "{}.{} RT {:?}",
                            self.store().store_object_id(),
                            self.object_id,
                            device_offset..device_offset + align_buf.len() as u64
                        );
                    }
                    self.store().device.read(device_offset, align_buf.as_mut()).await?;
                    if let Some(keys) = &self.keys {
                        keys.decrypt(offset, *key_id, align_buf.as_mut_slice())?;
                    }
                    buf.as_mut_slice().copy_from_slice(&align_buf.as_slice()[..end_align]);
                    buf = buf.subslice_mut(0..0);
                    break;
                }
            } else if extent_key.range.end >= offset + buf.len() as u64 {
                // Deleted extent covers remainder, so we're done.
                break;
            }

            iter.advance().await?;
        }
        buf.as_mut_slice().fill(0);
        Ok(to_do)
    }

    // This function has some alignment requirements: any whole blocks that are to be written must
    // be aligned; writes that only touch the head and tail blocks are fine.
    async fn txn_write<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        mut offset: u64,
        buf: BufferRef<'_>,
    ) -> Result<(), Error> {
        if buf.is_empty() {
            return Ok(());
        }
        self.apply_pending_properties(transaction).await?;
        let block_size = u64::from(self.block_size());
        let aligned = round_down(offset, block_size)
            ..round_up(offset + buf.len() as u64, block_size).ok_or(FxfsError::TooBig)?;
        let mut buf_offset = 0;
        let store = self.store();
        let store_id = store.store_object_id;
        if offset + buf.len() as u64 > self.txn_get_size(transaction) {
            transaction.add_with_object(
                store_id,
                Mutation::replace_or_insert_object(
                    ObjectKey::attribute(self.object_id, self.attribute_id),
                    ObjectValue::attribute(offset + buf.len() as u64),
                ),
                AssocObj::Borrowed(self),
            );
        }
        let mut allocated = 0;
        let allocator = store.allocator();
        let trace = self.trace.load(atomic::Ordering::Relaxed);
        let futures = FuturesUnordered::new();
        let mut aligned_offset = aligned.start;
        while buf_offset < buf.len() {
            let device_range = allocator
                .allocate(transaction, aligned.end - aligned_offset)
                .await
                .context("allocation failed")?;
            if trace {
                log::info!("{}.{} A {:?}", store_id, self.object_id, device_range);
            }
            allocated += device_range.end - device_range.start;
            let end = aligned_offset + device_range.end - device_range.start;
            let len = min(buf.len() - buf_offset, (end - offset) as usize);
            assert!(len > 0);
            futures.push(async move {
                let checksum = self
                    .write_at(
                        offset,
                        buf.subslice(buf_offset..buf_offset + len),
                        device_range.start + offset % block_size,
                        true,
                    )
                    .await?;
                Ok(Mutation::extent(
                    ExtentKey::new(self.object_id, self.attribute_id, aligned_offset..end),
                    ExtentValue::with_checksum(device_range.start, checksum),
                ))
            });
            aligned_offset = end;
            buf_offset += len;
            offset += len as u64;
        }
        let (mutations, _): (Vec<_>, _) = try_join!(futures.try_collect(), async {
            let deallocated = self.deallocate_old_extents(transaction, aligned.clone()).await?;
            self.update_allocated_size(transaction, allocated, deallocated).await
        })?;
        for m in mutations {
            transaction.add(store_id, m);
        }
        Ok(())
    }

    // All the extents for the range must have been preallocated using preallocate_range or from
    // existing writes.
    async fn overwrite(&self, mut offset: u64, buf: BufferRef<'_>) -> Result<(), Error> {
        let tree = &self.store().extent_tree;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let end = offset + buf.len() as u64;
        let mut iter = merger
            .seek(Bound::Included(
                &ExtentKey::new(self.object_id, self.attribute_id, offset..end).search_key(),
            ))
            .await?;
        let mut pos = 0;
        loop {
            let (device_offset, to_do) = match iter.get() {
                Some(ItemRef {
                    key: ExtentKey { object_id, attribute_id, range },
                    value: ExtentValue::Some { device_offset, checksums: Checksums::None, .. },
                    ..
                }) if *object_id == self.object_id
                    && *attribute_id == self.attribute_id
                    && range.start <= offset =>
                {
                    (
                        device_offset + (offset - range.start),
                        min(buf.len() - pos, (range.end - offset) as usize),
                    )
                }
                _ => bail!("offset {} not allocated/has checksums", offset),
            };
            self.write_at(offset, buf.subslice(pos..pos + to_do), device_offset, false).await?;
            pos += to_do;
            if pos == buf.len() {
                break;
            }
            offset += to_do as u64;
            iter.advance().await?;
        }
        Ok(())
    }

    fn get_size(&self) -> u64 {
        *self.size.lock().unwrap()
    }

    async fn truncate<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        size: u64,
    ) -> Result<(), Error> {
        let old_size = self.txn_get_size(transaction);
        if size < old_size {
            let block_size = self.block_size().into();
            let aligned_size = round_up(size, block_size).ok_or(FxfsError::TooBig)?;
            self.zero(
                transaction,
                aligned_size..round_up(old_size, block_size).ok_or(FxfsError::Inconsistent)?,
            )
            .await?;
            let to_zero = aligned_size - size;
            if to_zero > 0 {
                assert!(to_zero < block_size);
                // We intentionally use the COW write path even if we're in overwrite mode. There's
                // no need to support overwrite mode here, and it would be difficult since we'd need
                // to transactionalize zeroing the tail of the last block with the other metadata
                // changes, which we don't currently have a way to do.
                // TODO(csuter): This is allocating a small buffer that we'll just end up copying.
                // Is there a better way?
                // TODO(csuter): This might cause an allocation when there needs be none; ideally
                // this would know if the tail block is allocated and if it isn't, it should leave
                // it be.
                let mut buf = self.store().device.allocate_buffer(to_zero as usize);
                buf.as_mut_slice().fill(0);
                self.txn_write(transaction, size, buf.as_ref()).await?;
            }
        }
        transaction.add_with_object(
            self.store().store_object_id,
            Mutation::replace_or_insert_object(
                ObjectKey::attribute(self.object_id, self.attribute_id),
                ObjectValue::attribute(size),
            ),
            AssocObj::Borrowed(self),
        );
        self.apply_pending_properties(transaction).await?;
        Ok(())
    }

    // Must be multiple of block size.
    async fn preallocate_range<'a>(
        &'a self,
        transaction: &mut Transaction<'a>,
        mut file_range: Range<u64>,
    ) -> Result<Vec<Range<u64>>, Error> {
        assert_eq!(file_range.length() % self.block_size() as u64, 0);
        assert!(self.keys.is_none());
        let mut ranges = Vec::new();
        let tree = &self.store().extent_tree;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger
            .seek(Bound::Included(
                &ExtentKey::new(self.object_id, self.attribute_id, file_range.clone()).search_key(),
            ))
            .await?;
        let mut allocated = 0;
        'outer: while file_range.start < file_range.end {
            let allocate_end = loop {
                match iter.get() {
                    Some(ItemRef {
                        key: ExtentKey { object_id, attribute_id, range },
                        value: ExtentValue::Some { device_offset, .. },
                        ..
                    }) if *object_id == self.object_id
                        && *attribute_id == self.attribute_id
                        && range.start < file_range.end =>
                    {
                        if range.start <= file_range.start {
                            // Record the existing extent and move on.
                            let device_range = device_offset + file_range.start - range.start
                                ..device_offset + min(range.end, file_range.end) - range.start;
                            file_range.start += device_range.end - device_range.start;
                            ranges.push(device_range);
                            if file_range.start >= file_range.end {
                                break 'outer;
                            }
                            iter.advance().await?;
                            continue;
                        } else {
                            // There's nothing allocated between file_range.start and the beginning
                            // of this extent.
                            break range.start;
                        }
                    }
                    Some(ItemRef {
                        key: ExtentKey { object_id, attribute_id, range },
                        value: ExtentValue::None,
                        ..
                    }) if *object_id == self.object_id
                        && *attribute_id == self.attribute_id
                        && range.end < file_range.end =>
                    {
                        iter.advance().await?;
                    }
                    _ => {
                        // We can just preallocate the rest.
                        break file_range.end;
                    }
                }
            };
            let device_range = self
                .store()
                .allocator()
                .allocate(transaction, allocate_end - file_range.start)
                .await
                .context("Allocation failed")?;
            allocated += device_range.end - device_range.start;
            let this_file_range =
                file_range.start..file_range.start + device_range.end - device_range.start;
            file_range.start = this_file_range.end;
            transaction.add(
                self.store().store_object_id,
                Mutation::extent(
                    ExtentKey::new(self.object_id, self.attribute_id, this_file_range),
                    ExtentValue::new(device_range.start),
                ),
            );
            ranges.push(device_range);
            // If we didn't allocate all that we requested, we'll loop around and try again.
        }
        // Update the file size if it changed.
        if file_range.end > self.txn_get_size(transaction) {
            transaction.add_with_object(
                self.store().store_object_id,
                Mutation::replace_or_insert_object(
                    ObjectKey::attribute(self.object_id, self.attribute_id),
                    ObjectValue::attribute(file_range.end),
                ),
                AssocObj::Borrowed(self),
            );
        }
        self.update_allocated_size(transaction, allocated, 0).await?;
        Ok(ranges)
    }

    async fn update_timestamps<'a>(
        &'a self,
        transaction: Option<&mut Transaction<'a>>,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        let (crtime, mtime) = {
            let mut pending = self.pending_properties.lock().unwrap();

            if transaction.is_none() {
                // Just buffer the new values for later.
                if crtime.is_some() {
                    pending.creation_time = crtime;
                }
                if mtime.is_some() {
                    pending.modification_time = mtime;
                }
                return Ok(());
            }
            (crtime.or(pending.creation_time.clone()), mtime.or(pending.modification_time.clone()))
        };
        self.write_timestamps(transaction.unwrap(), crtime, mtime).await
    }

    // TODO(jfsulliv): Make StoreObjectHandle per-object (not per-attribute as it currently is)
    // and pass in a list of attributes to fetch properties for.
    async fn get_properties(&self) -> Result<ObjectProperties, Error> {
        // Take a read guard since we need to return a consistent view of all object properties.
        let fs = self.store().filesystem();
        let _guard = fs
            .read_lock(&[LockKey::object_attribute(
                self.store().store_object_id,
                self.object_id,
                self.attribute_id,
            )])
            .await;
        let item = self
            .store()
            .tree
            .find(&ObjectKey::object(self.object_id))
            .await?
            .expect("Unable to find object record");
        match item.value {
            ObjectValue::Object {
                kind: ObjectKind::File { refs, allocated_size, .. },
                attributes: ObjectAttributes { creation_time, modification_time },
            } => {
                let data_attribute_size = self.get_size();
                let pending = self.pending_properties.lock().unwrap();
                Ok(ObjectProperties {
                    refs,
                    allocated_size,
                    data_attribute_size,
                    creation_time: pending.creation_time.clone().unwrap_or(creation_time),
                    modification_time: pending
                        .modification_time
                        .clone()
                        .unwrap_or(modification_time),
                    sub_dirs: 0,
                })
            }
            _ => bail!(FxfsError::NotFile),
        }
    }

    async fn new_transaction<'a>(&self) -> Result<Transaction<'a>, Error> {
        self.new_transaction_with_options(Options {
            skip_journal_checks: self.options.skip_journal_checks,
            ..Default::default()
        })
        .await
    }

    async fn new_transaction_with_options<'a>(
        &self,
        options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        Ok(self
            .store()
            .filesystem()
            .new_transaction(
                &[LockKey::object_attribute(
                    self.store().store_object_id,
                    self.object_id,
                    self.attribute_id,
                )],
                options,
            )
            .await?)
    }

    async fn flush_device(&self) -> Result<(), Error> {
        self.store().device().flush().await
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            lsm_tree::types::{ItemRef, LayerIterator},
            object_handle::{ObjectHandle, ObjectHandleExt, ObjectProperties},
            object_store::{
                crypt::InsecureCrypt,
                filesystem::{Filesystem, FxFilesystem, Mutations, OpenFxFilesystem},
                record::{ExtentKey, ObjectKey, ObjectKeyData, Timestamp},
                round_up,
                transaction::{Options, TransactionHandler},
                HandleOptions, ObjectStore, StoreObjectHandle,
            },
        },
        fuchsia_async as fasync,
        futures::{channel::oneshot::channel, join},
        matches::assert_matches,
        rand::Rng,
        std::{
            ops::Bound,
            sync::{Arc, Mutex},
            time::Duration,
        },
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    // Some tests (the preallocate_range ones) currently assume that the data only occupies a single
    // device block.
    const TEST_DATA_OFFSET: u64 = 5000;
    const TEST_DATA: &[u8] = b"hello";
    const TEST_OBJECT_SIZE: u64 = 5678;
    const TEST_OBJECT_ALLOCATED_SIZE: u64 = 4096;

    async fn test_filesystem() -> OpenFxFilesystem {
        let device = DeviceHolder::new(FakeDevice::new(8192, TEST_DEVICE_BLOCK_SIZE));
        FxFilesystem::new_empty(device, Arc::new(InsecureCrypt::new()))
            .await
            .expect("new_empty failed")
    }

    async fn test_filesystem_and_object_with_key(
        wrapping_key: Option<u64>,
    ) -> (OpenFxFilesystem, StoreObjectHandle<ObjectStore>) {
        let fs = test_filesystem().await;
        let store = fs.root_store();
        let object;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object = ObjectStore::create_object(
            &store,
            &mut transaction,
            HandleOptions::default(),
            wrapping_key,
        )
        .await
        .expect("create_object failed");
        {
            let align = TEST_DATA_OFFSET as usize % TEST_DEVICE_BLOCK_SIZE as usize;
            let mut buf = object.allocate_buffer(align + TEST_DATA.len());
            buf.as_mut_slice()[align..].copy_from_slice(TEST_DATA);
            object
                .txn_write(&mut transaction, TEST_DATA_OFFSET, buf.subslice(align..))
                .await
                .expect("write failed");
        }
        object.truncate(&mut transaction, TEST_OBJECT_SIZE).await.expect("truncate failed");
        transaction.commit().await.expect("commit failed");
        (fs, object)
    }

    async fn test_filesystem_and_object() -> (OpenFxFilesystem, StoreObjectHandle<ObjectStore>) {
        test_filesystem_and_object_with_key(Some(0)).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zero_buf_len_read() {
        let (fs, object) = test_filesystem_and_object().await;
        let mut buf = object.allocate_buffer(0);
        assert_eq!(object.read(0u64, buf.as_mut()).await.expect("read failed"), 0);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_beyond_eof_read() {
        let (fs, object) = test_filesystem_and_object().await;
        let offset = TEST_OBJECT_SIZE as usize - 2;
        let align = offset % fs.block_size() as usize;
        let len: usize = 2;
        let mut buf = object.allocate_buffer(align + len + 1);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(
            object.read((offset - align) as u64, buf.as_mut()).await.expect("read failed"),
            align + len
        );
        assert_eq!(&buf.as_slice()[align..align + len], &vec![0u8; len]);
        assert_eq!(&buf.as_slice()[align + len..], &vec![123u8; buf.len() - align - len]);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sparse() {
        let (fs, object) = test_filesystem_and_object().await;
        // Deliberately read not right to eof.
        let len = TEST_OBJECT_SIZE as usize - 1;
        let mut buf = object.allocate_buffer(len);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), len);
        let mut expected = vec![0; len];
        let offset = TEST_DATA_OFFSET as usize;
        expected[offset..offset + TEST_DATA.len()].copy_from_slice(TEST_DATA);
        assert_eq!(buf.as_slice()[..len], expected[..]);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_after_writes_interspersed_with_flush() {
        let (fs, object) = test_filesystem_and_object().await;

        object.owner().flush().await.expect("flush failed");

        // Write more test data to the first block fo the file.
        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        object.write(0u64, buf.as_ref()).await.expect("write failed");

        let len = TEST_OBJECT_SIZE as usize - 1;
        let mut buf = object.allocate_buffer(len);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), len);

        let mut expected = vec![0u8; len];
        let offset = TEST_DATA_OFFSET as usize;
        expected[offset..offset + TEST_DATA.len()].copy_from_slice(TEST_DATA);
        expected[..TEST_DATA.len()].copy_from_slice(TEST_DATA);
        assert_eq!(buf.as_slice(), &expected);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_after_truncate_and_extend() {
        let (fs, object) = test_filesystem_and_object().await;

        // Arrange for there to be <extent><deleted-extent><extent>.
        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        object.write(0, buf.as_ref()).await.expect("write failed"); // This adds an extent at 0..512.
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object.truncate(&mut transaction, 3).await.expect("truncate failed"); // This deletes 512..1024.
        transaction.commit().await.expect("commit failed");
        let data = b"foo";
        let offset = 1500u64;
        let align = (offset % fs.block_size() as u64) as usize;
        let mut buf = object.allocate_buffer(align + data.len());
        buf.as_mut_slice()[align..].copy_from_slice(data);
        object.write(1500, buf.subslice(align..)).await.expect("write failed"); // This adds 1024..1536.

        const LEN1: usize = 1503;
        let mut buf = object.allocate_buffer(LEN1);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), LEN1);
        let mut expected = [0; LEN1];
        expected[..3].copy_from_slice(&TEST_DATA[..3]);
        expected[1500..].copy_from_slice(b"foo");
        assert_eq!(buf.as_slice(), &expected);

        // Also test a read that ends midway through the deleted extent.
        const LEN2: usize = 601;
        let mut buf = object.allocate_buffer(LEN2);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), LEN2);
        assert_eq!(buf.as_slice(), &expected[..LEN2]);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_whole_blocks_with_multiple_objects() {
        let (fs, object) = test_filesystem_and_object().await;
        let mut buffer = object.allocate_buffer(512);
        buffer.as_mut_slice().fill(0xaf);
        object.write(0, buffer.as_ref()).await.expect("write failed");

        let store = object.owner();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let object2 =
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), Some(0))
                .await
                .expect("create_object failed");
        transaction.commit().await.expect("commit failed");
        let mut ef_buffer = object.allocate_buffer(512);
        ef_buffer.as_mut_slice().fill(0xef);
        object2.write(0, ef_buffer.as_ref()).await.expect("write failed");

        let mut buffer = object.allocate_buffer(512);
        buffer.as_mut_slice().fill(0xaf);
        object.write(512, buffer.as_ref()).await.expect("write failed");
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object.truncate(&mut transaction, 1536).await.expect("truncate failed");
        transaction.commit().await.expect("commit failed");
        object2.write(512, ef_buffer.as_ref()).await.expect("write failed");

        let mut buffer = object.allocate_buffer(2048);
        buffer.as_mut_slice().fill(123);
        assert_eq!(object.read(0, buffer.as_mut()).await.expect("read failed"), 1536);
        assert_eq!(&buffer.as_slice()[..1024], &[0xaf; 1024]);
        assert_eq!(&buffer.as_slice()[1024..1536], &[0; 512]);
        assert_eq!(object2.read(0, buffer.as_mut()).await.expect("read failed"), 1024);
        assert_eq!(&buffer.as_slice()[..1024], &[0xef; 1024]);
        fs.close().await.expect("Close failed");
    }

    async fn test_preallocate_common(fs: &FxFilesystem, object: StoreObjectHandle<ObjectStore>) {
        let allocator = fs.allocator();
        let allocated_before = allocator.get_allocated_bytes();
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object
            .preallocate_range(&mut transaction, 0..fs.block_size() as u64)
            .await
            .expect("preallocate_range failed");
        transaction.commit().await.expect("commit failed");
        assert!(object.get_size() < 1048576);
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object
            .preallocate_range(&mut transaction, 0..1048576)
            .await
            .expect("preallocate_range failed");
        transaction.commit().await.expect("commit failed");
        assert_eq!(object.get_size(), 1048576);
        // Check that it didn't reallocate the space for the existing extent
        let allocated_after = allocator.get_allocated_bytes();
        assert_eq!(allocated_after - allocated_before, 1048576 - fs.block_size() as u64);

        let mut buf =
            object.allocate_buffer(round_up(TEST_DATA_OFFSET, fs.block_size()).unwrap() as usize);
        buf.as_mut_slice().fill(47);
        object.write(0, buf.subslice(..TEST_DATA_OFFSET as usize)).await.expect("write failed");
        buf.as_mut_slice().fill(95);
        let offset = round_up(TEST_OBJECT_SIZE, fs.block_size()).unwrap();
        object.overwrite(offset, buf.as_ref()).await.expect("write failed");

        // Make sure there were no more allocations.
        assert_eq!(allocator.get_allocated_bytes(), allocated_after);

        // Read back the data and make sure it is what we expect.
        let mut buf = object.allocate_buffer(104876);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), buf.len());
        assert_eq!(&buf.as_slice()[..TEST_DATA_OFFSET as usize], &[47; TEST_DATA_OFFSET as usize]);
        assert_eq!(
            &buf.as_slice()[TEST_DATA_OFFSET as usize..TEST_DATA_OFFSET as usize + TEST_DATA.len()],
            TEST_DATA
        );
        assert_eq!(&buf.as_slice()[offset as usize..offset as usize + 2048], &[95; 2048]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_preallocate_range() {
        let (fs, object) = test_filesystem_and_object_with_key(None).await;
        test_preallocate_common(&fs, object).await;
        fs.close().await.expect("Close failed");
    }

    // This is identical to the previous test except that we flush so that extents end up in
    // different layers.
    #[fasync::run_singlethreaded(test)]
    async fn test_preallocate_suceeds_when_extents_are_in_different_layers() {
        let (fs, object) = test_filesystem_and_object_with_key(None).await;
        object.owner().flush().await.expect("flush failed");
        test_preallocate_common(&fs, object).await;
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_already_preallocated() {
        let (fs, object) = test_filesystem_and_object_with_key(None).await;
        let allocator = fs.allocator();
        let allocated_before = allocator.get_allocated_bytes();
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        let offset = TEST_DATA_OFFSET - TEST_DATA_OFFSET % fs.block_size() as u64;
        object
            .preallocate_range(&mut transaction, offset..offset + fs.block_size() as u64)
            .await
            .expect("preallocate_range failed");
        transaction.commit().await.expect("commit failed");
        // Check that it didn't reallocate any new space.
        assert_eq!(allocator.get_allocated_bytes(), allocated_before);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_overwrite_fails_if_not_preallocated() {
        let (fs, object) = test_filesystem_and_object().await;

        let object =
            ObjectStore::open_object(&object.owner, object.object_id(), HandleOptions::default())
                .await
                .expect("open_object failed");
        let mut buf = object.allocate_buffer(2048);
        buf.as_mut_slice().fill(95);
        let offset = round_up(TEST_OBJECT_SIZE, fs.block_size()).unwrap();
        object.overwrite(offset, buf.as_ref()).await.expect_err("write succeeded");
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_extend() {
        let fs = test_filesystem().await;
        let handle;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let store = fs.root_store();
        handle =
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), None)
                .await
                .expect("create_object failed");
        handle
            .extend(&mut transaction, 0..5 * fs.block_size() as u64)
            .await
            .expect("extend failed");
        transaction.commit().await.expect("commit failed");
        let mut buf = handle.allocate_buffer(5 * fs.block_size() as usize);
        buf.as_mut_slice().fill(123);
        handle.write(0, buf.as_ref()).await.expect("write failed");
        buf.as_mut_slice().fill(67);
        handle.read(0, buf.as_mut()).await.expect("read failed");
        assert_eq!(buf.as_slice(), &vec![123; 5 * fs.block_size() as usize]);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncate_deallocates_old_extents() {
        let (fs, object) = test_filesystem_and_object().await;
        let mut buf = object.allocate_buffer(5 * fs.block_size() as usize);
        buf.as_mut_slice().fill(0xaa);
        object.write(0, buf.as_ref()).await.expect("write failed");

        let allocator = fs.allocator();
        let allocated_before = allocator.get_allocated_bytes();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object.truncate(&mut transaction, fs.block_size() as u64).await.expect("truncate failed");
        transaction.commit().await.expect("commit failed");
        let allocated_after = allocator.get_allocated_bytes();
        assert!(
            allocated_after < allocated_before,
            "before = {} after = {}",
            allocated_before,
            allocated_after
        );
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_adjust_refs() {
        let (fs, object) = test_filesystem_and_object().await;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let store = object.owner();
        assert_eq!(
            store
                .adjust_refs(&mut transaction, object.object_id(), 1)
                .await
                .expect("adjust_refs failed"),
            false
        );
        transaction.commit().await.expect("commit failed");

        let allocator = fs.allocator();
        let allocated_before = allocator.get_allocated_bytes();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        assert_eq!(
            store
                .adjust_refs(&mut transaction, object.object_id(), -2)
                .await
                .expect("adjust_refs failed"),
            true
        );
        transaction.commit().await.expect("commit failed");

        assert_eq!(allocator.get_allocated_bytes(), allocated_before);

        store
            .tombstone(
                object.object_id,
                Options { borrow_metadata_space: true, ..Default::default() },
            )
            .await
            .expect("purge failed");

        assert_eq!(allocated_before - allocator.get_allocated_bytes(), fs.block_size() as u64,);

        let layer_set = store.tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        let mut found = false;
        while let Some(ItemRef { key: ObjectKey { object_id, data }, .. }) = iter.get() {
            if *object_id == object.object_id() {
                if let ObjectKeyData::Tombstone = data {
                    assert!(!found);
                    found = true;
                } else {
                    assert!(false, "Unexpected item {:?}", iter.get());
                }
            }
            iter.advance().await.expect("advance failed");
        }
        assert!(found);

        // Make sure there are no extents.
        let layer_set = store.extent_tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        while let Some(ItemRef { key: ExtentKey { object_id, .. }, value, .. }) = iter.get() {
            assert!(*object_id != object.object_id() || value.is_deleted());
            iter.advance().await.expect("advance failed");
        }

        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_locks() {
        let (fs, object) = test_filesystem_and_object().await;
        let (send1, recv1) = channel();
        let (send2, recv2) = channel();
        let (send3, recv3) = channel();
        let done = Mutex::new(false);
        join!(
            async {
                let mut t = object.new_transaction().await.expect("new_transaction failed");
                send1.send(()).unwrap(); // Tell the next future to continue.
                send3.send(()).unwrap(); // Tell the last future to continue.
                recv2.await.unwrap();
                let mut buf = object.allocate_buffer(5);
                buf.as_mut_slice().copy_from_slice(b"hello");
                object.txn_write(&mut t, 0, buf.as_ref()).await.expect("write failed");
                // This is a halting problem so all we can do is sleep.
                fasync::Timer::new(Duration::from_millis(100)).await;
                assert!(!*done.lock().unwrap());
                t.commit().await.expect("commit failed");
            },
            async {
                recv1.await.unwrap();
                // Reads should not block.
                let offset = TEST_DATA_OFFSET as usize;
                let align = offset % fs.block_size() as usize;
                let len = TEST_DATA.len();
                let mut buf = object.allocate_buffer(align + len);
                assert_eq!(
                    object.read((offset - align) as u64, buf.as_mut()).await.expect("read failed"),
                    align + TEST_DATA.len()
                );
                assert_eq!(&buf.as_slice()[align..], TEST_DATA);
                // Tell the first future to continue.
                send2.send(()).unwrap();
            },
            async {
                // This should block until the first future has completed.
                recv3.await.unwrap();
                let _t = object.new_transaction().await.expect("new_transaction failed");
                let mut buf = object.allocate_buffer(5);
                assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), 5);
                assert_eq!(buf.as_slice(), b"hello");
            }
        );
        fs.close().await.expect("Close failed");
    }

    #[fasync::run(10, test)]
    async fn test_racy_reads() {
        let fs = test_filesystem().await;
        let object;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let store = fs.root_store();
        object = Arc::new(
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), Some(0))
                .await
                .expect("create_object failed"),
        );
        transaction.commit().await.expect("commit failed");
        for _ in 0..100 {
            let cloned_object = object.clone();
            let writer = fasync::Task::spawn(async move {
                let mut buf = cloned_object.allocate_buffer(10);
                buf.as_mut_slice().fill(123);
                cloned_object.write(0, buf.as_ref()).await.expect("write failed");
            });
            let cloned_object = object.clone();
            let reader = fasync::Task::spawn(async move {
                let wait_time = rand::thread_rng().gen_range(0, 5);
                fasync::Timer::new(Duration::from_millis(wait_time)).await;
                let mut buf = cloned_object.allocate_buffer(10);
                buf.as_mut_slice().fill(23);
                let amount = cloned_object.read(0, buf.as_mut()).await.expect("write failed");
                // If we succeed in reading data, it must include the write; i.e. if we see the size
                // change, we should see the data too.  For this to succeed it requires locking on
                // the read size to ensure that when we read the size, we get the extents changed in
                // that same transaction.
                if amount != 0 {
                    assert_eq!(amount, 10);
                    assert_eq!(buf.as_slice(), &[123; 10]);
                }
            });
            writer.await;
            reader.await;
            let mut transaction = object.new_transaction().await.expect("new_transaction failed");
            object.truncate(&mut transaction, 0).await.expect("truncate failed");
            transaction.commit().await.expect("commit failed");
        }
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocated_size() {
        let (fs, object) = test_filesystem_and_object_with_key(None).await;

        let before = object.get_allocated_size().await.expect("get_allocated_size failed");
        let mut buf = object.allocate_buffer(5);
        buf.as_mut_slice().copy_from_slice(b"hello");
        object.write(0, buf.as_ref()).await.expect("write failed");
        let after = object.get_allocated_size().await.expect("get_allocated_size failed");
        assert_eq!(after, before + fs.block_size() as u64);

        // Do the same write again and there should be no change.
        object.write(0, buf.as_ref()).await.expect("write failed");
        assert_eq!(object.get_allocated_size().await.expect("get_allocated_size failed"), after);

        // extend...
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        let offset = 1000 * fs.block_size() as u64;
        let before = after;
        object
            .extend(&mut transaction, offset..offset + fs.block_size() as u64)
            .await
            .expect("extend failed");
        transaction.commit().await.expect("commit failed");
        let after = object.get_allocated_size().await.expect("get_allocated_size failed");
        assert_eq!(after, before + fs.block_size() as u64);

        // truncate...
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        let before = after;
        let size = object.get_size();
        object
            .truncate(&mut transaction, size - fs.block_size() as u64)
            .await
            .expect("extend failed");
        transaction.commit().await.expect("commit failed");
        let after = object.get_allocated_size().await.expect("get_allocated_size failed");
        assert_eq!(after, before - fs.block_size() as u64);

        // preallocate_range...
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        let before = after;
        object
            .preallocate_range(&mut transaction, offset..offset + fs.block_size() as u64)
            .await
            .expect("extend failed");
        transaction.commit().await.expect("commit failed");
        let after = object.get_allocated_size().await.expect("get_allocated_size failed");
        assert_eq!(after, before + fs.block_size() as u64);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run(10, test)]
    async fn test_zero() {
        let (fs, object) = test_filesystem_and_object().await;
        let expected_size = object.get_size();
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object.zero(&mut transaction, 0..fs.block_size() as u64 * 10).await.expect("zero failed");
        transaction.commit().await.expect("commit failed");
        assert_eq!(object.get_size(), expected_size);
        let mut buf = object.allocate_buffer(fs.block_size() as usize * 10);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed") as u64, expected_size);
        assert_eq!(
            &buf.as_slice()[0..expected_size as usize],
            vec![0u8; expected_size as usize].as_slice()
        );
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_properties() {
        let (fs, object) = test_filesystem_and_object().await;
        const CRTIME: Timestamp = Timestamp::from_nanos(1234);
        const MTIME: Timestamp = Timestamp::from_nanos(5678);

        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        object
            .update_timestamps(Some(&mut transaction), Some(CRTIME), Some(MTIME))
            .await
            .expect("update_timestamps failed");
        transaction.commit().await.expect("commit failed");

        let properties = object.get_properties().await.expect("get_properties failed");
        assert_matches!(
            properties,
            ObjectProperties {
                refs: 1u64,
                allocated_size: TEST_OBJECT_ALLOCATED_SIZE,
                data_attribute_size: TEST_OBJECT_SIZE,
                creation_time: CRTIME,
                modification_time: MTIME,
                ..
            }
        );
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_pending_properties() {
        let (fs, object) = test_filesystem_and_object().await;
        let crtime = Timestamp::from_nanos(1234u64);
        let mtime = Timestamp::from_nanos(5678u64);

        object
            .update_timestamps(None, Some(crtime.clone()), None)
            .await
            .expect("update_timestamps failed");
        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_ne!(properties.modification_time, mtime);

        object
            .update_timestamps(None, None, Some(mtime.clone()))
            .await
            .expect("update_timestamps failed");
        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_eq!(properties.modification_time, mtime);

        object
            .update_timestamps(None, None, Some(mtime.clone()))
            .await
            .expect("update_timestamps failed");
        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_eq!(properties.modification_time, mtime);

        // Writes should flush the pending attrs, rather than using the current time (which would
        // change mtime).
        let mut buf = object.allocate_buffer(5);
        buf.as_mut_slice().copy_from_slice(b"hello");
        object.write(0, buf.as_ref()).await.expect("write failed");

        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_eq!(properties.modification_time, mtime);
        fs.close().await.expect("Close failed");
    }
}
