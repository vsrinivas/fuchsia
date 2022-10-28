// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::CachePackagesInitError,
    fuchsia_hash::Hash,
    fuchsia_url::{AbsolutePackageUrl, PinnedAbsolutePackageUrl, UnpinnedAbsolutePackageUrl},
    serde::{Deserialize, Serialize},
};

#[derive(Debug, PartialEq, Eq)]
pub struct CachePackages {
    contents: Vec<PinnedAbsolutePackageUrl>,
}

impl CachePackages {
    /// Create a new instance of `CachePackages` containing entries provided.
    pub fn from_entries(entries: Vec<PinnedAbsolutePackageUrl>) -> Self {
        CachePackages { contents: entries }
    }

    /// Create a new instance of `CachePackages` from parsing a json.
    /// If there are no cache packages, `file_contents` must be empty.
    pub(crate) fn from_json(file_contents: &[u8]) -> Result<Self, CachePackagesInitError> {
        if file_contents.is_empty() {
            return Ok(CachePackages { contents: vec![] });
        }
        let contents = parse_json(file_contents)?;
        if contents.is_empty() {
            return Err(CachePackagesInitError::NoCachePackages);
        }
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

    pub fn serialize(&self, writer: impl std::io::Write) -> Result<(), serde_json::Error> {
        if self.contents.is_empty() {
            return Ok(());
        }
        let content = Packages { version: "1".to_string(), content: self.contents.clone() };
        serde_json::to_writer(writer, &content)
    }

    pub fn find_unpinned_url(
        &self,
        url: &UnpinnedAbsolutePackageUrl,
    ) -> Option<&PinnedAbsolutePackageUrl> {
        self.contents().find(|pinned_url| pinned_url.as_unpinned() == url)
    }
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(deny_unknown_fields)]
struct Packages {
    version: String,
    content: Vec<PinnedAbsolutePackageUrl>,
}

fn parse_json(contents: &[u8]) -> Result<Vec<PinnedAbsolutePackageUrl>, CachePackagesInitError> {
    match serde_json::from_slice(contents).map_err(CachePackagesInitError::JsonError)? {
        Packages { ref version, content } if version == "1" => Ok(content),
        Packages { version, .. } => Err(CachePackagesInitError::VersionNotSupported(version)),
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

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
    fn populate_from_empty_json() {
        let packages = CachePackages::from_json(b"").unwrap();
        assert_eq!(packages.into_contents().count(), 0);
    }

    #[test]
    fn reject_non_empty_json_with_no_cache_packages() {
        let file_contents = br#"
        {
            "version": "1",
            "content": []
        }"#;

        assert_matches!(
            CachePackages::from_json(file_contents),
            Err(CachePackagesInitError::NoCachePackages)
        );
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
                &AbsolutePackageUrl::parse("fuchsia-pkg://fuchsia.com/name").unwrap()
            )
        );
    }

    #[test]
    fn test_serialize() {
        let hash = fuchsia_hash::Hash::from([0; 32]);
        let packages = CachePackages::from_entries(vec![PinnedAbsolutePackageUrl::parse(
            &format!("fuchsia-pkg://foo.bar/qwe/0?hash={hash}"),
        )
        .unwrap()]);
        let mut bytes = vec![];

        let () = packages.serialize(&mut bytes).unwrap();

        assert_eq!(
            serde_json::from_slice::<serde_json::Value>(bytes.as_slice()).unwrap(),
            serde_json::json!({
                "version": "1",
                "content": vec![
                    "fuchsia-pkg://foo.bar/qwe/0?hash=0000000000000000000000000000000000000000000000000000000000000000"
                    ],
            })
        );
    }

    #[test]
    fn test_serialize_deserialize_round_trip() {
        let hash = fuchsia_hash::Hash::from([0; 32]);
        let packages = CachePackages::from_entries(vec![PinnedAbsolutePackageUrl::parse(
            &format!("fuchsia-pkg://foo.bar/qwe/0?hash={hash}"),
        )
        .unwrap()]);
        let mut bytes = vec![];

        packages.serialize(&mut bytes).unwrap();

        assert_eq!(CachePackages::from_json(&bytes).unwrap(), packages);
    }
}
