// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/96139): need validation after deserialization.
use {
    crate::{
        crypt::WrappedKeys,
        lsm_tree::types::{Item, ItemRef, NextKey, OrdLowerBound, OrdUpperBound, RangeKey},
        object_store::extent_record::{Checksums, ExtentKey, ExtentValue},
        serialized_types::Versioned,
    },
    serde::{Deserialize, Serialize},
    std::convert::From,
    std::time::{Duration, SystemTime, UNIX_EPOCH},
    type_hash::TypeHash,
};

/// ObjectDescriptor is the set of possible records in the object store.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, TypeHash)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub enum ObjectDescriptor {
    /// A file (in the generic sense; i.e. an object with some attributes).
    File,
    /// A directory (in the generic sense; i.e. an object with children).
    Directory,
    /// A volume, which is the root of a distinct object store containing Files and Directories.
    Volume,
}

#[derive(Clone, Debug, Eq, PartialEq, PartialOrd, Ord, Serialize, Deserialize, TypeHash)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub enum ObjectKeyData {
    /// A generic, untyped object.  This must come first and sort before all other keys for a given
    /// object because it's also used as a tombstone and it needs to merge with all following keys.
    Object,
    /// Encryption keys for an object.
    Keys,
    /// An attribute associated with an object.  It has a 64-bit ID.
    Attribute(u64, AttributeKey),
    /// A child of a directory.
    /// We store the filename as a case-preserving unicode string.
    Child { name: String },
    /// A graveyard entry.
    GraveyardEntry { object_id: u64 },
}

#[derive(Clone, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize, TypeHash)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub enum AttributeKey {
    Size,
    Extent(ExtentKey),
}

/// ObjectKey is a key in the object store.
#[derive(
    Clone, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize, TypeHash, Versioned,
)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub struct ObjectKey {
    /// The ID of the object referred to.
    pub object_id: u64,
    /// The type and data of the key.
    pub data: ObjectKeyData,
}

impl ObjectKey {
    /// Creates a generic ObjectKey.
    pub fn object(object_id: u64) -> Self {
        Self { object_id: object_id, data: ObjectKeyData::Object }
    }

    /// Creates an ObjectKey for encryption keys.
    pub fn keys(object_id: u64) -> Self {
        Self { object_id, data: ObjectKeyData::Keys }
    }

    /// Creates an ObjectKey for an attribute.
    pub fn attribute(object_id: u64, attribute_id: u64, key: AttributeKey) -> Self {
        Self { object_id, data: ObjectKeyData::Attribute(attribute_id, key) }
    }

    /// Creates an ObjectKey for an extent.
    pub fn extent(object_id: u64, attribute_id: u64, range: std::ops::Range<u64>) -> Self {
        Self {
            object_id,
            data: ObjectKeyData::Attribute(
                attribute_id,
                AttributeKey::Extent(ExtentKey::new(range)),
            ),
        }
    }

    /// Creates an ObjectKey from an extent.
    pub fn from_extent(object_id: u64, attribute_id: u64, extent: ExtentKey) -> Self {
        Self {
            object_id,
            data: ObjectKeyData::Attribute(attribute_id, AttributeKey::Extent(extent)),
        }
    }

    /// Creates an ObjectKey for a child.
    pub fn child(object_id: u64, name: &str) -> Self {
        Self { object_id, data: ObjectKeyData::Child { name: name.to_owned() } }
    }

    /// Creates a graveyard entry.
    pub fn graveyard_entry(graveyard_object_id: u64, object_id: u64) -> Self {
        Self { object_id: graveyard_object_id, data: ObjectKeyData::GraveyardEntry { object_id } }
    }

    /// Returns the search key for this extent; that is, a key which is <= this key under Ord and
    /// OrdLowerBound.
    /// This would be used when searching for an extent with |find| (when we want to find any
    /// overlapping extent, which could include extents that start earlier).
    pub fn search_key(&self) -> Self {
        if let Self {
            object_id,
            data: ObjectKeyData::Attribute(attribute_id, AttributeKey::Extent(e)),
        } = self
        {
            Self::attribute(*object_id, *attribute_id, AttributeKey::Extent(e.search_key()))
        } else {
            self.clone()
        }
    }

