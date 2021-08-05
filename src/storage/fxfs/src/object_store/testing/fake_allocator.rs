// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{
        allocator::{Allocator, Reservation},
        filesystem::Mutations,
        journal::checksum_list::ChecksumList,
        transaction::{AssocObj, Mutation, Transaction},
    },
    anyhow::Error,
    async_trait::async_trait,
    std::{
        any::Any,
        ops::Range,
        sync::{Arc, Mutex},
    },
};

const ALLOCATOR_OBJECT_ID: u64 = 2;

#[derive(Debug)]
struct Inner {
    next_offset: u64,
    alloc_bytes: usize,
    dealloc_bytes: usize,
}

pub struct FakeAllocator(Mutex<Inner>);

impl FakeAllocator {
    pub fn new() -> Self {
        FakeAllocator(Mutex::new(Inner { next_offset: 0, alloc_bytes: 0, dealloc_bytes: 0 }))
    }
}

#[async_trait]
impl Allocator for FakeAllocator {
    fn object_id(&self) -> u64 {
        ALLOCATOR_OBJECT_ID
    }

    async fn allocate(
        &self,
        _transaction: &mut Transaction<'_>,
        len: u64,
    ) -> Result<Range<u64>, Error> {
        let mut inner = self.0.lock().unwrap();
        let result = inner.next_offset..inner.next_offset + len;
        inner.next_offset = result.end;
        inner.alloc_bytes += len as usize;
        Ok(result)
    }

    fn add_ref(&self, _transaction: &mut Transaction<'_>, _device_range: Range<u64>) {
        unreachable!();
    }

    async fn deallocate(
        &self,
        _transaction: &mut Transaction<'_>,
        device_range: Range<u64>,
    ) -> Result<u64, Error> {
        let mut inner = self.0.lock().unwrap();
        assert!(device_range.end <= inner.next_offset);
        let len = device_range.end - device_range.start;
        inner.dealloc_bytes += len as usize;
        assert!(inner.dealloc_bytes <= inner.alloc_bytes);
        Ok(len)
    }

    async fn mark_allocated(
        &self,
        _transaction: &mut Transaction<'_>,
        device_range: Range<u64>,
    ) -> Result<(), Error> {
        let mut inner = self.0.lock().unwrap();
        inner.next_offset = std::cmp::max(device_range.end, inner.next_offset);
        Ok(())
    }

    fn as_mutations(self: Arc<Self>) -> Arc<dyn Mutations> {
        self
    }

    fn as_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync> {
        self
    }

    async fn did_flush_device(&self, _flush_log_offset: u64) {}

    fn reserve(self: Arc<Self>, amount: u64) -> Option<Reservation> {
        Some(Reservation::new(self, amount))
    }

    fn reserve_at_most(self: Arc<Self>, amount: u64) -> Reservation {
        Reservation::new(self, amount)
    }

    fn release_reservation(&self, _amount: u64) {}

    fn get_allocated_bytes(&self) -> u64 {
        let inner = self.0.lock().unwrap();
        (inner.alloc_bytes - inner.dealloc_bytes) as u64
    }

    fn get_used_bytes(&self) -> u64 {
        self.get_allocated_bytes()
    }

    async fn validate_mutation(
        &self,
        _journal_offset: u64,
        _mutation: &Mutation,
        _checksum_list: &mut ChecksumList,
    ) -> Result<bool, Error> {
        Ok(true)
    }
}

#[async_trait]
impl Mutations for FakeAllocator {
    async fn apply_mutation(
        &self,
        _mutation: Mutation,
        _transaction: Option<&Transaction<'_>>,
        _log_offset: u64,
        _associated_object: AssocObj<'_>,
    ) {
    }

    fn drop_mutation(&self, _mutation: Mutation, _transaction: &Transaction<'_>) {}

    async fn flush(&self) -> Result<(), Error> {
        Ok(())
    }
}
