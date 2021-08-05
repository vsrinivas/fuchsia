// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        object_handle::{ObjectHandle, ObjectProperties},
        object_store::{
            transaction::{
                LockKey, LockManager, MetadataReservation, Options, ReadGuard, Transaction,
                TransactionHandler, TransactionLocks, WriteGuard,
            },
            Timestamp,
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    std::{
        cmp::min,
        ops::Range,
        sync::{Arc, Mutex},
        vec::Vec,
    },
    storage_device::{
        buffer::{Buffer, BufferRef, MutableBufferRef},
        buffer_allocator::{BufferAllocator, MemBufferSource},
    },
};

pub struct FakeObject {
    buf: Mutex<Vec<u8>>,
    lock_manager: LockManager,
}

impl FakeObject {
    pub fn new() -> Self {
        FakeObject { buf: Mutex::new(Vec::new()), lock_manager: LockManager::new() }
    }

    pub fn read(&self, offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        let our_buf = self.buf.lock().unwrap();
        let to_do = min(buf.len(), our_buf.len() - offset as usize);
        buf.as_mut_slice()[0..to_do]
            .copy_from_slice(&our_buf[offset as usize..offset as usize + to_do]);
        Ok(to_do)
    }

    pub fn write(&self, offset: u64, buf: BufferRef<'_>) -> Result<(), Error> {
        let mut our_buf = self.buf.lock().unwrap();
        let required_len = offset as usize + buf.len();
        if our_buf.len() < required_len {
            our_buf.resize(required_len, 0);
        }
        our_buf[offset as usize..offset as usize + buf.len()].copy_from_slice(buf.as_slice());
        Ok(())
    }

    pub fn get_size(&self) -> u64 {
        self.buf.lock().unwrap().len() as u64
    }
}

#[async_trait]
impl TransactionHandler for FakeObject {
    async fn new_transaction<'a>(
        self: Arc<Self>,
        locks: &[LockKey],
        _options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        Ok(Transaction::new(self, MetadataReservation::Borrowed, &[], locks).await)
    }

    async fn new_transaction_with_locks<'a>(
        self: Arc<Self>,
        locks: TransactionLocks<'_>,
        _options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        Ok(Transaction::new_with_locks(self, MetadataReservation::Borrowed, &[], locks).await)
    }

    async fn transaction_lock<'a>(&'a self, lock_keys: &[LockKey]) -> TransactionLocks<'a> {
        let lock_manager: &LockManager = self.as_ref();
        TransactionLocks(lock_manager.txn_lock(lock_keys).await)
    }

    async fn commit_transaction(
        self: Arc<Self>,
        transaction: &mut Transaction<'_>,
    ) -> Result<u64, Error> {
        std::mem::take(&mut transaction.mutations);
        Ok(0)
    }

    fn drop_transaction(&self, transaction: &mut Transaction<'_>) {
        self.lock_manager.drop_transaction(transaction);
    }

    async fn read_lock<'a>(&'a self, lock_keys: &[LockKey]) -> ReadGuard<'a> {
        self.lock_manager.read_lock(lock_keys).await
    }

    async fn write_lock<'a>(&'a self, lock_keys: &[LockKey]) -> WriteGuard<'a> {
        self.lock_manager.write_lock(lock_keys).await
    }
}

impl AsRef<LockManager> for FakeObject {
    fn as_ref(&self) -> &LockManager {
        &self.lock_manager
    }
}

pub struct FakeObjectHandle {
    object: Arc<FakeObject>,
    allocator: BufferAllocator,
}

impl FakeObjectHandle {
    pub fn new(object: Arc<FakeObject>) -> Self {
        // TODO(jfsulliv): Should this take an allocator as parameter?
        let allocator = BufferAllocator::new(512, Box::new(MemBufferSource::new(32 * 1024 * 1024)));
        FakeObjectHandle { object, allocator }
    }
}

#[async_trait]
impl ObjectHandle for FakeObjectHandle {
    fn object_id(&self) -> u64 {
        0
    }

    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.allocator.allocate_buffer(size)
    }

    fn block_size(&self) -> u32 {
        self.allocator.block_size() as u32
    }

    async fn read(&self, offset: u64, buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        self.object.read(offset, buf)
    }

    async fn txn_write<'a>(
        &'a self,
        _transaction: &mut Transaction<'a>,
        offset: u64,
        buf: BufferRef<'_>,
    ) -> Result<(), Error> {
        self.object.write(offset, buf)
    }

    async fn overwrite(&self, offset: u64, buf: BufferRef<'_>) -> Result<(), Error> {
        self.object.write(offset, buf)
    }

    fn get_size(&self) -> u64 {
        self.object.get_size()
    }

    async fn truncate<'a>(
        &'a self,
        _transaction: &mut Transaction<'a>,
        length: u64,
    ) -> Result<(), Error> {
        self.object.buf.lock().unwrap().resize(length as usize, 0);
        Ok(())
    }

    async fn preallocate_range<'a>(
        &'a self,
        _transaction: &mut Transaction<'a>,
        _range: Range<u64>,
    ) -> Result<Vec<Range<u64>>, Error> {
        panic!("Unsupported");
    }

    async fn write_timestamps<'a>(
        &'a self,
        _transaction: &mut Transaction<'a>,
        _ctime: Option<Timestamp>,
        _mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        Ok(())
    }

    async fn get_properties(&self) -> Result<ObjectProperties, Error> {
        let size = self.object.get_size();
        Ok(ObjectProperties {
            refs: 1u64,
            allocated_size: size,
            data_attribute_size: size,
            creation_time: Timestamp::zero(),
            modification_time: Timestamp::zero(),
            sub_dirs: 0,
        })
    }

    async fn new_transaction_with_options<'a>(
        &self,
        options: Options<'a>,
    ) -> Result<Transaction<'a>, Error> {
        self.object.clone().new_transaction(&[], options).await
    }

    async fn flush_device(&self) -> Result<(), Error> {
        Ok(())
    }
}
