// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::MetaContentsError;
use fuchsia_merkle::Hash;
use std::collections::BTreeMap;
use std::io;

/// A `MetaContents` represents the "meta/contents" file of a Fuchsia archive
/// file of a Fuchsia package.
/// It validates that all resource paths are valid and that none of them start
/// with "meta/".
#[derive(Debug)]
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
            write!(writer, "{}={}\n", path, hash)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::errors::ResourcePathError;
    use crate::test::*;
    use maplit::btreemap;
    use proptest::{prop_assert, prop_assert_eq, prop_assume, proptest, proptest_helper};
    use std::str::FromStr;

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
    }
}
