// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::{PackagePath, PackagePathError};

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(transparent)]
pub struct PackageManifest(VersionedPackageManifest);

impl PackageManifest {
    pub fn into_blobs(self) -> Vec<BlobInfo> {
        match self.0 {
            VersionedPackageManifest::Version1(manifest) => manifest.blobs,
        }
    }

    pub fn name(&self) -> &str {
        match &self.0 {
            VersionedPackageManifest::Version1(manifest) => &manifest.package.name,
        }
    }

    pub fn package_path(&self) -> Result<PackagePath, PackagePathError> {
        match &self.0 {
            VersionedPackageManifest::Version1(manifest) => PackagePath::from_name_and_variant(
                manifest.package.name.as_str(),
                manifest.package.version.as_str(),
            ),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version")]
enum VersionedPackageManifest {
    #[serde(rename = "1")]
    Version1(PackageManifestV1),
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
struct PackageManifestV1 {
    package: PackageMetadata,
    blobs: Vec<BlobInfo>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
struct PackageMetadata {
    name: String,
    version: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct BlobInfo {
    pub source_path: String,
    pub path: String,
    pub merkle: fuchsia_merkle::Hash,
    pub size: u64,
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn test_version1_blobs() {
        let manifest = serde_json::from_value::<PackageManifest>(json!(
            {
                "version": "1",
                "package": {
                    "name": "example",
                    "version": "0"
                },
                "blobs": [
                    {
                        "source_path": "../p1",
                        "path": "data/p1",
                        "merkle": "0000000000000000000000000000000000000000000000000000000000000000",
                        "size": 1
                    },
                ]
            }
        )).expect("valid json");
        assert_eq!(
            manifest.into_blobs(),
            [BlobInfo {
                source_path: "../p1".into(),
                path: "data/p1".into(),
                merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                    .parse()
                    .unwrap(),
                size: 1
            }]
        )
    }
}
