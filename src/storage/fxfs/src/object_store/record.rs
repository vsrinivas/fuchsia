// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(jfsulliv): need validation after deserialization.

use {
    crate::lsm_tree::types::{Item, ItemRef, NextKey, OrdLowerBound, OrdUpperBound},
    serde::{Deserialize, Serialize},
    std::cmp::{max, min},
    std::convert::From,
    std::ops::Range,
};

/// The common case for extents which cover the data payload of some object.
pub const DEFAULT_DATA_ATTRIBUTE_ID: u64 = 0;

/// ObjectDescriptor is the set of possible records in the object store.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub enum ObjectDescriptor {
    /// A file (in the generic sense; i.e. an object with some attributes).
    File,
    /// A directory (in the generic sense; i.e. an object with children).
    Directory,
    /// A volume, which is the root of a distinct object store containing Files and Directories.
    Volume,
}

#[derive(Clone, Debug, Eq, PartialEq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum ObjectKeyData {
    /// An object tombstone.  This *must* go first because it needs to sort before all the other
    /// records, so that when it merges it can delete them.
    Tombstone,
    /// A generic, untyped object.
    Object,
    /// An attribute associated with an object.  It has a 64-bit ID.
    Attribute(u64, AttributeKey),
    /// A child of a directory.
    Child { name: String }, // TODO(jfsulliv): Should this be a string or array of bytes?
    /// A graveyard entry.
    GraveyardEntry { store_object_id: u64, object_id: u64 },
}

#[derive(Clone, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
pub enum AttributeKey {
    Size,
    Extent(ExtentKey),
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct ExtentKey {
    pub range: Range<u64>,
}

impl ExtentKey {
    /// Creates an ExtentKey.
    pub fn new(range: std::ops::Range<u64>) -> Self {
        ExtentKey { range }
    }

    /// Returns the range of bytes common between this extent and |other|.
    pub fn overlap(&self, other: &ExtentKey) -> Option<Range<u64>> {
        if self.range.end <= other.range.start || self.range.start >= other.range.end {
            None
        } else {
            Some(max(self.range.start, other.range.start)..min(self.range.end, other.range.end))
        }
    }

    /// Returns the search key for this extent; that is, a key which is <= this key under Ord and
    /// OrdLowerBound.
    /// This would be used when searching for an extent with |find| (when we want to find any
    /// overlapping extent, which could include extents that start earlier).
    /// For example, if the tree has extents 50..150 and 150..200 and we wish to read 100..200,
    /// we'd search for 0..101 which would set the iterator to 50..150.
    pub fn search_key(&self) -> Self {
        assert_ne!(self.range.start, self.range.end);
        ExtentKey { range: 0..self.range.start + 1 }
    }

    /// Returns the merge key for this extent; that is, a key which is <= this extent and any other
    /// possibly overlapping extent, under Ord. This would be used to set the hint for |merge_into|.
    ///
    /// For example, if the tree has extents 0..50, 50..150 and 150..200 and we wish to insert
    /// 100..150, we'd use a merge hint of 0..100 which would set the iterator to 50..150 (the first
    /// element > 100..150 under Ord).
    pub fn key_for_merge_into(&self) -> Self {
        ExtentKey { range: 0..self.range.start }
    }
}

// The normal comparison uses the end of the range before the start of the range. This makes
// searching for records easier because it's easy to find K.. (where K is the key you are searching
// for), which is what we want since our search routines find items with keys >= a search key.
// OrdLowerBound orders by the start of an extent.
impl OrdUpperBound for ExtentKey {
    fn cmp_upper_bound(&self, other: &ExtentKey) -> std::cmp::Ordering {
        // The comparison uses the end of the range so that we can more easily do queries.  Whilst
        // it might be tempting to break ties by comparing the range start, next_key currently
        // relies on keys with the same end being equal, and since we do not support overlapping
        // keys within the same layer, ties can always be broken using layer index.  Insertions into
        // the mutable layer should always be done using merge_into, which will ensure keys don't
        // end up overlapping.
        self.range.end.cmp(&other.range.end)
    }
}

impl OrdLowerBound for ExtentKey {
    // Orders by the start of the range rather than the end, and doesn't include the end in the
    // comparison. This is used when merging, where we want to merge keys in lower-bound order.
    fn cmp_lower_bound(&self, other: &ExtentKey) -> std::cmp::Ordering {
        return self.range.start.cmp(&other.range.start);
    }
}

impl Ord for ExtentKey {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        // We expect cmp_upper_bound and cmp_lower_bound to be used mostly, but ObjectKey needs an
        // Ord method in order to compare other enum variants, and Transaction requires an ObjectKey
        // to implement Ord.
        self.range.start.cmp(&other.range.start).then(self.range.end.cmp(&other.range.end))
    }
}

