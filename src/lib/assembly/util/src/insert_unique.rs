// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{btree_map, BTreeMap, BTreeSet};

/// Extension trait for inserting into a collection, while validating that no
/// duplicate values (or keys, in the case of a Map) are being inserted.
///
/// This is similar to the unstable [`BTreeMap::try_insert()`], but can be
/// implemented for any collection type.
///
/// In Map implementations, the Error implements ['DuplicateKeyError'] to provide
/// access to the duplicate key, the previous value, and the new value.
///
/// # Examples
///
/// Set implementations return `Err(value)` in the case of a duplicate.
/// ```
/// use std::collections::BTreeSet;
/// use assembly_util::InsertUniqueExt;
///
/// # fn main() -> Result<(), String> {
/// let mut set = BTreeSet::new();
/// set.try_insert_unique("some string".to_string())?; // Ok(())
/// let result = set.try_insert_unique("some string".to_string());
/// let err_value = result.unwrap_err();
/// assert_eq!(err_value, "some string");
/// Ok(())
/// # }
/// ```
///
/// Map Implementations return `Err(impl DuplicateKeyError<K,V>)` in the case of
/// attempting to insert an entry with a key that already exists in the map.
/// ```
/// use std::collections::BTreeMap;
/// use assembly_util::{InsertUniqueExt, MapEntry, DuplicateKeyError};
///
/// # fn main() -> Result<(), String> {
/// let mut map = BTreeMap::<&str, i32>::new();
/// map.try_insert_unique(MapEntry("some key", 42)).map_err(|e| e.to_string())?;
/// let result = map.try_insert_unique(MapEntry("some key", 34));
/// let error = result.unwrap_err();  // impl DuplicateKeyError<String, i32>
/// println!(
///     "attempted to set duplicate value '{}' for key '{}', previously was '{}'",
///     error.new_value(),
///     error.key(),
///     error.previous_value());
/// # Ok(())
/// # }
/// ```
pub trait InsertUniqueExt<T> {
    /// The error type that is returned by an implementation of the trait, it is
    /// used to provide access to the previously-set value (or key-value pair).
    type Error;

    /// Inserts a value into a collection, returning an error if the value to be
    /// inserted is a duplicate of an existing value.
    fn try_insert_unique(self, value: T) -> Result<(), Self::Error>;
}

/// Extension trait for inserting into a collection, while validating that no
/// duplicate values are being inserted.
///
/// In the event of a duplicate item, the iterator is consumed up to the
/// duplicate, and the `Error` returned will contain the duplicate item.
///
/// In Map implementations, the Error implements [`DuplicateKeyError`] to
/// provide access to the duplicate key, the previous value, and the new value.
///
/// # Examples
///
/// ```
/// use std::collections::BTreeSet;
/// use assembly_util::{InsertAllUniqueExt, InsertUniqueExt};
///
/// # fn main() -> Result<(), String> {
/// let mut set = BTreeSet::new();
/// set.try_insert_unique("some string".to_string())?; // Ok(())
///
/// let mut items_to_insert = vec![
///     "another string".to_string(),
///     "some string".to_string(),
///     "yet another string".to_string()
/// ].into_iter();
///
///
/// let result = set.try_insert_all_unique(&mut items_to_insert);
/// let err_value = result.unwrap_err();
/// assert_eq!(err_value, "some string");
/// assert_eq!(items_to_insert.next().as_deref(), Some("yet another string"));
/// # Ok(())
/// # }
/// ```
pub trait InsertAllUniqueExt<T> {
    /// The error type that is returned by an implementation of the trait, it is
    /// used to provide access to the previously-set value (or key-value pair).
    type Error;

    /// Inserts all items from the iterator into a collection, returning an error
    /// if any value to be inserted is a duplicate.
    fn try_insert_all_unique(self, iter: impl IntoIterator<Item = T>) -> Result<(), Self::Error>;
}

impl<T: Ord> InsertUniqueExt<T> for &mut BTreeSet<T> {
    type Error = T;
    fn try_insert_unique(self, value: T) -> Result<(), T> {
        if self.contains(&value) {
            Err(value)
        } else {
            self.insert(value);
            Ok(())
        }
    }
}

impl<T: Ord> InsertAllUniqueExt<T> for &mut BTreeSet<T> {
    type Error = T;
    fn try_insert_all_unique(self, iter: impl IntoIterator<Item = T>) -> Result<(), T> {
        for value in iter.into_iter() {
            if self.contains(&value) {
                return Err(value);
            } else {
                self.insert(value);
            }
        }
        Ok(())
    }
}

