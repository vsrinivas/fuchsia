// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::types::ItemRef,
        object_store::{
            allocator::{AllocatorKey, AllocatorValue},
            ObjectDescriptor,
        },
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

impl<K: std::fmt::Debug> From<&K> for Key {
    fn from(k: &K) -> Self {
        Self(format!("{:?}", k))
    }
}

#[derive(Clone, Debug)]
#[allow(dead_code)]
pub struct Value(String);

impl<K, V: std::fmt::Debug> From<ItemRef<'_, K, V>> for Value {
    fn from(item: ItemRef<'_, K, V>) -> Self {
        Self(format!("{:?}", item.value))
    }
}

// `From<V: std::fmt::Debug> for Value` creates a recursive definition since Value is Debug, so we
// have to go concrete here.
impl From<ObjectDescriptor> for Value {
    fn from(d: ObjectDescriptor) -> Self {
        Self(format!("{:?}", d))
    }
}

impl<V: std::fmt::Debug> From<&V> for Value {
    fn from(v: &V) -> Self {
        Self(format!("{:?}", v))
    }
}

#[derive(Clone, Debug)]
pub enum FsckWarning {
    ExtentForMissingAttribute(u64, u64, u64),
    ExtentForDirectory(u64, u64),
    ExtentForNonexistentObject(u64, u64),
    GraveyardRecordForAbsentObject(u64, u64),
    InvalidObjectIdInStore(u64, Key, Value),
    OrphanedAttribute(u64, u64, u64),
    OrphanedObject(u64, u64),
}

impl FsckWarning {
    fn to_string(&self) -> String {
        match self {
            FsckWarning::ExtentForMissingAttribute(store_id, object_id, attr_id) => {
                format!(
                    "Found an extent in store {} for missing attribute {} on object {}",
                    store_id, attr_id, object_id
                )
            }
            FsckWarning::ExtentForDirectory(store_id, object_id) => {
                format!(
                    "Found an extent in store {} for a directory object {}",
                    store_id, object_id
                )
            }
            FsckWarning::ExtentForNonexistentObject(store_id, object_id) => {
                format!(
                    "Found an extent in store {} for a non-existent object {}",
                    store_id, object_id
                )
            }
            FsckWarning::GraveyardRecordForAbsentObject(store_id, object_id) => {
                format!(
                    "Graveyard contains an entry for object {} in store {}, but that object is \
                    absent",
                    store_id, object_id
                )
            }
            FsckWarning::InvalidObjectIdInStore(store_id, key, value) => {
                format!("Store {} has an invalid object ID ({:?}, {:?})", store_id, key, value)
            }
            FsckWarning::OrphanedAttribute(store_id, object_id, attribute_id) => {
                format!(
                    "Attribute {} found for object {} which doesn't exist in store {}",
                    attribute_id, object_id, store_id
                )
            }
            FsckWarning::OrphanedObject(store_id, object_id) => {
                format!("Orphaned object {} was found in store {}", object_id, store_id)
            }
        }
    }
}

