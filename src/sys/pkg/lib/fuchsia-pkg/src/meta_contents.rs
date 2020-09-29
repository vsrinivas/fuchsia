// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::MetaContentsError;
use fuchsia_merkle::Hash;
use std::collections::{btree_map, BTreeMap};
use std::io::{self, BufRead};
use std::str::FromStr;

/// A `MetaContents` represents the "meta/contents" file of a Fuchsia archive
/// file of a Fuchsia package.
/// It validates that all resource paths are valid and that none of them start
/// with "meta/".
#[derive(Debug, PartialEq, Eq)]
pub struct MetaContents {
    contents: BTreeMap<String, Hash>,
}

impl MetaContents {
    /// Creates a `MetaContents` from a `map` from resource paths to Merkle roots.
    /// Validates that all resource paths are valid Fuchsia package resource paths,
    /// and that none of the resource paths start with "meta/".
    ///
    /// # Examples
    /// ```
    /// # use fuchsia_merkle::Hash;
    /// # use fuchsia_pkg::MetaContents;
    /// # use maplit::btreemap;
    /// # use std::str::FromStr;
    /// let map = btreemap! {
    ///     "bin/my_prog".to_string() =>
    ///         Hash::from_str(
    ///             "0000000000000000000000000000000000000000000000000000000000000000")
    ///         .unwrap(),
    ///     "lib/mylib.so".to_string() =>
    ///         Hash::from_str(
    ///             "1111111111111111111111111111111111111111111111111111111111111111")
    ///         .unwrap(),
    /// };
    /// let meta_contents = MetaContents::from_map(map).unwrap();
    pub fn from_map(map: BTreeMap<String, Hash>) -> Result<Self, MetaContentsError> {
        for resource_path in map.keys() {
            crate::path::check_resource_path(&resource_path).map_err(|e| {
                MetaContentsError::ResourcePath { cause: e, path: resource_path.to_string() }
            })?;
            if resource_path.starts_with("meta/") {
                return Err(MetaContentsError::ExternalContentInMetaDirectory {
                    path: resource_path.to_string(),
                });
            }
        }
        Ok(MetaContents { contents: map })
    }

    /// Serializes a "meta/contents" file to `writer`.
    ///
    /// # Examples
    /// ```
    /// # use fuchsia_merkle::Hash;
    /// # use fuchsia_pkg::MetaContents;
    /// # use maplit::btreemap;
    /// # use std::str::FromStr;
    /// let map = btreemap! {
    ///     "bin/my_prog".to_string() =>
    ///         Hash::from_str(
    ///             "0000000000000000000000000000000000000000000000000000000000000000")
    ///         .unwrap(),
    ///     "lib/mylib.so".to_string() =>
    ///         Hash::from_str(
    ///             "1111111111111111111111111111111111111111111111111111111111111111")
    ///         .unwrap(),
    /// };
    /// let meta_contents = MetaContents::from_map(map).unwrap();
    /// let mut bytes = Vec::new();
    /// meta_contents.serialize(&mut bytes).unwrap();
    /// let expected = "bin/my_prog=0000000000000000000000000000000000000000000000000000000000000000\n\
    ///                 lib/mylib.so=1111111111111111111111111111111111111111111111111111111111111111\n";
    /// assert_eq!(bytes.as_slice(), expected.as_bytes());
    /// ```
    pub fn serialize(&self, writer: &mut impl io::Write) -> io::Result<()> {
        for (path, hash) in self.contents.iter() {
            writeln!(writer, "{}={}", path, hash)?;
        }
        Ok(())
    }

    /// Deserializes a "meta/contents" file from a `reader`.
    ///
    /// # Examples
    /// ```
    /// # use fuchsia_merkle::Hash;
    /// # use fuchsia_pkg::MetaContents;
    /// # use maplit::btreemap;
    /// # use std::str::FromStr;
    /// let bytes = "bin/my_prog=0000000000000000000000000000000000000000000000000000000000000000\n\
    ///              lib/mylib.so=1111111111111111111111111111111111111111111111111111111111111111\n".as_bytes();
    /// let meta_contents = MetaContents::deserialize(bytes).unwrap();
    /// let expected_contents = btreemap! {
    ///     "bin/my_prog".to_string() =>
    ///         Hash::from_str(
    ///             "0000000000000000000000000000000000000000000000000000000000000000")
    ///         .unwrap(),
    ///     "lib/mylib.so".to_string() =>
    ///         Hash::from_str(
    ///             "1111111111111111111111111111111111111111111111111111111111111111")
    ///         .unwrap(),
    /// };
    /// assert_eq!(meta_contents.contents(), &expected_contents);
    /// ```
    pub fn deserialize(reader: impl io::Read) -> Result<Self, MetaContentsError> {
        let reader = io::BufReader::new(reader);
        let mut contents = BTreeMap::new();
        for line in reader.lines() {
            let line = line?;
            let i = line
                .rfind('=')
                .ok_or_else(|| MetaContentsError::EntryHasNoEqualsSign { entry: line.clone() })?;

            let hash = Hash::from_str(&line[i + 1..])?;
            let path = line[..i].to_string();

            match contents.entry(path) {
                btree_map::Entry::Vacant(entry) => entry.insert(hash),
                btree_map::Entry::Occupied(entry) => {
                    return Err(MetaContentsError::DuplicateResourcePath {
                        path: entry.key().clone(),
                    });
                }
            };
        }
        Ok(MetaContents { contents })
    }

