// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(jfsulliv): need validation after deserialization.

use {
    crate::lsm_tree::types::{Item, ItemRef, OrdLowerBound},
    serde::{Deserialize, Serialize},
    std::cmp::{max, min},
    std::ops::Range,
};

/// The common case for extents which cover the data payload of some object.
pub const DEFAULT_DATA_ATTRIBUTE_ID: u64 = 0;

/// ExtentKey is a range-based key describing an extent of bytes.
/// Keys are ordered first by |attribute_id|, then by |range.end| and |range.start|.
#[derive(Debug, Eq, PartialEq, Clone, Serialize, Deserialize)]
pub struct ExtentKey {
    /// The attribute this extent describes.
    pub attribute_id: u64,
    /// The range of bytes the extent covers.
    pub range: std::ops::Range<u64>,
}

impl ExtentKey {
    /// Creates an ExtentKey.
    pub fn new(attribute_id: u64, range: std::ops::Range<u64>) -> Self {
        ExtentKey { attribute_id, range }
    }

    /// Creates an ExtentKey describing the data payload for an object.
    pub fn data_extent(range: std::ops::Range<u64>) -> Self {
        ExtentKey { attribute_id: DEFAULT_DATA_ATTRIBUTE_ID, range }
    }

    /// Returns the range of bytes common between this extent and |other|.
    pub fn overlap(&self, other: &ExtentKey) -> Option<Range<u64>> {
        if self.attribute_id != other.attribute_id
            || self.range.end <= other.range.start
            || self.range.start >= other.range.end
        {
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
        ExtentKey { attribute_id: self.attribute_id, range: 0..self.range.start + 1 }
    }

    /// Returns the merge key for this extent; that is, a key which is <= this extent and any other
    /// possibly overlapping extent, under Ord. This would be used to set the hint for |merge_into|.
    ///
    /// For example, if the tree has extents 0..50, 50..150 and 150..200 and we wish to insert
    /// 100..150, we'd use a merge hint of 0..100 which would set the iterator to 50..150 (the first
    /// element > 100..150 under Ord).
    pub fn key_for_merge_into(&self) -> Self {
        ExtentKey { attribute_id: self.attribute_id, range: 0..self.range.start }
    }
}

// The normal comparison uses the end of the range before the start of the range. This makes
// searching for records easier because it's easy to find K.. (where K is the key you are searching
// for), which is what we want since our search routines find items with keys >= a search key.
// OrdLowerBound orders by the start of an extent.
impl Ord for ExtentKey {
    fn cmp(&self, other: &ExtentKey) -> std::cmp::Ordering {
        // The comparison uses the end of the range so that we can more easily do queries.
        self.attribute_id
            .cmp(&other.attribute_id)
            .then(self.range.end.cmp(&other.range.end))
            .then(self.range.start.cmp(&other.range.start))
    }
}

impl PartialOrd for ExtentKey {
    fn partial_cmp(&self, other: &ExtentKey) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl OrdLowerBound for ExtentKey {
    // Orders by the start of the range rather than the end, and doesn't include the end in the
    // comparison. This is used when merging, where we want to merge keys in lower-bound order.
    fn cmp_lower_bound(&self, other: &ExtentKey) -> std::cmp::Ordering {
        return self
            .attribute_id
            .cmp(&other.attribute_id)
            .then(self.range.start.cmp(&other.range.start));
    }
}

/// ObjectType is the set of possible records in the object store.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub enum ObjectType {
    /// A file (in the generic sense; i.e. an object with some attributes).
    File,
    /// A directory (in the generic sense; i.e. an object with children).
    Directory,
    /// A volume, which is the root of a distinct object store containing Files and Directories.
    Volume,
}

#[derive(Clone, Debug, Eq, PartialEq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum ObjectKeyData {
    /// A generic, untyped object.
    Object,
    /// An attribute associated with an object. |attribute_id| describes the type of attribute.
    Attribute { attribute_id: u64 },
    /// An extent associated with the object.
    Extent(ExtentKey),
    /// A child of the .
    Child { name: String }, // TODO(jfsulliv): Should this be a string or array of bytes?
}

/// ObjectKey is a key in the object store.
#[derive(Debug, Eq, PartialEq, PartialOrd, Ord, Serialize, Deserialize, Clone)]
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
        Self { object_id, data: ObjectKeyData::Attribute { attribute_id } }
    }

    /// Creates an ObjectKey for an extent.
    pub fn extent(object_id: u64, attribute_id: u64, range: std::ops::Range<u64>) -> Self {
        Self { object_id, data: ObjectKeyData::Extent(ExtentKey::new(attribute_id, range)) }
    }

    /// Creates an ObjectKey from an ExtentKey.
    pub fn with_extent_key(object_id: u64, extent_key: ExtentKey) -> Self {
        Self { object_id, data: ObjectKeyData::Extent(extent_key) }
    }

    /// Creates an ObjectKey for a child.
    pub fn child(object_id: u64, name: &str) -> Self {
        Self { object_id, data: ObjectKeyData::Child { name: name.to_owned() } }
    }

    /// Returns the search key for this extent; that is, a key which is <= this key under Ord and
    /// OrdLowerBound.
    /// This would be used when searching for an extent with |find| (when we want to find any
    /// overlapping extent, which could include extents that start earlier).
    pub fn search_key(&self) -> Self {
        if let Self { object_id, data: ObjectKeyData::Extent(e) } = self {
            Self::with_extent_key(*object_id, e.search_key())
        } else {
            self.clone()
        }
    }

    /// Returns the merge key for this key; that is, a key which is <= this key and any
    /// other possibly overlapping key, under Ord. This would be used for the hint in |merge_into|.
    pub fn key_for_merge_into(&self) -> Self {
        if let Self { object_id, data: ObjectKeyData::Extent(e) } = self {
            Self::with_extent_key(*object_id, e.key_for_merge_into())
        } else {
            self.clone()
        }
    }
}

