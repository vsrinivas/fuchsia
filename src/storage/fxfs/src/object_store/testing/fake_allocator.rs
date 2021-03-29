// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{
        allocator::Allocator,
        filesystem::ApplyMutations,
        transaction::{Mutation, Transaction},
    },
    anyhow::Error,
    async_trait::async_trait,
    std::{
        ops::Range,
        sync::{Arc, Mutex},
    },
};

const ALLOCATOR_OBJECT_ID: u64 = 1;

pub struct FakeAllocator(Mutex<u64>);

impl FakeAllocator {
    pub fn new() -> Self {
        FakeAllocator(Mutex::new(0))
    }

    pub fn allocated(&self) -> u64 {
        *self.0.lock().unwrap()
    }
}

#[async_trait]
impl Allocator for FakeAllocator {
    fn object_id(&self) -> u64 {
        ALLOCATOR_OBJECT_ID
    }

    async fn allocate(
        &self,
        _transaction: &mut Transaction,
        len: u64,
    ) -> Result<Range<u64>, Error> {
        let mut last_end = self.0.lock().unwrap();
        let result = *last_end..*last_end + len;
        *last_end = result.end;
        Ok(result)
    }

    async fn deallocate(&self, _transaction: &mut Transaction, _device_range: Range<u64>) {}

    async fn flush(&self, _force: bool) -> Result<(), Error> {
        Ok(())
    }

    async fn reserve(&self, _transaction: &mut Transaction, device_range: Range<u64>) {
        let mut last_end = self.0.lock().unwrap();
        *last_end = std::cmp::max(device_range.end, *last_end);
    }

    fn as_apply_mutations(self: Arc<Self>) -> Arc<dyn ApplyMutations> {
        self
    }
}

#[async_trait]
impl ApplyMutations for FakeAllocator {
    async fn apply_mutation(&self, _mutation: Mutation, _replay: bool) {}
}
