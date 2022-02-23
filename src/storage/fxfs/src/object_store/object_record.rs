// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(jfsulliv): need validation after deserialization.

use {
    crate::{
        crypt::WrappedKeys,
        lsm_tree::types::{Item, NextKey, OrdLowerBound, OrdUpperBound},
        serialized_types::Versioned,
    },
    serde::{Deserialize, Serialize},
    std::convert::From,
    std::time::{Duration, SystemTime, UNIX_EPOCH},
};

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
    GraveyardEntry { object_id: u64 },
}

/// ObjectKey is a key in the object store.
#[derive(Clone, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize, Versioned)]
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
    pub fn graveyard_entry(graveyard_object_id: u64, object_id: u64) -> Self {
        Self { object_id: graveyard_object_id, data: ObjectKeyData::GraveyardEntry { object_id } }
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
    AES256XTS(WrappedKeys),
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
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Versioned)]
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

#[cfg(test)]
mod tests {
    use {super::ObjectKey, crate::lsm_tree::types::NextKey};

    // TODO(ripper): Tests similar to extent_record.rs

    #[test]
    fn test_next_key() {
        assert!(ObjectKey::object(1).next_key().is_none());
    }
}
