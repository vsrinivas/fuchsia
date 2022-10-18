// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        log::*,
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
    // TODO(fxbug.dev/95352): Localization
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
    pub fn log(&self) {
        match self {
            FsckIssue::Warning(w) => w.log(),
            FsckIssue::Error(e) => e.log(),
            FsckIssue::Fatal(f) => f.log(),
        }
    }
}

#[derive(Clone, Debug)]
#[allow(dead_code)]
pub struct Allocation {
    range: Range<u64>,
    value: AllocatorValue,
}

impl From<ItemRef<'_, AllocatorKey, AllocatorValue>> for Allocation {
    fn from(item: ItemRef<'_, AllocatorKey, AllocatorValue>) -> Self {
        Self { range: item.key.device_range.clone(), value: item.value.clone() }
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
    LimitForNonExistentStore(u64, u64),
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
            FsckWarning::LimitForNonExistentStore(store_id, limit) => {
                format!("Bytes limit of {} found for nonexistent store id {}", limit, store_id)
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

    fn log(&self) {
        match self {
            FsckWarning::ExtentForMissingAttribute(store_id, oid, attr_id) => {
                warn!(store_id, oid, attr_id, "Found an extent for a missing attribute");
            }
            FsckWarning::ExtentForDirectory(store_id, oid) => {
                warn!(store_id, oid, "Extent for a directory object");
            }
            FsckWarning::ExtentForNonexistentObject(store_id, oid) => {
                warn!(store_id, oid, "Extent for missing object");
            }
            FsckWarning::GraveyardRecordForAbsentObject(store_id, oid) => {
                warn!(store_id, oid, "Graveyard entry for missing object");
            }
            FsckWarning::InvalidObjectIdInStore(store_id, key, value) => {
                warn!(store_id, ?key, ?value, "Invalid object ID");
            }
            FsckWarning::LimitForNonExistentStore(store_id, limit) => {
                warn!(store_id, limit, "Found limit for non-existent owner store.");
            }
            FsckWarning::OrphanedAttribute(store_id, oid, attribute_id) => {
                warn!(store_id, oid, attribute_id, "Attribute for missing object");
            }
            FsckWarning::OrphanedObject(store_id, oid) => {
                warn!(oid, store_id, "Orphaned object");
            }
        }
    }
}

#[derive(Clone, Debug)]
pub enum FsckError {
    AllocatedBytesMismatch(Vec<(u64, i64)>, Vec<(u64, i64)>),
    AllocatedSizeMismatch(u64, u64, u64, u64),
    AllocationForNonexistentOwner(Allocation),
    AllocationMismatch(Allocation, Allocation),
    AttributeOnDirectory(u64, u64),
    ConflictingTypeForLink(u64, u64, Value, Value),
    ExtentExceedsLength(u64, u64, u64, u64, Value),
    ExtraAllocations(Vec<Allocation>),
    FileHasChildren(u64, u64),
    UnexpectedJournalFileOffset(u64),
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
    BadGraveyardValue(u64, u64),
}

impl FsckError {
    fn to_string(&self) -> String {
        match self {
            FsckError::AllocatedBytesMismatch(expected, actual) => {
                format!(
                    "Expected allocated bytes for each owner to be {:?}, but found {:?}",
                    expected, actual
                )
            }
            FsckError::AllocatedSizeMismatch(store_id, oid, expected, actual) => {
                format!(
                    "Expected {} bytes allocated for object {} in store {}, but found {} bytes",
                    expected, oid, store_id, actual
                )
            }
            FsckError::AllocationForNonexistentOwner(alloc) => {
                format!("Allocation {:?} for non-existent owner", alloc)
            }
            FsckError::AllocationMismatch(expected, actual) => {
                format!("Expected allocation {:?} but found allocation {:?}", expected, actual)
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
            FsckError::UnexpectedJournalFileOffset(object_id) => {
                format!(
                    "SuperBlock journal_file_offsets contains unexpected object_id ({:?}).",
                    object_id
                )
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
            FsckError::BadGraveyardValue(store_id, object_id) => {
                format!("Bad graveyard value with key <{}, {}>", store_id, object_id)
            }
        }
    }

    fn log(&self) {
        match self {
            FsckError::AllocatedBytesMismatch(expected, actual) => {
                error!(?expected, ?actual, "Unexpected allocated bytes");
            }
            FsckError::AllocatedSizeMismatch(store_id, oid, expected, actual) => {
                error!(expected, oid, store_id, actual, "Unexpected allocated size");
            }
            FsckError::AllocationForNonexistentOwner(alloc) => {
                error!(?alloc, "Allocation for non-existent owner")
            }
            FsckError::AllocationMismatch(expected, actual) => {
                error!(?expected, ?actual, "Unexpected allocation");
            }
            FsckError::AttributeOnDirectory(store_id, oid) => {
                error!(store_id, oid, "Attribute for directory");
            }
            FsckError::ConflictingTypeForLink(store_id, oid, expected, actual) => {
                error!(store_id, oid, ?expected, ?actual, "Bad link");
            }
            FsckError::ExtentExceedsLength(store_id, oid, attr_id, size, extent) => {
                error!(store_id, oid, attr_id, size, ?extent, "Extent exceeds length");
            }
            FsckError::ExtraAllocations(allocations) => {
                error!(?allocations, "Unexpected allocations");
            }
            FsckError::FileHasChildren(store_id, oid) => {
                error!(store_id, oid, "File has children");
            }
            FsckError::UnexpectedJournalFileOffset(object_id) => {
                error!(
                    oid = object_id,
                    "SuperBlock journal_file_offsets contains unexpected object-id"
                );
            }
            FsckError::LinkCycle(store_id, oid) => {
                error!(store_id, oid, "Link cycle");
            }
            FsckError::MalformedAllocation(allocations) => {
                error!(?allocations, "Malformed allocations");
            }
            FsckError::MalformedExtent(store_id, oid, extent, device_offset) => {
                error!(store_id, oid, ?extent, device_offset, "Malformed extent");
            }
            FsckError::MalformedObjectRecord(store_id, key, value) => {
                error!(store_id, ?key, ?value, "Mismatched key and value");
            }
            FsckError::MisalignedAllocation(allocations) => {
                error!(?allocations, "Misaligned allocation");
            }
            FsckError::MisalignedExtent(store_id, oid, extent, device_offset) => {
                error!(store_id, oid, ?extent, device_offset, "Misaligned extent");
            }
            FsckError::MissingAllocation(allocation) => {
                error!(?allocation, "Missing allocation");
            }
            FsckError::MissingDataAttribute(store_id, oid) => {
                error!(store_id, oid, "Missing default attribute");
            }
            FsckError::MissingObjectInfo(store_id, oid) => {
                error!(store_id, oid, "Missing object record");
            }
            FsckError::MultipleLinksToDirectory(store_id, oid) => {
                error!(store_id, oid, "Directory with multiple links");
            }
            FsckError::ObjectCountMismatch(store_id, expected, actual) => {
                error!(store_id, expected, actual, "Object count mismatch");
            }
            FsckError::RefCountMismatch(oid, expected, actual) => {
                error!(oid, expected, actual, "Reference count mistmatch");
            }
            FsckError::RootObjectHasParent(store_id, oid, apparent_parent_id) => {
                error!(store_id, oid, apparent_parent_id, "Root object is a child");
            }
            FsckError::SubDirCountMismatch(store_id, oid, expected, actual) => {
                error!(store_id, oid, expected, actual, "Sub-dir count mismatch");
            }
            FsckError::TombstonedObjectHasRecords(store_id, oid) => {
                error!(store_id, oid, "Tombstoned object with references");
            }
            FsckError::UnexpectedObjectInGraveyard(oid) => {
                error!(oid, "Unexpected object in graveyard");
            }
            FsckError::UnexpectedRecordInObjectStore(store_id, key, value) => {
                error!(store_id, ?key, ?value, "Unexpected record");
            }
            FsckError::VolumeInChildStore(store_id, oid) => {
                error!(store_id, oid, "Volume in child store");
            }
            FsckError::BadGraveyardValue(store_id, oid) => {
                error!(store_id, oid, "Bad graveyard value");
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
                    layer_file_id, store_id
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

    fn log(&self) {
        match self {
            FsckFatal::MalformedGraveyard => {
                error!("Graveyard is malformed; root store is inconsistent");
            }
            FsckFatal::MalformedLayerFile(store_id, layer_file_id) => {
                error!(store_id, layer_file_id, "Layer file malformed");
            }
            FsckFatal::MalformedStore(id) => {
                error!(id, "Malformed store; root store is inconsistent");
            }
            FsckFatal::MisOrderedLayerFile(store_id, layer_file_id) => {
                // This can be for stores or the allocator.
                error!(oid = store_id, layer_file_id, "Layer file contains out-of-oder records");
            }
            FsckFatal::MisOrderedObjectStore(store_id) => {
                // This can be for stores or the allocator.
                error!(
                    oid = store_id,
                    "Store/allocator contains out-of-order or duplicate records"
                );
            }
            FsckFatal::MissingLayerFile(store_id, layer_file_id) => {
                // This can be for stores or the allocator.
                error!(oid = store_id, layer_file_id, "Missing layer file");
            }
            FsckFatal::MissingStoreInfo(id) => {
                error!(id, "Missing store info");
            }
            FsckFatal::OverlappingKeysInLayerFile(store_id, layer_file_id, key1, key2) => {
                // This can be for stores or the allocator.
                error!(oid = store_id, layer_file_id, ?key1, ?key2, "Overlapping keys");
            }
        }
    }
}
