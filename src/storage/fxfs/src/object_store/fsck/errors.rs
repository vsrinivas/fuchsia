// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::types::ItemRef,
        object_store::allocator::{AllocatorKey, AllocatorValue},
    },
    std::ops::Range,
};

#[derive(Clone, Debug)]
pub enum FsckError {
    Warning(FsckWarning),
    Fatal(FsckFatal),
}

impl FsckError {
    /// Translates an error to a human-readable string, intended for reporting errors to the user.
    /// For debugging, std::fmt::Debug is preferred.
    // TODO(jfsulliv): Localization
    pub fn to_string(&self) -> String {
        match self {
            FsckError::Warning(w) => format!("WARNING: {}", w.to_string()),
            FsckError::Fatal(f) => format!("ERROR: {}", f.to_string()),
        }
    }
    pub fn is_fatal(&self) -> bool {
        if let FsckError::Fatal(_) = self {
            true
        } else {
            false
        }
    }
}

#[derive(Clone, Debug)]
pub enum FsckWarning {
    // An object ID was found which had no parent.
    // Parameters are (store_id, object_id)
    OrphanedObject(u64, u64),
}

impl FsckWarning {
    fn to_string(&self) -> String {
        match self {
            FsckWarning::OrphanedObject(store_id, object_id) => {
                format!("Orphaned object {} was found in store {}", object_id, store_id)
            }
        }
    }
}

#[derive(Clone, Debug)]
#[allow(dead_code)]
pub struct Allocation {
    range: Range<u64>,
    ref_count: i64,
}

impl From<ItemRef<'_, AllocatorKey, AllocatorValue>> for Allocation {
    fn from(item: ItemRef<'_, AllocatorKey, AllocatorValue>) -> Self {
        Self { range: item.key.device_range.clone(), ref_count: item.value.delta }
    }
}

#[derive(Clone, Debug)]
#[allow(dead_code)]
pub struct Key(String);

impl<K: std::fmt::Debug, V> From<ItemRef<'_, K, V>> for Key {
    fn from(item: ItemRef<'_, K, V>) -> Self {
        Self(format!("{:?}", item.key))
    }
}

#[derive(Clone, Debug)]
pub enum FsckFatal {
    MalformedGraveyard,
    MalformedStore(u64),
    MalformedLayerFile(u64, u64),
    MisOrderedLayerFile(u64, u64),
    OverlappingKeysInLayerFile(u64, u64, Key, Key),
    MissingStoreInfo(u64),
    MissingLayerFile(u64, u64),
    AllocationMismatch(Allocation, Allocation),
    AllocatedBytesMismatch(u64, u64),
    MissingAllocation(Allocation),
    ExtraAllocations(Vec<Allocation>),
    MisalignedAllocation(Allocation),
    MalformedAllocation(Allocation),
    MisalignedExtent(u64, u64, Range<u64>, u64),
    MalformedExtent(u64, u64, Range<u64>, u64),
    RefCountMismatch(u64, u64, u64),
    ObjectCountMismatch(u64, u64, u64),
}

impl FsckFatal {
    fn to_string(&self) -> String {
        match self {
            FsckFatal::MalformedGraveyard => {
                "Graveyard file is malformed; root store is inconsistent".to_string()
            }
            FsckFatal::MalformedStore(id) => {
                format!("Object store {} is malformed; root store is inconsistent", id)
            }
            FsckFatal::MalformedLayerFile(store_id, layer_file_id) => {
                format!("Layer file {} in object store {} is malformed", layer_file_id, store_id)
            }
            FsckFatal::MisOrderedLayerFile(store_id, layer_file_id) => {
                format!(
                    "Layer file {} for store/allocator {} contains out-of-order records",
                    store_id, layer_file_id
                )
            }
            FsckFatal::OverlappingKeysInLayerFile(store_id, layer_file_id, key1, key2) => {
                format!(
                    "Layer file {} for store/allocator {} contains overlapping keys {:?} and {:?}",
                    store_id, layer_file_id, key1, key2
                )
            }
            FsckFatal::MissingStoreInfo(id) => {
                format!("Object store {} has no store info object", id)
            }
            FsckFatal::MissingLayerFile(store_id, layer_file_id) => {
                format!(
                    "Object store {} requires layer file {} which is missing",
                    store_id, layer_file_id
                )
            }
            FsckFatal::AllocationMismatch(expected, actual) => {
                format!("Expected allocation {:?} but found allocation {:?}", expected, actual)
            }
            FsckFatal::AllocatedBytesMismatch(expected, actual) => {
                format!("Expected {} bytes allocated, but found {} bytes", expected, actual)
            }
            FsckFatal::MissingAllocation(allocation) => {
                format!("Expected but didn't find allocation {:?}", allocation)
            }
            FsckFatal::ExtraAllocations(allocations) => {
                format!("Unexpected allocations {:?}", allocations)
            }
            FsckFatal::MisalignedAllocation(allocations) => {
                format!("Misaligned allocation {:?}", allocations)
            }
            FsckFatal::MalformedAllocation(allocations) => {
                format!("Malformed allocation {:?}", allocations)
            }
            FsckFatal::MisalignedExtent(store_id, oid, extent, device_offset) => {
                format!(
                    "Extent {:?} (offset {}) for object {} in store {} is misaligned",
                    extent, device_offset, oid, store_id
                )
            }
            FsckFatal::MalformedExtent(store_id, oid, extent, device_offset) => {
                format!(
                    "Extent {:?} (offset {}) for object {} in store {} is malformed",
                    extent, device_offset, oid, store_id
                )
            }
            FsckFatal::RefCountMismatch(oid, expected, actual) => {
                format!("Object {} had {} references, expected {}", oid, actual, expected)
            }
            FsckFatal::ObjectCountMismatch(store_id, expected, actual) => {
                format!("Store {} had {} objects, expected {}", store_id, actual, expected)
            }
        }
    }
}