/// Wrapper for adding a Key-Value pair to a Map using InsertUniqueExt.
pub struct MapEntry<K, V>(pub K, pub V);

/// Trait for consistently providing access to the duplicated key, and both the
/// previously- and newly-inserted values for any Map insert (BTreeMap, HashMap,
/// etc.).
pub trait DuplicateKeyError<K, V> {
    /// Accessor for the key which had an attempt to add a duplicate value for.
    fn key(&self) -> &K;

    /// Accessor for the previously-stored value at that key.
    fn previous_value(&self) -> &V;

    /// Accessor for the value that the caller was attempting to add over the
    /// previous value.
    fn new_value(&self) -> &V;
}

impl<'a, K: Ord, V> InsertUniqueExt<MapEntry<K, V>> for &'a mut BTreeMap<K, V> {
    type Error = BTreeMapDuplicateKeyError<'a, K, V>;
    fn try_insert_unique(self, entry: MapEntry<K, V>) -> Result<(), Self::Error> {
        let MapEntry(key, new_value) = entry;
        match self.entry(key) {
            btree_map::Entry::Vacant(entry) => {
                entry.insert(new_value);
                Ok(())
            }
            btree_map::Entry::Occupied(existing_entry) => {
                Err(BTreeMapDuplicateKeyError { existing_entry, new_value })
            }
        }
    }
}

impl<'a, K: Ord, V> InsertAllUniqueExt<MapEntry<K, V>> for &'a mut BTreeMap<K, V> {
    type Error = BTreeMapDuplicateKeyError<'a, K, V>;
    fn try_insert_all_unique(
        self,
        iter: impl IntoIterator<Item = MapEntry<K, V>>,
    ) -> Result<(), Self::Error> {
        let result = iter.into_iter().try_for_each(|entry| {
            let MapEntry(key, new_value) = entry;
            if self.contains_key(&key) {
                Err((key, new_value))
            } else {
                self.insert(key, new_value);
                Ok(())
            }
        });
        if let Err((key, new_value)) = result {
            if let btree_map::Entry::Occupied(existing_entry) = self.entry(key) {
                return Err(BTreeMapDuplicateKeyError { existing_entry, new_value });
            }
            // The result cannot be Err() and the map not have an occupied entry,
            // based on the contains_key() check above, so this is not reachable.
            unreachable!();
        }
        Ok(())
    }
}

/// Wrapper to provide both the duplicated key (and its existing value) with the
/// new value that was attempted to be inserted.
#[derive(Debug)]
pub struct BTreeMapDuplicateKeyError<'a, K: Ord, V> {
    existing_entry: btree_map::OccupiedEntry<'a, K, V>,
    new_value: V,
}

impl<'a, K: Ord, V> DuplicateKeyError<K, V> for BTreeMapDuplicateKeyError<'a, K, V> {
    fn key(&self) -> &K {
        self.existing_entry.key()
    }
    fn previous_value(&self) -> &V {
        self.existing_entry.get()
    }
    fn new_value(&self) -> &V {
        &self.new_value
    }
}

impl<'a, K: 'a + Ord, V: 'a> std::fmt::Display for BTreeMapDuplicateKeyError<'a, K, V>
where
    K: 'a + std::fmt::Display,
    V: 'a + std::fmt::Display,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "duplicate key: '{}', found while inserting value: '{}', the previous value was: '{}'",
            self.key(),
            self.new_value(),
            &self.previous_value()
        )
    }
}