    /// Returns the merge key for this key; that is, a key which is <= this key and any
    /// other possibly overlapping key, under Ord. This would be used for the hint in |merge_into|.
    pub fn key_for_merge_into(&self) -> Self {
        if let Self {
            object_id,
            data: ObjectKeyData::Attribute(attribute_id, AttributeKey::Extent(e)),
        } = self
        {
            Self::attribute(*object_id, *attribute_id, AttributeKey::Extent(e.key_for_merge_into()))
        } else {
            self.clone()
        }
    }
}

impl OrdUpperBound for ObjectKey {
    fn cmp_upper_bound(&self, other: &ObjectKey) -> std::cmp::Ordering {
        self.object_id.cmp(&other.object_id).then_with(|| match (&self.data, &other.data) {
            (
                ObjectKeyData::Attribute(left_attr_id, AttributeKey::Extent(ref left_extent)),
                ObjectKeyData::Attribute(right_attr_id, AttributeKey::Extent(ref right_extent)),
            ) => left_attr_id.cmp(right_attr_id).then(left_extent.cmp_upper_bound(right_extent)),
            _ => self.data.cmp(&other.data),
        })
    }
}

impl OrdLowerBound for ObjectKey {
    fn cmp_lower_bound(&self, other: &ObjectKey) -> std::cmp::Ordering {
        self.object_id.cmp(&other.object_id).then_with(|| match (&self.data, &other.data) {
            (
                ObjectKeyData::Attribute(left_attr_id, AttributeKey::Extent(ref left_extent)),
                ObjectKeyData::Attribute(right_attr_id, AttributeKey::Extent(ref right_extent)),
            ) => left_attr_id.cmp(right_attr_id).then(left_extent.cmp_lower_bound(right_extent)),
            _ => self.data.cmp(&other.data),
        })
    }
}

impl NextKey for ObjectKey {
    fn next_key(&self) -> Option<Self> {
        if let ObjectKey { data: ObjectKeyData::Attribute(_, AttributeKey::Extent(_)), .. } = self {
            let mut key = self.clone();
            if let ObjectKey {
                data: ObjectKeyData::Attribute(_, AttributeKey::Extent(ExtentKey { range })),
                ..
            } = &mut key
            {
                // We want a key such that cmp_lower_bound returns Greater for any key which starts
                // after end, and a key such that if you search for it, you'll get an extent whose
                // end > range.end.
                *range = range.end..range.end + 1;
            }
            Some(key)
        } else {
            None
        }
    }
}

impl RangeKey for ObjectKey {
    fn overlaps(&self, other: &Self) -> bool {
        if self.object_id != other.object_id {
            return false;
        }
        match (&self.data, &other.data) {
            (
                ObjectKeyData::Attribute(left_attr_id, AttributeKey::Extent(left_key)),
                ObjectKeyData::Attribute(right_attr_id, AttributeKey::Extent(right_key)),
            ) if *left_attr_id == *right_attr_id => {
                left_key.range.end > right_key.range.start
                    && left_key.range.start < right_key.range.end
            }
            (a, b) => a == b,
        }
    }
}

/// UNIX epoch based timestamp in the UTC timezone.
#[derive(
    Copy, Clone, Debug, Default, Eq, PartialEq, Ord, PartialOrd, Serialize, Deserialize, TypeHash,
)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub struct Timestamp {
    pub secs: u64,
    pub nanos: u32,
}

impl Timestamp {
    const NSEC_PER_SEC: u64 = 1_000_000_000;

    pub fn now() -> Self {
        SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or(Duration::ZERO).into()
    }

    pub const fn zero() -> Self {
        Self { secs: 0, nanos: 0 }
    }

    pub const fn from_nanos(nanos: u64) -> Self {
        let subsec_nanos = (nanos % Self::NSEC_PER_SEC) as u32;
        Self { secs: nanos / Self::NSEC_PER_SEC, nanos: subsec_nanos }
    }

    pub fn as_nanos(&self) -> u64 {
        Self::NSEC_PER_SEC
            .checked_mul(self.secs)
            .and_then(|val| val.checked_add(self.nanos as u64))
            .unwrap_or(0u64)
    }
}

impl From<std::time::Duration> for Timestamp {
    fn from(duration: std::time::Duration) -> Timestamp {
        Timestamp { secs: duration.as_secs(), nanos: duration.subsec_nanos() }
    }
}

