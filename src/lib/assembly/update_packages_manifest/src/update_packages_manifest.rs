// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Result};
use fuchsia_hash::Hash;
use fuchsia_pkg::{PackageManifest, PackagePath};
use fuchsia_url::{PinnedAbsolutePackageUrl, RepositoryUrl};
use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;

/// A list of all packages included in an update, which can be written as a JSON
/// packages manifest.
/// TODO(fxbug.dev/76488): Share this enum with the rest of the SWD code.
///
/// ```json
/// {
///   "version": "1",
///   "content": [
///       "fuchsia-pkg://fuchsia.com/build-info/0?hash=30a83..",
///       "fuchsia-pkg://fuchsia.com/log_listener/0?hash=816c8..",
///     ...
///   ]
/// }
/// ```
#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
pub enum UpdatePackagesManifest {
    /// Version 1.
    #[serde(rename = "1")]
    V1(BTreeSet<PinnedAbsolutePackageUrl>),
}

impl Default for UpdatePackagesManifest {
    fn default() -> Self {
        UpdatePackagesManifest::V1(BTreeSet::new())
    }
}

impl UpdatePackagesManifest {
    /// Add a package to be updated by its PackageManifest.
    pub fn add_by_manifest(&mut self, package: PackageManifest) -> Result<()> {
        let path = package.package_path();
        let meta_blob = package.blobs().into_iter().find(|blob| blob.path == "meta/");
        match meta_blob {
            Some(meta_blob) => self.add(path, meta_blob.merkle, package.repository()),
            _ => bail!(format!("Failed to find the meta far in package {}", path)),
        }
    }

    /// Add a package to be updated by its path and meta far merkle.
    pub fn add(&mut self, path: PackagePath, merkle: Hash, repository: Option<&str>) -> Result<()> {
        let repository = repository.unwrap_or("fuchsia.com");
        let (name, variant) = path.into_name_and_variant();
        let url = PinnedAbsolutePackageUrl::new(
            RepositoryUrl::parse_host(repository.to_string())
                .with_context(|| format!("failed to convert '{repository}' to a RepositoryUrl"))?,
            name,
            Some(variant),
            merkle,
        );
        let () = self.add_by_url(url);
        Ok(())
    }

    /// Append all the |packages| to |self|.
    pub fn append(&mut self, packages: UpdatePackagesManifest) {
        match packages {
            UpdatePackagesManifest::V1(contents) => {
                for url in contents {
                    self.add_by_url(url);
                }
            }
        }
    }

    /// Add a new package by |url|.
    fn add_by_url(&mut self, url: PinnedAbsolutePackageUrl) {
        match self {
            UpdatePackagesManifest::V1(contents) => contents.insert(url),
        };
    }
}

#[cfg(test)]
mod tests {
    use super::UpdatePackagesManifest;
    use fuchsia_pkg::{BlobInfo, MetaPackage, PackageManifestBuilder};
    use serde_json::json;

    #[test]
    fn update_packages_manifest() {
        let mut manifest = UpdatePackagesManifest::default();
        manifest.add("one/0".parse().unwrap(), [0u8; 32].into(), None).unwrap();
        let package_manifest =
            PackageManifestBuilder::new(MetaPackage::from_name("two".parse().unwrap()))
                .repository("two.com")
                .add_blob(BlobInfo {
                    source_path: "source_path".into(),
                    path: "meta/".into(),
                    merkle: [0x22u8; 32].into(),
                    size: 42,
                })
                .build();
        manifest.add_by_manifest(package_manifest).unwrap();
        let out = serde_json::to_value(&manifest).unwrap();
        assert_eq!(
            out,
            json!({
                "version": "1",
                "content": [
                    "fuchsia-pkg://fuchsia.com/one/0?hash=0000000000000000000000000000000000000000000000000000000000000000",
                    "fuchsia-pkg://two.com/two/0?hash=2222222222222222222222222222222222222222222222222222222222222222"
                ],
            })
        );
    }
}
