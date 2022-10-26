// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        debug_assert_not_too_long,
        errors::FxfsError,
        log::*,
        object_handle::{
            GetProperties, ObjectHandle, ObjectProperties, ReadObjectHandle, WriteObjectHandle,
        },
        object_store::{
            allocator::{self, Allocator},
            object_record::{AttributeKey, Timestamp},
            transaction::{LockKey, Options, TRANSACTION_METADATA_MAX_AMOUNT},
            writeback_cache::{FlushableMetadata, StorageReservation, WritebackCache},
            AssocObj, HandleOwner, Mutation, ObjectKey, ObjectStore, ObjectValue,
            StoreObjectHandle, TrimMode, TrimResult,
        },
        round::round_up,
    },
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    std::sync::{
        atomic::{self},
        Arc,
    },
    storage_device::buffer::{Buffer, BufferRef, MutableBufferRef},
};

pub use crate::object_store::writeback_cache::CACHE_READ_AHEAD_SIZE;

/// How much data each sync transaction in a given flush will cover.
const FLUSH_BATCH_SIZE: u64 = 524_288;

pub struct CachingObjectHandle<S: HandleOwner> {
    handle: StoreObjectHandle<S>,
    cache: WritebackCache<S::Buffer>,
}

impl<S: HandleOwner> CachingObjectHandle<S> {
    pub fn new(handle: StoreObjectHandle<S>) -> Self {
        let size = handle.get_size();
        let buffer = handle.owner().create_data_buffer(handle.object_id(), size);
        Self { handle, cache: WritebackCache::new(buffer) }
    }

    pub fn owner(&self) -> &Arc<S> {
        self.handle.owner()
    }

    pub fn store(&self) -> &ObjectStore {
        self.handle.store()
    }

    pub fn data_buffer(&self) -> &S::Buffer {
        &self.cache.data_buffer()
    }

    pub fn uncached_handle(&self) -> &StoreObjectHandle<S> {
        &self.handle
    }

    pub async fn read_cached(&self, offset: u64, buf: &mut [u8]) -> Result<usize, Error> {
        self.cache.read(offset, buf, &self.handle).await
    }

