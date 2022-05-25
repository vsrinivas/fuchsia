// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        path_hash_mapping::{Cache, PathHashMapping},
        CachePackagesInitError,
    },
    fuchsia_hash::Hash,
    fuchsia_url::{AbsolutePackageUrl, ParseError, PinnedAbsolutePackageUrl, RepositoryUrl},
    serde::{Deserialize, Serialize},
};

const DEFAULT_PACKAGE_DOMAIN: &str = "fuchsia.com";

#[derive(Debug, PartialEq, Eq)]
pub struct CachePackages {
    contents: Vec<PinnedAbsolutePackageUrl>,
}

impl CachePackages {
    /// Create a new instance of `CachePackages` containing entries provided.
    pub fn from_entries(entries: Vec<PinnedAbsolutePackageUrl>) -> Self {
        CachePackages { contents: entries }
    }

    pub(crate) fn from_path_hash_mapping(
        mapping: PathHashMapping<Cache>,
    ) -> Result<Self, ParseError> {
        let contents = mapping
            .into_contents()
            .map(|(path, hash)| {
                let (name, variant) = path.into_name_and_variant();
                PinnedAbsolutePackageUrl::new(
                    RepositoryUrl::parse_host(DEFAULT_PACKAGE_DOMAIN.to_string())
                        .expect("parse static string as repository host"),
                    name,
                    Some(variant),
                    hash,
                )
            })
            .collect::<Vec<_>>();
        Ok(CachePackages { contents })
    }

    pub(crate) fn from_json(file_contents: &[u8]) -> Result<Self, CachePackagesInitError> {
        let contents = parse_json(file_contents)?;
        Ok(CachePackages { contents })
    }

    /// Iterator over the contents of the mapping.
    pub fn contents(&self) -> impl Iterator<Item = &PinnedAbsolutePackageUrl> + ExactSizeIterator {
        self.contents.iter()
    }

    /// Iterator over the contents of the mapping, consuming self.
    pub fn into_contents(
        self,
    ) -> impl Iterator<Item = PinnedAbsolutePackageUrl> + ExactSizeIterator {
        self.contents.into_iter()
    }

    /// Get the hash for a package.
    pub fn hash_for_package(&self, pkg: &AbsolutePackageUrl) -> Option<Hash> {
        self.contents.iter().find_map(|candidate| {
            if pkg.as_unpinned() == candidate.as_unpinned() {
                match pkg.hash() {
                    None => Some(candidate.hash()),
                    Some(hash) if hash == candidate.hash() => Some(hash),
                    _ => None,
                }
            } else {
                None
            }
        })
    }
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(deny_unknown_fields)]
struct Packages {
    version: String,
    content: Vec<PinnedAbsolutePackageUrl>,
}

fn parse_json(contents: &[u8]) -> Result<Vec<PinnedAbsolutePackageUrl>, CachePackagesInitError> {
    match serde_json::from_slice(&contents).map_err(CachePackagesInitError::JsonError)? {
        Packages { ref version, content } if version == "1" => Ok(content),
        Packages { version, .. } => Err(CachePackagesInitError::VersionNotSupported(version)),
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_pkg::PackagePath};

    #[test]
    fn populate_from_path_hash_mapping() {
        let path_hash_packages = PathHashMapping::<Cache>::from_entries(vec![(
            PackagePath::from_name_and_variant("name0".parse().unwrap(), "0".parse().unwrap()),
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
        )]);

        let packages = CachePackages::from_path_hash_mapping(path_hash_packages).unwrap();
        let expected = vec![
            "fuchsia-pkg://fuchsia.com/name0/0?hash=0000000000000000000000000000000000000000000000000000000000000000"
        ];
        assert!(packages.into_contents().map(|u| u.to_string()).eq(expected.into_iter()));
    }

    #[test]
    fn populate_from_valid_json() {
        let file_contents = br#"
        {
            "version": "1",
            "content": [
                "fuchsia-pkg://foo.bar/qwe/0?hash=0000000000000000000000000000000000000000000000000000000000000000",
                "fuchsia-pkg://foo.bar/rty/0?hash=1111111111111111111111111111111111111111111111111111111111111111"
            ]
        }"#;

        let packages = CachePackages::from_json(file_contents).unwrap();
        let expected = vec![
            "fuchsia-pkg://foo.bar/qwe/0?hash=0000000000000000000000000000000000000000000000000000000000000000",
            "fuchsia-pkg://foo.bar/rty/0?hash=1111111111111111111111111111111111111111111111111111111111111111"
        ];
        assert!(packages.into_contents().map(|u| u.to_string()).eq(expected.into_iter()));
    }

    #[test]
    fn test_hash_for_package_returns_none() {
        let correct_hash = fuchsia_hash::Hash::from([0; 32]);
        let packages = CachePackages::from_entries(vec![PinnedAbsolutePackageUrl::parse(
            &format!("fuchsia-pkg://fuchsia.com/name?hash={correct_hash}"),
        )
        .unwrap()]);
        let wrong_hash = fuchsia_hash::Hash::from([1; 32]);
        assert_eq!(
            None,
            packages.hash_for_package(
                &AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/wrong-name").unwrap()
            )
        );
        assert_eq!(
            None,
            packages.hash_for_package(
                &AbsolutePackageUrl::parse(&format!(
                    "fuchsia-pkg://fuchsia.com/name?hash={wrong_hash}"
                ))
                .unwrap()
            )
        );
    }

    #[test]
    fn test_hash_for_package_returns_hashes() {
        let hash = fuchsia_hash::Hash::from([0; 32]);
        let packages = CachePackages::from_entries(vec![PinnedAbsolutePackageUrl::parse(
            &format!("fuchsia-pkg://fuchsia.com/name?hash={hash}"),
        )
        .unwrap()]);
        assert_eq!(
            Some(hash),
            packages.hash_for_package(
                &AbsolutePackageUrl::parse(&format!("fuchsia-pkg://fuchsia.com/name?hash={hash}"))
                    .unwrap()
            )
        );
        assert_eq!(
            Some(hash),
            packages.hash_for_package(
                &AbsolutePackageUrl::parse(&format!("fuchsia-pkg://fuchsia.com/name")).unwrap()
            )
        );
    }
}
