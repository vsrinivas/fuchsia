// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::MetaSubpackagesError,
    anyhow,
    fuchsia_merkle::Hash,
    fuchsia_url::pkg_url::PackageName,
    serde::{Deserialize, Serialize},
    std::{collections::HashMap, io, iter::FromIterator},
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
    pub fn subpackages(&self) -> &HashMap<PackageName, Hash> {
        match &self.0 {
            VersionedMetaSubpackages::Version1(meta) => &meta.subpackages,
        }
    }

    /// Take the map from subpackage names to Merkle Tree root hashes.
    pub fn into_subpackages(self) -> HashMap<PackageName, Hash> {
        match self.0 {
            VersionedMetaSubpackages::Version1(meta) => meta.subpackages,
        }
    }

    /// Take the Merkle Tree root hashes in an iterator. The returned iterator may include
    /// duplicates.
    pub fn into_hashes_undeduplicated(self) -> impl Iterator<Item = Hash> {
        self.into_subpackages().into_iter().map(|(_, hash)| hash)
    }

    pub fn deserialize(reader: impl io::BufRead) -> Result<Self, MetaSubpackagesError> {
        Ok(MetaSubpackages::from_v1(serde_json::from_reader(reader)?))
    }

    pub fn serialize(&self, writer: impl io::Write) -> Result<(), MetaSubpackagesError> {
        Ok(serde_json::to_writer(writer, &self)?)
    }
}

impl FromIterator<(PackageName, Hash)> for MetaSubpackages {
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = (PackageName, Hash)>,
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

#[derive(Clone, Debug, Default, PartialEq, Eq, Deserialize, Serialize)]
struct MetaSubpackagesV1 {
    subpackages: HashMap<PackageName, Hash>,
}

/// The following functions may be deprecated. They support the initial
/// implementation of Subpackages (RFC-0154) but are likely to move to
/// a different module.
pub mod transitional {
    use {
        super::*,
        fuchsia_url::{
            errors::ParseError as PkgUrlParseError, errors::ResourcePathError, pkg_url::PkgUrl,
        },
    };

    /// Given a component URL (which may be absolute, or may start with a relative
    /// package path), extract the component resource (from the URL fragment) and
    /// create the `PkgUrl`, and return both, as a tuple.
    pub fn parse_package_url_and_resource(
        component_url: &str,
    ) -> Result<(PkgUrl, String), PkgUrlParseError> {
        let pkg_url = PkgUrl::parse_maybe_relative(component_url)?;
        let resource = pkg_url
            .resource()
            .ok_or_else(|| PkgUrlParseError::InvalidResourcePath(ResourcePathError::PathIsEmpty))?;
        Ok((pkg_url.root_url(), resource.to_string()))
    }

    /// Returns a package-resolver-specific value representing the given
    /// subpackage name-to-merkle map, or `None` if the map is empty.
    pub fn context_bytes_from_subpackages_map(
        subpackage_hashes: &HashMap<PackageName, Hash>,
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
        context_bytes: &Vec<u8>,
    ) -> Result<HashMap<PackageName, Hash>, anyhow::Error> {
        let json_value = serde_json::from_slice(context_bytes)?;
        Ok(serde_json::from_value::<HashMap<PackageName, Hash>>(json_value)?)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::transitional::*, super::*, crate::test::*, fuchsia_url::test::random_package_name,
        maplit::hashmap, proptest::prelude::*, serde_json::json, std::str::FromStr,
    };

    fn zeros_hash() -> Hash {
        Hash::from_str("0000000000000000000000000000000000000000000000000000000000000000").unwrap()
    }

    fn ones_hash() -> Hash {
        Hash::from_str("1111111111111111111111111111111111111111111111111111111111111111").unwrap()
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
            PackageName::from_str("a_0_subpackage").unwrap() => zeros_hash(),
            PackageName::from_str("other-1-subpackage").unwrap() => ones_hash(),
        };
        assert_eq!(meta_subpackages.subpackages(), &expected_subpackages);
        assert_eq!(meta_subpackages.into_subpackages(), expected_subpackages);
    }

    proptest! {
        #[test]
        fn serialize(
            ref path0 in random_package_name(),
            ref hex0 in random_hash(),
            ref path1 in random_package_name(),
            ref hex1 in random_hash())
        {
            prop_assume!(path0 != path1);
            let map = hashmap! {
                path0.clone() => hex0.clone(),
                path1.clone() => hex1.clone(),
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
                random_package_name(), random_hash(), 0..4)
        ) {
            let meta_subpackages = MetaSubpackages::from_iter(subpackages);
            let deserialized = MetaSubpackages::deserialize(&*serde_json::to_vec(&meta_subpackages).unwrap()).unwrap();
            prop_assert_eq!(meta_subpackages, deserialized);
        }
    }

    #[test]
    pub fn test_parse_package_url_and_resource() {
        let (abs_pkgurl, resource) =
            parse_package_url_and_resource("fuchsia-pkg://fuchsia.com/package#meta/comp.cm")
                .unwrap();
        assert_eq!(abs_pkgurl.is_relative(), false);
        assert_eq!(abs_pkgurl.host(), "fuchsia.com");
        assert_eq!(abs_pkgurl.name().as_ref(), "package");
        assert_eq!(resource, "meta/comp.cm");

        let (rel_pkgurl, resource) =
            parse_package_url_and_resource("package#meta/comp.cm").unwrap();
        assert_eq!(rel_pkgurl.is_relative(), true);
        assert_eq!(rel_pkgurl.name().as_ref(), "package");
        assert_eq!(resource, "meta/comp.cm");
    }

    #[test]
    pub fn test_context_bytes_from_subpackages_map_and_back() {
        let subpackages_map = hashmap! {
            PackageName::from_str("a_0_subpackage").unwrap() => zeros_hash(),
            PackageName::from_str("other-1-subpackage").unwrap() => ones_hash(),
        };
        let bytes = context_bytes_from_subpackages_map(&subpackages_map).unwrap().unwrap();
        let restored_map = subpackages_map_from_context_bytes(&bytes).unwrap();
        assert_eq!(&restored_map, &subpackages_map);
    }
}
