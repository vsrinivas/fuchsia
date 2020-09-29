// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::PathHashMappingError,
    fuchsia_hash::Hash,
    fuchsia_pkg::PackagePath,
    std::{
        io::{self, BufRead as _},
        marker::PhantomData,
        str::FromStr as _,
    },
};

/// PhantomData type marker to indicate a `PathHashMapping` is a "data/static_packages" file.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub struct Static;
/// PhantomData type marker to indicate a `PathHashMapping` is a "data/cache_packages" file.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub struct Cache;

pub type StaticPackages = PathHashMapping<Static>;
pub type CachePackages = PathHashMapping<Cache>;

/// A `PathHashMapping` reads and writes line-oriented "{package_path}={hash}\n" files, e.g. "data/static_packages"
/// and "data/cache_packages".
#[derive(Debug, PartialEq, Eq)]
pub struct PathHashMapping<T> {
    contents: Vec<(PackagePath, Hash)>,
    phantom: PhantomData<T>,
}

impl<T> PathHashMapping<T> {
    /// Reads the line-oriented "package-path=hash" static_packages file.
    /// Validates the package paths and hashes.
    pub fn deserialize(reader: impl io::Read) -> Result<Self, PathHashMappingError> {
        let reader = io::BufReader::new(reader);
        let mut contents = vec![];
        for line in reader.lines() {
            let line = line?;
            let i = line.rfind('=').ok_or_else(|| PathHashMappingError::EntryHasNoEqualsSign {
                entry: line.clone(),
            })?;
            let hash = Hash::from_str(&line[i + 1..])?;
            let path = line[..i].parse()?;
            contents.push((path, hash));
        }
        Ok(Self { contents, phantom: PhantomData })
    }

    /// Iterator over the contents of the mapping.
    pub fn contents(&self) -> impl Iterator<Item = &(PackagePath, Hash)> + ExactSizeIterator {
        self.contents.iter()
    }

    /// Iterator over the contained hashes.
    pub fn hashes(&self) -> impl Iterator<Item = &Hash> {
        self.contents.iter().map(|(_, hash)| hash)
    }

    /// Get the hash for a package.
    pub fn hash_for_package(&self, path: &PackagePath) -> Option<Hash> {
        self.contents.iter().find_map(|(n, hash)| if n == path { Some(*hash) } else { None })
    }

    /// Create an empty mapping.
    pub fn empty() -> Self {
        Self { contents: vec![], phantom: PhantomData }
    }

    /// Create a `PathHashMapping` from a `Vec` of `(PackagePath, Hash)` pairs.
    pub fn from_entries(entries: Vec<(PackagePath, Hash)>) -> Self {
        Self { contents: entries, phantom: PhantomData }
    }

    /// Write a `static_packages` file.
    pub fn serialize(&self, mut writer: impl io::Write) -> Result<(), PathHashMappingError> {
        for entry in self.contents.iter() {
            writeln!(&mut writer, "{}={}", entry.0, entry.1)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fuchsia_pkg::test::random_package_path, matches::assert_matches,
        proptest::prelude::*,
    };

    #[test]
    fn deserialize_empty_file() {
        let empty = Vec::new();
        let static_packages = StaticPackages::deserialize(empty.as_slice()).unwrap();
        assert_eq!(static_packages.hashes().count(), 0);
    }

    #[test]
    fn deserialize_valid_file_list_hashes() {
        let bytes =
            "name/variant=0000000000000000000000000000000000000000000000000000000000000000\n\
             other-name/other-variant=1111111111111111111111111111111111111111111111111111111111111111\n"
                .as_bytes();
        let static_packages = StaticPackages::deserialize(bytes).unwrap();
        assert_eq!(
            static_packages.hashes().cloned().collect::<Vec<_>>(),
            vec![
                "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
                "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap()
            ]
        );
    }

    #[test]
    fn deserialze_rejects_invalid_package_path() {
        let bytes =
            "name/=0000000000000000000000000000000000000000000000000000000000000000\n".as_bytes();
        let res = StaticPackages::deserialize(bytes);
        assert_matches!(res, Err(PathHashMappingError::ParsePackagePath(_)));
    }

    #[test]
    fn deserialize_rejects_invalid_hash() {
        let bytes = "name/variant=invalid-hash\n".as_bytes();
        let res = StaticPackages::deserialize(bytes);
        assert_matches!(res, Err(PathHashMappingError::ParseHash(_)));
    }

    #[test]
    fn deserialize_rejects_missing_equals() {
        let bytes =
            "name/variant~0000000000000000000000000000000000000000000000000000000000000000\n"
                .as_bytes();
        let res = StaticPackages::deserialize(bytes);
        assert_matches!(res, Err(PathHashMappingError::EntryHasNoEqualsSign { .. }));
    }

    #[test]
    fn from_entries_serialize() {
        let static_packages = StaticPackages::from_entries(vec![(
            PackagePath::from_name_and_variant("name0", "0").unwrap(),
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
        )]);

        let mut serialized = vec![];
        static_packages.serialize(&mut serialized).unwrap();
        assert_eq!(
            serialized,
            &b"name0/0=0000000000000000000000000000000000000000000000000000000000000000\n"[..]
        );
    }

    #[test]
    fn hash_for_package_success() {
        let bytes =
            "name/variant=0000000000000000000000000000000000000000000000000000000000000000\n\
             "
            .as_bytes();
        let static_packages = StaticPackages::deserialize(bytes).unwrap();
        let res = static_packages
            .hash_for_package(&PackagePath::from_name_and_variant("name", "variant").unwrap());
        assert_eq!(
            res,
            Some(
                "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
            )
        );
    }

    #[test]
    fn hash_for_missing_package_is_none() {
        let bytes =
            "name/variant=0000000000000000000000000000000000000000000000000000000000000000\n\
             "
            .as_bytes();
        let static_packages = StaticPackages::deserialize(bytes).unwrap();
        let res = static_packages
            .hash_for_package(&PackagePath::from_name_and_variant("nope", "variant").unwrap());
        assert_eq!(res, None);
    }

    prop_compose! {
        fn random_hash()(s in "[A-Fa-f0-9]{64}") -> Hash {
            s.parse().unwrap()
        }
    }

    prop_compose! {
        fn random_static_packages()
            (vec in prop::collection::vec(
                (random_package_path(), random_hash()), 0..4)
            ) -> PathHashMapping<Static> {
                StaticPackages::from_entries(vec)
            }
    }

    proptest! {
        #[test]
        fn serialize_deserialize_identity(static_packages in random_static_packages()) {
            let mut serialized = vec![];
            static_packages.serialize(&mut serialized).unwrap();
            let deserialized = StaticPackages::deserialize(serialized.as_slice()).unwrap();
            prop_assert_eq!(
                static_packages,
                deserialized
            );
        }
    }
}