impl From<Timestamp> for std::time::Duration {
    fn from(timestamp: Timestamp) -> std::time::Duration {
        Duration::new(timestamp.secs, timestamp.nanos)
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, TypeHash)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub enum ObjectKind {
    File {
        /// The number of references to this file.
        refs: u64,
        /// The number of bytes allocated to all extents across all attributes for this file.
        allocated_size: u64,
    },
    Directory {
        /// The number of sub-directories in this directory.
        sub_dirs: u64,
    },
    Graveyard,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, TypeHash)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub enum EncryptionKeys {
    AES256XTS(WrappedKeys),
}

/// Object-level attributes.  Note that these are not the same as "attributes" in the
/// ObjectValue::Attribute sense, which refers to an arbitrary data payload associated with an
/// object.  This naming collision is unfortunate.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, TypeHash)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub struct ObjectAttributes {
    /// The timestamp at which the object was created (i.e. crtime).
    pub creation_time: Timestamp,
    /// The timestamp at which the object's data was last modified (i.e. mtime).
    pub modification_time: Timestamp,
}

/// ObjectValue is the value of an item in the object store.
/// Note that the tree stores deltas on objects, so these values describe deltas. Unless specified
/// otherwise, a value indicates an insert/replace mutation.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, TypeHash, Versioned)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub enum ObjectValue {
    /// Some keys have no value (this often indicates a tombstone of some sort).  Records with this
    /// value are always filtered when a major compaction is performed, so the meaning must be the
    /// same as if the item was not present.
    None,
    /// Some keys have no value but need to differentiate between a present value and no value
    /// (None) i.e. their value is really a boolean: None => false, Some => true.
    Some,
    /// The value for an ObjectKey::Object record.
    Object { kind: ObjectKind, attributes: ObjectAttributes },
    /// Encryption keys for an object.
    Keys(EncryptionKeys),
    /// An attribute associated with a file object. |size| is the size of the attribute in bytes.
    Attribute { size: u64 },
    /// An extent associated with an object.
    Extent(ExtentValue),
    /// A child of an object. |object_id| is the ID of the child, and |object_descriptor| describes
    /// the child.
    Child { object_id: u64, object_descriptor: ObjectDescriptor },
    /// Graveyard entries can contain these entries which will cause a file that has extents beyond
    /// EOF to be trimmed at mount time.  This is used in cases where shrinking a file can exceed
    /// the bounds of a single transaction.
    Trim,
}

impl ObjectValue {
    /// Creates an ObjectValue for a file object.
    pub fn file(
        refs: u64,
        allocated_size: u64,
        creation_time: Timestamp,
        modification_time: Timestamp,
    ) -> ObjectValue {
        ObjectValue::Object {
            kind: ObjectKind::File { refs, allocated_size },
            attributes: ObjectAttributes { creation_time, modification_time },
        }
    }
    pub fn keys(keys: EncryptionKeys) -> ObjectValue {
        ObjectValue::Keys(keys)
    }
    /// Creates an ObjectValue for an object attribute.
    pub fn attribute(size: u64) -> ObjectValue {
        ObjectValue::Attribute { size }
    }
    /// Creates an ObjectValue for an insertion/replacement of an object extent.
    pub fn extent(device_offset: u64) -> ObjectValue {
        ObjectValue::Extent(ExtentValue::with_checksum(device_offset, Checksums::None))
    }
    /// Creates an ObjectValue for an insertion/replacement of an object extent.
    pub fn extent_with_checksum(device_offset: u64, checksum: Checksums) -> ObjectValue {
        ObjectValue::Extent(ExtentValue::with_checksum(device_offset, checksum))
    }
    /// Creates an ObjectValue for a deletion of an object extent.
    pub fn deleted_extent() -> ObjectValue {
        ObjectValue::Extent(ExtentValue::deleted_extent())
    }
    /// Creates an ObjectValue for an object child.
    pub fn child(object_id: u64, object_descriptor: ObjectDescriptor) -> ObjectValue {
        ObjectValue::Child { object_id, object_descriptor }
    }
}

pub type ObjectItem = Item<ObjectKey, ObjectValue>;