impl<'a, K: 'a + Ord, V: 'a> std::error::Error for BTreeMapDuplicateKeyError<'a, K, V>
where
    K: 'a + std::fmt::Display + std::fmt::Debug,
    V: 'a + std::fmt::Display + std::fmt::Debug,
{
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
    fn test_btrees_set_insert_all_ok() {
        let mut set = BTreeSet::new();
        set.insert("some string".to_string());

        let items_to_insert = vec!["another string".to_string(), "yet another string".to_string()];
        let result = set.try_insert_all_unique(items_to_insert);
        assert!(result.is_ok());

        assert_eq!(set.len(), 3);
        assert!(set.contains("some string"));
        assert!(set.contains("another string"));
        assert!(set.contains("yet another string"));
    }

    #[test]
    fn test_btrees_set_insert_all_fails_on_duplicate() {
        let mut set = BTreeSet::new();
        set.insert("some string".to_string());

        // Setup an iterator that emits:
        //  - a new value
        //  - a duplicate value
        //  - another new value
        let mut items_to_insert = vec![
            "another string".to_string(),
            "some string".to_string(),
            "yet another string".to_string(),
        ]
        .into_iter();

        let result = set.try_insert_all_unique(&mut items_to_insert);

        // Validate that the error contains the duplicate value.
        let err = result.unwrap_err();
        assert_eq!(err, "some string");

        // Validate that the first new value was added:
        assert_eq!(set.len(), 2);
        assert!(set.contains("some string"));
        assert!(set.contains("another string"));

        // Validate that the iterator will still return the next value after the
        // the duplicate.
        assert_eq!(items_to_insert.next().unwrap(), "yet another string");
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
    fn test_btree_map_error_impls_display() {
        let mut map = BTreeMap::new();
        map.insert("first_key".to_string(), "first_value".to_string());
        let result =
            map.try_insert_unique(MapEntry("first_key".to_string(), "second_value".to_string()));

        let error = result.unwrap_err();
        let error_display_string = format!("{}", error);
        assert_eq!(error_display_string, format!("duplicate key: '{}', found while inserting value: '{}', the previous value was: '{}'", "first_key", "second_value", "first_value"));
    }

    #[test]
    fn test_btree_map_error_impls_error() {
        let mut map = BTreeMap::new();
        map.insert("first_key".to_string(), "first_value".to_string());
        let result =
            map.try_insert_unique(MapEntry("first_key".to_string(), "second_value".to_string()));

        let error = result.unwrap_err();

        // call Error::source as a fully qualified fn on the trait so that we
        // know that the error returned implements Error.
        assert!(std::error::Error::source(&error).is_none());
    }

    #[test]
    fn test_btree_map_error_is_returnable_via_anyhow() {
        fn inner_fn(map: &mut BTreeMap<String, String>) -> anyhow::Result<()> {
            map.try_insert_unique(MapEntry("key".to_string(), "value".to_string()))
                .map_err(|e| anyhow::anyhow!(e.to_string()))
        }

        let mut map = BTreeMap::new();
        map.insert("key".to_string(), "value".to_string());
        let result = inner_fn(&mut map);

        assert!(result.is_err());
    }

    #[test]
    fn test_btree_map_insert_all_ok() {
        let mut map = BTreeMap::new();
        map.insert("first_key".to_string(), "first_value".to_string());

        let entries_to_insert = vec![
            MapEntry("second_key".to_string(), "second_value".to_string()),
            MapEntry("third_key".to_string(), "third_value".to_string()),
        ];
        let result = map.try_insert_all_unique(entries_to_insert);

        // Validate that it returned ok.
        assert!(result.is_ok());

        // And that both key-value pairs are present.
        assert_eq!(map.len(), 3);
        assert_eq!(map.get("first_key").unwrap(), "first_value");
        assert_eq!(map.get("second_key").unwrap(), "second_value");
        assert_eq!(map.get("third_key").unwrap(), "third_value");
    }

    #[test]
    fn test_btree_map_insert_all_fails_on_duplicate_key() {
        let mut map = BTreeMap::new();
        map.insert("some key".to_string(), "some value".to_string());

        // Setup an iterator that emits:
        //  - a new value
        //  - a duplicate value
        //  - another new value
        let mut entries_to_insert = vec![
            MapEntry("new key".to_string(), "new value".to_string()),
            MapEntry("some key".to_string(), "duplicate key value".to_string()),
            MapEntry("another key".to_string(), "another value".to_string()),
        ]
        .into_iter();
        let result = map.try_insert_all_unique(&mut entries_to_insert);

        // Validate it returned an error, referencing the duplicated key / value
        assert!(result.is_err());
        let entry = result.unwrap_err();
        assert_eq!(entry.key(), "some key");
        assert_eq!(entry.previous_value(), "some value");
        assert_eq!(entry.new_value(), "duplicate key value");

        // That both the original and the second key-value pairs are present,
        // but not the third or fourth.
        assert_eq!(map.len(), 2);
        assert_eq!(map.get("some key").unwrap(), "some value");
        assert_eq!(map.get("new key").unwrap(), "new value");

        // And that the iterator still contains the item after the duplicate.
        let MapEntry(next_key, next_value) = entries_to_insert.next().unwrap();
        assert_eq!(next_key, "another key");
        assert_eq!(next_value, "another value");
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