impl PartialOrd for ExtentKey {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

/// ObjectKey is a key in the object store.
#[derive(Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize, Clone)]
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

    /// Creates an ObjectKey for an attribute.
    pub fn attribute(object_id: u64, attribute_id: u64) -> Self {
        Self { object_id, data: ObjectKeyData::Attribute(attribute_id, AttributeKey::Size) }
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

    /// Creates an ObjectKey from an ExtentKey.
    pub fn with_extent_key(object_id: u64, attribute_id: u64, extent_key: ExtentKey) -> Self {
        Self {
            object_id,
            data: ObjectKeyData::Attribute(attribute_id, AttributeKey::Extent(extent_key)),
        }
    }

    /// Creates an ObjectKey for a child.
    pub fn child(object_id: u64, name: &str) -> Self {
        Self { object_id, data: ObjectKeyData::Child { name: name.to_owned() } }
    }

    /// Creates a graveyard entry.
    pub fn graveyard_entry(graveyard_object_id: u64, store_object_id: u64, object_id: u64) -> Self {
        Self {
            object_id: graveyard_object_id,
            data: ObjectKeyData::GraveyardEntry { store_object_id, object_id },
        }
    }

    pub fn tombstone(object_id: u64) -> Self {
        Self { object_id, data: ObjectKeyData::Tombstone }
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
            Self::with_extent_key(*object_id, *attribute_id, e.search_key())
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
            Self::with_extent_key(*object_id, *attribute_id, e.key_for_merge_into())
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
                // after end, and a key such that if you search for it, you'll get an extent that
                // whose end > range.end.
                *range = range.end..range.end + 1;
            }
            Some(key)
        } else {
            None
        }
    }
}

/// ExtentValue is the payload for an extent in the object store, which describes where the extent
/// is physically located.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub struct ExtentValue {
    /// The device offset for the extent. A value of None indicates a deleted extent; that is, the
    /// logical range described by the extent key is considered to be deleted.
    pub device_offset: Option<u64>,
}

impl ExtentValue {
    /// Returns a new ExtentValue offset by |amount|.
    pub fn offset_by(&self, amount: u64) -> Self {
        Self { device_offset: self.device_offset.map(|o| o + amount) }
    }
}

/// UNIX epoch based timestamp in the UTC timezone.
#[derive(Clone, Debug, Default, Eq, PartialEq, Ord, PartialOrd, Serialize, Deserialize)]
pub struct Timestamp {
    pub secs: u64,
    pub nanos: u32,
}

impl Timestamp {
    const NSEC_PER_SEC: u64 = 1_000_000_000;

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

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
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

/// Object-level attributes.  Note that these are not the same as "attributes" in the
/// ObjectValue::Attribute sense, which refers to an arbitrary data payload associated with an
/// object.  This naming collision is unfortunate.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct ObjectAttributes {
    /// The timestamp at which the object was created (i.e. crtime).
    pub creation_time: Timestamp,
    /// The timestamp at which the object's data was last modified (i.e. mtime).
    pub modification_time: Timestamp,
}