    pub async fn read_uncached(&self, range: std::ops::Range<u64>) -> Result<Buffer<'_>, Error> {
        let mut buffer = self.allocate_buffer((range.end - range.start) as usize);
        let read = self.handle.read(range.start, buffer.as_mut()).await?;
        buffer.as_mut_slice()[read..].fill(0);
        Ok(buffer)
    }

    pub fn uncached_size(&self) -> u64 {
        self.handle.get_size()
    }

    pub async fn write_or_append_cached(
        &self,
        offset: Option<u64>,
        buf: &[u8],
    ) -> Result<u64, Error> {
        let fs = self.store().filesystem();
        let _locks = fs
            .transaction_lock(&[LockKey::cached_write(
                self.store().store_object_id,
                self.handle.object_id,
                self.handle.attribute_id,
            )])
            .await;
        let extends_file = if let Some(offset) = &offset {
            if *offset + buf.len() as u64 > self.cache.content_size() {
                true
            } else {
                false
            }
        } else {
            true
        };

        if self.cache.dirty_bytes() >= FLUSH_BATCH_SIZE {
            self.flush_impl(/* take_lock: */ false).await?;
        } else if extends_file {
            self.flush_metadata().await?;
        }

        let time = Timestamp::now().into();
        let len = buf.len();
        self.cache
            .write_or_append(
                offset,
                buf,
                self.handle.block_size().into(),
                self,
                Some(time),
                &self.handle,
            )
            .await?;
        Ok(len as u64)
    }

    async fn flush_impl(&self, take_lock: bool) -> Result<(), Error> {
        let bs = self.block_size() as u64;
        let store = self.store();
        let fs = store.filesystem();
        let store_id = store.store_object_id;
        let reservation = store.allocator().reserve_at_most(Some(store_id), 0);

        // Whilst we are calling take_flushable we need to guard against changes to the cache so
        // that we can grab a snapshot, so we take the cached_write lock and then we can drop it
        // after take_flushable returns.
        let cached_write_lock = if take_lock {
            Some(
                fs.transaction_lock(&[LockKey::cached_write(
                    store_id,
                    self.handle.object_id,
                    self.handle.attribute_id,
                )])
                .await,
            )
        } else {
            None
        };

        // We must flush metadata first here in case the file shrank since we don't do anything
        // to handle shrinking a file below.
        self.flush_metadata().await?;

        // We use the transaction lock here to make sure that flush calls are correctly sequenced,
        // i.e. that we commit transactions in the same order to which take_flushable was executed.
        // The order in which locks, flushable and transaction are dropped is important.  The
        // transaction must be dropped first so that it returns any reservations.  flushable is next
        // which relies on the reservations being returned.  flushable must be dropped before the
        // locks since we need to stop take_flushable from being called.
        let locks = fs
            .transaction_lock(&[LockKey::object_attribute(
                store_id,
                self.handle.object_id,
                self.handle.attribute_id,
            )])
            .await;

        let old_size = self.handle.get_size();

        let mut flushable = self.cache.take_flushable(
            bs,
            self.handle.get_size(),
            |size| self.allocate_buffer(size),
            self,
            &reservation,
        );

        if flushable.data.is_none() && flushable.metadata.is_none() {
            return Ok(());
        }

        std::mem::drop(cached_write_lock);

        if matches!(flushable.metadata, Some(FlushableMetadata { content_size: Some(_), .. })) {
            // Before proceeding, we must make sure the result of any previous trim has completed
            // because otherwise there's the potential for us to reveal old extents.
            let mut transaction = fs
                .clone()
                .new_transaction(
                    &[],
                    Options {
                        borrow_metadata_space: true,
                        skip_journal_checks: self.handle.options.skip_journal_checks,
                        ..Default::default()
                    },
                )
                .await?;
            while matches!(
                store
                    .trim_some(
                        &mut transaction,
                        self.handle.object_id,
                        self.handle.attribute_id,
                        TrimMode::FromOffset(old_size)
                    )
                    .await?,
                TrimResult::Incomplete
            ) {
                transaction.commit_and_continue().await?;
            }
            transaction.commit().await?;
        }

        let mut transaction = fs
            .clone()
            .new_transaction(
                &[],
                Options {
                    // If there is no data then the reservation won't have any space for the
                    // transaction. Since it should only be for file size or metadata changes,
                    // we should be able to borrow metadata space.
                    borrow_metadata_space: flushable.data.is_none(),
                    skip_journal_checks: self.handle.options.skip_journal_checks,
                    allocator_reservation: Some(&reservation),
                    ..Default::default()
                },
            )
            .await?;

        if self.handle.trace.load(atomic::Ordering::Relaxed) {
            info!(store_id, oid = self.handle.object_id, ?flushable);
        }

        if let Some(metadata) = flushable.metadata.as_ref() {
            if let Some(content_size) = metadata.content_size {
                transaction.add_with_object(
                    store_id,
                    Mutation::replace_or_insert_object(
                        ObjectKey::attribute(
                            self.handle.object_id,
                            self.handle.attribute_id,
                            AttributeKey::Size,
                        ),
                        ObjectValue::attribute(content_size),
                    ),
                    AssocObj::Borrowed(&self.handle),
                );
                self.handle
                    .write_timestamps(
                        &mut transaction,
                        metadata.creation_time.map(|t| t.into()),
                        metadata.modification_time.map(|t| t.into()),
                    )
                    .await?;
            }
        }

        if let Some(data) = flushable.data.as_mut() {
            self.handle.multi_write(&mut transaction, &data.ranges, data.buffer.as_mut()).await?;
        }

        debug_assert_not_too_long!(locks.commit_prepare());
        transaction
            .commit_with_callback(|_| {
                self.cache.complete_flush(flushable);
            })
            .await
            .context("Failed to commit transaction")?;

        Ok(())
    }

    // Tries to flush metadata.  It is important that this is done *before* any operation that
    // increases the size of the file because otherwise there are races that can cause reads to
    // return the wrong data. Consider the following scenario:
    //
    //   1. File is 10,000 bytes.
    //   2. File is shrunk to 200 bytes.
    //   3. File grows back to 10,000 bytes.
    //
    // If a read takes place after #3, it is important that the bytes between 200 and 10,000 are
    // zeroed.  If metadata is not flushed between 2 & 3, then the read will be fulfilled as if it
    // occurs prior to 2.
    async fn flush_metadata(&self) -> Result<(), Error> {
        let flushable = self.cache.take_flushable_metadata(self.handle.get_size());
        if let Some(metadata) = flushable.metadata.as_ref() {
            let mut transaction = self
                .handle
                .new_transaction_with_options(Options {
                    borrow_metadata_space: true,
                    ..Default::default()
                })
                .await?;
            let mut needs_trim = false;
            if let Some(size) = metadata.content_size {
                needs_trim = self.handle.shrink(&mut transaction, size).await?.0;
            }
            self.handle
                .write_timestamps(
                    &mut transaction,
                    metadata.creation_time.map(|t| t.into()),
                    metadata.modification_time.map(|t| t.into()),
                )
                .await?;
            transaction.commit_with_callback(|_| self.cache.complete_flush(flushable)).await?;
            if needs_trim {
                self.handle.store().trim(self.object_id()).await?;
            }
        }
        Ok(())
    }
}

