// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{btree_map, BTreeMap, BTreeSet};

/// Extension trait for inserting into a collection, while validating that no
/// duplicate values (or keys, in the case of a Map) are being inserted.
///
/// This is similar to the unstable `try_insert()` fn on `BTreeMap`, but can be
/// implemented for any collection type.
///
/// # Example
///
/// ```
/// use std::collections::BTreeMap;
/// use crate::{InsertUniqueExt, MapEntry, DuplicateKeyError};
///
/// let mut map = BTreeMap::new()
/// map.try_insert_unique(MapEntry("some key", 42))?;
/// let result = map.try_insert_unique(MapEntry("some key", 34));
/// let error = result.unwrap_err();
/// println!(
///     "attempted to set duplicate value '{}' for key '{}', previously was '{}'",
///     error.new_value(),
///     error.key(),
///     error.previous_value());
pub trait InsertUniqueExt<'a, T> {
    /// The error type that is returned by an implementation of the trait, it is
    /// used to provide access to the previously-set value (or key-value pair).
    type Error;
    /// Inserts a value into a collection, returning an error if the value to be
    /// inserted is a duplicate of an existing value.
    fn try_insert_unique(&'a mut self, value: T) -> Result<(), Self::Error>;
}

impl<'a, T: Ord> InsertUniqueExt<'a, T> for BTreeSet<T> {
    type Error = T;
    fn try_insert_unique(&'a mut self, value: T) -> Result<(), T> {
        if self.contains(&value) {
            Err(value)
        } else {
            self.insert(value);
            Ok(())
        }
    }
}

/// Wrapper for adding a Key-Value pair to a Map using InsertUniqueExt.
pub struct MapEntry<K, V>(pub K, pub V);

/// Trait for consistently providing access to the duplicated key, and both the
/// previously- and newly-inserted values for any Map insert (BTreeMap, HashMap,
/// etc.).
pub trait DuplicateKeyError<'a, K, V> {
    /// Accessor for the key which had an attempt to add a duplicate value for.
    fn key(&'a self) -> &'a K;

    /// Accessor for the previously-stored value at that key.
    fn previous_value(&'a self) -> &'a V;

    /// Accessor for the value that the caller was attempting to add over the
    /// previous value.
    fn new_value(&'a self) -> &'a V;
}

impl<'a, K: Ord + 'a, V: 'a> InsertUniqueExt<'a, MapEntry<K, V>> for BTreeMap<K, V> {
    type Error = BTreeMapDuplicateKeyError<'a, K, V>;
    fn try_insert_unique(&'a mut self, entry: MapEntry<K, V>) -> Result<(), Self::Error> {
        let MapEntry(key, value) = entry;
        match self.entry(key) {
            btree_map::Entry::Vacant(entry) => {
                entry.insert(value);
                Ok(())
            }
            btree_map::Entry::Occupied(entry) => {
                Err(BTreeMapDuplicateKeyError { existing_entry: entry, new_value: value })
            }
        }
    }
}

/// Wrapper to provide both the duplicated key (and its existing value) with the
/// new value that was attempted to be inserted.
pub struct BTreeMapDuplicateKeyError<'a, K, V> {
    existing_entry: btree_map::OccupiedEntry<'a, K, V>,
    new_value: V,
}

impl<'a, K: 'a + Ord, V: 'a> DuplicateKeyError<'a, K, V> for BTreeMapDuplicateKeyError<'a, K, V> {
    fn key(&'a self) -> &'a K {
        self.existing_entry.key()
    }
    fn previous_value(&'a self) -> &'a V {
        self.existing_entry.get()
    }
    fn new_value(&'a self) -> &'a V {
        &self.new_value
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_btrees_set_insert_ok() {
        let mut set = BTreeSet::new();
        set.insert("some string");
        let result = set.try_insert_unique("some other string");
        assert!(result.is_ok());

        assert_eq!(set.len(), 2);
        assert!(set.contains("some string"));
        assert!(set.contains("some other string"));
    }

    #[test]
    fn test_btrees_set_insert_fails_on_duplicate() {
        let mut set = BTreeSet::new();
        set.insert("some string");
        let result = set.try_insert_unique("some string");
        let err = result.unwrap_err();
        assert_eq!("some string", err);

        assert_eq!(set.len(), 1);
        assert!(set.contains("some string"));
    }

    #[test]
    fn test_btree_map_insert_ok() {
        let mut map = BTreeMap::new();
        map.insert("first_key".to_string(), "first_value".to_string());
        let result =
            map.try_insert_unique(MapEntry("second_key".to_string(), "second_value".to_string()));

        // Validate that it returned ok.
        assert!(result.is_ok());

        // And that both key-value pairs are present.
        assert_eq!(map.len(), 2);
        assert_eq!(map.get("first_key").unwrap(), "first_value");
        assert_eq!(map.get("second_key").unwrap(), "second_value");
    }

    #[test]
    fn test_btree_map_insert_fails_on_duplicate_key() {
        let mut map = BTreeMap::new();
        map.insert("first_key".to_string(), "first_value".to_string());
        let result =
            map.try_insert_unique(MapEntry("first_key".to_string(), "second_value".to_string()));

        // Validate it returned the correct error
        assert!(result.is_err());
        let entry = result.unwrap_err();
        assert_eq!(entry.key(), "first_key");
        assert_eq!(entry.previous_value(), "first_value");
        assert_eq!(entry.new_value(), "second_value");

        // And that the map hasn't changed the key (or added another)
        assert_eq!(map.len(), 1);
        assert_eq!(map.get("first_key").unwrap(), "first_value");
    }

    #[test]
    fn test_btree_set_insert_fails_on_duplicate_value_with_tricky_type() {
        // Types can implement Ord themselves, but say only over certain of
        // their fields.  This test proves that the correct _real_ values are
        // left in the Set, and returned on duplicate value error.

        struct TrickyType {
            key_field: u32,
            value_field: String,
        }
        impl PartialOrd for TrickyType {
            // PartialOrd is only implemented on the `key_field`
            fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
                self.key_field.partial_cmp(&other.key_field)
            }
        }
        impl PartialEq for TrickyType {
            // PartialEq is only implemented on the `key_field`
            fn eq(&self, other: &Self) -> bool {
                self.key_field == other.key_field
            }
        }
        impl Ord for TrickyType {
            // Ord is only implemented on the `key_field`
            fn cmp(&self, other: &Self) -> std::cmp::Ordering {
                self.key_field.cmp(&other.key_field)
            }
        }
        impl Eq for TrickyType {}

        let first_tricky_value = TrickyType { key_field: 42, value_field: "first_value".into() };
        let second_tricky_value = TrickyType { key_field: 42, value_field: "second_value".into() };

        let mut set = BTreeSet::new();
        set.insert(first_tricky_value);

        let result = set.try_insert_unique(second_tricky_value);
        assert!(result.is_err());
        let result_value = result.unwrap_err();

        // Validate that the value in the result is the second value.
        assert_eq!(result_value.value_field, "second_value");

        // Validate that the only value in the set is the first value.
        let set_value = set.iter().nth(0).unwrap();
        assert_eq!(set_value.value_field, "first_value");
    }
}
