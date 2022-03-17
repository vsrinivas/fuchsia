// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::CachePackagesInitError,
    fuchsia_hash::Hash,
    fuchsia_url::pkg_url::{PinnedPkgUrl, PkgUrl},
    serde::{Deserialize, Serialize},
};

#[derive(Debug, PartialEq, Eq)]
pub struct CachePackages {
    contents: Vec<PinnedPkgUrl>,
}

impl CachePackages {
    /// Create a new instance of `CachePackages` containing entries provided.
    pub fn from_entries(entries: Vec<PinnedPkgUrl>) -> Self {
        CachePackages { contents: entries }
    }

    pub(crate) fn from_json(file_contents: &[u8]) -> Result<Self, CachePackagesInitError> {
        let contents = parse_json(file_contents)?;
        Ok(CachePackages { contents })
    }

    /// Iterator over the contents of the mapping.
    pub fn contents(&self) -> impl Iterator<Item = &PinnedPkgUrl> + ExactSizeIterator {
        self.contents.iter()
    }

    /// Iterator over the contents of the mapping, consuming self.
    pub fn into_contents(self) -> impl Iterator<Item = PinnedPkgUrl> + ExactSizeIterator {
        self.contents.into_iter()
    }

    /// Get the hash for a package.
    pub fn hash_for_package(&self, url: &PkgUrl) -> Option<Hash> {
        let hash = url.package_hash();
        let url = url.strip_hash();
        self.contents.iter().find_map(|candidate| {
            if url == candidate.strip_hash() {
                let candidate_hash = candidate.package_hash();
                match hash {
                    None => Some(candidate_hash),
                    Some(hash) if hash == &candidate_hash => Some(candidate_hash),
                    _ => None,
                }
            } else {
                None
            }
        })
    }

    pub fn serialize(&self, writer: impl std::io::Write) -> Result<(), serde_json::Error> {
        let content = Packages { version: "1".to_string(), content: self.contents.clone() };
        serde_json::to_writer(writer, &content)
    }
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(deny_unknown_fields)]
struct Packages {
    version: String,
    content: Vec<PinnedPkgUrl>,
}

fn parse_json(contents: &[u8]) -> Result<Vec<PinnedPkgUrl>, CachePackagesInitError> {
    match serde_json::from_slice(&contents).map_err(CachePackagesInitError::JsonError)? {
        Packages { ref version, content } if version == "1" => Ok(content),
        Packages { version, .. } => Err(CachePackagesInitError::VersionNotSupported(version)),
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::convert::TryFrom};

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
        let hash = fuchsia_hash::Hash::from([0; 32]);
        let packages = CachePackages::from_entries(vec![PinnedPkgUrl::new_package(
            "foo.bar".to_string(),
            "/qwe/0".to_string(),
            hash,
        )
        .unwrap()]);
        assert_eq!(
            None,
            packages.hash_for_package(
                &PkgUrl::parse(&format!("fuchsia-pkg://foo.bar/rty/0?hash={}", hash)).unwrap()
            )
        );
    }

    #[test]
    fn test_hash_for_package_returns_hashes() {
        let hashes = vec![fuchsia_hash::Hash::from([0; 32]), fuchsia_hash::Hash::from([1; 32])];
        let unpinned_urls: Vec<PkgUrl> = vec![
            PkgUrl::parse(&format!("fuchsia-pkg://foo.bar/qwe/0?hash={}", hashes[0])).unwrap(),
            PkgUrl::parse(&format!("fuchsia-pkg://foo.bar/rty/0?hash={}", hashes[1])).unwrap(),
        ];
        let pinned_urls = unpinned_urls
            .iter()
            .cloned()
            .map(PinnedPkgUrl::try_from)
            .collect::<Result<Vec<_>, _>>()
            .unwrap();
        let packages = CachePackages::from_entries(pinned_urls);
        assert!(unpinned_urls
            .iter()
            .map(|u| packages.hash_for_package(u).unwrap())
            .eq(hashes.into_iter()));
    }

    #[test]
    fn test_serialize() {
        let packages = CachePackages::from_entries(vec![PinnedPkgUrl::new_package(
            "foo.bar".to_string(),
            "/qwe/0".to_string(),
            fuchsia_hash::Hash::from([0; 32]),
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
        let packages = CachePackages::from_entries(vec![PinnedPkgUrl::new_package(
            "foo.bar".to_string(),
            "/qwe/0".to_string(),
            fuchsia_hash::Hash::from([0; 32]),
        )
        .unwrap()]);
        let mut bytes = vec![];

        packages.serialize(&mut bytes).unwrap();

        assert_eq!(CachePackages::from_json(&bytes).unwrap(), packages);
    }
}