impl<S: HandleOwner> Drop for CachingObjectHandle<S> {
    fn drop(&mut self) {
        self.cache.cleanup(self);
    }
}

impl<S: HandleOwner> StorageReservation for CachingObjectHandle<S> {
    fn reservation_needed(&self, mut amount: u64) -> u64 {
        amount = round_up(amount, self.block_size()).unwrap();
        amount
            + round_up(amount, FLUSH_BATCH_SIZE).unwrap() / FLUSH_BATCH_SIZE
                * TRANSACTION_METADATA_MAX_AMOUNT
    }
    fn reserve(&self, mut amount: u64) -> Result<allocator::Reservation, Error> {
        amount = round_up(amount, self.block_size()).unwrap();
        let store_id = self.store().store_object_id;
        self.store().allocator().reserve(Some(store_id), amount).ok_or(anyhow!(FxfsError::NoSpace))
    }
    fn wrap_reservation(&self, amount: u64) -> allocator::Reservation {
        let store_id = self.store().store_object_id;
        let r = self.store().allocator().reserve_at_most(Some(store_id), 0);
        r.add(amount);
        r
    }
}

impl<S: HandleOwner> ObjectHandle for CachingObjectHandle<S> {
    fn set_trace(&self, v: bool) {
        self.handle.set_trace(v);
    }

    fn object_id(&self) -> u64 {
        self.handle.object_id
    }

    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.handle.allocate_buffer(size)
    }

    fn block_size(&self) -> u64 {
        self.handle.block_size()
    }

    fn get_size(&self) -> u64 {
        self.cache.content_size()
    }
}

#[async_trait]
impl<S: HandleOwner> GetProperties for CachingObjectHandle<S> {
    async fn get_properties(&self) -> Result<ObjectProperties, Error> {
        // TODO(fxbug.dev/95354): This could be optimized to skip getting the underlying handle's
        // properties if the cache has all of the timestamps we need.
        let mut props = self.handle.get_properties().await?;
        let cached_metadata = self.cache.cached_metadata();
        props.allocated_size = props.allocated_size + cached_metadata.dirty_bytes;
        props.data_attribute_size = cached_metadata.content_size;
        props.creation_time =
            cached_metadata.creation_time.map(|t| t.into()).unwrap_or(props.creation_time);
        props.modification_time =
            cached_metadata.modification_time.map(|t| t.into()).unwrap_or(props.modification_time);
        Ok(props)
    }
}

#[async_trait]
impl<S: HandleOwner> ReadObjectHandle for CachingObjectHandle<S> {
    async fn read(&self, offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        self.cache.read(offset, buf.as_mut_slice(), &self.handle).await
    }
}

#[async_trait]
impl<S: HandleOwner> WriteObjectHandle for CachingObjectHandle<S> {
    async fn write_or_append(&self, offset: Option<u64>, buf: BufferRef<'_>) -> Result<u64, Error> {
        self.write_or_append_cached(offset, buf.as_slice()).await
    }