impl ObjectItem {
    pub fn is_tombstone(&self) -> bool {
        matches!(
            self,
            Item {
                key: ObjectKey { data: ObjectKeyData::Object, .. },
                value: ObjectValue::None,
                ..
            }
        )
    }
}

// If the given item describes an extent, unwraps it and returns the extent key/value.
impl<'a> From<ItemRef<'a, ObjectKey, ObjectValue>>
    for Option<(/*object-id*/ u64, /*attribute-id*/ u64, &'a ExtentKey, &'a ExtentValue)>
{
    fn from(item: ItemRef<'a, ObjectKey, ObjectValue>) -> Self {
        match item {
            ItemRef {
                key:
                    ObjectKey {
                        object_id,
                        data:
                            ObjectKeyData::Attribute(
                                attribute_id, //
                                AttributeKey::Extent(ref extent_key),
                            ),
                    },
                value: ObjectValue::Extent(ref extent_value),
                ..
            } => Some((*object_id, *attribute_id, extent_key, extent_value)),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::ObjectKey,
        crate::lsm_tree::types::{NextKey, OrdLowerBound, OrdUpperBound, RangeKey},
        std::cmp::Ordering,
    };

    #[test]
    fn test_next_key() {
        let next_key = ObjectKey::extent(1, 0, 0..100).next_key().unwrap();
        assert_eq!(ObjectKey::extent(1, 0, 101..200).cmp_lower_bound(&next_key), Ordering::Greater);
        assert_eq!(ObjectKey::extent(1, 0, 100..200).cmp_lower_bound(&next_key), Ordering::Equal);
        assert_eq!(ObjectKey::extent(1, 0, 100..101).cmp_lower_bound(&next_key), Ordering::Equal);
        assert_eq!(ObjectKey::extent(1, 0, 99..100).cmp_lower_bound(&next_key), Ordering::Less);
        assert_eq!(ObjectKey::extent(1, 0, 0..100).cmp_upper_bound(&next_key), Ordering::Less);
        assert_eq!(ObjectKey::extent(1, 0, 99..100).cmp_upper_bound(&next_key), Ordering::Less);
        assert_eq!(ObjectKey::extent(1, 0, 100..101).cmp_upper_bound(&next_key), Ordering::Equal);
        assert_eq!(ObjectKey::extent(1, 0, 100..200).cmp_upper_bound(&next_key), Ordering::Greater);
        assert_eq!(ObjectKey::extent(1, 0, 50..101).cmp_upper_bound(&next_key), Ordering::Equal);
        assert_eq!(ObjectKey::extent(1, 0, 50..200).cmp_upper_bound(&next_key), Ordering::Greater);
    }
    #[test]
    fn test_range_key() {
        assert_eq!(ObjectKey::object(1).overlaps(&ObjectKey::object(1)), true);
        assert_eq!(ObjectKey::object(1).overlaps(&ObjectKey::object(2)), false);
        assert_eq!(ObjectKey::extent(1, 0, 0..100).overlaps(&ObjectKey::object(1)), false);
        assert_eq!(ObjectKey::object(1).overlaps(&ObjectKey::extent(1, 0, 0..100)), false);
        assert_eq!(
            ObjectKey::extent(1, 0, 0..100).overlaps(&ObjectKey::extent(2, 0, 0..100)),
            false
        );
        assert_eq!(
            ObjectKey::extent(1, 0, 0..100).overlaps(&ObjectKey::extent(1, 1, 0..100)),
            false
        );
        assert_eq!(
            ObjectKey::extent(1, 0, 0..100).overlaps(&ObjectKey::extent(1, 0, 0..100)),
            true
        );

        assert_eq!(
            ObjectKey::extent(1, 0, 0..50).overlaps(&ObjectKey::extent(1, 0, 49..100)),
            true
        );
        assert_eq!(
            ObjectKey::extent(1, 0, 49..100).overlaps(&ObjectKey::extent(1, 0, 0..50)),
            true
        );

        assert_eq!(
            ObjectKey::extent(1, 0, 0..50).overlaps(&ObjectKey::extent(1, 0, 50..100)),
            false
        );
        assert_eq!(
            ObjectKey::extent(1, 0, 50..100).overlaps(&ObjectKey::extent(1, 0, 0..50)),
            false
        );
    }
}
