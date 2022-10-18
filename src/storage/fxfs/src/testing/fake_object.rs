// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        object_handle::{
            GetProperties, ObjectHandle, ObjectProperties, ReadObjectHandle, WriteObjectHandle,
        },
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
        convert::TryInto,
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

    fn read(&self, offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        let our_buf = self.buf.lock().unwrap();
        let to_do = min(buf.len(), our_buf.len() - offset as usize);
        buf.as_mut_slice()[0..to_do]
            .copy_from_slice(&our_buf[offset as usize..offset as usize + to_do]);
        Ok(to_do)
    }

    fn write_or_append(&self, offset: Option<u64>, buf: BufferRef<'_>) -> Result<u64, Error> {
        let mut our_buf = self.buf.lock().unwrap();
        let offset = offset.unwrap_or(our_buf.len() as u64);
        let required_len = offset as usize + buf.len();
        if our_buf.len() < required_len {
            our_buf.resize(required_len, 0);
        }
        our_buf[offset as usize..offset as usize + buf.len()].copy_from_slice(buf.as_slice());
        Ok(our_buf.len() as u64)
    }

    fn truncate(&self, size: u64) {
        self.buf.lock().unwrap().resize(size as usize, 0);
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

    async fn transaction_lock<'a>(&'a self, lock_keys: &[LockKey]) -> TransactionLocks<'a> {
        let lock_manager: &LockManager = self.as_ref();
        TransactionLocks(lock_manager.txn_lock(lock_keys).await)
    }

    async fn commit_transaction(
        self: Arc<Self>,
        transaction: &mut Transaction<'_>,
        callback: &mut (dyn FnMut(u64) + Send),
    ) -> Result<u64, Error> {
        std::mem::take(&mut transaction.mutations);
        callback(0);
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
    pub fn new_with_block_size(object: Arc<FakeObject>, block_size: usize) -> Self {
        let allocator =
            BufferAllocator::new(block_size, Box::new(MemBufferSource::new(32 * 1024 * 1024)));
        Self { object, allocator }
    }
    pub fn new(object: Arc<FakeObject>) -> Self {
        Self::new_with_block_size(object, 512)
    }
}

#[async_trait]
impl ObjectHandle for FakeObjectHandle {
    fn object_id(&self) -> u64 {
        0
    }

    fn block_size(&self) -> u64 {
        self.allocator.block_size().try_into().unwrap()
    }

    fn get_size(&self) -> u64 {
        self.object.get_size()
    }

    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.allocator.allocate_buffer(size)
    }
}

#[async_trait]
impl GetProperties for FakeObjectHandle {
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
}

#[async_trait]
impl ReadObjectHandle for FakeObjectHandle {
    async fn read(&self, offset: u64, buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        self.object.read(offset, buf)
    }
}

#[async_trait]
impl WriteObjectHandle for FakeObjectHandle {
    async fn write_or_append(&self, offset: Option<u64>, buf: BufferRef<'_>) -> Result<u64, Error> {
        self.object.write_or_append(offset, buf)
    }

    async fn truncate(&self, size: u64) -> Result<(), Error> {
        self.object.truncate(size);
        Ok(())
    }

    async fn write_timestamps<'a>(
        &'a self,
        _crtime: Option<Timestamp>,
        _mtime: Option<Timestamp>,
    ) -> Result<(), Error> {
        Ok(())
    }

    async fn flush(&self) -> Result<(), Error> {
        Ok(())
    }
}
