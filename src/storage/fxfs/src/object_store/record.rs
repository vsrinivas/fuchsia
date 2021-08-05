// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(jfsulliv): need validation after deserialization.

use {
    crate::lsm_tree::types::{Item, NextKey, OrdLowerBound, OrdUpperBound},
    serde::{Deserialize, Serialize},
    std::cmp::{max, min},
    std::convert::From,
    std::ops::Range,
    std::time::{Duration, SystemTime, UNIX_EPOCH},
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
    /// A generic, untyped object.  This must come first and sort before all other keys for a given
    /// object because it's also used as a tombstone and it needs to merge with all following keys.
    Object,
    /// An attribute associated with an object.  It has a 64-bit ID.
    Attribute(u64),
    /// A child of a directory.
    Child { name: String }, // TODO(jfsulliv): Should this be a string or array of bytes?
    /// A graveyard entry.
    GraveyardEntry { store_object_id: u64, object_id: u64 },
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct ExtentKey {
    pub object_id: u64,
    pub attribute_id: u64,
    pub range: Range<u64>,
}

impl ExtentKey {
    /// Creates an ExtentKey.
    pub fn new(object_id: u64, attribute_id: u64, range: std::ops::Range<u64>) -> Self {
        ExtentKey { object_id, attribute_id, range }
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
        ExtentKey::search_key_from_offset(self.object_id, self.attribute_id, self.range.start)
    }

    /// Similar to previous, but from an offset.  Returns a search key that will find the first
    /// extent that touches offset..
    pub fn search_key_from_offset(object_id: u64, attribute_id: u64, offset: u64) -> Self {
        ExtentKey { object_id, attribute_id, range: 0..offset + 1 }
    }

    /// Returns the merge key for this extent; that is, a key which is <= this extent and any other
    /// possibly overlapping extent, under Ord. This would be used to set the hint for |merge_into|.
    ///
    /// For example, if the tree has extents 0..50, 50..150 and 150..200 and we wish to insert
    /// 100..150, we'd use a merge hint of 0..100 which would set the iterator to 50..150 (the first
    /// element > 100..150 under Ord).
    pub fn key_for_merge_into(&self) -> Self {
        ExtentKey {
            object_id: self.object_id,
            attribute_id: self.attribute_id,
            range: 0..self.range.start,
        }
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
        self.object_id
            .cmp(&other.object_id)
            .then(self.attribute_id.cmp(&other.attribute_id))
            .then(self.range.end.cmp(&other.range.end))
    }
}

impl OrdLowerBound for ExtentKey {
    // Orders by the start of the range rather than the end, and doesn't include the end in the
    // comparison. This is used when merging, where we want to merge keys in lower-bound order.
    fn cmp_lower_bound(&self, other: &ExtentKey) -> std::cmp::Ordering {
        self.object_id.cmp(&other.object_id).then(
            self.attribute_id
                .cmp(&other.attribute_id)
                .then(self.range.start.cmp(&other.range.start)),
        )
    }
}

impl Ord for ExtentKey {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        // We expect cmp_upper_bound and cmp_lower_bound to be used mostly, but ObjectKey needs an
        // Ord method in order to compare other enum variants, and Transaction requires an ObjectKey
        // to implement Ord.
        self.object_id.cmp(&other.object_id).then(self.attribute_id.cmp(&other.attribute_id).then(
            self.range.start.cmp(&other.range.start).then(self.range.end.cmp(&other.range.end)),
        ))
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
        Self { object_id, data: ObjectKeyData::Attribute(attribute_id) }
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

    /// Returns the merge key for this key; that is, a key which is <= this key and any other
    /// possibly overlapping key, under Ord. This would be used for `lower_bound` to calls to
    /// `merge_into`.  For now, object keys don't have any range based bits, so this just returns a
    /// clone.
    pub fn key_for_merge_into(&self) -> Self {
        self.clone()
    }
}

impl OrdUpperBound for ObjectKey {
    fn cmp_upper_bound(&self, other: &ObjectKey) -> std::cmp::Ordering {
        self.cmp(other)
    }
}

impl OrdLowerBound for ObjectKey {
    fn cmp_lower_bound(&self, other: &ObjectKey) -> std::cmp::Ordering {
        self.cmp(other)
    }
}

impl NextKey for ObjectKey {}

impl NextKey for ExtentKey {
    fn next_key(&self) -> Option<Self> {
        Some(ExtentKey::new(self.object_id, self.attribute_id, self.range.end..self.range.end + 1))
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum Checksums {
    None,
    /// A vector of checksums, one per block.
    Fletcher(Vec<u64>),
}

/// ExtentValue is the payload for an extent in the object store, which describes where the extent
/// is physically located.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum ExtentValue {
    /// Indicates a deleted extent; that is, the logical range described by the extent key is
    /// considered to be deleted.
    None,
    /// The location of the extent and other related information.  `key_id` identifies which
    /// of the object's keys should be used.  `checksums` hold post-encryption checksums.
    Some { device_offset: u64, checksums: Checksums, key_id: u64 },
}

impl ExtentValue {
    pub fn new(device_offset: u64) -> ExtentValue {
        ExtentValue::Some { device_offset, checksums: Checksums::None, key_id: 0 }
    }

    /// Creates an ExtentValue with a checksum
    pub fn with_checksum(device_offset: u64, checksums: Checksums) -> ExtentValue {
        ExtentValue::Some { device_offset, checksums, key_id: 0 }
    }

    /// Creates an ObjectValue for a deletion of an object extent.
    pub fn deleted_extent() -> ExtentValue {
        ExtentValue::None
    }

    pub fn is_deleted(&self) -> bool {
        if let ExtentValue::None = self {
            true
        } else {
            false
        }
    }

    /// Returns a new ExtentValue offset by `amount`.  Both `amount` and `extent_len` must be
    /// multiples of the underlying block size.
    pub fn offset_by(&self, amount: u64, extent_len: u64) -> Self {
        match self {
            ExtentValue::None => Self::deleted_extent(),
            ExtentValue::Some { device_offset, checksums, key_id } => {
                if let Checksums::Fletcher(checksums) = checksums {
                    if checksums.len() > 0 {
                        let index = (amount / (extent_len / checksums.len() as u64)) as usize;
                        return ExtentValue::Some {
                            device_offset: device_offset + amount,
                            checksums: Checksums::Fletcher(checksums[index..].to_vec()),
                            key_id: *key_id,
                        };
                    }
                }
                ExtentValue::Some {
                    device_offset: device_offset + amount,
                    checksums: Checksums::None,
                    key_id: *key_id,
                }
            }
        }
    }

    /// Returns a new ExtentValue after shrinking the extent from |original_len| to |new_len|.
    pub fn shrunk(&self, original_len: u64, new_len: u64) -> Self {
        match self {
            ExtentValue::None => Self::deleted_extent(),
            ExtentValue::Some { device_offset, checksums, key_id } => {
                if let Checksums::Fletcher(checksums) = checksums {
                    if checksums.len() > 0 {
                        let checksum_len =
                            (new_len / (original_len / checksums.len() as u64)) as usize;
                        return ExtentValue::Some {
                            device_offset: *device_offset,
                            checksums: Checksums::Fletcher(checksums[..checksum_len].to_vec()),
                            key_id: *key_id,
                        };
                    }
                }
                ExtentValue::Some {
                    device_offset: *device_offset,
                    checksums: Checksums::None,
                    key_id: *key_id,
                }
            }
        }
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

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub enum ObjectKind {
    File {
        /// The number of references to this file.
        refs: u64,
        /// The number of bytes allocated to all extents across all attributes for this file.
        allocated_size: u64,
        /// The encryption keys for the file.
        keys: EncryptionKeys,
    },
    Directory {
        /// The number of sub-directories in this directory.
        sub_dirs: u64,
    },
    Graveyard,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub enum EncryptionKeys {
    None,
    AES256XTS(AES256XTSKeys),
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct AES256XTSKeys {
    /// The identifier of the wrapping key.  The identifier has meaning to whatever is doing the
    /// unwrapping.
    pub wrapping_key_id: u64,

    /// The keys (wrapped).  To support key rolling and clones, there can be more than one key.
    /// Each of the keys is given an identifier.  The identifier is unique to the object.  AES 256
    /// requires a 512 bit key, which is made of two 256 bit keys, one for the data and one for the
    /// tweak.  Both those keys are derived from the single 256 bit key we have here.
    pub keys: Vec<(/* id= */ u64, [u8; 32])>,
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
    /// Some keys have no value (this often indicates a tombstone of some sort).  Records with this
    /// value are always filtered when a major compaction is performed, so the meaning must be the
    /// same as if the item was not present.
    None,
    /// Some keys have no value but need to differentiate between a present value and no value
    /// (None) i.e. their value is really a boolean: None => false, Some => true.
    Some,
    /// The value for an ObjectKey::Object record.
    Object { kind: ObjectKind, attributes: ObjectAttributes },
    /// An attribute associated with a file object. |size| is the size of the attribute in bytes.
    Attribute { size: u64 },
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
        keys: EncryptionKeys,
    ) -> ObjectValue {
        ObjectValue::Object {
            kind: ObjectKind::File { refs, allocated_size, keys },
            attributes: ObjectAttributes { creation_time, modification_time },
        }
    }
    /// Creates an ObjectValue for an object attribute.
    pub fn attribute(size: u64) -> ObjectValue {
        ObjectValue::Attribute { size }
    }
    /// Creates an ObjectValue for an object child.
    pub fn child(object_id: u64, object_descriptor: ObjectDescriptor) -> ObjectValue {
        ObjectValue::Child { object_id, object_descriptor }
    }
}

pub type ObjectItem = Item<ObjectKey, ObjectValue>;

#[cfg(test)]
mod tests {
    use {
        super::{ExtentKey, ObjectKey},
        crate::lsm_tree::types::{NextKey, OrdLowerBound, OrdUpperBound},
        std::cmp::Ordering,
    };

    #[test]
    fn test_extent_cmp() {
        let extent = ExtentKey::new(1, 0, 100..150);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(1, 0, 0..100)), Ordering::Greater);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(1, 0, 0..110)), Ordering::Greater);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(1, 0, 0..150)), Ordering::Equal);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(1, 0, 99..150)), Ordering::Equal);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(1, 0, 100..150)), Ordering::Equal);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(1, 0, 0..151)), Ordering::Less);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(1, 0, 100..151)), Ordering::Less);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(1, 0, 150..1000)), Ordering::Less);

        // Attribute ID takes precedence over range
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(1, 1, 0..100)), Ordering::Less);
        // Object ID takes precedence over all
        assert_eq!(
            ExtentKey::new(1, 1, 0..100).cmp_upper_bound(&ExtentKey::new(0, 1, 150..1000)),
            Ordering::Greater
        );
    }

    #[test]
    fn test_extent_cmp_lower_bound() {
        let extent = ExtentKey::new(1, 0, 100..150);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(1, 0, 0..100)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(1, 0, 0..110)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(1, 0, 0..150)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(1, 0, 0..1000)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(1, 0, 99..1000)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(1, 0, 100..150)), Ordering::Equal);
        // cmp_lower_bound does not check the upper bound of the range
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(1, 0, 100..1000)), Ordering::Equal);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(1, 0, 101..102)), Ordering::Less);

        // Attribute ID takes precedence over range
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(1, 1, 0..100)), Ordering::Less);
        // Object ID takes precedence over all
        assert_eq!(
            ExtentKey::new(1, 1, 0..100).cmp_lower_bound(&ExtentKey::new(0, 1, 150..1000)),
            Ordering::Greater
        );
    }

    #[test]
    fn test_extent_search_and_insertion_key() {
        let extent = ExtentKey::new(1, 0, 100..150);
        assert_eq!(extent.search_key(), ExtentKey::new(1, 0, 0..101));
        assert_eq!(extent.cmp_lower_bound(&extent.search_key()), Ordering::Greater);
        assert_eq!(extent.cmp_upper_bound(&extent.search_key()), Ordering::Greater);
        assert_eq!(extent.key_for_merge_into(), ExtentKey::new(1, 0, 0..100));
        assert_eq!(extent.cmp_lower_bound(&extent.key_for_merge_into()), Ordering::Greater);
    }

    #[test]
    fn test_next_key() {
        let next_key = ExtentKey::new(1, 0, 0..100).next_key().unwrap();
        assert_eq!(ExtentKey::new(1, 0, 101..200).cmp_lower_bound(&next_key), Ordering::Greater);
        assert_eq!(ExtentKey::new(1, 0, 100..200).cmp_lower_bound(&next_key), Ordering::Equal);
        assert_eq!(ExtentKey::new(1, 0, 100..101).cmp_lower_bound(&next_key), Ordering::Equal);
        assert_eq!(ExtentKey::new(1, 0, 99..100).cmp_lower_bound(&next_key), Ordering::Less);
        assert_eq!(ExtentKey::new(1, 0, 0..100).cmp_upper_bound(&next_key), Ordering::Less);
        assert_eq!(ExtentKey::new(1, 0, 99..100).cmp_upper_bound(&next_key), Ordering::Less);
        assert_eq!(ExtentKey::new(1, 0, 100..101).cmp_upper_bound(&next_key), Ordering::Equal);
        assert_eq!(ExtentKey::new(1, 0, 100..200).cmp_upper_bound(&next_key), Ordering::Greater);
        assert_eq!(ExtentKey::new(1, 0, 50..101).cmp_upper_bound(&next_key), Ordering::Equal);
        assert_eq!(ExtentKey::new(1, 0, 50..200).cmp_upper_bound(&next_key), Ordering::Greater);

        assert!(ObjectKey::object(1).next_key().is_none());
    }
}