    /// Get the map from blob resource paths to Merkle Tree root hashes.
    pub fn contents(&self) -> &BTreeMap<String, Hash> {
        &self.contents
    }

    /// Take the map from blob resource paths to Merkle Tree root hashes.
    pub fn into_contents(self) -> BTreeMap<String, Hash> {
        self.contents
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::errors::ResourcePathError;
    use crate::test::*;
    use maplit::btreemap;
    use proptest::prelude::*;
    use std::str::FromStr;

    #[test]
    fn test_deserialize_empty_file() {
        let empty = Vec::new();
        let meta_contents = MetaContents::deserialize(empty.as_slice()).unwrap();
        assert_eq!(meta_contents.contents(), &BTreeMap::new());
        assert_eq!(meta_contents.into_contents(), BTreeMap::new());
    }

    #[test]
    fn test_deserialize_known_file() {
        let bytes =
            "a-host/path=0000000000000000000000000000000000000000000000000000000000000000\n\
             other/host/path=1111111111111111111111111111111111111111111111111111111111111111\n"
                .as_bytes();
        let meta_contents = MetaContents::deserialize(bytes).unwrap();
        let expected_contents = btreemap! {
            "a-host/path".to_string() =>
                Hash::from_str("0000000000000000000000000000000000000000000000000000000000000000").unwrap(),
            "other/host/path".to_string() =>
                Hash::from_str("1111111111111111111111111111111111111111111111111111111111111111").unwrap(),
        };
        assert_eq!(meta_contents.contents(), &expected_contents);
        assert_eq!(meta_contents.into_contents(), expected_contents);
    }

    proptest! {
        #[test]
        fn test_reject_invalid_resource_path(
            ref path in random_resource_path(1, 3),
            ref hex in random_merkle_hex())
        {
            prop_assume!(!path.starts_with("meta/"));
            let invalid_path = format!("{}/", path);
            let map = btreemap! {
                invalid_path.clone() =>
                    Hash::from_str(hex.as_str()).unwrap(),
            };
            assert_matches!(
                MetaContents::from_map(map),
                Err(MetaContentsError::ResourcePath {
                    cause: ResourcePathError::PathEndsWithSlash,
                    path })
                    => prop_assert_eq!(path, invalid_path));
        }

        #[test]
        fn test_reject_file_in_meta(
            ref path in random_resource_path(1, 3),
            ref hex in random_merkle_hex())
        {
            let invalid_path = format!("meta/{}", path);
            let map = btreemap! {
                invalid_path.clone() =>
                    Hash::from_str(hex.as_str()).unwrap(),
            };
            assert_matches!(
                MetaContents::from_map(map),
                Err(MetaContentsError::ExternalContentInMetaDirectory { path })
                    => prop_assert_eq!(path, invalid_path));
        }

        #[test]
        fn test_serialize(
            ref path0 in random_resource_path(1, 3),
            ref hex0 in random_merkle_hex(),
            ref path1 in random_resource_path(1, 3),
            ref hex1 in random_merkle_hex())
        {
            prop_assume!(path0 != path1);
            let map = btreemap! {
                path0.clone() =>
                    Hash::from_str(hex0.as_str()).unwrap(),
                path1.clone() =>
                    Hash::from_str(hex1.as_str()).unwrap(),
            };
            let meta_contents = MetaContents::from_map(map).unwrap();
            let mut bytes = Vec::new();

            meta_contents.serialize(&mut bytes).unwrap();

            let ((first_path, first_hex), (second_path, second_hex)) = if path0 <= path1 {
                ((path0, hex0), (path1, hex1))
            } else {
                ((path1, hex1), (path0, hex0))
            };
            let expected = format!(
                "{}={}\n{}={}\n",
                first_path,
                first_hex.to_ascii_lowercase(),
                second_path,
                second_hex.to_ascii_lowercase());
            prop_assert_eq!(bytes.as_slice(), expected.as_bytes());
        }

        #[test]
        fn test_serialize_deserialize_is_id(
            contents in prop::collection::btree_map(
                random_external_resource_path(), random_hash(), 0..4)
        ) {
            let meta_contents = MetaContents::from_map(contents).unwrap();
            let mut serialized = Vec::new();
            meta_contents.serialize(&mut serialized).unwrap();
            let deserialized = MetaContents::deserialize(serialized.as_slice()).unwrap();
            prop_assert_eq!(meta_contents, deserialized);
        }
    }
}