#[derive(Clone, Debug)]
pub enum FsckError {
    AllocatedBytesMismatch(u64, u64),
    AllocatedSizeMismatch(u64, u64, u64, u64),
    AllocationMismatch(Allocation, Allocation),
    AttributeOnDirectory(u64, u64),
    ConflictingTypeForLink(u64, u64, Value, Value),
    ExtentExceedsLength(u64, u64, u64, u64, Value),
    ExtraAllocations(Vec<Allocation>),
    FileHasChildren(u64, u64),
    LinkCycle(u64, u64),
    MalformedAllocation(Allocation),
    MalformedExtent(u64, u64, Range<u64>, u64),
    MalformedObjectRecord(u64, Key, Value),
    MisalignedAllocation(Allocation),
    MisalignedExtent(u64, u64, Range<u64>, u64),
    MissingAllocation(Allocation),
    MissingDataAttribute(u64, u64),
    MissingObjectInfo(u64, u64),
    MultipleLinksToDirectory(u64, u64),
    ObjectCountMismatch(u64, u64, u64),
    RefCountMismatch(u64, u64, u64),
    RootObjectHasParent(u64, u64, u64),
    SubDirCountMismatch(u64, u64, u64, u64),
    TombstonedObjectHasRecords(u64, u64),
    UnexpectedObjectInGraveyard(u64),
    UnexpectedRecordInObjectStore(u64, Key, Value),
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
            FsckError::AllocatedSizeMismatch(store_id, oid, expected, actual) => {
                format!(
                    "Expected {} bytes allocated for object {} in store {}, but found {} bytes",
                    expected, oid, store_id, actual
                )
            }
            FsckError::AttributeOnDirectory(store_id, object_id) => {
                format!("Directory {} in store {} had attributes", object_id, store_id)
            }
            FsckError::ConflictingTypeForLink(store_id, object_id, expected, actual) => {
                format!(
                    "Object {} in store {} is of type {:?} but has a link of type {:?}",
                    store_id, object_id, expected, actual
                )
            }
            FsckError::ExtentExceedsLength(store_id, oid, attr_id, size, extent) => {
                format!(
                    "Extent {:?} exceeds length {} of attr {} on object {} in store {}",
                    extent, size, attr_id, oid, store_id
                )
            }
            FsckError::ExtraAllocations(allocations) => {
                format!("Unexpected allocations {:?}", allocations)
            }
            FsckError::FileHasChildren(store_id, object_id) => {
                format!("Object {} in store {} has children", object_id, store_id)
            }
            FsckError::LinkCycle(store_id, object_id) => {
                format!("Detected cycle involving object {} in store {}", store_id, object_id)
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
            FsckError::MalformedObjectRecord(store_id, key, value) => {
                format!(
                    "Object record in store {} has mismatched key {:?} and value {:?}",
                    store_id, key, value
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
            FsckError::MissingDataAttribute(store_id, oid) => {
                format!("File {} in store {} didn't have the default data attribute", store_id, oid)
            }
            FsckError::MissingObjectInfo(store_id, object_id) => {
                format!("Object {} in store {} had no object record", store_id, object_id)
            }
            FsckError::MultipleLinksToDirectory(store_id, object_id) => {
                format!("Directory {} in store {} has multiple links", store_id, object_id)
            }
            FsckError::ObjectCountMismatch(store_id, expected, actual) => {
                format!("Store {} had {} objects, expected {}", store_id, actual, expected)
            }
            FsckError::RefCountMismatch(oid, expected, actual) => {
                format!("Object {} had {} references, expected {}", oid, actual, expected)
            }
            FsckError::RootObjectHasParent(store_id, object_id, apparent_parent_id) => {
                format!(
                    "Object {} is child of {} but is a root object of store {}",
                    object_id, apparent_parent_id, store_id
                )
            }
            FsckError::SubDirCountMismatch(store_id, object_id, expected, actual) => {
                format!(
                    "Directory {} in store {} should have {} sub dirs but had {}",
                    object_id, store_id, expected, actual
                )
            }
            FsckError::TombstonedObjectHasRecords(store_id, object_id) => {
                format!(
                    "Tombstoned object {} in store {} was referenced by other records",
                    store_id, object_id
                )
            }
            FsckError::UnexpectedObjectInGraveyard(object_id) => {
                format!("Found a non-file object {} in graveyard", object_id)
            }
            FsckError::UnexpectedRecordInObjectStore(store_id, key, value) => {
                format!("Unexpected record ({:?}, {:?}) in object store {}", key, value, store_id)
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
    MisOrderedObjectStore(u64),
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
            FsckFatal::MisOrderedObjectStore(store_id) => {
                format!("Store/allocator {} contains out-of-order or duplicate records", store_id)
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