/// ObjectValue is the value of an item in the object store.
/// Note that the tree stores deltas on objects, so these values describe deltas. Unless specified
/// otherwise, a value indicates an insert/replace mutation.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub enum ObjectValue {
    /// Some keys (e.g. tombstones) have no value.
    None,
    /// Some keys have no value but need to differentiate between a present value and no value
    /// (None) i.e. their value is really a boolean: None => false, Some => true.
    Some,
    /// The value for an ObjectKey::Object record.
    Object { kind: ObjectKind, attributes: ObjectAttributes },
    /// An attribute associated with a file object. |size| is the size of the attribute in bytes.
    Attribute { size: u64 },
    /// An extent associated with an object.
    Extent(ExtentValue),
    /// A child of an object. |object_id| is the ID of the child, and |object_descriptor| describes
    /// the child.
    Child { object_id: u64, object_descriptor: ObjectDescriptor },
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
    /// Creates an ObjectValue for an object attribute.
    pub fn attribute(size: u64) -> ObjectValue {
        ObjectValue::Attribute { size }
    }
    /// Creates an ObjectValue for an insertion/replacement of an object extent.
    pub fn extent(device_offset: u64) -> ObjectValue {
        ObjectValue::Extent(ExtentValue { device_offset: Some(device_offset) })
    }
    /// Creates an ObjectValue for a deletion of an object extent.
    pub fn deleted_extent() -> ObjectValue {
        ObjectValue::Extent(ExtentValue { device_offset: None })
    }
    /// Creates an ObjectValue for an object child.
    pub fn child(object_id: u64, object_descriptor: ObjectDescriptor) -> ObjectValue {
        ObjectValue::Child { object_id, object_descriptor }
    }
}

pub type ObjectItem = Item<ObjectKey, ObjectValue>;

/// If the given item describes an extent, unwraps it and returns the extent key/value.
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
        super::{ExtentKey, ObjectKey},
        crate::lsm_tree::types::{NextKey, OrdLowerBound, OrdUpperBound},
        std::cmp::Ordering,
    };

    #[test]
    fn test_extent_cmp() {
        let extent = ObjectKey::with_extent_key(1, 0, ExtentKey::new(100..150));
        assert_eq!(
            extent.cmp_upper_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..100))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_upper_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..110))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_upper_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..150))),
            Ordering::Equal
        );
        assert_eq!(
            extent.cmp_upper_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(99..150))),
            Ordering::Equal
        );
        assert_eq!(
            extent.cmp_upper_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(100..150))),
            Ordering::Equal
        );
        assert_eq!(
            extent.cmp_upper_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..151))),
            Ordering::Less
        );
        assert_eq!(
            extent.cmp_upper_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(100..151))),
            Ordering::Less
        );
        assert_eq!(
            extent.cmp_upper_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(150..1000))),
            Ordering::Less
        );

        // Attribute ID takes precedence over range
        assert_eq!(extent.cmp_upper_bound(&ObjectKey::extent(1, 1, 0..100)), Ordering::Less);
        // Object ID takes precedence over all
        assert_eq!(
            ObjectKey::extent(1, 1, 0..100).cmp_upper_bound(&ObjectKey::with_extent_key(
                0,
                1,
                ExtentKey::new(150..1000)
            )),
            Ordering::Greater
        );
    }

    #[test]
    fn test_extent_cmp_lower_bound() {
        let extent = ObjectKey::with_extent_key(1, 0, ExtentKey::new(100..150));
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..100))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..110))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..150))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..1000))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(99..1000))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(100..150))),
            Ordering::Equal
        );
        // cmp_lower_bound does not check the upper bound of the range
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(100..1000))),
            Ordering::Equal
        );
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, 0, ExtentKey::new(101..102))),
            Ordering::Less
        );

        // Attribute ID takes precedence over range
        assert_eq!(extent.cmp_lower_bound(&ObjectKey::extent(1, 1, 0..100)), Ordering::Less);
        // Object ID takes precedence over all
        assert_eq!(
            ObjectKey::extent(1, 1, 0..100).cmp_lower_bound(&ObjectKey::with_extent_key(
                0,
                1,
                ExtentKey::new(150..1000)
            )),
            Ordering::Greater
        );
    }

    #[test]
    fn test_extent_search_and_insertion_key() {
        let extent = ObjectKey::with_extent_key(1, 0, ExtentKey::new(100..150));
        assert_eq!(extent.search_key(), ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..101)));
        assert_eq!(extent.cmp_lower_bound(&extent.search_key()), Ordering::Greater);
        assert_eq!(extent.cmp_upper_bound(&extent.search_key()), Ordering::Greater);
        assert_eq!(
            extent.key_for_merge_into(),
            ObjectKey::with_extent_key(1, 0, ExtentKey::new(0..100))
        );
        assert_eq!(extent.cmp_lower_bound(&extent.key_for_merge_into()), Ordering::Greater);
    }

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

        assert!(ObjectKey::object(1).next_key().is_none());
    }
}