    async fn truncate(&self, size: u64) -> Result<(), Error> {
        let fs = self.store().filesystem();
        let _locks = fs
            .transaction_lock(&[LockKey::cached_write(
                self.store().store_object_id,
                self.handle.object_id,
                self.handle.attribute_id,
            )])
            .await;

        if size > self.cache.content_size() {
            // If we're trying to grow after we previously shrunk, we need to shrink now.
            self.flush_metadata().await?;
        }
        self.cache.resize(size, self.block_size() as u64, self).await?;
        // Try and resize immediately, but since we successfully resized the cache, don't propagate
        // errors here.
        if let Err(e) = self.flush_metadata().await {
            warn!(error = e.as_value(), "Failed to flush after resize");
        }
        Ok(())
    }

    async fn write_timestamps<'a>(
        &'a self,
        crtime: Option<Timestamp>,
        mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        let fs = self.store().filesystem();
        let _locks = fs
            .transaction_lock(&[LockKey::cached_write(
                self.store().store_object_id,
                self.handle.object_id,
                self.handle.attribute_id,
            )])
            .await;
        self.cache.update_timestamps(crtime.map(|t| t.into()), mtime.map(|t| t.into()));
        Ok(())
    }

    async fn flush(&self) -> Result<(), Error> {
        self.flush_impl(/* take_lock: */ true).await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::CACHE_READ_AHEAD_SIZE,
        crate::{
            filesystem::{Filesystem, FxFilesystem, OpenFxFilesystem},
            fsck::{fsck_with_options, FsckOptions},
            lsm_tree::types::{ItemRef, LayerIterator},
            object_handle::{GetProperties, ObjectHandle, ReadObjectHandle, WriteObjectHandle},
            object_store::{
                allocator::Allocator,
                directory::Directory,
                object_record::{ObjectKey, ObjectKeyData, ObjectValue, Timestamp},
                transaction::{Options, TransactionHandler},
                CachingObjectHandle, HandleOptions, ObjectStore, TRANSACTION_MUTATION_THRESHOLD,
            },
        },
        fuchsia_async as fasync,
        rand::Rng,
        std::ops::Bound,
        std::sync::Arc,
        std::time::Duration,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    const TEST_DEVICE_BLOCK_SIZE: u32 = 512;

    // Some tests (the preallocate_range ones) currently assume that the data only occupies a single
    // device block.
    const TEST_DATA_OFFSET: u64 = 5000;
    const TEST_DATA: &[u8] = b"hello";
    const TEST_OBJECT_SIZE: u64 = 5678;

    async fn test_filesystem() -> OpenFxFilesystem {
        let device = DeviceHolder::new(FakeDevice::new(16384, TEST_DEVICE_BLOCK_SIZE));
        FxFilesystem::new_empty(device).await.expect("new_empty failed")
    }

    async fn test_filesystem_and_object() -> (OpenFxFilesystem, CachingObjectHandle<ObjectStore>) {
        let fs = test_filesystem().await;
        let store = fs.root_store();
        let handle;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        handle =
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), None)
                .await
                .expect("create_object failed");
        transaction.commit().await.expect("commit failed");
        let object = CachingObjectHandle::new(handle);
        {
            let align = TEST_DATA_OFFSET as usize % TEST_DEVICE_BLOCK_SIZE as usize;
            let mut buf = object.allocate_buffer(align + TEST_DATA.len());
            buf.as_mut_slice()[align..].copy_from_slice(TEST_DATA);
            object
                .write_or_append(Some(TEST_DATA_OFFSET), buf.subslice(align..))
                .await
                .expect("write failed");
        }
        object.truncate(TEST_OBJECT_SIZE).await.expect("truncate failed");
        object.flush().await.expect("flush failed");
        (fs, object)
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
        let fs = test_filesystem().await;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let handle = ObjectStore::create_object(
            &fs.root_store(),
            &mut transaction,
            HandleOptions::default(),
            None,
        )
        .await
        .expect("create_object failed");
        transaction.commit().await.expect("transaction commit failed");
        let object = CachingObjectHandle::new(handle);

        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        object.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");

        object.flush().await.expect("flush failed");

        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        object.write_or_append(Some(100), buf.as_ref()).await.expect("write failed");

        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        object.write_or_append(Some(TEST_DATA_OFFSET), buf.as_ref()).await.expect("write failed");

        let len = object.get_size() as usize;
        assert_eq!(len, TEST_DATA_OFFSET as usize + TEST_DATA.len());
        let mut buf = object.allocate_buffer(len);
        buf.as_mut_slice().fill(123u8);
        assert_eq!(object.read(0, buf.as_mut()).await.expect("read failed"), len);

        let mut expected = vec![0u8; len];
        expected[..TEST_DATA.len()].copy_from_slice(TEST_DATA);
        expected[100..100 + TEST_DATA.len()].copy_from_slice(TEST_DATA);
        expected[TEST_DATA_OFFSET as usize..TEST_DATA_OFFSET as usize + TEST_DATA.len()]
            .copy_from_slice(TEST_DATA);
        assert_eq!(buf.as_slice(), &expected);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_after_truncate_and_extend() {
        let (fs, object) = test_filesystem_and_object().await;

        // Arrange for there to be <extent><deleted-extent><extent>.
        let mut buf = object.allocate_buffer(TEST_DATA.len());
        buf.as_mut_slice().copy_from_slice(TEST_DATA);
        // This adds an extent at 0..512.
        object.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");
        object.truncate(3).await.expect("truncate failed"); // This deletes 512..1024.
        let data = b"foo";
        let offset = 1500u64;
        let align = (offset % fs.block_size() as u64) as usize;
        let mut buf = object.allocate_buffer(align + data.len());
        buf.as_mut_slice()[align..].copy_from_slice(data);
        object.write_or_append(Some(1500), buf.subslice(align..)).await.expect("write failed"); // This adds 1024..1536.

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
        object.write_or_append(Some(0), buffer.as_ref()).await.expect("write failed");

        let store = object.owner();
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        let handle2 =
            ObjectStore::create_object(&store, &mut transaction, HandleOptions::default(), None)
                .await
                .expect("create_object failed");
        transaction.commit().await.expect("commit failed");
        let object2 = CachingObjectHandle::new(handle2);
        let mut ef_buffer = object.allocate_buffer(512);
        ef_buffer.as_mut_slice().fill(0xef);
        object2.write_or_append(Some(0), ef_buffer.as_ref()).await.expect("write failed");

        let mut buffer = object.allocate_buffer(512);
        buffer.as_mut_slice().fill(0xaf);
        object.write_or_append(Some(512), buffer.as_ref()).await.expect("write failed");
        object.truncate(1536).await.expect("truncate failed");
        object2.write_or_append(Some(512), ef_buffer.as_ref()).await.expect("write failed");

        let mut buffer = object.allocate_buffer(2048);
        buffer.as_mut_slice().fill(123);
        assert_eq!(object.read(0, buffer.as_mut()).await.expect("read failed"), 1536);
        assert_eq!(&buffer.as_slice()[..1024], &[0xaf; 1024]);
        assert_eq!(&buffer.as_slice()[1024..1536], &[0; 512]);
        assert_eq!(object2.read(0, buffer.as_mut()).await.expect("read failed"), 1024);
        assert_eq!(&buffer.as_slice()[..1024], &[0xef; 1024]);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncate_deallocates_old_extents() {
        let (fs, object) = test_filesystem_and_object().await;
        let mut buf = object.allocate_buffer(5 * fs.block_size() as usize);
        buf.as_mut_slice().fill(0xaa);
        object.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");
        object.flush().await.expect("flush failed");

        let allocator = fs.allocator();
        let allocated_before_truncate = allocator.get_allocated_bytes();
        object.truncate(fs.block_size() as u64).await.expect("truncate failed");
        object.flush().await.expect("flush failed");
        let allocated_after = allocator.get_allocated_bytes();
        assert!(
            allocated_after < allocated_before_truncate,
            "before = {} after = {}",
            allocated_before_truncate,
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
                object.object_id(),
                Options { borrow_metadata_space: true, ..Default::default() },
            )
            .await
            .expect("purge failed");

        assert_eq!(allocated_before - allocator.get_allocated_bytes(), fs.block_size() as u64);

        let layer_set = store.tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        let mut found_tombstone = false;
        while let Some(ItemRef { key: ObjectKey { object_id, data }, value, .. }) = iter.get() {
            if *object_id == object.object_id() {
                match (data, value) {
                    // Tombstone entry
                    (ObjectKeyData::Object, ObjectValue::None) => {
                        assert!(!found_tombstone);
                        found_tombstone = true;
                    }
                    // We don't expect anything else.
                    _ => assert!(false, "Unexpected item {:?}", iter.get()),
                }
            }
            iter.advance().await.expect("advance failed");
        }
        assert!(found_tombstone);

        fs.close().await.expect("Close failed");
    }

    #[fasync::run(10, test)]
    async fn test_racy_reads() {
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
        let object = Arc::new(CachingObjectHandle::new(handle));
        transaction.commit().await.expect("commit failed");
        for _ in 0..100 {
            let cloned_object = object.clone();
            let writer = fasync::Task::spawn(async move {
                let mut buf = cloned_object.allocate_buffer(10);
                buf.as_mut_slice().fill(123);
                cloned_object.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");
                cloned_object.flush().await.expect("flush failed");
            });
            let cloned_object = object.clone();
            let reader = fasync::Task::spawn(async move {
                let wait_time = rand::thread_rng().gen_range(0..5);
                fasync::Timer::new(Duration::from_millis(wait_time)).await;
                let mut buf = cloned_object.allocate_buffer(10);
                buf.as_mut_slice().fill(23);
                let amount = cloned_object.read(0, buf.as_mut()).await.expect("write failed");
                // If we succeed in reading data, it must include the write; i.e. if we see the size
                // change, we should see the data too.
                if amount != 0 {
                    assert_eq!(amount, 10);
                    assert_eq!(buf.as_slice(), &[123; 10]);
                }
            });
            writer.await;
            reader.await;
            object.truncate(0).await.expect("truncate failed");
            object.flush().await.expect("flush failed");
        }
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_properties() {
        let (fs, object) = test_filesystem_and_object().await;
        let crtime = Timestamp::from_nanos(1234u64);
        let mtime = Timestamp::from_nanos(5678u64);

        object
            .write_timestamps(Some(crtime.clone()), None)
            .await
            .expect("update_timestamps failed");
        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_ne!(properties.modification_time, mtime);

        object.write_timestamps(None, Some(mtime.clone())).await.expect("update_timestamps failed");
        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_eq!(properties.modification_time, mtime);

        // Writes should update mtime.
        let mut buf = object.allocate_buffer(5);
        buf.as_mut_slice().copy_from_slice(b"hello");
        object.write_or_append(Some(0), buf.as_ref()).await.expect("write failed");

        let properties = object.get_properties().await.expect("get_properties failed");
        assert_eq!(properties.creation_time, crtime);
        assert_ne!(properties.modification_time, mtime);
        fs.close().await.expect("Close failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cached_writes() {
        let fs = test_filesystem().await;
        let object_id = {
            let handle;
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            handle = ObjectStore::create_object(
                &fs.root_store(),
                &mut transaction,
                HandleOptions::default(),
                None,
            )
            .await
            .expect("create_object failed");
            transaction.commit().await.expect("transaction commit failed");
            let object = CachingObjectHandle::new(handle);

            let mut buf = object.allocate_buffer(2 * CACHE_READ_AHEAD_SIZE as usize);

            // Simple case is an aligned, non-sparse write.
            buf.as_mut_slice()[..12].copy_from_slice(b"hello, mars!");
            object.write_or_append(Some(0), buf.subslice(..12)).await.expect("write failed");
            assert_eq!(object.get_size(), 12);

            // Partially overwrite that write.
            buf.as_mut_slice()[..6].copy_from_slice(b"earth!");
            object.write_or_append(Some(7), buf.subslice(..6)).await.expect("write failed");
            assert_eq!(object.get_size(), 13);

            // Also do an unaligned, sparse appending write.
            buf.as_mut_slice().fill(0xaa);
            object
                .write_or_append(Some(2 * CACHE_READ_AHEAD_SIZE - 1), buf.as_ref())
                .await
                .expect("write failed");

            // Also do an unaligned, sparse non-appending write.
            buf.as_mut_slice().fill(0xbb);
            object.write_or_append(Some(8000), buf.subslice(..500)).await.expect("write failed");

            // Also truncate.
            object.truncate(4 * CACHE_READ_AHEAD_SIZE - 2).await.expect("truncate failed");

            object.flush().await.expect("flush failed");

            object.object_id()
        };

        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen(false);
        let fs = FxFilesystem::open(device).await.expect("FS open failed");

        let handle =
            ObjectStore::open_object(&fs.root_store(), object_id, HandleOptions::default(), None)
                .await
                .expect("open_object failed");
        let object = CachingObjectHandle::new(handle);
        assert_eq!(object.get_size(), 4 * CACHE_READ_AHEAD_SIZE - 2);
        let mut buf = object.allocate_buffer(object.get_size() as usize);
        object.read(0, buf.as_mut()).await.expect("read failed");
        assert_eq!(&buf.as_slice()[..13], b"hello, earth!");
        assert_eq!(buf.as_slice()[13..8000], [0u8; 7987]);
        assert_eq!(buf.as_slice()[8000..8500], [0xbb; 500]);
        assert_eq!(
            buf.as_slice()[8500..2 * CACHE_READ_AHEAD_SIZE as usize - 1],
            [0u8; 2 * CACHE_READ_AHEAD_SIZE as usize - 8501]
        );
        assert_eq!(
            buf.as_slice()[2 * CACHE_READ_AHEAD_SIZE as usize - 1..],
            vec![0xaa; 2 * CACHE_READ_AHEAD_SIZE as usize - 1]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cached_writes_unflushed() {
        let fs = test_filesystem().await;
        let object_id = {
            let object;
            let mut transaction = fs
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            object = ObjectStore::create_object(
                &fs.root_store(),
                &mut transaction,
                HandleOptions::default(),
                None,
            )
            .await
            .expect("create_object failed");
            transaction.commit().await.expect("transaction commit failed");
            let object = CachingObjectHandle::new(object);

            let mut buf = object.allocate_buffer(2 * CACHE_READ_AHEAD_SIZE as usize + 6);

            // First, do a write and immediately flush. Only this should persist.
            buf.as_mut_slice()[..5].copy_from_slice(b"hello");
            object.write_or_append(Some(0), buf.subslice(..5)).await.expect("write failed");
            object.flush().await.expect("Flush failed");
            assert_eq!(object.get_size(), 5);

            buf.as_mut_slice()[..5].copy_from_slice(b"bye!!");
            object.write_or_append(Some(0), buf.subslice(..5)).await.expect("write failed");

            buf.as_mut_slice().fill(0xaa);
            object
                .write_or_append(Some(CACHE_READ_AHEAD_SIZE - 5), buf.as_ref())
                .await
                .expect("write failed");
            buf.as_mut_slice().fill(0xbb);
            object
                .write_or_append(Some(CACHE_READ_AHEAD_SIZE + 1), buf.as_ref())
                .await
                .expect("write failed");
            object.truncate(3 * CACHE_READ_AHEAD_SIZE + 6).await.expect("truncate failed");

            let mut buf = object.allocate_buffer(object.get_size() as usize);
            buf.as_mut_slice().fill(0x00);
            object.read(0, buf.as_mut()).await.expect("read failed");
            assert_eq!(&buf.as_slice()[..5], b"bye!!");
            assert_eq!(
                &buf.as_slice()[5..CACHE_READ_AHEAD_SIZE as usize - 5],
                vec![0u8; CACHE_READ_AHEAD_SIZE as usize - 10]
            );
            assert_eq!(
                &buf.as_slice()
                    [CACHE_READ_AHEAD_SIZE as usize - 5..CACHE_READ_AHEAD_SIZE as usize + 1],
                vec![0xaa; 6]
            );
            assert_eq!(
                &buf.as_slice()
                    [CACHE_READ_AHEAD_SIZE as usize + 1..CACHE_READ_AHEAD_SIZE as usize + 65536],
                vec![0xbb; 65535]
            );

            object.object_id()
        };

        fs.close().await.expect("Close failed");
        let device = fs.take_device().await;
        device.reopen(false);
        let fs = FxFilesystem::open(device).await.expect("FS open failed");

        let object =
            ObjectStore::open_object(&fs.root_store(), object_id, HandleOptions::default(), None)
                .await
                .expect("open_object failed");
        let object = CachingObjectHandle::new(object);
        assert_eq!(object.get_size(), 5);
        let mut buf = object.allocate_buffer(5);
        object.read(0, buf.subslice_mut(..5)).await.expect("read failed");
        assert_eq!(&buf.as_slice()[..5], b"hello");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_flush() {
        let fs = test_filesystem().await;
        let object;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object = ObjectStore::create_object(
            &fs.root_store(),
            &mut transaction,
            HandleOptions::default(),
            None,
        )
        .await
        .expect("create_object failed");
        transaction.commit().await.expect("transaction commit failed");
        let object = CachingObjectHandle::new(object);

        const DATA_TO_WRITE: usize = super::FLUSH_BATCH_SIZE as usize + 32_768;
        let mut data = vec![123u8; DATA_TO_WRITE];
        object.write_or_append_cached(None, &data[..]).await.expect("write failed");

        object.flush().await.expect("flush failed");

        let object_id = object.object_id();
        std::mem::drop(object);
        let object =
            ObjectStore::open_object(&fs.root_store(), object_id, HandleOptions::default(), None)
                .await
                .expect("open_object failed");
        let object = CachingObjectHandle::new(object);

        data.fill(0);
        object.read_cached(0, &mut data[..]).await.expect("read failed");

        for chunk in data.chunks(32_768) {
            assert_eq!(chunk, &[123u8; 32_768]);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_trim_before_flush() {
        let fs = test_filesystem().await;
        let store = fs.root_store();
        let root_directory =
            Directory::open(&store, store.root_directory_object_id()).await.expect("open failed");
        let object;
        let mut transaction = fs
            .clone()
            .new_transaction(&[], Options::default())
            .await
            .expect("new_transaction failed");
        object = root_directory
            .create_child_file(&mut transaction, "foo")
            .await
            .expect("create_child_file failed");
        let block_size = object.block_size();
        let object_size = (TRANSACTION_MUTATION_THRESHOLD as u64 + 10) * 2 * block_size;

        // Create enough extents in it such that when we truncate the object it will require more
        // than one transaction.
        {
            let mut buf = object.allocate_buffer(5);
            buf.as_mut_slice().fill(1);
            // Write every other block.
            for offset in (0..object_size).into_iter().step_by(2 * block_size as usize) {
                object
                    .txn_write(&mut transaction, offset, buf.as_ref())
                    .await
                    .expect("write failed");
            }
            transaction.commit().await.expect("commit failed");
        }

        // Truncate the file, but don't commit more than the first transaction.
        let mut transaction = object.new_transaction().await.expect("new_transaction failed");
        assert_eq!(object.shrink(&mut transaction, 0).await.expect("shrink failed").0, true);
        transaction.commit().await.expect("commit failed");

        // Grow the file using caching_object_handle.
        let object = CachingObjectHandle::new(object);
        object.truncate(object_size).await.expect("truncate failed");
        object.flush().await.expect("flush failed");

        // The tail of the file should have been zeroed.
        let buf = object
            .read_uncached(object_size - 2 * block_size..object_size)
            .await
            .expect("read_uncached failed");
        assert_eq!(buf.as_slice(), &vec![0; 2 * block_size as usize]);

        fsck_with_options(
            fs.clone(),
            &FsckOptions {
                fail_on_warning: true,
                on_error: Box::new(|err| println!("fsck error: {:?}", err)),
                ..Default::default()
            },
        )
        .await
        .expect("fsck_with_options failed");
    }
}
