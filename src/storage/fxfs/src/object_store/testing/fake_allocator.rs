// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        filesystem::{ApplyContext, JournalingObject},
        object_store::{
            allocator::{Allocator, AllocatorInfo, Reservation, ReservationOwner},
            transaction::{AssocObj, Mutation, Transaction},
        },
        serialized_types::{Version, LATEST_VERSION},
    },
    anyhow::Error,
    async_trait::async_trait,
    std::{
        any::Any,
        collections::BTreeMap,
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

    fn info(&self) -> AllocatorInfo {
        Default::default()
    }

    async fn allocate(
        &self,
        _transaction: &mut Transaction<'_>,
        _store_object_id: u64,
        len: u64,
    ) -> Result<Range<u64>, Error> {
        let mut inner = self.0.lock().unwrap();
        let result = inner.next_offset..inner.next_offset + len;
        inner.next_offset = result.end;
        inner.alloc_bytes += len as usize;
        Ok(result)
    }

    async fn deallocate(
        &self,
        _transaction: &mut Transaction<'_>,
        _object_id: u64,
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
        _store_object_id: u64,
        device_range: Range<u64>,
    ) -> Result<(), Error> {
        let mut inner = self.0.lock().unwrap();
        inner.next_offset = std::cmp::max(device_range.end, inner.next_offset);
        Ok(())
    }

    async fn mark_for_deletion(&self, _transaction: &mut Transaction<'_>, _owner_object_id: u64) {
        unimplemented!();
    }

    fn as_journaling_object(self: Arc<Self>) -> Arc<dyn JournalingObject> {
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

    fn get_allocated_bytes(&self) -> u64 {
        let inner = self.0.lock().unwrap();
        (inner.alloc_bytes - inner.dealloc_bytes) as u64
    }

    fn get_owner_allocated_bytes(&self) -> BTreeMap<u64, i64> {
        unimplemented!();
    }

    fn get_used_bytes(&self) -> u64 {
        self.get_allocated_bytes()
    }
}

impl ReservationOwner for FakeAllocator {
    fn release_reservation(&self, _amount: u64) {}
}

#[async_trait]
impl JournalingObject for FakeAllocator {
    async fn apply_mutation(
        &self,
        _mutation: Mutation,
        _context: &ApplyContext<'_, '_>,
        _associated_object: AssocObj<'_>,
    ) -> Result<(), Error> {
        Ok(())
    }

    fn drop_mutation(&self, _mutation: Mutation, _transaction: &Transaction<'_>) {}

    async fn flush(&self) -> Result<Version, Error> {
        return Ok(LATEST_VERSION);
    }
}
