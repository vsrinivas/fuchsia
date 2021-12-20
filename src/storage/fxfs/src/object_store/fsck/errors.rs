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
pub enum FsckIssue {
    /// Warnings don't prevent the filesystem from mounting and don't fail fsck, but they indicate a
    /// consistency issue.
    Warning(FsckWarning),
    /// Errors prevent the filesystem from mounting, and will result in fsck failing, but will let
    /// fsck continue to run to find more issues.
    Error(FsckError),
    /// Fatal errors are like Errors, but they're serious enough that fsck should be halted, as any
    /// further results will probably be false positives.
    Fatal(FsckFatal),
}

impl FsckIssue {
    /// Translates an error to a human-readable string, intended for reporting errors to the user.
    /// For debugging, std::fmt::Debug is preferred.
    // TODO(jfsulliv): Localization
    pub fn to_string(&self) -> String {
        match self {
            FsckIssue::Warning(w) => format!("WARNING: {}", w.to_string()),
            FsckIssue::Error(e) => format!("ERROR: {}", e.to_string()),
            FsckIssue::Fatal(f) => format!("FATAL: {}", f.to_string()),
        }
    }
    pub fn is_error(&self) -> bool {
        match self {
            FsckIssue::Error(_) | FsckIssue::Fatal(_) => true,
            FsckIssue::Warning(_) => false,
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
pub enum FsckError {
    AllocatedBytesMismatch(u64, u64),
    AllocationMismatch(Allocation, Allocation),
    ExtraAllocations(Vec<Allocation>),
    MalformedAllocation(Allocation),
    MalformedExtent(u64, u64, Range<u64>, u64),
    MisalignedAllocation(Allocation),
    MisalignedExtent(u64, u64, Range<u64>, u64),
    MissingAllocation(Allocation),
    ObjectCountMismatch(u64, u64, u64),
    RefCountMismatch(u64, u64, u64),
    VolumeInChildStore(u64, u64),
}

impl FsckError {
    fn to_string(&self) -> String {
        match self {
            FsckError::AllocationMismatch(expected, actual) => {
                format!("Expected allocation {:?} but found allocation {:?}", expected, actual)
            }
            FsckError::AllocatedBytesMismatch(expected, actual) => {
                format!("Expected {} bytes allocated, but found {} bytes", expected, actual)
            }
            FsckError::ExtraAllocations(allocations) => {
                format!("Unexpected allocations {:?}", allocations)
            }
            FsckError::MalformedAllocation(allocations) => {
                format!("Malformed allocation {:?}", allocations)
            }
            FsckError::MalformedExtent(store_id, oid, extent, device_offset) => {
                format!(
                    "Extent {:?} (offset {}) for object {} in store {} is malformed",
                    extent, device_offset, oid, store_id
                )
            }
            FsckError::MisalignedAllocation(allocations) => {
                format!("Misaligned allocation {:?}", allocations)
            }
            FsckError::MisalignedExtent(store_id, oid, extent, device_offset) => {
                format!(
                    "Extent {:?} (offset {}) for object {} in store {} is misaligned",
                    extent, device_offset, oid, store_id
                )
            }
            FsckError::MissingAllocation(allocation) => {
                format!("Expected but didn't find allocation {:?}", allocation)
            }
            FsckError::ObjectCountMismatch(store_id, expected, actual) => {
                format!("Store {} had {} objects, expected {}", store_id, actual, expected)
            }
            FsckError::RefCountMismatch(oid, expected, actual) => {
                format!("Object {} had {} references, expected {}", oid, actual, expected)
            }
            FsckError::VolumeInChildStore(store_id, object_id) => {
                format!(
                    "Volume {} found in child store {} instead of root store",
                    object_id, store_id
                )
            }
        }
    }
}

#[derive(Clone, Debug)]
pub enum FsckFatal {
    MalformedGraveyard,
    MalformedLayerFile(u64, u64),
    MalformedStore(u64),
    MisOrderedLayerFile(u64, u64),
    MissingLayerFile(u64, u64),
    MissingStoreInfo(u64),
    OverlappingKeysInLayerFile(u64, u64, Key, Key),
}

impl FsckFatal {
    fn to_string(&self) -> String {
        match self {
            FsckFatal::MalformedGraveyard => {
                "Graveyard is malformed; root store is inconsistent".to_string()
            }
            FsckFatal::MalformedLayerFile(store_id, layer_file_id) => {
                format!("Layer file {} in object store {} is malformed", layer_file_id, store_id)
            }
            FsckFatal::MalformedStore(id) => {
                format!("Object store {} is malformed; root store is inconsistent", id)
            }
            FsckFatal::MisOrderedLayerFile(store_id, layer_file_id) => {
                format!(
                    "Layer file {} for store/allocator {} contains out-of-order records",
                    store_id, layer_file_id
                )
            }
            FsckFatal::MissingLayerFile(store_id, layer_file_id) => {
                format!(
                    "Object store {} requires layer file {} which is missing",
                    store_id, layer_file_id
                )
            }
            FsckFatal::MissingStoreInfo(id) => {
                format!("Object store {} has no store info object", id)
            }
            FsckFatal::OverlappingKeysInLayerFile(store_id, layer_file_id, key1, key2) => {
                format!(
                    "Layer file {} for store/allocator {} contains overlapping keys {:?} and {:?}",
                    store_id, layer_file_id, key1, key2
                )
            }
        }
    }
}
