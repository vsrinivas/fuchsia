// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::MetaSubpackagesError,
    fuchsia_merkle::Hash,
    fuchsia_url::RelativePackageUrl,
    serde::{ser::Serializer, Deserialize, Serialize},
    std::{
        collections::{BTreeMap, HashMap},
        io,
        iter::FromIterator,
    },
};

/// A `MetaSubpackages` represents the "meta/fuchsia.pkg/subpackages" file of a Fuchsia
/// archive file of a Fuchsia package. It validates that all subpackage names
/// are valid.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize)]
#[serde(transparent)]
pub struct MetaSubpackages(VersionedMetaSubpackages);

impl MetaSubpackages {
    pub const PATH: &'static str = "meta/fuchsia.pkg/subpackages";

    fn from_v1(meta_subpackages_v1: MetaSubpackagesV1) -> Self {
        Self(VersionedMetaSubpackages::Version1(meta_subpackages_v1))
    }

    /// Get the map from subpackage names to Merkle Tree root hashes.
    pub fn subpackages(&self) -> &HashMap<RelativePackageUrl, Hash> {
        match &self.0 {
            VersionedMetaSubpackages::Version1(meta) => &meta.subpackages,
        }
    }

    /// Take the map from subpackage names to Merkle Tree root hashes.
    pub fn into_subpackages(self) -> HashMap<RelativePackageUrl, Hash> {
        match self.0 {
            VersionedMetaSubpackages::Version1(meta) => meta.subpackages,
        }
    }

    /// Take the Merkle Tree root hashes in an iterator. The returned iterator may include
    /// duplicates.
    pub fn into_hashes_undeduplicated(self) -> impl Iterator<Item = Hash> {
        self.into_subpackages().into_values()
    }

    pub fn deserialize(reader: impl io::BufRead) -> Result<Self, MetaSubpackagesError> {
        Ok(MetaSubpackages::from_v1(serde_json::from_reader(reader)?))
    }

    pub fn serialize(&self, writer: impl io::Write) -> Result<(), MetaSubpackagesError> {
        Ok(serde_json::to_writer(writer, &self)?)
    }
}

impl FromIterator<(RelativePackageUrl, Hash)> for MetaSubpackages {
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = (RelativePackageUrl, Hash)>,
    {
        MetaSubpackages(VersionedMetaSubpackages::Version1(MetaSubpackagesV1 {
            subpackages: HashMap::from_iter(iter),
        }))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version", deny_unknown_fields)]
enum VersionedMetaSubpackages {
    #[serde(rename = "1")]
    Version1(MetaSubpackagesV1),
}

impl Default for VersionedMetaSubpackages {
    fn default() -> Self {
        VersionedMetaSubpackages::Version1(MetaSubpackagesV1::default())
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Deserialize)]
struct MetaSubpackagesV1 {
    subpackages: HashMap<RelativePackageUrl, Hash>,
}

impl Serialize for MetaSubpackagesV1 {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let MetaSubpackagesV1 { subpackages } = self;

        // Sort the subpackages list to make sure it's in a consistent order.

        #[derive(Serialize)]
        struct Helper<'a> {
            subpackages: BTreeMap<&'a RelativePackageUrl, &'a Hash>,
        }

        Helper { subpackages: subpackages.iter().collect() }.serialize(serializer)
    }
}

/// The following functions may be deprecated. They support the initial
/// implementation of Subpackages (RFC-0154) but are likely to move to
/// a different module.
pub mod transitional {
    use super::*;

    /// Returns a package-resolver-specific value representing the given
    /// subpackage name-to-merkle map, or `None` if the map is empty.
    pub fn context_bytes_from_subpackages_map(
        subpackage_hashes: &HashMap<RelativePackageUrl, Hash>,
    ) -> Result<Option<Vec<u8>>, anyhow::Error> {
        if subpackage_hashes.is_empty() {
            Ok(None)
        } else {
            Ok(Some(serde_json::to_vec(&subpackage_hashes).map_err(|err| {
                anyhow::format_err!(
                    "could not convert subpackages map to json: {:?}, with map values: {:?}",
                    err,
                    subpackage_hashes
                )
            })?))
        }
    }

    /// The inverse of `context_bytes_from_subpackages_map()`.
    pub fn subpackages_map_from_context_bytes(
        context_bytes: &[u8],
    ) -> Result<HashMap<RelativePackageUrl, Hash>, anyhow::Error> {
        let json_value = serde_json::from_slice(context_bytes)?;
        Ok(serde_json::from_value::<HashMap<RelativePackageUrl, Hash>>(json_value)?)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::transitional::*, super::*, crate::test::*,
        fuchsia_url::test::random_relative_package_url, maplit::hashmap, proptest::prelude::*,
        serde_json::json,
    };

    fn zeros_hash() -> Hash {
        "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap()
    }

    fn ones_hash() -> Hash {
        "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap()
    }

    #[test]
    fn deserialize_known_file() {
        let bytes = r#"{
                "version": "1",
                "subpackages": {
                    "a_0_subpackage": "0000000000000000000000000000000000000000000000000000000000000000",
                    "other-1-subpackage": "1111111111111111111111111111111111111111111111111111111111111111"
                }
            }"#.as_bytes();
        let meta_subpackages = MetaSubpackages::deserialize(bytes).unwrap();
        let expected_subpackages = hashmap! {
            RelativePackageUrl::parse("a_0_subpackage").unwrap() => zeros_hash(),
            RelativePackageUrl::parse("other-1-subpackage").unwrap() => ones_hash(),
        };
        assert_eq!(meta_subpackages.subpackages(), &expected_subpackages);
        assert_eq!(meta_subpackages.into_subpackages(), expected_subpackages);
    }

    proptest! {
        #![proptest_config(ProptestConfig{
            failure_persistence: None,
            ..Default::default()
        })]

        #[test]
        fn serialize(
            ref path0 in random_relative_package_url(),
            ref hex0 in random_hash(),
            ref path1 in random_relative_package_url(),
            ref hex1 in random_hash())
        {
            prop_assume!(path0 != path1);
            let map = hashmap! {
                path0.clone() => *hex0,
                path1.clone() => *hex1,
            };
            let meta_subpackages = MetaSubpackages::from_iter(map);

            prop_assert_eq!(
                serde_json::to_value(meta_subpackages).unwrap(),
                json!(
                    {
                        "version": "1",
                        "subpackages": {
                            path0: hex0,
                            path1: hex1,
                        }
                    }
                )
            );
        }

        #[test]
        fn serialize_deserialize_is_id(
            subpackages in prop::collection::hash_map(
                random_relative_package_url(), random_hash(), 0..4)
        ) {
            let meta_subpackages = MetaSubpackages::from_iter(subpackages);
            let deserialized = MetaSubpackages::deserialize(
                &*serde_json::to_vec(&meta_subpackages).unwrap()
            )
            .unwrap();
            prop_assert_eq!(meta_subpackages, deserialized);
        }
    }

    #[test]
    fn test_context_bytes_from_subpackages_map_and_back() {
        let subpackages_map = hashmap! {
            RelativePackageUrl::parse("a_0_subpackage").unwrap() => zeros_hash(),
            RelativePackageUrl::parse("other-1-subpackage").unwrap() => ones_hash(),
        };
        let bytes = context_bytes_from_subpackages_map(&subpackages_map).unwrap().unwrap();
        let restored_map = subpackages_map_from_context_bytes(&bytes).unwrap();
        assert_eq!(&restored_map, &subpackages_map);
    }
}
