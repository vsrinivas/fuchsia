// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod merge;

use {
    crate::{
        lsm_tree::types::{Item, OrdLowerBound},
        object_store::transaction::Transaction,
    },
    anyhow::Error,
    async_trait::async_trait,
    serde::{Deserialize, Serialize},
};

/// Allocators must implement this.  An allocator is responsible for allocating ranges on behalf of
/// an object-store.
#[async_trait]
pub trait Allocator: Send + Sync {
    /// Returns the object ID for the allocator.
    fn object_id(&self) -> u64;

    /// Tries to allocate enough space for |object_range| in the specified object and returns the
    /// device ranges allocated.
    async fn allocate(
        &self,
        transaction: &mut Transaction,
        store_object_id: u64,
        object_id: u64,
        attribute_id: u64,
        object_range: std::ops::Range<u64>,
    ) -> Result<std::ops::Range<u64>, Error>;

    /// Deallocates the given device range for the specified object.
    async fn deallocate(
        &self,
        transaction: &mut Transaction,
        store_object_id: u64,
        object_id: u64,
        attribute_id: u64,
        device_range: std::ops::Range<u64>,
        file_offset: u64,
    );
}

// Our allocator implementation tracks extents with a reference count.  At time of writing, these
// reference counts should never exceed 1, but that might change with snapshots and clones.

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AllocatorKey {
    device_range: std::ops::Range<u64>,
}

impl Ord for AllocatorKey {
    fn cmp(&self, other: &AllocatorKey) -> std::cmp::Ordering {
        self.device_range.end.cmp(&other.device_range.end)
    }
}

impl PartialOrd for AllocatorKey {
    fn partial_cmp(&self, other: &AllocatorKey) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl OrdLowerBound for AllocatorKey {
    fn cmp_lower_bound(&self, other: &AllocatorKey) -> std::cmp::Ordering {
        self.device_range.start.cmp(&other.device_range.start)
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AllocatorValue {
    // This is the delta on a reference count for the extent.
    delta: i64,
}

pub type AllocatorItem = Item<AllocatorKey, AllocatorValue>;
