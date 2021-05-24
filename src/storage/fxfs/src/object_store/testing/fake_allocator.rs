// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{
        allocator::Allocator,
        filesystem::Mutations,
        transaction::{Mutation, Transaction},
    },
    anyhow::Error,
    async_trait::async_trait,
    std::{
        any::Any,
        ops::Range,
        sync::{Arc, Mutex},
    },
};

const ALLOCATOR_OBJECT_ID: u64 = 1;

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

    pub fn allocated(&self) -> usize {
        self.0.lock().unwrap().alloc_bytes
    }

    pub fn deallocated(&self) -> usize {
        self.0.lock().unwrap().dealloc_bytes
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

    async fn deallocate(&self, _transaction: &mut Transaction<'_>, device_range: Range<u64>) {
        let mut inner = self.0.lock().unwrap();
        assert!(device_range.end <= inner.next_offset);
        let len = device_range.end - device_range.start;
        inner.dealloc_bytes += len as usize;
        assert!(inner.dealloc_bytes <= inner.alloc_bytes);
    }

    async fn reserve(&self, _transaction: &mut Transaction<'_>, device_range: Range<u64>) {
        let mut inner = self.0.lock().unwrap();
        inner.next_offset = std::cmp::max(device_range.end, inner.next_offset);
    }

    fn as_mutations(self: Arc<Self>) -> Arc<dyn Mutations> {
        self
    }

    fn as_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync> {
        self
    }

    async fn did_flush_device(&self, _flush_log_offset: u64) {}
}

#[async_trait]
impl Mutations for FakeAllocator {
    async fn apply_mutation(&self, _mutation: Mutation, _log_offset: u64, _replay: bool) {}

    fn drop_mutation(&self, _mutation: Mutation) {}

    async fn flush(&self) -> Result<(), Error> {
        Ok(())
    }
}