impl OrdLowerBound for ObjectKey {
    fn cmp_lower_bound(&self, other: &ObjectKey) -> std::cmp::Ordering {
        self.object_id.cmp(&other.object_id).then_with(|| match (&self.data, &other.data) {
            (ObjectKeyData::Extent(ref left_extent), ObjectKeyData::Extent(ref right_extent)) => {
                left_extent.cmp_lower_bound(right_extent)
            }
            _ => self.data.cmp(&other.data),
        })
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

/// ObjectValue is the value of an item in the object store.
/// Note that the tree stores deltas on objects, so these values describe deltas. Unless specified
/// otherwise, a value indicates an insert/replace mutation.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub enum ObjectValue {
    /// A generic object in the store with no parent. (Most objects will likely be |Child| of some
    /// other objects.)
    Object { object_type: ObjectType },
    /// An attribute associated with an object. |size| is the size of the attribute in bytes.
    Attribute { size: u64 },
    /// An extent associated with an object.
    Extent(ExtentValue),
    /// A child of an object. |object_id| is the ID of the child, and |object_type| its type.
    Child { object_id: u64, object_type: ObjectType },
}

impl ObjectValue {
    /// Creates an ObjectValue for a generic parentless object.
    pub fn object(object_type: ObjectType) -> ObjectValue {
        ObjectValue::Object { object_type }
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
    pub fn child(object_id: u64, object_type: ObjectType) -> ObjectValue {
        ObjectValue::Child { object_id, object_type }
    }
}

pub type ObjectItem = Item<ObjectKey, ObjectValue>;

/// If the given item describes an extent, unwraps it and returns the extent key/value.
pub fn decode_extent(
    item: ItemRef<'_, ObjectKey, ObjectValue>,
) -> Option<(u64, &ExtentKey, &ExtentValue)> {
    match item {
        ItemRef {
            key: ObjectKey { object_id, data: ObjectKeyData::Extent(ref extent_key) },
            value: ObjectValue::Extent(ref extent_value),
        } => Some((*object_id, extent_key, extent_value)),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{ExtentKey, ObjectKey},
        crate::lsm_tree::types::OrdLowerBound,
        fuchsia_async as fasync,
        std::cmp::Ordering,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_extent_cmp() {
        let extent = ObjectKey::with_extent_key(1, ExtentKey::data_extent(100..150));
        assert_eq!(
            extent.cmp(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..100))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..110))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..150))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(99..150))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(100..150))),
            Ordering::Equal
        );
        assert_eq!(
            extent.cmp(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..151))),
            Ordering::Less
        );
        assert_eq!(
            extent.cmp(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(100..151))),
            Ordering::Less
        );
        assert_eq!(
            extent.cmp(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(150..1000))),
            Ordering::Less
        );

        // Attribute ID takes precedence over range
        assert_eq!(extent.cmp(&ObjectKey::extent(1, 1, 0..100)), Ordering::Less);
        // Object ID takes precedence over all
        assert_eq!(
            ObjectKey::extent(0, 1, 0..100)
                .cmp(&ObjectKey::with_extent_key(0, ExtentKey::data_extent(150..1000))),
            Ordering::Greater
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_extent_cmp_lower_bound() {
        let extent = ObjectKey::with_extent_key(1, ExtentKey::data_extent(100..150));
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..100))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..110))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..150))),
            Ordering::Greater
        );
        assert_eq!(
            extent.cmp_lower_bound(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..1000))),
            Ordering::Greater
        );
        assert_eq!(
            extent
                .cmp_lower_bound(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(99..1000))),
            Ordering::Greater
        );
        assert_eq!(
            extent
                .cmp_lower_bound(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(100..150))),
            Ordering::Equal
        );
        // cmp_lower_bound does not check the upper bound of the range
        assert_eq!(
            extent
                .cmp_lower_bound(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(100..1000))),
            Ordering::Equal
        );
        assert_eq!(
            extent
                .cmp_lower_bound(&ObjectKey::with_extent_key(1, ExtentKey::data_extent(101..102))),
            Ordering::Less
        );

        // Attribute ID takes precedence over range
        assert_eq!(extent.cmp_lower_bound(&ObjectKey::extent(1, 1, 0..100)), Ordering::Less);
        // Object ID takes precedence over all
        assert_eq!(
            ObjectKey::extent(0, 1, 0..100)
                .cmp_lower_bound(&ObjectKey::with_extent_key(0, ExtentKey::data_extent(150..1000))),
            Ordering::Greater
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_extent_search_and_insertion_key() {
        let extent = ObjectKey::with_extent_key(1, ExtentKey::data_extent(100..150));
        assert_eq!(
            extent.search_key(),
            ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..101))
        );
        assert_eq!(extent.cmp_lower_bound(&extent.search_key()), Ordering::Greater);
        assert_eq!(extent.cmp(&extent.search_key()), Ordering::Greater);
        assert_eq!(
            extent.key_for_merge_into(),
            ObjectKey::with_extent_key(1, ExtentKey::data_extent(0..100))
        );
        assert_eq!(extent.cmp_lower_bound(&extent.key_for_merge_into()), Ordering::Greater);
    }
}
