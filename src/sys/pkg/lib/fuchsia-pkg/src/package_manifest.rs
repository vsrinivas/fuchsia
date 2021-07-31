// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::{Package, PackageManifestError, PackagePath, PackagePathError};

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

    pub fn from_package(package: Package) -> Result<Self, PackageManifestError> {
        let blobs = package
            .blobs()
            .iter()
            .map(|(merkle, blob_entry)| BlobInfo {
                source_path: blob_entry.source_path().into_os_string().into_string().unwrap(),
                path: blob_entry.blob_path().to_string(),
                merkle: *merkle,
                size: blob_entry.size(),
            })
            .collect();
        let package_metadata = PackageMetadata {
            name: package.meta_package().name().to_string(),
            version: package.meta_package().variant().to_string(),
        };
        let manifest_v1 = PackageManifestV1 { package: package_metadata, blobs };
        Ok(PackageManifest(VersionedPackageManifest::Version1(manifest_v1)))
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
    use fuchsia_merkle::Hash;
    use serde_json::json;
    use std::path::PathBuf;
    use std::str::FromStr;

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

    #[test]
    fn test_create_package_manifest_from_package() {
        let mut package_builder = Package::builder("package-name", "package-variant").unwrap();
        package_builder.add_entry(
            String::from("bin/my_prog"),
            Hash::from_str("0000000000000000000000000000000000000000000000000000000000000000")
                .unwrap(),
            PathBuf::from("src/bin/my_prog"),
            1,
        );
        let package = package_builder.build().unwrap();
        let package_manifest = PackageManifest::from_package(package).unwrap();
        assert_eq!(String::from("package-name"), package_manifest.name());
    }
}
